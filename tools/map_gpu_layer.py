"""Map the statically linked XDK Direct3D layer in the recompiled code.

Scans generated/default/fable_2_recomp.*.cpp (needs codegen output) and
reports, per recompiled guest function:
  * calls to the kernel's Vd* graphics exports (VdSwap, VdInitializeRingBuffer,
    ...), which only the D3D library makes;
  * PM4 type-3 packet headers it builds - either lis rX,0xC0nn then
    ori rX,rX,opcode<<8, or the opcode first (li/addi/ori) and the type bits
    added by a later oris rX,rY,0xC0nn: draws, shader microcode loads,
    constant uploads, event writes, ...

    python tools/map_gpu_layer.py [--json out.json]

D3D9LTCG inlines much of Direct3D into game code, so packet writers found
outside the library's own address range are game functions with inlined
D3D state/draw code.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
GENERATED = ROOT / "generated" / "default"

# PM4 type-3 opcodes (Xenos command processor), see the SDK's
# include/rex/graphics/xenos.h.
PM4_OPCODES = {
    0x10: "NOP", 0x21: "REG_RMW", 0x22: "DRAW_INDX", 0x23: "VIZ_QUERY",
    0x27: "IM_LOAD", 0x2B: "IM_LOAD_IMMEDIATE", 0x2D: "SET_CONSTANT",
    0x2F: "LOAD_ALU_CONSTANT", 0x36: "DRAW_INDX_2", 0x37: "INDIRECT_BUFFER_PFD",
    0x3B: "INVALIDATE_STATE", 0x3C: "WAIT_REG_MEM", 0x3D: "MEM_WRITE",
    0x3F: "INDIRECT_BUFFER", 0x44: "COND_EXEC", 0x45: "COND_WRITE",
    0x46: "EVENT_WRITE", 0x48: "ME_INIT", 0x4C: "FIX_2_FLT_REG",
    0x50: "SET_BIN_MASK_LO", 0x51: "SET_BIN_MASK_HI", 0x52: "SET_BIN_SELECT_LO",
    0x53: "SET_BIN_SELECT_HI", 0x54: "INTERRUPT", 0x55: "SET_CONSTANT2",
    0x56: "SET_SHADER_CONSTANTS", 0x58: "EVENT_WRITE_SHD",
    0x59: "EVENT_WRITE_EXT", 0x5A: "EVENT_WRITE_ZPD", 0x5E: "CONTEXT_UPDATE",
}

FUNC_RE = re.compile(r"^DEFINE_REX_FUNC\((\w+)\)")
VD_RE = re.compile(r"__imp__(Vd\w+)\(")
LIS_RE = re.compile(r"//\s*lis (r\d+),(-?\d+)")
ORI_RE = re.compile(r"//\s*ori (r\d+),(r\d+),(\d+)")
# Opcode materialized before the type bits: li rD,imm / addi rD,rS,imm.
OPCODE_FIRST_RE = re.compile(r"//\s*(?:li (r\d+),|addi (r\d+),r\d+,)(\d+)$")
ORIS_RE = re.compile(r"//\s*oris (r\d+),(r\d+),(\d+)")
WINDOW = 12  # instructions between the two halves of a header
# D3D's "make room in the command buffer" helper: compares the device's write
# cursor (+48) with its limit (+56) before each packet. Every caller writes
# GPU commands, including D3D code inlined into game functions.
MAKE_SPACE_CALL = "FastHelper_821E8D20(ctx, base)"


def address_of(name):
    match = re.search(r"([0-9A-F]{8})$", name)
    return int(match.group(1), 16) if match else None


def scan():
    vd_calls = defaultdict(Counter)   # function -> Counter(import)
    packets = defaultdict(Counter)    # function -> Counter(opcode name)
    for path in sorted(GENERATED.glob("fable_2_recomp.*.cpp")):
        function = None
        recent_lis = {}     # register -> (line index, upper half)
        recent_opcode = {}  # register -> (line index, opcode)
        for index, line in enumerate(path.open(encoding="utf-8", errors="replace")):
            match = FUNC_RE.match(line)
            if match:
                function, recent_lis, recent_opcode = match.group(1), {}, {}
                continue
            if function is None:
                continue
            for name in VD_RE.findall(line):
                vd_calls[function][name] += 1
            # Type-3 header: bits 31:30 = 3, bits 7:0 = 0, opcode in 15:8.
            match = LIS_RE.search(line)
            if match:
                recent_lis[match.group(1)] = (index, int(match.group(2)) & 0xFFFF)
                continue
            match = ORI_RE.search(line)
            if match:
                dst, src, imm = match.group(1), match.group(2), int(match.group(3))
                if imm & 0xFF == 0 and (imm >> 8) in PM4_OPCODES:
                    lis = recent_lis.get(src)
                    if lis and index - lis[0] <= WINDOW and lis[1] >> 14 == 3:
                        packets[function][PM4_OPCODES[imm >> 8]] += 1
                    else:
                        recent_opcode[dst] = (index, imm >> 8)
                continue
            match = OPCODE_FIRST_RE.search(line)
            if match:
                dst, imm = match.group(1) or match.group(2), int(match.group(3))
                if imm & 0xFF == 0 and (imm >> 8) in PM4_OPCODES:
                    recent_opcode[dst] = (index, imm >> 8)
                continue
            match = ORIS_RE.search(line)
            if match:
                dst, src, upper = match.group(1), match.group(2), int(match.group(3))
                if upper >> 14 == 3:
                    opcode = recent_opcode.get(src)
                    if opcode and index - opcode[0] <= WINDOW:
                        packets[function][PM4_OPCODES[opcode[1]]] += 1
                    else:
                        # Type bits first (often OR'd into the device's
                        # predication word), opcode by a later ori.
                        recent_lis[dst] = (index, upper)
                continue
            if MAKE_SPACE_CALL in line:
                packets[function]["(command buffer reserve)"] += 1
    return vd_calls, packets


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--json", type=Path, help="also write the full map here")
    args = parser.parse_args()
    if not any(GENERATED.glob("fable_2_recomp.*.cpp")):
        raise SystemExit(f"no generated code under {GENERATED}; run codegen first")
    vd_calls, packets = scan()

    print("== Vd* kernel graphics calls (only the D3D library makes these) ==")
    for function in sorted(vd_calls, key=lambda f: address_of(f) or 0):
        calls = ", ".join(f"{n}x{c}" if c > 1 else n for n, c in sorted(vd_calls[function].items()))
        print(f"  {function:44} {calls}")

    totals = Counter()
    for counter in packets.values():
        totals.update(counter)
    print(f"\n== PM4 packet headers built in code: {len(packets)} functions ==")
    for name, count in totals.most_common():
        writers = sum(1 for counter in packets.values() if name in counter)
        print(f"  {name:22} {count:5} sites in {writers:4} functions")

    for kind in ("DRAW_INDX", "DRAW_INDX_2", "IM_LOAD", "IM_LOAD_IMMEDIATE",
                 "SET_SHADER_CONSTANTS", "EVENT_WRITE_SHD", "EVENT_WRITE_EXT"):
        writers = sorted((f for f, c in packets.items() if kind in c),
                         key=lambda f: address_of(f) or 0)
        if writers:
            shown = ", ".join(writers[:12]) + (" ..." if len(writers) > 12 else "")
            print(f"\n{kind} writers ({len(writers)}): {shown}")

    if args.json:
        args.json.write_text(json.dumps({
            "vd_calls": {f: dict(c) for f, c in vd_calls.items()},
            "packets": {f: dict(c) for f, c in packets.items()},
        }, indent=1), encoding="utf-8")
        print(f"\nfull map written to {args.json}")


if __name__ == "__main__":
    main()
