#!/usr/bin/env python3
"""Bind the native loader to the verified rewrites: runtime-tables.bin (stays local).

Derives the module layout, symbols, relocations and gates that a native loader needs from the
verified rewrites and the NSO metadata. The layout and binary format mirror games/totk's
scripts/prepare_runtime.py (the loader reads the same table format), so a Paper Mario runtime can
reuse the loader's table reader unchanged. Nothing of the game is committed by this script; the
tables are a local record.

Usage:
    python3 prepare_runtime.py <rewritten-dir> <exefs-dir> --out <private-dir>
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

GAME = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(GAME / 'third_party' / 'd2r'))
from relr import decode_relr

MODULES = ('main', 'sdk', 'subsdk0', 'rtld')
TITLE_ID = '0100ECD018EBE000'


def load_patched(rewritten, name):
    return (rewritten / f'{name}.patched.bin').read_bytes()


def dynamic_tags(image):
    m0 = struct.unpack_from('<I', image, 4)[0]
    assert image[m0:m0 + 4] == b'MOD0', 'no MOD0'
    dyn = m0 + struct.unpack_from('<i', image, m0 + 4)[0]
    tags = {}
    for offset in range(dyn, dyn + 4096 * 16, 16):
        tag, value = struct.unpack_from('<qQ', image, offset)
        if tag == 0:
            break
        tags[tag] = value
    else:
        raise ValueError('unterminated dynamic table')
    return tags


def build(rewritten, exefs, out):
    # 生成表前核对改写记录，拒绝失败或被修改的产物。
    report = json.loads((rewritten / 'report.json').read_text())
    rewriter = hashlib.sha256((GAME / 'third_party/d2r/patch_nso.py').read_bytes()).hexdigest()
    assert report['title_id'] == TITLE_ID and report['tool_sha256'] == rewriter
    manifest = json.loads((exefs.parent / 'extraction-manifest.json').read_text())
    assert manifest['program']['title_id'] == TITLE_ID
    originals = {entry['name']: entry['sha256'] for entry in manifest['files']}
    assert originals == json.loads((GAME / 'assets/expected-v1.0.0.json').read_text())['exefs_sha256'], '本体版本哈希不匹配'
    for name in MODULES:
        entry = report['modules'][name]
        assert entry['exit_code'] == 0
        original = hashlib.sha256((exefs / name).read_bytes()).hexdigest()
        assert original == originals[name] == entry['input_sha256']
        for suffix, expected in entry['outputs'].items():
            assert hashlib.sha256((rewritten / f'{name}.{suffix}').read_bytes()).hexdigest() == expected


    modules = []
    for name in MODULES:
        image = load_patched(rewritten, name)
        digest = hashlib.sha256(image).hexdigest()

        layout = {}
        for line in (rewritten / f'{name}.fixups.tsv').read_text().splitlines():
            _, key, value = line.split()
            layout[key] = int(value, 0)

        tags = dynamic_tags(image)

        # Symbol count: DT_HASH when present, else the NSO header's dynsym extent.
        if 4 in tags:
            nsym = struct.unpack_from('<I', image, tags[4] + 4)[0]
        else:
            raw_header = (exefs / name).read_bytes()[:256]
            _, _, symbol_offset, symbol_size = struct.unpack_from('<IIII', raw_header, 0x90)
            assert layout['RO_OFF'] + symbol_offset == tags[6] and symbol_size % 24 == 0
            nsym = symbol_size // 24

        assert tags[11] == 24 and tags[9] == 24, 'unexpected ELF entry sizes'
        symbols = []
        for i in range(nsym):
            no, info, other, section, value, size = struct.unpack_from('<IBBHQQ', image, tags[6] + 24 * i)
            end = image.index(0, tags[5] + no, tags[5] + tags[10])
            symbols.append(dict(name=image[tags[5] + no:end].decode(), value=value, size=size,
                                defined=bool(section), binding=info >> 4, type=info & 15))

        relocs = [dict(where=where, kind=1027, symbol=0, addend=addend)
                  for where, addend in decode_relr(image, tags, layout['DATA_OFF'], layout['KEY_PAGE_OFF'])]
        relocated = {r['where'] for r in relocs}
        for address, size in [(tags[7], tags[8]), (tags[23], tags[2])]:
            assert size % 24 == 0
            for offset in range(address, address + size, 24):
                where, info, addend = struct.unpack_from('<QQq', image, offset)
                kind, si = info & 0xffffffff, info >> 32
                assert kind in (257, 1025, 1026, 1027) and si < nsym
                assert layout['DATA_OFF'] <= where <= layout['KEY_PAGE_OFF'] - 8
                # rtld 的末尾符号是合法尾后指针（+0x4000），允许等于原映像末尾。
                assert kind != 1027 or 0 <= addend <= layout['KEY_PAGE_OFF']
                assert where not in relocated, 'duplicate RELR/RELA relocation'
                relocated.add(where)
                relocs.append(dict(where=where, kind=kind, symbol=si, addend=addend))

        gates = []
        for line in (rewritten / f'{name}.gates.tsv').read_text().splitlines():
            ret, kind, arg, original = line.split()
            assert kind in ('cntpct', 'cntvct', 'svc', 'spin')
            gates.append(dict(ret=int(ret, 0), timer={'svc': 0, 'spin': 2}.get(kind, 1),
                              arg=int(arg), original=int(original, 0)))
        gates.sort(key=lambda g: g['ret'])
        assert len({g['ret'] for g in gates}) == len(gates), 'duplicate gate return address'

        # DT_INIT's state word: the first adrp xR + ldr w8,[xR,#imm] pair in its first 16 instructions.
        found = None
        for k in range(16):
            at = tags[12] + 4 * k
            a = struct.unpack_from('<I', image, at)[0]
            if a & 0x9f000000 != 0x90000000:
                continue
            reg = a & 31
            for j in range(1, 4):
                w = struct.unpack_from('<I', image, at + 4 * j)[0]
                if w & 0xffc003ff == 0xb9400008 | (reg << 5):
                    found = (at, a, w)
                    break
            if found:
                break
        assert found, f'{name}: DT_INIT state load not found'
        at, a, w = found
        imm = ((a >> 29) & 3) | (((a >> 5) & 0x7ffff) << 2)
        imm = (imm ^ (1 << 20)) - (1 << 20)
        ctor_flag = (at & ~4095) + (imm << 12) + ((w >> 10) & 0xfff) * 4
        assert layout['DATA_OFF'] <= ctor_flag < layout['KEY_PAGE_OFF']

        modules.append(dict(name=name, sha256=digest, size=len(image), ctor_flag=ctor_flag,
                            layout=layout, tags=tags, symbols=symbols, relocs=relocs, gates=gates))

    # runtime-tables.bin: 64-byte header, 208-byte record per module, then arrays, then names.
    HEADER, RECORD = 64, 208
    strings, arrays, records = bytearray(), bytearray(), []
    def place(blob):
        arrays.extend(bytes(-len(arrays) % 8))
        arrays.extend(blob)
        return HEADER + RECORD * len(modules) + len(arrays) - len(blob)
    for m in modules:
        names = []
        for s in m['symbols']:
            names.append(len(strings))
            strings.extend(s['name'].encode() + b'\0')
        symbols = b''.join(struct.pack('<QQIBBBx', s['value'], s['size'], n, int(s['defined']), s['binding'], s['type'])
                           for s, n in zip(m['symbols'], names))
        relocs = b''.join(struct.pack('<QIIq', r['where'], r['kind'], r['symbol'], r['addend']) for r in m['relocs'])
        gates = b''.join(struct.pack('<QQII', g['ret'], g['original'], int(g['timer']), g['arg']) for g in m['gates'])
        l, t = m['layout'], m['tags']
        records.append(struct.pack('<16s72s13Q4I', m['name'].encode(), m['sha256'].encode(), m['size'], l['TEXT_END'],
                                   l['RO_OFF'], l['DATA_OFF'], l['PATCH_OFF'], l['KEY_PAGE_OFF'], t.get(12, 0),
                                   m['ctor_flag'], t.get(25, 0), t.get(27, 0), place(symbols), place(relocs),
                                   place(gates), len(m['symbols']), len(m['relocs']), len(m['gates']), 0))
    arrays.extend(bytes(-len(arrays) % 8))
    strings_offset = HEADER + RECORD * len(modules) + len(arrays)
    size = strings_offset + len(strings)
    header = struct.pack('<8s8sIIQQQQQ', b'TOTKTAB1', b'pmttyd00', len(modules), 1, 0, 0, strings_offset, len(strings), size)
    assert len(header) == HEADER and all(len(r) == RECORD for r in records)

    out.mkdir(parents=True, exist_ok=True)
    (out / 'runtime-tables.bin').write_bytes(header + b''.join(records) + arrays + strings)

    npdm = (exefs / 'main.npdm').read_bytes()
    assert npdm[:4] == b'META', 'main.npdm'
    (out / 'process.txt').write_text(f'# From main.npdm\nSYSTEM_RESOURCE_SIZE {struct.unpack_from("<I", npdm, 0x14)[0]}\n')
    (out / 'rewriter.txt').write_text(f'# SHA-256 of the patch_nso.py that rewrote the modules\nREWRITER_SHA256 {rewriter}\n')

    total_relocs = sum(len(m['relocs']) for m in modules)
    total_gates = sum(len(m['gates']) for m in modules)
    print(json.dumps({'modules': len(modules), 'relocations': total_relocs, 'gates': total_gates,
                      'nnMain': 'main+0x%x' % [s['value'] for s in modules[0]['symbols'] if s['name'] == 'nnMain'][0]},
                     indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rewritten', type=Path)
    parser.add_argument('exefs', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    build(args.rewritten, args.exefs, args.out)


if __name__ == '__main__':
    main()
