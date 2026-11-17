"""Decrypt + expand default.xex (XEX2, AES-CBC NORMAL, BASIC compression) into the
raw guest image at base 0x82000000. Layout per docs/patches.md. Needs pycryptodome.

usage: xex_image.py <default.xex> <out.bin>
"""
import struct
import sys
from Crypto.Cipher import AES

RETAIL_KEY = bytes([0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3, 0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91])

x = open(sys.argv[1], "rb").read()
assert x[:4] == b"XEX2"
hdr_size = struct.unpack(">I", x[8:12])[0]
sec_off = struct.unpack(">I", x[16:20])[0]
count = struct.unpack(">I", x[20:24])[0]
opts = {}
for i in range(count):
    k, v = struct.unpack(">II", x[24 + 8 * i:32 + 8 * i])
    opts[k] = v
ffi = opts[0x3FF]  # file format info (offset)
ffi_size = struct.unpack(">I", x[ffi:ffi + 4])[0]
enc, comp = struct.unpack(">HH", x[ffi + 4:ffi + 8])
blocks = []
p = ffi + 8
while p + 8 <= ffi + ffi_size:
    blocks.append(struct.unpack(">II", x[p:p + 8]))
    p += 8
aes_key = x[sec_off + 0x150:sec_off + 0x160]
session = AES.new(RETAIL_KEY, AES.MODE_CBC, bytes(16)).decrypt(aes_key)
data = x[hdr_size:]
data = data[:len(data) - len(data) % 16]
dec = AES.new(session, AES.MODE_CBC, bytes(16)).decrypt(data) if enc == 1 else data
out, q = bytearray(), 0
for ds, zs in blocks:
    out += dec[q:q + ds] + bytes(zs)
    q += ds
open(sys.argv[2], "wb").write(out)
print(f"hdr 0x{hdr_size:X} enc {enc} comp {comp} blocks {[(hex(a), hex(b)) for a, b in blocks]} -> {len(out):#x} bytes, MZ={out[:2]}")
