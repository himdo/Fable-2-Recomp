#!/usr/bin/env python3
"""Argument shapes of the game's Lua API, read from the compiled script banks.

The banks hold Lua 5.1 bytecode (little-endian, 4-byte float numbers). For every
call to a named function this walks the bytecode, tracks what each register holds
(global names, table members, literals, call results) and records how many
arguments the call passes and what they are. It also records the parameter names
of functions the scripts define on tables (e.g. Debug.SetHeroLevel(level, ...)).

The register tracking is linear (it ignores branches), so a sample can now and
then show the wrong source for an argument; the argument counts are exact.

Usage:
  lua_callshapes.py <data dir> <out.md> [--json out.json] [--loose <dir>...]

  <data dir>  the game's data folder (holds gamescripts.bnk, guiscripts.bnk)
  --loose     extra folders of loose .lua bytecode (e.g. data\\scripts\\Startup)
"""
import argparse
import importlib.machinery
import json
import os
import struct
import sys
import types
from collections import defaultdict

OPS = ("MOVE LOADK LOADBOOL LOADNIL GETUPVAL GETGLOBAL GETTABLE SETGLOBAL SETUPVAL SETTABLE "
       "NEWTABLE SELF ADD SUB MUL DIV MOD POW UNM NOT LEN CONCAT JMP EQ LT LE TEST TESTSET CALL "
       "TAILCALL RETURN FORLOOP FORPREP TFORLOOP SETLIST CLOSE CLOSURE VARARG").split()

# Tables summarised in the Markdown output.
TABLES = ("Debug", "Health", "Money", "Stats", "Inventory", "Timing", "Camera", "GUI", "Player",
          "Sound", "Combat", "Dog", "Navigation", "Carrying", "Follow", "SpellManager")


def load_bnk_reader():
    here = os.path.dirname(os.path.abspath(__file__))
    loader = importlib.machinery.SourceFileLoader("bnk", os.path.join(here, "bnk.lua"))
    mod = types.ModuleType(loader.name)
    loader.exec_module(mod)
    return mod


class Reader:
    def __init__(self, b):
        self.b, self.p = b, 12  # skip the 12-byte Lua 5.1 header

    def byte(self):
        v = self.b[self.p]
        self.p += 1
        return v

    def int(self):
        v = struct.unpack_from("<i", self.b, self.p)[0]
        self.p += 4
        return v

    def str(self):
        n = struct.unpack_from("<I", self.b, self.p)[0]
        self.p += 4
        s = self.b[self.p:self.p + n]
        self.p += n
        return s[:-1].decode("latin-1") if n else None


def read_func(r):
    f = {"source": r.str()}
    r.int(); r.int()
    r.byte(); f["nparams"] = r.byte(); r.byte(); r.byte()
    n = r.int()
    f["code"] = list(struct.unpack_from("<%dI" % n, r.b, r.p))
    r.p += 4 * n
    k = []
    for _ in range(r.int()):
        t = r.byte()
        if t == 0:
            k.append(("nil", None))
        elif t == 1:
            k.append(("bool", bool(r.byte())))
        elif t == 3:
            k.append(("num", struct.unpack_from("<f", r.b, r.p)[0]))
            r.p += 4
        elif t == 4:
            k.append(("str", r.str()))
        else:
            raise ValueError("constant type %d" % t)
    f["k"] = k
    f["protos"] = [read_func(r) for _ in range(r.int())]
    n = r.int()  # line info
    r.p += 4 * n
    f["locvars"] = []
    for _ in range(r.int()):
        f["locvars"].append(r.str())
        r.int(); r.int()
    for _ in range(r.int()):
        r.str()  # upvalue names
    return f


def kdesc(c):
    t, v = c
    if t == "str":
        return '"%s"' % v
    if t == "num":
        return str(int(v)) if v == int(v) else "%g" % v
    return str(v).lower() if t == "bool" else "nil"


