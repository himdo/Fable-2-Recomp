# Legal risk and clean distribution: technical notes

**Status:** research; its proposed test merged · **Commits:** 1a5bc57 (#250, the tracked-files test) · **Last checked against:** c2a9d12

Claims are labelled **Verified** (checked in this repo or a primary source), **Reported** (a source says so, not checked here) or **Inference** (reasoned, not tested). This came out of the [November 2026 research](../../reference/research/README.md). These are notices and news reports, not legal advice or court judgments.

## Goal

Record the late-2026 takedowns and policy statements around recomps, and the distribution rules that keep Fable 2 Recomp on the safe side.

## Findings

### Nintendo, November 2026 (Reported)

Nintendo filed four DMCA notices at GitHub on 6 Nov 2026. Removed projects include PowerMoon (Super Mario Odyssey), SkyRecomp (Skyward Sword HD), Project Tandem (Double Dash) and MTF-Recomp (Mario Tennis Fever).\[1\]\[2\] MTF-Recomp stated it was "built with heavy help from an AI coding agent (Claude)".\[3\] Hookshot (Ocarina of Time remake recomp, more than 8,500 stars) was taken down on 13 Nov 2026.\[4\] Secondary coverage says ports of *current* titles and asset distribution were the common factors (partly unverified). The dates are corroborated across several aggregators.

### Microsoft (Reported)

Microsoft has not taken comparable action against Xbox 360 recomps, as far as the sources checked show. Secondary reporting describes an October 2026 "Xbox Labs" note by Asha Sharma that names decomps, recomps and ports as areas of interest; Microsoft did not answer The Verge on unauthorized IP use.\[5\] That framing comes from a single secondary source.

**Inference:** this lowers but does not remove the risk for a 2008 first-party title, especially with a new *Fable* due in 2027.

### Clean-distribution norms

- **Reported:** the GTA V recomp states that "the output of the recompiler is the game's code in another form… It isn't legal to hand it to someone else". Its rule is that no recompiled code, shaders or game files appear in any repository or release.\[6\]
- **Verified:** Fable 2 Recomp already ships no game data.\[7\] The rest of this section shows how the repo enforces that.

## How the repo enforces the boundary today

All **Verified** (read in the repo at 7863041).

### What git ignores

`.gitignore` keeps game content and build output out of commits:

| Entry | What it keeps out |
|---|---|
| `default.xex`, `/data/`, `/nxeart`, `/$SystemUpdate/`, `/Gamefiles/` | The ripped disc: the executable, the game data and the extra disc folders. `tools/x360extract`'s README names `nxeart` and `$SystemUpdate`; `Gamefiles` came in with the German GOTY support (bfcc5d6), and no tool in the repo writes it. |
| `out`, `generated` | All build output, including the recompiled C++ (`generated/default/`). Only `generated/rexglue.cmake` is tracked (force-added). |
| `.fable2_image.bin` | The raw decrypted guest image that `tools/xex_image.py` and the Lua API analysis produce. |
| `/console/`, `fps_probe.log`, `fable2_func_*.log` | Runtime logs and console files that can land in the root. |
| `/scratch/`, `/*.ps1`, `/*.py`, `/*.sh`, `/*.png` in the root | Local scratch work (with `!build.sh` and `!run.sh` kept). |
| `.claude/*` (except `.claude/skills/`), `CLAUDE.local.md` | Personal AI-assistant files. |
| `thirdparty/rexglue-sdk-src/`, `thirdparty/android-ndk/` | Fetched toolchains, not ours to redistribute. |

Saves live in the build folder (`out/build/<preset>/saves/`, per the README), so they are covered by `out`.

### What git tracks

`tests/test_tracked_files.py` (1a5bc57, #250) runs `git ls-files` and fails if git tracks anything in the rip's folders at the repo root (`data/`, `nxeart`, `$SystemUpdate/`, `Gamefiles/`, the same list as `.gitignore`) or any `.xex`, `.bnk` or `.bik` file, in any letter case. So `git add -f` or an oddly named rip folder can't slip past `.gitignore` unnoticed. A guard test checks that the tracked-file list isn't empty. The three file types are the only game types the repo's own tools mention.

**Verified against an unpacked disc** (GOTY USA/Europe, identified by its `default.xex` hash; Windows, 26 Nov 2026). The disc's `data/` folder holds these file types:

| Kind | Types | Covered by the test? |
|---|---|---|
| Already in the test | `.bnk` (127 files, about 5.7 GB), `.bik` (40, about 860 MB) | Yes |
| Game-only types | `.adb` (114), `.bft` (12), `.babel` (7), `.gdb` (7), `.sbk` (2), `.animation_data`, `.animation_toc`, `.loco`, `.amp`, `.vfsconfig` (1 each) | Only inside `data/`. Elsewhere in the repo they would pass |
| Common types | `.xml` (56), `.lua` (42), `.txt` (15), `.dds` (14), `.swf` (7), `.list` (6), `.csv` (2), `.manifest` (1) | No, on purpose: the repo has its own `.lua` files (13 in `src/lua/`), and blocking the others would block the repo's own files |

None of the game-only types is tracked on master. Adding them to the test's list is a follow-up. The `.lua` count includes the recomp's own scripts, which the build stages into `data/scripts/recomp/`.

### What a release contains

`tools/package_release.py` (run by `build.cmd package`) builds the player zip from an **allowlist**, not by copying the build folder:

- the build's `fable_2.exe`, `rexruntime.dll`, `rexgpu-xenos.dll` and `fable2_build.json` (with the version stamped in);
- `Fable2Launcher.exe`;
- the repo's `README.md` and `config/fable2_config.toml` (the repo copy, not the build folder's, which may hold a developer's own settings);
- `licenses/XenosTiling-LICENSE.txt`;
- the recomp's own Lua scripts from `data/scripts/recomp/*.lua` (written in `src/lua/`), but not `dir.manifest`, which comes from the disc;
- the bundled keycap font under `assets/fonts/`, with its OFL licence next to it.

Symbols (`*.pdb`) go into a separate zip, and `SHA256SUMS` covers both. Anything not on the list stays out.

`tests/tools/test_package_release.py` guards this. Its fake build folder holds `default.xex`, a save, a log, `data/dir.manifest`, a game data pack and a game script next to the real outputs. The test asserts that the zip holds **exactly** the listed files, so a change that let game files in would fail CI.

### CI

`.github/workflows/tests.yml` runs on every pull request and on pushes to `master`, on `windows-latest`, with read-only repository permissions. It runs the version-constant check, the manifest naming check, pytest (which includes the tracked-files test), the launcher tests and the native tests. **It never builds the game**, so CI needs no game files and none ever reach a runner.

### Only real discs are accepted

`constants/game_versions.json` lists the supported executables by SHA-256, size, media ID and required files (GOTY USA/Europe, German GOTY and compatible variants). The README says the launcher's **Extract from ISO** checks the disc image against this list before it extracts `default.xex` and `data/`. **Inference:** this ties every install to a retail disc the player owns.

### Debug-only channels

The remote-control server (localhost TCP port 8791, injects gamepad input) is compiled in only when `FABLE2_REMOTE_CONTROL` is defined, and `CMakeLists.txt` defines it only for Debug builds. The README says it "must not ship to players".

### Docs and generated references

- The "Never commit" list in `CLAUDE.md` forbids game files, XEX dumps, saves, shader dumps, game text, dialogue, scripts, asset data, logs with player or local paths, and personal AI files. The docs index (`docs/README.md`) repeats the rule for docs.
- `FABLE2_LUA_API.md` and `docs/reference/lua_call_shapes.md` were generated from the script banks. They hold names, call counts and argument shapes, not script code.
  - **Corrected on 26 Nov 2026:** `lua_call_shapes.md` did hold game text. Its example argument lists quoted 62 multi-word strings from the scripts, such as on-screen objectives, yes/no prompts, info-box text and developer debug messages.
  - `tools/lua_callshapes.py` now prints a string literal only when it looks like a key, an ID or a path. Anything else is printed as `"…"`. The doc was regenerated from the disc, and only those strings changed. `tests/tools/test_lua_callshapes.py` guards the rule.
  - The old text is still in git history (from #240).
  - `FABLE2_LUA_API.md` holds no quoted text.
- The README's opening note says the project doesn't condone piracy, and that mentions or links lead to removal and a ban.

### Licences

- `LICENSE` (repo root): MIT, copyright 2026 Himdo. It covers the project's own code, not the game.
- `thirdparty/XenosTiling/LICENSE.txt` and `thirdparty/tomlplusplus/LICENSE` are the bundled third-party licences. The release ships the XenosTiling one, and the keycap font ships with its OFL text.
- **Inference:** the release zip is missing two licences.
  - It ships `rexruntime.dll` and `rexgpu-xenos.dll` from the SDK, but not `thirdparty/rexglue-sdk/LICENSE`, which covers the Xenia-derived code.
  - `fable_2.exe` is built with toml++ (MIT), but its licence isn't in the zip either.
  - `tools/package_release.py` adds only `licenses/XenosTiling-LICENSE.txt`.

### Gaps (Inference)

- **Closed in #250:** nothing used to check that no game file is tracked in git; `tests/test_tracked_files.py` does now (see "What git tracks"). Its file-type list has now been compared with an unpacked disc. Still open: adding the ten game-only types found there.
- The release zip is missing two licences (see "Licences").
- `tools/xex_image.py` contains the retail XEX AES key in plain text. It is the key the Xbox 360 emulators already publish, but it is worth knowing it's there if the repo is ever reviewed.
- The README links an external extractor tool. That's fine, but any link to disc images would break the project's own rule.

## Design (proposed)

- Keep `generated/` out of releases and forks, as now.
- Never ship assets. Anything restored (see [cut_content](../cut_content/technical.md)) must be read from the player's own `data/` files at runtime.
- Don't target unreleased or current IP.
- Add the git-tracked-files test described under Gaps. Done in #250. Next: add the ten game-only disc types from "What git tracks" to its list.
- Ship `thirdparty/rexglue-sdk/LICENSE` and `thirdparty/tomlplusplus/LICENSE` in the release zip.

## What changed

This doc is research. The one change it proposed, the tracked-files test, merged as 1a5bc57 (#250): `tests/test_tracked_files.py` and its row in `tests/README.md`.

## Verification

- **Verified in this repo:** `.gitignore`, `tools/package_release.py` and `tests/tools/test_package_release.py`, `.github/workflows/tests.yml`, `constants/game_versions.json`, the `FABLE2_REMOTE_CONTROL` gate in `CMakeLists.txt`, the licence files, the README notes and the "Never commit" list in `CLAUDE.md`.
- **Verified on Windows with the disc** (26 Nov 2026):
  - the disc's file types (see "What git tracks");
  - `python -m pytest` passes, including the packager and tracked-files tests (493 passed, 1 skipped, 15 xfailed at c2a9d12);
  - `tools/lua_callshapes.py` run on the disc's banks reproduces the committed `lua_call_shapes.md` exactly, before the redaction change.

## Not tested

- No legal review. The Microsoft position rests on one secondary source.
- `git ls-files` showed no game files tracked at 7863041, and `tests/test_tracked_files.py` passes at c2a9d12.
- Only the GOTY USA/Europe disc was checked. Other versions (such as the German GOTY) may have more file types.

## Sources

1. [Mario Odyssey And Skyward Sword Decomps Taken Down By Nintendo](https://www.thegamer.com/nintendo-takes-down-four-fan-ported-pc-games-dmca-wave/)
2. [GitLab Is Not Purging Recomp Projects. Two Throwaway Accounts Vanished and the Scene Could Not Tell the Difference](https://heldgames.com/guides/gitlab-recomp-takedowns)
3. [Nintendo DMCA Hits Odyssey & Zelda PC Ports - CDKeyPrices.com News](https://cdkeyprices.com/news/nintendo-takes-down-mario-odyssey-and-skyward-sword-recomp-ports)
4. [Ocarina of Time Remake on PC: DMCA Risks for Downloads and Tools](https://finalboss.io/ocarina-of-time-remake-on-pc-what-nintendos-dmca-claims-mean)
5. [Xbox Labs: Microsoft Backs AI Game Ports (Oct 2026)](https://www.explainx.ai/blog/xbox-labs-ai-game-decompiling-recompiling-ports-asha-sharma-2026)
6. [GitHub - andrewnakas/gtav-recomp: GTA V (Xbox 360, TU27) static recompilation with ReXGlue · GitHub](https://github.com/andrewnakas/gtav-recomp)
7. [\[Project\]: Fable-2-Recomp — Fable 2 Xbox 360 static recomp · C++ · Issue #192 · kaixinbaba/awesome-rea · GitHub](https://github.com/kaixinbaba/awesome-rea/issues/192)
