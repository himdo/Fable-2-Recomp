"""One-off GDFX (Xbox 360 disc) extractor: a Python port of
tools/x360extract/lib/Gdfx.cs (directory records, 0xFF sector-batch padding).
Usage: python3 -I gdfx_extract.py <iso> <out_dir>
"""
import os
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Partition offsets: rebuilt xiso, XGD2, XGD3 (GDFX header sits at +0x10000).
PARTITIONS = (0x0, 0xFD90000, 0x2080000, 0x18300000)


def main(iso_path, out_dir):
    iso = open(iso_path, "rb")
    iso_len = os.fstat(iso.fileno()).st_size
    for base in PARTITIONS:
        iso.seek(base + 0x10000)
        hdr = iso.read(0x24)
        if hdr[:20] == MAGIC:
            break
    else:
        sys.exit("no GDFX header found")
    root_sector, root_size = struct.unpack_from("<II", hdr, 20)
    sector_off = lambda s: base + s * SECTOR

    def read_dir(sector, size):
        start = sector_off(sector)
        end = start + size
        pos = start
        while pos + 14 <= end and pos + 14 <= iso_len:
            iso.seek(pos)
            rec = iso.read(14)
            if struct.unpack_from("<I", rec)[0] == 0xFFFFFFFF:
                nxt = start + ((((pos - start) >> 11) + 1) << 11)
                if nxt < end:
                    iso.seek(pos)
                    if iso.read(nxt - pos) == b"\xff" * (nxt - pos):
                        pos = nxt
                        continue
                return
            esector, esize, attrs, nlen = struct.unpack_from("<IIBB", rec, 4)
            if not 1 <= nlen <= 100:
                return
            name = iso.read(nlen)
            if any(c <= 0x20 or c >= 0x7F for c in name):
                return
            name = name.decode("ascii")
            if name in (".", "..") or "/" in name or "\\" in name:
                sys.exit(f"unsafe entry name: {name!r}")
            yield name, esector, esize, bool(attrs & 0x10)
            pos += (14 + nlen + 3) & ~3

    files = total = 0

    def walk(sector, size, dest):
        nonlocal files, total
        os.makedirs(dest, exist_ok=True)
        for name, esector, esize, is_dir in read_dir(sector, size):
            path = os.path.join(dest, name)
            if is_dir:
                walk(esector, esize, path)
                continue
            iso.seek(sector_off(esector))
            remaining = esize
            with open(path, "wb") as f:
                while remaining:
                    chunk = iso.read(min(remaining, 1 << 22))
                    if not chunk:
                        sys.exit(f"truncated ISO while reading {path}")
                    f.write(chunk)
                    remaining -= len(chunk)
            files += 1
            total += esize

    walk(root_sector, root_size, out_dir)
    print(f"GDFX at {hex(base + 0x10000)}: {files} files, {total / 2**30:.2f} GiB -> {out_dir}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
