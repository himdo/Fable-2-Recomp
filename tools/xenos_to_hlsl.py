"""Convert a dumped Xbox 360 pixel shader into HLSL for a material shader.

A material shader replaces the ReXGlue SDK's Direct3D 12 translation of one
of the game's shaders (see materials/README.md). This writes a faithful HLSL
port of the guest shader - one statement per microcode instruction, with the
translator's semantics (materials/include/xenos_d3d12.hlsli) - that renders
the same as the translation, as the starting point for improving it.

Inputs, from running the game with --dump_shaders=<dir>:
  shader_<HASH>.ucode.frag                          the microcode disassembly
  shader_<HASH>_<MOD>.d3d12_rtv.bindings.txt        constants and descriptors
  (one bindings file per modification - every one found becomes a variant)

    python tools/xenos_to_hlsl.py <dump dir> <HASH> [-o materials/src/<HASH>.hlsl]
"""
import argparse
import re
import struct
import sys
from pathlib import Path

ROUNDING_OFFSET = 1.5 / 1024.0

VECTOR_OPS = {
    "add", "mul", "max", "min", "seq", "sgt", "sge", "sne", "frc", "trunc", "floor", "mad",
    "cndeq", "cndge", "cndgt", "dp4", "dp3", "dp2add", "cube", "max4", "setp_eq_push",
    "setp_ne_push", "setp_gt_push", "setp_ge_push", "kill_eq", "kill_gt", "kill_ge", "kill_ne",
    "dst", "maxa",
}
SCALAR_OPS = {
    "adds", "adds_prev", "muls", "muls_prev", "muls_prev2", "maxs", "mins", "seqs", "sgts",
    "sges", "snes", "frcs", "truncs", "floors", "exp", "logc", "log", "rcpc", "rcpf", "rcp",
    "rsqc", "rsqf", "rsq", "maxas", "maxasf", "subs", "subs_prev", "setp_eq", "setp_ne",
    "setp_gt", "setp_ge", "setp_inv", "setp_pop", "setp_clr", "setp_rstr", "kills_eq",
    "kills_gt", "kills_ge", "kills_ne", "kills_one", "sqrt", "mulsc", "addsc", "subsc", "sin",
    "cos", "retain_prev",
}
FETCH_OPS = {
    "tfetch1D": ("fetch", 1), "tfetch2D": ("fetch", 2), "tfetch3D": ("fetch", 3),
    "tfetchCube": ("fetch", "cube"), "getWeights1D": ("weights", 1),
    "getWeights2D": ("weights", 2), "getWeights3D": ("weights", 3),
    "getWeightsCube": ("weights", "cube"), "setTexLOD": ("set_lod", None),
}
COMPONENTS = "xyzw"


class ConvertError(Exception):
    pass


# Parsing.

class Operand:
    """A source operand: rN / cN with abs, negation, relative addressing and
    a swizzle."""

    def __init__(self, text):
        match = re.fullmatch(
            r"(-?)([rc])(_abs)?(?:\[(\d+)(?:\+(a0|aL))?\]|(\d+))(?:\.([xyzw01]+))?", text)
        if not match:
            raise ConvertError(f"unknown operand {text!r}")
        self.negate = match.group(1) == "-"
        self.kind = match.group(2)
        self.abs = match.group(3) is not None
        self.index = int(match.group(4) or match.group(6))
        self.relative = match.group(5)
        self.swizzle = match.group(7) or "xyzw"

    def base(self, shader):
        if self.kind == "r":
            if self.relative:
                raise ConvertError("relative register addressing is not supported")
            shader.registers.add(self.index)
            return f"r{self.index}"
        if self.relative:
            if not shader.float_dynamic:
                raise ConvertError("relative constant addressing without dynamic constants")
            register = "xe_a0" if self.relative == "a0" else "xe_aL"
            return f"xe_float_constants[{register} + {self.index}]"
        shader.constants.add(self.index)
        return f"c{self.index}"

    def expression(self, shader, components):
        """The operand's first `components` swizzled components."""
        value = self.base(shader)
        if self.abs:
            value = f"abs({value})"
        swizzle = (self.swizzle + self.swizzle[-1] * 4)[:components]
        value = f"{value}.{swizzle}"
        return f"-{value}" if self.negate else value

    def component(self, shader, index):
        """One swizzled component (scalar operations' a and b)."""
        value = self.base(shader)
        if self.abs:
            value = f"abs({value})"
        value = f"{value}.{self.swizzle[min(index, len(self.swizzle) - 1)]}"
        return f"-{value}" if self.negate else value


