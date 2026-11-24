#!/usr/bin/env python3
"""Extract the base Program NCA's ExeFS from Paper Mario: The Thousand-Year Door.

Reads an NSP in place with the player's own prod.keys, finds the base Program NCA
(title 0100ECD018EBE000), and extracts its ExeFS (section 0, AES-CTR). Keys stay in
memory; nothing secret is printed. The NCA3 / PFS0 / ExeFS decoding primitives below
are the repo's own MIT code (same decoding as games/totk/scripts/extract_exefs.py).

Usage:
    python3 extract_exefs.py <base.nsp> --keys <prod.keys> --output <dir>
"""
import argparse
import hashlib
import io
import json
import struct
import sys
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

TITLE_ID = "0100ECD018EBE000"   # Paper Mario: The Thousand-Year Door, base game


def require(condition, message):
    if not condition:
        raise ValueError(message)


def decrypt(key, mode, data):
    ctx = Cipher(algorithms.AES(key), mode).decryptor()
    return ctx.update(data) + ctx.finalize()


def read_at(f, offset, length):
    require(0 <= offset and 0 <= length <= 512 * 1024 * 1024, "Invalid/big read")
    f.seek(offset)
    data = f.read(length)
    require(len(data) == length, "Truncated input")
    return data


def hfs(f, offset, bound):
    return partition_entries(f, offset, bound, b"HFS0", 64)


def partition_entries(f, offset, bound, magic=b"PFS0", stride=24):
    require(bound >= 16, "Truncated partition header")
    hdr = read_at(f, offset, 16)
    require(hdr[:4] == magic, f"Expected {magic.decode()}")
    count, strings_size = struct.unpack_from("<II", hdr, 4)
    require(count <= 4096 and strings_size <= 1024 * 1024, "Invalid partition table")
    size = 16 + count * stride + strings_size
    require(size <= bound, "Partition table out of bounds")
    raw = read_at(f, offset, size)
    strings = raw[16 + count * stride:]
    entries = []
    names = set()
    for i in range(count):
        off, length, nameoff = struct.unpack_from("<QQI", raw, 16 + i * stride)
        require(nameoff < len(strings), "Invalid partition filename offset")
        end = strings.find(b"\0", nameoff)
        require(end >= 0, "Unterminated partition filename")
        name = strings[nameoff:end].decode("utf-8")
        require(name not in ("", ".", "..") and "/" not in name and "\\" not in name, "Unsafe partition filename")
        require(name not in names, "Duplicate partition filename")
        names.add(name)
        require(size + off + length <= bound, "Partition entry out of bounds")
        entries.append((name, offset + size + off, length))
    end = offset + size
    for _, start, length in sorted(entries, key=lambda e: e[1]):
        require(start >= end, "Overlapping partition entries")
        end = start + length
    return entries


def container_entries(f, size):
    if read_at(f, 0, 4) == b"PFS0":
        return "nsp", partition_entries(f, 0, size)
    require(size >= 0x138 and read_at(f, 0x100, 4) == b"HEAD", "Expected an XCI or NSP (PFS0) game file")
    root = struct.unpack("<Q", read_at(f, 0x130, 8))[0]
    partitions = hfs(f, root, size - root)
    secure = [e for e in partitions if e[0] == "secure"]
    require(len(secure) == 1, "Expected one XCI secure partition")
    return "xci", hfs(f, secure[0][1], secure[0][2])


def load_keys(path):
    keys = {}
    for number, line in enumerate(path.read_text().splitlines(), 1):
        line = line.split("#", 1)[0].strip()
        if "=" in line:
            name, value = line.split("=", 1)
            try:
                keys[name.strip().lower()] = bytes.fromhex(value.strip())
            except ValueError:
                raise ValueError(f"Invalid key encoding at line {number}") from None
    return keys


def common_tickets(f, entries):
    tickets = {}
    bodies = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80,
              0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}
    for name, offset, size in entries:
        if not name.lower().endswith(".tik"):
            continue
        require(size >= 4, "Truncated NSP ticket")
        sig = struct.unpack("<I", read_at(f, offset, 4))[0]
        require(sig in bodies, "Unsupported NSP ticket signature type")
        body = bodies[sig]
        require(size >= body + 0x180, "Truncated NSP ticket body")
        data = read_at(f, offset + body, 0x180)
        require(data[0x141] == 0, "Personalized NSP tickets are unsupported; a common ticket is required")
        rights, key = data[0x160:0x170], data[0x40:0x50]
        require(rights not in tickets or tickets[rights] == key, "Conflicting NSP tickets")
        tickets[rights] = key
    return tickets


def read_nca_header(f, offset, size, keys):
    require(size >= 0xC00, "Truncated NCA header")
    require("header_key" in keys, "Missing header_key in prod.keys")
    raw = read_at(f, offset, 0xC00)
    hdr = b"".join(decrypt(keys["header_key"], modes.XTS(i.to_bytes(16, "big")),
                           raw[i * 512:(i + 1) * 512]) for i in range(6))
    require(hdr[0x200:0x204] == b"NCA3", "Expected NCA3 (check prod.keys and the game file)")
    require(struct.unpack_from("<Q", hdr, 0x208)[0] == size, "NCA size does not match container entry")
    for i in range(4):
        start, end = struct.unpack_from("<II", hdr, 0x240 + i * 16)
        require((start == end == 0) or 0xC00 <= start * 512 < end * 512 <= size, "NCA section out of bounds")
    return hdr


