"""Disassemble a range of the guest image (PPC32 big-endian) with resolved lis/addi constants.
Needs capstone.

usage: image_dis.py <image.bin> <start hex> <end hex>
"""
import struct
import sys
from capstone import Cs, CS_ARCH_PPC, CS_MODE_32, CS_MODE_BIG_ENDIAN

BASE = 0x82000000
img = open(sys.argv[1], "rb").read()
start, end = int(sys.argv[2], 16), int(sys.argv[3], 16)
md = Cs(CS_ARCH_PPC, CS_MODE_32 | CS_MODE_BIG_ENDIAN)


def cstr(a):
    o = a - BASE
    if 0 <= o < len(img):
        e = img.find(b"\x00", o, o + 80)
        s = img[o:e]
        if e > o and all(32 <= c < 127 for c in s):
            return '"' + s.decode() + '"'
    return ""


his = {}
for i in md.disasm(img[start - BASE:end - BASE], start):
    note = ""
    ins = struct.unpack_from(">I", img, i.address - BASE)[0]
    op, rd, ra, imm = ins >> 26, (ins >> 21) & 31, (ins >> 16) & 31, ins & 0xFFFF
    simm = imm - 0x10000 if imm & 0x8000 else imm
    if op == 15 and ra == 0:
        his[rd] = (imm << 16) & 0xFFFFFFFF
    elif op in (14, 24) and ra in his:
        a = (his[ra] + simm) & 0xFFFFFFFF if op == 14 else his[ra] | imm
        note = f"  ; ={a:#010x} {cstr(a)}"
    elif op in (32, 36) and ra in his:  # lwz/stw
        note = f"  ; [{(his[ra] + simm) & 0xFFFFFFFF:#010x}]"
    print(f"{i.address:#010x}  {i.mnemonic:8s} {i.op_str}{note}")
