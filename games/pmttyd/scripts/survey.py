#!/usr/bin/env python3
"""Survey the Paper Mario TTYD base modules (read-only; prints no secrets or game code).

Reports the NSO header layout and build-id, the SDK/library composition, the graphics
stack (NVN + NintendoWare), and the scale of each module's relocation/import table. Uses
the local d2r reader (third_party/d2r/nsoimg.py).

Usage:
    python3 survey.py <exefs-dir>
"""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "third_party" / "d2r"))
import nsoimg

MODULES = ("main", "sdk", "subsdk0", "rtld")


def nso_header(path):
    d = path.read_bytes()
    assert d[:4] == b"NSO0", path
    flags = struct.unpack_from("<I", d, 0x0C)[0]
    segs = [struct.unpack_from("<III", d, 0x10 + i * 0x10) for i in range(3)]
    bss = struct.unpack_from("<I", d, 0x3C)[0]
    build = d[0x40:0x60].hex()
    return dict(flags=flags, segs=segs, bss=bss, build_id=build)


def strings(img, minlen=6):
    return [m.group().decode("ascii", "replace")
            for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, img)]


def main(exefs):
    print("=== NSO modules ===")
    images = {}
    for mod in MODULES:
        p = exefs / mod
        if not p.exists():
            continue
        h = nso_header(p)
        img = nsoimg.load(p)
        images[mod] = img
        text, ro, data = h["segs"]
        print(f"\n{mod}: build-id {h['build_id'][:32]}…")
        print(f"  text@{text[1]:#x} ({text[2]:#x})  rodata@{ro[1]:#x} ({ro[2]:#x})  "
              f"data@{data[1]:#x} ({data[2]:#x})  bss {h['bss']:#x}  image {len(img):#x}")

    print("\n=== SDK / library composition (SDK MW+Nintendo strings) ===")
    for mod in ("main", "sdk", "subsdk0"):
        for s in strings(images[mod], minlen=8):
            if re.search(r"SDK MW\+Nintendo", s):
                print(f"  [{mod}] {s[:120]}")

    print("\n=== Relocation / import scale ===")
    for mod in MODULES:
        rels = nsoimg.relocs(nsoimg.load(exefs / mod))
        imports = {r[2] for r in rels if r[2] and r[3] is None}
        print(f"  {mod:8s}: {len(rels):8d} relocations, {len(imports):6d} distinct imported symbols")

    print("\n=== Graphics stack markers (NVN / NvRm / gralloc / nn::gfx) ===")
    markers = ("nvn", "NvRm", "NvBlit", "gralloc", "nn::gfx", "gfx::", "G3d", "Vfx", "Atk", "movie")
    for mod in ("main", "sdk", "subsdk0"):
        ss = strings(images[mod])
        counts = {m: sum(1 for s in ss if m.lower() in s.lower()) for m in markers}
        present = {m: c for m, c in counts.items() if c}
        print(f"  {mod}: {present}")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exefs", type=Path)
    args = parser.parse_args()
    main(args.exefs)