def nca_ctr_key(hdr, keys, tickets):
    generation = max(max(hdr[0x206], hdr[0x220]) - 1, 0)
    rights = hdr[0x230:0x240]
    if rights != bytes(16):
        require(rights in tickets, "Missing common ticket for rights-managed NCA in the NSP")
        keyname = f"titlekek_{generation:02x}"
        require(keyname in keys, f"Missing {keyname} in prod.keys")
        return decrypt(keys[keyname], modes.ECB(), tickets[rights])
    require(hdr[0x207] < 3, "Unsupported NCA key area")
    area = ("application", "ocean", "system")[hdr[0x207]]
    keyname = f"key_area_key_{area}_{generation:02x}"
    require(keyname in keys, f"Missing {keyname} in prod.keys")
    return decrypt(keys[keyname], modes.ECB(), hdr[0x300:0x340])[32:48]


def nca_inventory(f, entries, keys):
    records = []
    for name, offset, length in entries:
        if not name.lower().endswith(".nca"):
            continue
        hdr = read_nca_header(f, offset, length, keys)
        record = dict(name=name, nca_offset=offset, size=length, content_type=hdr[0x205],
                      title_id=f"{struct.unpack_from('<Q', hdr, 0x210)[0]:016X}")
        records.append((record, hdr))
    return records


def extract_exefs(game, keys_path, output):
    keys = load_keys(keys_path)
    f = game.open("rb")
    try:
        kind, entries = container_entries(f, game.stat().st_size)
        tickets = common_tickets(f, entries)
        records = nca_inventory(f, entries, keys)

        programs = [(r, h) for r, h in records if r["content_type"] == 0 and r["title_id"] == TITLE_ID]
        require(len(programs) == 1, f"Expected one base Program NCA for {TITLE_ID}")
        record, hdr = programs[0]
        ctr_key = nca_ctr_key(hdr, keys, tickets)

        extracted = []
        for i in range(4):
            start, end = struct.unpack_from("<II", hdr, 0x240 + 16 * i)
            if not start:
                continue
            fs = hdr[0x400 + 512 * i:0x600 + 512 * i]
            require(hashlib.sha256(fs).digest() == hdr[0x280 + 32 * i:0x2A0 + 32 * i], "FS header hash mismatch")
            if fs[2:4] != b"\x01\x02":
                continue                       # RomFS / non-PartitionFS section
            if fs[4] != 3:
                continue                       # unencrypted partition (e.g. Logo), not the ExeFS
            blocksize, levels = struct.unpack_from("<II", fs, 0x28)
            hashoff, hashsize, pfsoff, pfssize = struct.unpack_from("<QQQQ", fs, 0x30)
            secstart, secsize = start * 512, (end - start) * 512
            require(levels == 2 and blocksize > 0 and pfssize <= 512 * 1024 * 1024, "Unsupported ExeFS hash layout")

            def section_read(relative, size):
                require(0 <= relative and relative + size <= secsize, "Section read out of bounds")
                absolute = secstart + relative
                aligned = absolute & ~15
                iv = fs[0x140:0x148][::-1] + (aligned // 16).to_bytes(8, "big")
                data = read_at(f, record["nca_offset"] + aligned, size + absolute - aligned)
                return decrypt(ctr_key, modes.CTR(iv), data)[absolute - aligned:]

            hashes = section_read(hashoff, hashsize)
            require(hashlib.sha256(hashes).digest() == fs[8:40], "ExeFS hash-table mismatch")
            pfs = section_read(pfsoff, pfssize)
            for idx, pos in enumerate(range(0, pfssize, blocksize)):
                expected = hashes[idx * 32:(idx + 1) * 32]
                require(hashlib.sha256(pfs[pos:pos + blocksize]).digest() == expected, f"ExeFS block hash mismatch: {idx}")
            files = [(name, pfs[off:off + size]) for name, off, size in
                     partition_entries(io.BytesIO(pfs), 0, len(pfs))]
            if not any(name == "main.npdm" for name, _ in files):
                continue                       # not the ExeFS
            output.mkdir(parents=True, exist_ok=True)
            for name, data in files:
                (output / name).write_bytes(data)
                extracted.append({"name": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
            break
    finally:
        f.close()

    require(extracted, "No ExeFS section found")
    manifest = {"source": str(game.resolve()), "container": kind, "program": record, "files": extracted}
    (output.parent / "extraction-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"integrity": "FS header, ExeFS hash table, and every ExeFS data block verified; RSA signatures not checked",
                      "files": extracted}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="base game NSP")
    parser.add_argument("--keys", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    require(not args.output.exists() or not any(args.output.iterdir()), "Output already exists; choose a new directory")
    extract_exefs(args.game, args.keys, args.output)


if __name__ == "__main__":
    main()
