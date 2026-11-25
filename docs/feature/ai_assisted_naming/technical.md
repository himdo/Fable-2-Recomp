# AI-assisted function naming: technical notes

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** 7863041

Claims are labelled **Verified** (checked in this repo or a primary source), **Reported** (a source says so, not checked here) or **Inference** (reasoned, not tested). This came out of the [November 2026 research](../../reference/research/README.md).

## Goal

Learn what the reverse-engineering scene found in 2026 about LLM agents naming functions, and decide what Fable 2 Recomp needs so AI-proposed names in `fable_2_manifest.toml` stay trustworthy. The process this leads to is in [function_mapping](../function_mapping/technical.md).

## Findings

### Naming works only with external memory and verification

- **Reported:** a June 2026 arXiv study (arXiv:2606.06838) by Neil Archibald and Ruben Thijssen of the Commonwealth Bank of Australia gave an LLM agent direct control of Ghidra through LaurieWired's GhidraMCP. The authors report that this phase "suffered from incomplete coverage and inconsistent improvements due to lack of quantitative guidance". The agent improved the first functions, then degraded, and later "misremember[ed] Function A's signature", producing inconsistent naming across the binary.\[1\]\[2\]
- **Verified:** Fable 2 Recomp has the same problem. PR #238 flags `QuatScaleToMatrix_8222E6C0` as the least certain of its names, and that name replaced the earlier, wrong `ComputeVectorHash`.\[3\] Confident wrong names already exist in the manifest (examples below).

### Wrong and uncertain names in the manifest's history (Verified, from `git log` on `fable_2_manifest.toml`)