class Result:
    """A destination: rN, oCN, oPos, oDepth... with per-component sources
    (x/y/z/w, 0/1) or _ for unwritten components."""

    def __init__(self, text):
        match = re.fullmatch(r"(r|oC|oPos|oDepth|oPts|o|eA|eM)(\d*)(?:\.([xyzw01_]+))?", text)
        if not match:
            raise ConvertError(f"unknown result {text!r}")
        self.kind = match.group(1)
        self.index = int(match.group(2)) if match.group(2) else 0
        components = match.group(3)
        if components is None:
            components = "xyzw"
        elif components == "_":
            components = "____"
        self.components = (components + "____")[:4]

    @property
    def written(self):
        return any(c != "_" for c in self.components)

    def target(self, shader):
        if self.kind == "r":
            shader.registers.add(self.index)
            return f"r{self.index}"
        if self.kind == "oC":
            shader.color_targets.add(self.index)
            return f"xe_color{self.index}"
        if self.kind == "oDepth":
            raise ConvertError("depth output is not supported")
        raise ConvertError(f"pixel shaders can't write {self.kind}")


class Instruction:
    def __init__(self, number, predicate):
        self.number = number
        self.predicate = predicate  # None, True or False
        self.vector = None  # (opcode, saturate, result text, operand texts)
        self.scalar = None
        self.fetch = None  # (opcode, result text, operand texts, attributes)
        self.text = []


class Cf:
    def __init__(self, address, kind, **fields):
        self.address = address
        self.kind = kind
        self.__dict__.update(fields)
        self.instructions = []


def split_operands(text):
    return [part.strip() for part in text.split(",")]


def parse_op(text):
    """'mad_sat r0.__zw, r0.yyyx, c9.yyyx' -> ('mad', True, 'r0.__zw', [...])."""
    match = re.match(r"(\w+?)(_sat)?\s+(.*)", text)
    if not match:
        raise ConvertError(f"can't parse {text!r}")
    parts = split_operands(match.group(3))
    return match.group(1), match.group(2) is not None, parts[0], parts[1:]


def parse_predicate(text):
    match = re.match(r"\s*(\((!?)p0\))?\s*(.*)", text)
    predicate = None
    if match.group(1):
        predicate = match.group(2) != "!"
    return predicate, match.group(3).strip()


def parse_disassembly(text):
    cfs = []
    labels = {}
    current = None
    pending_label = None
    for raw_line in text.splitlines():
        line = raw_line.rstrip()
        if not line.strip():
            continue
        line = re.sub(r"\s*//.*$", "", line)
        label = re.fullmatch(r"\s*label L(\d+)", line)
        if label:
            pending_label = int(label.group(1))
            continue
        header = re.match(r"/\*\s*([\d.]+)\s*\*/(.*)", line)
        if header:
            number, rest = header.group(1), header.group(2)
            if "." in number:
                # Control flow.
                major, minor = number.split(".")
                address = int(major) * 2 + int(minor)
                if pending_label is not None and pending_label != address:
                    raise ConvertError(f"label L{pending_label} not at address {address}")
                if pending_label is not None:
                    labels[address] = True
                    pending_label = None
                predicate, body = parse_predicate(rest)
                words = body.replace(",", " ").split()
                opcode = words[0]
                if opcode in ("exec", "exece"):
                    current = Cf(address, "exec", end=opcode == "exece", predicate=predicate,
                                 bool_constant=None)
                elif opcode in ("cexec", "cexece"):
                    condition = words[1]
                    current = Cf(address, "exec", end=opcode == "cexece", predicate=None,
                                 bool_constant=(int(condition.lstrip("!b")),
                                                not condition.startswith("!")))
                elif opcode == "jmp":
                    current = Cf(address, "jmp", predicate=predicate, bool_constant=None,
                                 target=int(words[1].lstrip("L")))
                elif opcode == "cjmp":
                    condition = words[1]
                    current = Cf(address, "jmp", predicate=None,
                                 bool_constant=(int(condition.lstrip("!b")),
                                                not condition.startswith("!")),
                                 target=int(words[2].lstrip("L")))
                elif opcode in ("alloc", "cnop", "nop"):
                    current = Cf(address, "nop")
                else:
                    raise ConvertError(f"control flow {opcode!r} is not supported")
                cfs.append(current)
                continue
            # An instruction.
            instruction = Instruction(int(number), None)
            current.instructions.append(instruction)
            body = rest
        else:
            plus = re.match(r"\s*\+(.*)", line)
            if plus:
                # The scalar operation co-issued with the vector one.
                predicate, body = parse_predicate(plus.group(1))
                instruction = current.instructions[-1]
                instruction.text.append(body)
                instruction.scalar = parse_op(body)
                continue
            # The operation following `serialize`.
            instruction = current.instructions[-1]
            body = line
        predicate, body = parse_predicate(body)
        if not body or body == "serialize":
            continue
        if predicate is not None:
            instruction.predicate = predicate
        instruction.text.append(body)
        opcode = body.split()[0]
        base_opcode = re.sub(r"_sat$", "", opcode)
        if opcode == "nop":
            continue
        if base_opcode in FETCH_OPS:
            words = body.split(None, 1)
            parts = split_operands(words[1])
            attributes = {}
            operands = []
            for part in parts:
                if "=" in part:
                    key, value = part.split("=")
                    attributes[key] = value
                else:
                    operands.append(part)
            instruction.fetch = (base_opcode, operands, attributes)
        elif base_opcode in VECTOR_OPS:
            instruction.vector = parse_op(body)
        elif base_opcode in SCALAR_OPS:
            instruction.scalar = parse_op(body)
        else:
            raise ConvertError(f"unknown instruction {opcode!r}")
    return cfs


