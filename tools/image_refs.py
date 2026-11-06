"""Find strings in the guest image and every reference to them: as a 32-bit
pointer in data (luaL_Reg-style tables) or as a lis/addi pair in code.

usage: image_refs.py <image.bin> <string>...
"""
import struct
import sys

BASE = 0x82000000
img = open(sys.argv[1], "rb").read()
names = sys.argv[2:]


def str_addrs(s):
    needle = b"\x00" + s.encode() + b"\x00"
    out, i = [], 0
    while True:
        i = img.find(needle, i)
        if i < 0: return out
        out.append(BASE + i + 1)
        i += 1


targets = {}
for n in names:
    for a in str_addrs(n):
        targets[a] = n
print("strings:", {n: [hex(a) for a, m in targets.items() if m == n] for n in names})

# data pointers
for off in range(0, len(img) - 8, 4):
    v = struct.unpack_from(">I", img, off)[0]
    if v in targets:
        nxt = struct.unpack_from(">I", img, off + 4)[0]
        print(f"ptr  {targets[v]:14s} at {BASE+off:#010x}  next word {nxt:#010x}")

# lis/addi pairs (code)
hi_regs = {}
for off in range(0, min(len(img), 0x1200000), 4):
    ins = struct.unpack_from(">I", img, off)[0]
    op = ins >> 26
    rd, ra, imm = (ins >> 21) & 31, (ins >> 16) & 31, ins & 0xFFFF
    simm = imm - 0x10000 if imm & 0x8000 else imm
    if op == 15 and ra == 0:  # lis
        hi_regs[rd] = ((imm << 16) & 0xFFFFFFFF, off)
    elif op == 14 and ra in hi_regs and off - hi_regs[ra][1] < 64:  # addi
        a = (hi_regs[ra][0] + simm) & 0xFFFFFFFF
        if a in targets:
            print(f"code {targets[a]:14s} at {BASE+off:#010x}")