| Name | What happened | How it was caught |
| --- | --- | --- |
| `KeWaitForSingleObject`, `KeWaitForMultipleObjects` | Two guest-internal wait wrappers were named after the kernel exports. Codegen emits `__imp__<name>`, so the names silently shadowed the runtime's kernel hooks and broke audio. Renamed to `KeWaitForSingleObject_Guest` / `KeWaitForMultipleObjects_Guest` on 2026-09-14 (f8678b1, "Naming more functions and fixing sounds"). | By the symptom (no audio). `tools/check_manifest_collisions.py` followed on 2026-11-01 (833532c) and its tests on 2026-11-04 (d776be2). `tests/tools/test_manifest_collisions.py` still asserts on the real manifest that the bare names stay free. |
| `ComputeVectorHash_8222E6C0` | Arrived in a batch naming commit on 2026-09-16 (1522cfd), named from an automatic description of the instruction shape. It is a quaternion-plus-scale → matrix routine. Renamed `QuatScaleToMatrix_8222E6C0` on 2026-11-21 (#238), so it was wrong for about two months. | A human reading the body for the cloth work. The new name is still labelled Inference in its comment ("Inference from the instruction shape; 29 calling files"). Automatic comments on other entries still mention `ComputeVectorHash` 9 times on master. |
| `LuaBind_RunScript_82806168` | Still in the manifest. The comment in `src/core/fable2_f5_lua.h` says it was "named 'LuaBind_RunScript' by the decompiler, but it is the general thunk every Lua->C++ method call goes through", so the name is too narrow. | Runtime probing for the F5 Lua runner. Not renamed yet. |

**Inference:** all three follow the study's pattern. A fast first pass produced plausible names; nothing recorded how each was known; later work had to rediscover the truth. The batch passes in September 2026 took the manifest from 998 entries (16 Sep) to 4,842 (21 Sep), 2,499 of them `ProcessAndProcessAndProcess<N>_` placeholders. On master today no manifest comment uses the word "Verified" and one uses "Inference" ([function_mapping](../function_mapping/technical.md#where-the-manifest-stands-verified-measured-at-7863041)).

### How conventions are enforced today (Verified)

| Where | What it enforces | Automatic? |
| --- | --- | --- |
| `CLAUDE.md` ("Naming functions") | `Subsystem_WhatItDoes_ADDRESS`, a comment block with evidence, rename in place, update every old use, no clash with runtime exports | No (instructions) |
| `.claude/skills/name-functions/SKILL.md` (added 2026-11-23, #240) | The same, plus **Verified**/**Inference** labels, reusing existing subsystem prefixes, and a TOML parse and collision check before the PR | No (instructions an agent follows) |
| `tools/check_manifest_collisions.py` | Only that no name equals a runtime DLL export (`__imp__<name>` or the bare name). It needs a built SDK's DLL. | Yes, run by hand |
| `tests/tools/test_manifest_collisions.py` | The tool's behaviour with fake DLLs, plus the two kernel-wait names on the real manifest | Yes, in CI (pytest) |

Nothing checks the name format, the address suffix, or whether a comment or evidence label exists. **Inference:** that is the gap ghidra-mcp's tool-layer convention enforcement closes, and the convention check proposed in [function_mapping](../function_mapping/technical.md#a-convention-check-in-toolscheck_manifest_collisionspy-proposed) would close it here.

### Tooling elsewhere (Reported)

- **GhidraMCP** (LaurieWired, 12.0k stars, 1.3k forks) exposes decompile, rename, cross-reference and import queries to any MCP client. bethington's **ghidra-mcp v5.0** adds "convention enforcement" at the tool layer, which forces agents to follow naming rules.\[4\]
- **pyghidra-mcp** and **RE-MCP** (formerly ida-mcp; gained a Ghidra backend in May 2026, including Ghidra's Function ID library matching).\[5\]
- **REA 6.7** (Oct 2026) wraps Ghidra, Hopper or IDA. Its example rebuilt a function that matched the original on 3,205 test inputs and 63 bytes of machine code.\[6\]
- **SK2Decompile** (arXiv 2509.22114) separates structure recovery from identifier naming, which supports treating naming as its own verified step.

**Verified:** none of these tools is set up in this repo. The naming work so far reads the PowerPC kept as comments in `generated/default/*.cpp` (see `FUNCTION_NAMES.md`, "How these were determined"), not a disassembler project.

### The project's AI use

- **Verified:** the README's opening paragraph says the project "was made to test the capabilities of local and open source AI", using Qwen3.8 27b, which "got the game running" in about an hour, and that "a large amount of human trial and error" then mapped enough functions to finish the game.\[7\]
- **Verified:** 119 of master's 364 commits carry at least one `Co-Authored-By: Claude …` trailer (218 trailer lines in all, across several Claude model versions), counted with `git log master` at 7863041.
- **Verified:** the debug-only remote-control server is described in the README as an input channel so an AI harness can drive the game with "no human at the keyboard" (design: `plans/ai-remote-input-control.md`). The tooling for agent-run checks already exists in part ([function_mapping](../function_mapping/technical.md#tie-in-with-the-automated-checking-loop)).
- **Reported:** Emulation King labels the project "Mostly AI-built", and recomp.gg says "people wrote the code with an AI's help".\[8\]\[9\] Contributor PRs carry Claude co-author trailers.\[10\]\[11\]

## Design (proposed)

- Treat the manifest and its per-entry evidence comments as the external memory these agents lack. An agent starting a session reads the comments, not its own earlier context.
- Agents propose names and draft evidence. A name merges only with an [evidence level](../function_mapping/technical.md#evidence-levels).
- Copy ghidra-mcp's tool-layer convention enforcement by adding a no-DLL mode to `tools/check_manifest_collisions.py` (format, address suffix, comment and evidence label present on new or renamed entries), tested in `tests/tools/test_manifest_collisions.py` so CI runs it.
- When a name is proved wrong, also fix the old name in other entries' comments (the 9 stale `ComputeVectorHash` mentions), so a later agent doesn't re-learn the wrong name from them. The `/name-functions` skill already asks for this in step 5 for code and docs. Manifest comments are where agents read it back.
- **Inference:** disclosure is already public, so the defensible position is verifiability. Every AI-proposed name carries evidence and a test, which answers "AI slop" criticism with checkable artefacts.

## What changed

Nothing in the code. This is research only.

## Verification

- **Verified in this repo, at 7863041:** the three name histories above (`git log -S` and `git show` on `fable_2_manifest.toml`); `QuatScaleToMatrix_8222E6C0` and its Inference note; the comment in `src/core/fable2_f5_lua.h`; the README text; the trailer counts (`git log master --format=%B`); the four enforcement points and what they check.

## Not tested

- None of the MCP tools were tried against this binary.
- The trailer count shows commits made with an AI's help, not how much of each commit the AI wrote.

## Sources

1. [Improving Decompiled Code Readability with LLM Agents](https://medium.com/commbank-technology/improving-decompiled-code-readability-with-llm-agents-f67bf10d2505)
2. [1 Introduction](https://arxiv.org/html/2606.06838)
3. [Name the game functions used by recent PRs by delta4o · Pull Request #238 · himdo/Fable-2-Recomp](https://github.com/himdo/Fable-2-Recomp/pull/238)
4. [ghidra-mcp](https://www.bioplayground.com/en/ai-tools/ghidra-mcp)
5. [IDA-MCP Is Now RE-MCP With Ghidra Support](https://jtsylve.blog/post/2026/05/04/ida-mcp-becomes-re-mcp)
6. [Reverse Engineer Anything Explained (2026): Ghidra via MCP](https://www.sevydevy.com/learn/reverse-engineer-anything-explained-ai-agents-ghidra-mcp)
7. [GitHub - himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp)
8. [Fable 2 Recomp: Fable II PC Port - Emulation King](https://emulationking.com/microsoft/xbox360/games/fableii/ports/fable2recomp/)
9. [Fable-2-Recomp: Fable II recompilation · recomp.gg](https://recomp.gg/p/fable-2-recomp)
10. [Make dir.manifest self-heal idempotent and drop duplicate lines by Locme · Pull Request #61 · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/pull/61)
11. [Add tests for the manifest collision check and the PE normalizer by delta4o · Pull Request #134 · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/pull/134)