def parse_bindings(path):
    bindings = {"textures": [], "samplers": [], "float_constants": None}
    for line in path.read_text().splitlines():
        words = line.split()
        if not words:
            continue
        if words[0] == "float_constants":
            bindings["float_constants"] = (
                "dynamic" if words[1] == "dynamic" else [int(word) for word in words[2:]])
        elif words[0] in ("texture", "sampler"):
            fields = dict(word.split("=") for word in words[2:])
            bindings[words[0] + "s"].append(fields)
    return bindings


def parse_modification(value):
    """DxbcShaderTranslator::Modification::PixelShaderModification."""
    low, high = value & 0xFFFFFFFF, value >> 32
    return {
        "interpolator_mask": low & 0xFFFF,
        "centroid_mask": low >> 16,
        "param_gen": high & 1,
        "param_gen_interpolator": (high >> 1) & 0xF,
        "param_gen_point": (high >> 5) & 1,
        "depth_stencil_mode": (high >> 14) & 3,
    }


# Code generation.

def float_literal(value):
    if value == int(value):
        return f"{value:.1f}"
    return repr(struct.unpack("f", struct.pack("f", value))[0])


class Shader:
    def __init__(self, cfs, bindings, shader_hash):
        self.cfs = cfs
        self.bindings = bindings
        self.hash = shader_hash
        self.float_dynamic = bindings["float_constants"] == "dynamic"
        self.registers = set()
        self.constants = set()
        self.color_targets = set()
        self.kills = False
        self.lines = []
        self.indent = 1

    def emit(self, text):
        self.lines.append("  " * self.indent + text)

    # Operations.

    def vector_value(self, opcode, operands):
        a = lambda i: operands[i].expression(self, 4)
        if opcode == "add":
            return f"{a(0)} + {a(1)}"
        if opcode == "mul":
            return f"XeMul({a(0)}, {a(1)})"
        if opcode == "mad":
            return f"XeMad({a(0)}, {a(1)}, {a(2)})"
        if opcode in ("max", "min", "maxa"):
            if operands[0].__dict__ == operands[1].__dict__:
                return a(0)
            return f"Xe{'Max' if opcode != 'min' else 'Min'}({a(0)}, {a(1)})"
        simple = {"seq": "XeSeq", "sgt": "XeSgt", "sge": "XeSge", "sne": "XeSne", "dp4": "XeDp4",
                  "dp3": "XeDp3", "dst": "XeDst"}
        if opcode in simple:
            return f"{simple[opcode]}({a(0)}, {a(1)})"
        if opcode in ("frc", "trunc", "floor"):
            return f"{ {'frc': 'frac', 'trunc': 'trunc', 'floor': 'floor'}[opcode] }({a(0)})"
        if opcode in ("cndeq", "cndge", "cndgt"):
            return f"Xe{opcode[:3].capitalize()}{opcode[3:].capitalize()}({a(0)}, {a(1)}, {a(2)})"
        if opcode == "dp2add":
            return f"XeDp2Add({a(0)}, {a(1)}, {a(2)})"
        if opcode == "cube":
            return f"XeCube({a(0)})"
        if opcode == "max4":
            return f"XeMax4({a(0)})"
        if opcode.startswith("setp_") and opcode.endswith("_push"):
            condition = {"eq": "== 0.0", "ne": "!= 0.0", "gt": "> 0.0", "ge": ">= 0.0"}[
                opcode[5:7]]
            return f"XeSetpPush({a(0)}, ({a(1)}) {condition}, xe_p0)"
        raise ConvertError(f"vector {opcode} is not supported")

    def scalar_value(self, opcode, operands):
        a = operands[0].component(self, 0) if operands else None
        b = operands[0].component(self, 1) if operands else None
        c = operands[1].component(self, 0) if len(operands) > 1 else None
        table = {
            "adds": f"{a} + {b}", "adds_prev": f"{a} + xe_ps", "muls": f"XeMul({a}, {b})",
            "muls_prev": f"XeMul({a}, xe_ps)", "muls_prev2": f"XeMulsPrev2({a}, {b}, xe_ps)",
            "seqs": f"({a} == 0.0) ? 1.0 : 0.0", "sgts": f"(0.0 < {a}) ? 1.0 : 0.0",
            "sges": f"({a} >= 0.0) ? 1.0 : 0.0", "snes": f"({a} != 0.0) ? 1.0 : 0.0",
            "frcs": f"frac({a})", "truncs": f"trunc({a})", "floors": f"floor({a})",
            "exp": f"exp2({a})", "logc": f"XeLogC({a})", "log": f"log2({a})",
            "rcpc": f"XeClampInfinity(rcp({a}))", "rcpf": f"XeFlushInfinity(rcp({a}))",
            "rcp": f"rcp({a})", "rsqc": f"XeClampInfinity(rsqrt({a}))",
            "rsqf": f"XeFlushInfinity(rsqrt({a}))", "rsq": f"rsqrt({a})",
            "subs": f"{a} - {b}", "subs_prev": f"{a} - xe_ps", "sqrt": f"sqrt({a})",
            "mulsc": f"XeMul({a}, {c})", "addsc": f"{a} + {c}", "subsc": f"{a} - {c}",
            "sin": f"sin({a})", "cos": f"cos({a})",
        }
        if opcode in ("maxs", "mins"):
            if operands[0].swizzle[0] == (operands[0].swizzle[1:2] or operands[0].swizzle[0]):
                return a
            return f"Xe{'Max' if opcode == 'maxs' else 'Min'}({a}, {b})"
        if opcode in table:
            return table[opcode]
        raise ConvertError(f"scalar {opcode} is not supported")

    def store(self, result_text, value, saturate, replicated=False):
        result = Result(result_text)
        if not result.written:
            return
        target = result.target(self)
        mask = "".join(COMPONENTS[i] for i in range(4) if result.components[i] not in "_01")
        if mask:
            source = "".join("x" if replicated else result.components[i]
                             for i in range(4) if result.components[i] not in "_01")
            stored = f"({value}).{source}"
            if saturate:
                stored = f"saturate({stored})"
            self.emit(f"{target}.{mask} = {stored};")
        constant_mask = "".join(COMPONENTS[i] for i in range(4) if result.components[i] in "01")
        if constant_mask:
            constants = ", ".join(float_literal(float(result.components[i]))
                                  for i in range(4) if result.components[i] in "01")
            constant_type = "float" if len(constant_mask) == 1 else f"float{len(constant_mask)}"
            self.emit(f"{target}.{constant_mask} = {constant_type}({constants});")

    def emit_alu(self, instruction):
        vector, scalar = instruction.vector, instruction.scalar
        stores = []
        if vector:
            opcode, saturate, result_text, operand_texts = vector
            operands = [Operand(text) for text in operand_texts]
            if opcode == "cube":
                operands = operands[:1]
            result = Result(result_text)
            if opcode.startswith("kill_"):
                self.kills = True
                comparison = {"eq": "==", "gt": ">", "ge": ">=", "ne": "!="}[opcode[5:]]
                self.emit(f"xe_v = (any({operands[0].expression(self, 4)} {comparison} "
                          f"{operands[1].expression(self, 4)})) ? 1.0 : 0.0;")
                self.emit("if (xe_v.x != 0.0) {")
                self.emit("  discard;")
                self.emit("}")
                stores.append((result_text, "xe_v", saturate, True))
            elif result.written or opcode.startswith("setp_") or opcode == "maxa":
                replicated = opcode in ("dp4", "dp3", "dp2add", "max4") or opcode.startswith(
                    "setp_")
                self.emit(f"xe_v = {self.vector_value(opcode, operands)};")
                if opcode == "maxa":
                    self.emit(f"xe_a0 = int(clamp(floor({operands[0].component(self, 3)} + 0.5), "
                              f"-256.0, 255.0));")
                stores.append((result_text, "xe_v", saturate, replicated))
        if scalar:
            opcode, saturate, result_text, operand_texts = scalar
            operands = [Operand(text) for text in operand_texts]
            if opcode == "retain_prev":
                pass
            elif opcode.startswith("setp_"):
                a = operands[0].component(self, 0) if operands else "0.0"
                if opcode in ("setp_eq", "setp_ne", "setp_gt", "setp_ge"):
                    condition = {"setp_eq": f"{a} == 0.0", "setp_ne": f"{a} != 0.0",
                                 "setp_gt": f"0.0 < {a}", "setp_ge": f"{a} >= 0.0"}[opcode]
                    self.emit(f"xe_p0 = {condition};")
                    self.emit("xe_ps = xe_p0 ? 0.0 : 1.0;")
                elif opcode == "setp_inv":
                    self.emit(f"xe_ps = ({a} == 0.0) ? 1.0 : {a};")
                    self.emit(f"xe_p0 = {a} == 1.0;")
                    self.emit("xe_ps = xe_p0 ? 0.0 : xe_ps;")
                elif opcode == "setp_pop":
                    self.emit(f"xe_ps = {a} - 1.0;")
                    self.emit("xe_p0 = 0.0 >= xe_ps;")
                    self.emit("xe_ps = xe_p0 ? 0.0 : xe_ps;")
                elif opcode == "setp_clr":
                    self.emit("xe_ps = kXeFltMax;")
                    self.emit("xe_p0 = false;")
                elif opcode == "setp_rstr":
                    self.emit(f"xe_p0 = {a} == 0.0;")
                    self.emit(f"xe_ps = xe_p0 ? 0.0 : {a};")
                stores.append((result_text, "xe_ps", saturate, True))
            elif opcode.startswith("kills_"):
                self.kills = True
                a = operands[0].component(self, 0)
                condition = {"kills_eq": f"{a} == 0.0", "kills_gt": f"0.0 < {a}",
                             "kills_ge": f"{a} >= 0.0", "kills_ne": f"{a} != 0.0",
                             "kills_one": f"{a} == 1.0"}[opcode]
                self.emit(f"xe_ps = ({condition}) ? 1.0 : 0.0;")
                self.emit("if (xe_ps != 0.0) {")
                self.emit("  discard;")
                self.emit("}")
                stores.append((result_text, "xe_ps", saturate, True))
            elif opcode in ("maxas", "maxasf"):
                a = operands[0].component(self, 0)
                rounding = f"floor({a})" if opcode == "maxasf" else f"floor({a} + 0.5)"
                self.emit(f"xe_a0 = int(clamp({rounding}, -256.0, 255.0));")
                self.emit(f"xe_ps = {self.scalar_value('maxs', operands)};")
                stores.append((result_text, "xe_ps", saturate, True))
            else:
                self.emit(f"xe_ps = {self.scalar_value(opcode, operands)};")
                stores.append((result_text, "xe_ps", saturate, True))
        for result_text, value, saturate, replicated in stores:
            self.store(result_text, value, saturate, replicated)

    def texture_slots(self, fetch, dimension):
        binding_dimension = "cube" if dimension == "cube" else ("3d" if dimension == 3 else "2d")
        slots = {}
        for binding in self.bindings["textures"]:
            if int(binding["fetch"]) == fetch and binding["dimension"] == binding_dimension:
                slots[binding["signed"] == "1"] = int(binding["descriptor"])
        if False not in slots or True not in slots:
            raise ConvertError(f"no {binding_dimension} texture bindings for tf{fetch}")
        return slots[False], slots[True]

    def sampler_slot(self, fetch, attributes, computed_lod):
        names = {"point": "point", "linear": "linear", "basemap": "basemap", "keep": "keep"}
        mag = names[attributes.get("MagFilter", "keep")]
        min_ = names[attributes.get("MinFilter", "keep")]
        mip = names[attributes.get("MipFilter", "keep")]
        aniso = attributes.get("AnisoFilter", "keep") if computed_lod else "disabled"
        aniso = {"max1to1": "1", "max2to1": "2", "max4to1": "4", "max8to1": "8",
                 "max16to1": "16"}.get(aniso, aniso)
        if aniso not in ("disabled", "keep"):
            mag = min_ = mip = "linear"
        for binding in self.bindings["samplers"]:
            if (int(binding["fetch"]) == fetch and binding["mag"] == mag and
                    binding["min"] == min_ and binding["mip"] == mip and
                    binding["aniso"] == aniso):
                return int(binding["descriptor"])
        raise ConvertError(f"no sampler binding for tf{fetch} {mag} {min_} {mip} {aniso}")

    def emit_fetch(self, instruction):
        opcode, operands, attributes = instruction.fetch
        kind, dimension = FETCH_OPS[opcode]
        if kind == "set_lod":
            self.emit(f"xe_register_lod = {Operand(operands[0]).component(self, 0)};")
            return
        result = Result(operands[0])
        coords = Operand(operands[1])
        fetch = int(operands[2].lstrip("tf"))
        if attributes.get("UseRegisterGradients") == "true":
            raise ConvertError("register gradients are not supported")
        used = 0
        for component in result.components:
            if component in COMPONENTS:
                used |= 1 << COMPONENTS.index(component)
        if kind == "weights":
            used &= 0b0111
        if not used:
            self.store(operands[0], "0.0.xxxx", False)
            return
        unnormalized = attributes.get("UnnormalizedTextureCoords") == "true"
        offsets = [float(attributes.get(f"Offset{axis}", 0.0)) for axis in "XYZ"]
        if kind == "weights":
            if dimension != 2:
                raise ConvertError(f"{opcode} is not supported")
            values = [offsets[i] + ROUNDING_OFFSET - 0.5 for i in range(2)]
            flags = "XE_FETCH_UNNORMALIZED" if unnormalized else "0u"
            self.emit(f"xe_f = XeGetWeights2D({fetch}u, {coords.expression(self, 2)}, "
                      f"float2({', '.join(map(float_literal, values))}), {flags});")
            self.store(operands[0], "xe_f", False)
            return
        computed_lod = attributes.get("UseComputedLOD") != "false"
        flags = []
        if computed_lod:
            flags.append("XE_FETCH_COMPUTED_LOD")
        if attributes.get("UseRegisterLOD") == "true":
            flags.append("XE_FETCH_REGISTER_LOD")
        if attributes.get("MipFilter") == "basemap":
            flags.append("XE_FETCH_BASE_MAP")
        if unnormalized:
            flags.append("XE_FETCH_UNNORMALIZED")
        flags_text = " | ".join(flags) if flags else "0u"
        lod_bias = float_literal(float(attributes.get("LODBias", 0.0)))
        sampler = self.sampler_slot(fetch, attributes, computed_lod)
        unsigned_slot, signed_slot = self.texture_slots(fetch, dimension)
        common = f"{fetch}u, {sampler}u, {unsigned_slot}u, {signed_slot}u, 0x{used:X}u"
        if dimension == 1:
            call = (f"XeTextureFetch1D({common}, {coords.component(self, 0)}, "
                    f"{float_literal(offsets[0] + ROUNDING_OFFSET)}")
        elif dimension == 2:
            values = [offsets[i] + ROUNDING_OFFSET for i in range(2)]
            call = (f"XeTextureFetch2D({common}, {coords.expression(self, 2)}, "
                    f"float2({', '.join(map(float_literal, values))})")
        elif dimension == "cube":
            values = [offsets[0] + ROUNDING_OFFSET, offsets[1] + ROUNDING_OFFSET, offsets[2]]
            call = (f"XeTextureFetchCube({common}, {coords.expression(self, 3)}, "
                    f"float3({', '.join(map(float_literal, values))})")
        else:
            raise ConvertError(f"{opcode} is not supported")
        self.emit(f"xe_f = {call}, {flags_text}, {lod_bias}, xe_register_lod);")
        self.store(operands[0], "xe_f", False)

    def emit_instruction(self, instruction):
        for text in instruction.text:
            self.emit(f"// {instruction.number}: {text}")
        if instruction.predicate is not None:
            self.emit(f"if ({'' if instruction.predicate else '!'}xe_p0) {{")
            self.indent += 1
        if instruction.fetch:
            self.emit_fetch(instruction)
        else:
            self.emit_alu(instruction)
        if instruction.predicate is not None:
            self.indent -= 1
            self.emit("}")

    def condition(self, cf):
        if cf.bool_constant is not None:
            index, value = cf.bool_constant
            return f"{'' if value else '!'}XeBoolConstant({index}u)"
        if cf.predicate is not None:
            return f"{'' if cf.predicate else '!'}xe_p0"
        return None

    def generate_body(self):
        # Forward jumps become `if`s closed at their targets.
        open_targets = []
        for cf in self.cfs:
            while open_targets and open_targets[-1] == cf.address:
                open_targets.pop()
                self.indent -= 1
                self.emit("}")
            if cf.kind == "jmp":
                if cf.target <= cf.address:
                    raise ConvertError("backward jumps (loops) are not supported")
                if open_targets and cf.target > open_targets[-1]:
                    raise ConvertError("overlapping jumps are not supported")
                condition = self.condition(cf)
                not_taken = f"!({condition})" if condition else "false"
                self.emit(f"// jmp L{cf.target}")
                self.emit(f"if ({not_taken}) {{")
                self.indent += 1
                open_targets.append(cf.target)
                continue
            if cf.kind != "exec":
                continue
            condition = self.condition(cf)
            if condition:
                self.emit(f"if ({condition}) {{")
                self.indent += 1
            for instruction in cf.instructions:
                self.emit_instruction(instruction)
            if condition:
                self.indent -= 1
                self.emit("}")
            if cf.end:
                if open_targets or condition:
                    raise ConvertError("ending the shader inside a condition is not supported")
                break
        while open_targets:
            open_targets.pop()
            self.indent -= 1
            self.emit("}")