class Analysis:
    def __init__(self):
        self.calls = defaultdict(lambda: {"n": 0, "argc": defaultdict(int), "lits": set()})
        self.defs = {}

    def run(self, f, fname):
        reg = {}
        k = f["k"]
        for ins in f["code"]:
            opn = ins & 0x3F
            op = OPS[opn] if opn < len(OPS) else "?"
            a, c, b, bx = (ins >> 6) & 0xFF, (ins >> 14) & 0x1FF, (ins >> 23) & 0x1FF, ins >> 14
            if op == "GETGLOBAL":
                reg[a] = k[bx][1]
            elif op == "GETTABLE":
                base = reg.get(b)
                key = k[c - 256] if c >= 256 else None
                ok = base and key and key[0] == "str" and not base.startswith(('"', "?"))
                reg[a] = base + "." + key[1] if ok else "?"
            elif op == "SELF":
                key = k[c - 256][1] if c >= 256 and k[c - 256][0] == "str" else "?"
                reg[a + 1] = reg.get(b, "?")
                reg[a] = "self:" + key
            elif op == "LOADK":
                reg[a] = kdesc(k[bx])
            elif op == "LOADBOOL":
                reg[a] = "true" if b else "false"
            elif op == "LOADNIL":
                for i in range(a, b + 1):
                    reg[i] = "nil"
            elif op == "MOVE":
                reg[a] = reg.get(b, "?")
            elif op == "GETUPVAL":
                reg[a] = "upvalue"
            elif op in ("CALL", "TAILCALL"):
                fn = reg.get(a, "?")
                nargs = b - 1
                args = [reg.get(a + 1 + i, "?") for i in range(nargs)] if nargs >= 0 else ["..."]
                if fn and fn != "?" and not fn.startswith('"'):
                    name = fn
                    if fn.startswith("self:"):
                        name, args = ":" + fn[5:], args[1:]
                        nargs = nargs - 1 if nargs >= 0 else nargs
                    e = self.calls[name]
                    e["n"] += 1
                    e["argc"][str(nargs) if nargs >= 0 else "var"] += 1
                    if nargs > 0 and len(e["lits"]) < 400:
                        e["lits"].add(", ".join(args))
                for i in range(a, a + max(c - 1, 1)):
                    reg[i] = "ret:" + fn
            elif op == "NEWTABLE":
                reg[a] = "{}"
            elif op == "CLOSURE":
                reg[a] = "function#%d" % bx
            elif op == "SETTABLE":
                tbl = reg.get(a)
                key = k[b - 256] if b >= 256 else None
                v = reg.get(c) if c < 256 else None
                if tbl and key and key[0] == "str" and v and v.startswith("function#") \
                        and not tbl.startswith(("?", '"')):
                    p = f["protos"][int(v[9:])]
                    self.defs[tbl + "." + key[1]] = (p["locvars"][:p["nparams"]], fname)
            elif op == "SETGLOBAL":
                v = reg.get(a)
                if v and v.startswith("function#"):
                    p = f["protos"][int(v[9:])]
                    self.defs[k[bx][1]] = (p["locvars"][:p["nparams"]], fname)
            elif op in ("ADD", "SUB", "MUL", "DIV", "MOD", "POW", "UNM", "NOT", "LEN", "CONCAT",
                        "VARARG", "TESTSET"):
                reg[a] = "expr"
            elif op in ("FORLOOP", "FORPREP", "TFORLOOP"):
                for i in range(a, a + 4):
                    reg[i] = "loopvar"
        for p in f["protos"]:
            self.run(p, fname)

    def add(self, data, fname):
        if data[:5] != b"\x1bLuaQ":
            return False
        self.run(read_func(Reader(data)), fname)
        return True


def bank_entries(bnk_mod, path):
    raw = open(path, "rb").read()
    base, _, _, _, entries = bnk_mod.read(raw)
    for e in entries:
        if e["name"].lower().endswith(".lua"):
            yield e["name"], bnk_mod.payload(raw, base, e)


def write_md(an, out, sources):
    lines = [
        "# Fable 2 Lua API: argument shapes",
        "",
        "Generated by `tools/lua_callshapes.py` from " + ", ".join(sources) + ".",
        "For each function: how many call sites pass how many arguments, and up to eight",
        "distinct argument lists seen. `?` is a value the reader could not name, `expr` a",
        "computed value, `ret:F` the result of calling F. Argument counts are exact; the",
        "named sources are best effort.",
        "",
        "## Functions the scripts define (parameter names)",
        "",
    ]
    for name in sorted(an.defs):
        if name.split(".")[0] in TABLES:
            params, fname = an.defs[name]
            lines.append("- `%s(%s)`  (%s)" % (name, ", ".join(p or "?" for p in params), fname))
    for t in TABLES:
        names = sorted(n for n in an.calls if n.startswith(t + "."))
        if not names:
            continue
        lines += ["", "## " + t, "", "| Function | Calls | Arg counts | Examples |", "|---|---:|---|---|"]
        for n in names:
            e = an.calls[n]
            argc = ", ".join("%s args: %d" % (k, v) for k, v in sorted(e["argc"].items()))
            ex = " · ".join("`(%s)`" % s.replace("|", "\\|") for s in sorted(e["lits"])[:8])
            lines.append("| `%s` | %d | %s | %s |" % (n, e["n"], argc, ex))
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data")
    ap.add_argument("out_md")
    ap.add_argument("--json")
    ap.add_argument("--loose", nargs="*", default=[])
    args = ap.parse_args()

    bnk = load_bnk_reader()
    an = Analysis()
    sources, files = [], 0
    for name in ("gamescripts.bnk", "guiscripts.bnk"):
        path = os.path.join(args.data, name)
        if not os.path.exists(path):
            print("missing", path, file=sys.stderr)
            continue
        sources.append("`" + name + "`")
        for ename, data in bank_entries(bnk, path):
            files += an.add(data, ename)
    for d in args.loose:
        sources.append("loose scripts in `" + os.path.basename(os.path.normpath(d)) + "`")
        for fn in sorted(os.listdir(d)):
            if fn.lower().endswith(".lua"):
                files += an.add(open(os.path.join(d, fn), "rb").read(), fn)
    write_md(an, args.out_md, sources)
    if args.json:
        res = {k: {"n": v["n"], "argc": dict(v["argc"]), "lits": sorted(v["lits"])} for k, v in an.calls.items()}
        res["__defs__"] = an.defs
        json.dump(res, open(args.json, "w"), indent=1)
    print("%d scripts, %d called functions, %d defined functions -> %s" % (files, len(an.calls), len(an.defs), args.out_md))


if __name__ == "__main__":
    main()