def generate(dump, shader_hash):
    ucode_path = dump / f"shader_{shader_hash}.ucode.frag"
    cfs = parse_disassembly(ucode_path.read_text())
    variants = []
    for path in sorted(dump.glob(f"shader_{shader_hash}_*.d3d12_rtv.bindings.txt")):
        modification = int(path.name.split("_")[2].split(".")[0], 16)
        variants.append((modification, parse_bindings(path)))
    if not variants:
        raise ConvertError(f"no shader_{shader_hash}_*.d3d12_rtv.bindings.txt in {dump}")
    bindings = variants[0][1]
    shader = Shader(cfs, bindings, shader_hash)
    shader.generate_body()

    float_constants = bindings["float_constants"]
    descriptor_count = max([int(b["descriptor"]) for b in bindings["textures"]] +
                           [int(b["descriptor"]) for b in bindings["samplers"]] + [0]) + 1
    registers = max(shader.registers | set(range(16))) + 1 if shader.registers else 16
    out = []
    out.append(f"// Material shader for the guest pixel shader {shader_hash} - generated by")
    out.append("// tools/xenos_to_hlsl.py from the dumped microcode as a faithful port of the")
    out.append("// translation (same output), then improved by hand.")
    out.append("//")
    out.append("// xe_modifications: " + " ".join(f"{m:016X}" for m, _ in variants))
    out.append("")
    if float_constants == "dynamic":
        out.append("#define XE_FLOAT_CONSTANT_COUNT 256")
    else:
        out.append(f"#define XE_FLOAT_CONSTANT_COUNT {max(len(float_constants), 1)}")
    out.append(f"#define XE_DESCRIPTOR_INDEX_VECTOR_COUNT {(descriptor_count + 3) // 4}")
    out.append('#include "../include/xenos_d3d12.hlsli"')
    out.append("")
    out.append("// The guest float constants.")
    if float_constants != "dynamic":
        for packed, constant in enumerate(float_constants):
            out.append(f"#define c{constant} xe_float_constants[{packed}]")
    else:
        for constant in sorted(shader.constants):
            out.append(f"#define c{constant} xe_float_constants[{constant}]")
    missing = sorted(shader.constants - set(float_constants if float_constants != "dynamic"
                                            else shader.constants))
    if missing:
        raise ConvertError(f"constants {missing} are not in the bindings")
    out.append("")
    # Variants: the build defines XE_MODIFICATION_INDEX.
    for index, (modification, _) in enumerate(variants):
        fields = parse_modification(modification)
        if fields["param_gen_point"]:
            raise ConvertError("point sprite parameters are not supported")
        if fields["depth_stencil_mode"] > 1:
            raise ConvertError("float24 depth modes are not supported")
        early = fields["depth_stencil_mode"] == 1 and not shader.kills
        out.append(f"{'#if' if index == 0 else '#elif'} XE_MODIFICATION_INDEX == {index}"
                   f"  // {modification:016X}")
        out.append("struct XeInput {")
        mask, centroid = fields["interpolator_mask"], fields["centroid_mask"]
        packed = 0
        for i in range(16):
            if mask & (1 << i):
                qualifier = "centroid " if centroid & (1 << i) else ""
                out.append(f"  {qualifier}float4 interpolator{i} : TEXCOORD{packed};")
                packed += 1
        out.append("  float4 position : SV_Position;")
        out.append("  bool is_front_face : SV_IsFrontFace;")
        out.append("};")
        out.append(f"#define XE_EARLY_DEPTH_STENCIL {1 if early else 0}")
        out.append(f"#define XE_INTERPOLATOR_MASK 0x{mask:X}")
        param_gen = fields["param_gen_interpolator"] if fields["param_gen"] else -1
        out.append(f"#define XE_PARAM_GEN_REGISTER {param_gen}")
    out.append("#endif")
    out.append("")
    out.append("#if XE_EARLY_DEPTH_STENCIL")
    out.append("[earlydepthstencil]")
    out.append("#endif")
    outputs = []
    for target in sorted(shader.color_targets):
        outputs.append(f"out float4 xe_out_color{target} : SV_Target{target}")
    signature = ",\n          ".join(["XeInput xe_input"] + outputs)
    if 0 in shader.color_targets:
        out.append("#if !XE_EARLY_DEPTH_STENCIL")
        out.append(f"void main({signature},\n          out uint xe_out_coverage : SV_Coverage) {{")
        out.append("#else")
        out.append(f"void main({signature}) {{")
        out.append("#endif")
    else:
        out.append(f"void main({signature}) {{")
    out.append("  // The registers: interpolators, the pixel parameters, the rest zeroed.")
    for i in range(registers):
        out.append(f"  float4 r{i} = 0.0;")
    out.append("#if XE_PARAM_GEN_REGISTER >= 0")
    out.append("  float4 xe_param_gen = XePsParamGen(xe_input.position, xe_input.is_front_face);")
    out.append("#endif")
    for i in range(16):
        out.append(f"#if XE_INTERPOLATOR_MASK & 0x{1 << i:X}")
        out.append(f"  r{i} = xe_input.interpolator{i};")
        out.append("#endif")
        if i < registers:
            out.append(f"#if XE_PARAM_GEN_REGISTER == {i}")
            out.append(f"  r{i} = xe_param_gen;")
            out.append("#endif")
    for target in sorted(shader.color_targets):
        out.append(f"  float4 xe_color{target} = 0.0;")
    out.append("  float4 xe_v = 0.0, xe_f = 0.0;")
    out.append("  float xe_ps = 0.0, xe_register_lod = 0.0;")
    out.append("  bool xe_p0 = false;")
    out.append("  int xe_a0 = 0;")
    out.append("")
    out.extend(shader.lines)
    out.append("")
    out.append("  // Epilogue.")
    if 0 in shader.color_targets:
        out.append("#if !XE_EARLY_DEPTH_STENCIL")
        out.append("  XeAlphaTest(xe_color0.a);")
        out.append("  xe_out_coverage = XeAlphaToCoverage(xe_color0.a, xe_input.position);")
        out.append("#endif")
    for target in sorted(shader.color_targets):
        out.append(f"  xe_out_color{target} = XeColorOutput(xe_color{target}, {target}u);")
    out.append("}")
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump", type=Path, help="the --dump_shaders directory")
    parser.add_argument("hash", help="the guest shader's hash (16 hex digits)")
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()
    try:
        hlsl = generate(args.dump, args.hash.upper())
    except ConvertError as error:
        raise SystemExit(f"{args.hash}: {error}")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with open(args.output, "w", newline="\n") as file:
            file.write(hlsl)
        print(f"wrote {args.output}")
    else:
        sys.stdout.write(hlsl)


if __name__ == "__main__":
    main()
