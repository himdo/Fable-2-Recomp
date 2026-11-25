# Legal risk and clean distribution: technical notes

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** 7863041

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
| `default.xex`, `/data/`, `/nxeart`, `/$SystemUpdate/`, `/Gamefiles/` | The ripped disc: the executable, the game data and the extra disc folders `tools/x360extract` writes. |
| `out`, `generated` | All build output, including the recompiled C++ (`generated/default/`). Only `generated/rexglue.cmake` is tracked (force-added). |
| `.fable2_image.bin` | The raw decrypted guest image that `tools/xex_image.py` and the Lua API analysis produce. |
| `/console/`, `fps_probe.log`, `fable2_func_*.log` | Runtime logs and console files that can land in the root. |
| `/scratch/`, `/*.ps1`, `/*.py`, `/*.sh`, `/*.png` in the root | Local scratch work (with `!build.sh` and `!run.sh` kept). |
| `.claude/*` (except `.claude/skills/`), `CLAUDE.local.md` | Personal AI-assistant files. |
| `thirdparty/rexglue-sdk-src/`, `thirdparty/android-ndk/` | Fetched toolchains, not ours to redistribute. |

Saves live in the build folder (`out/build/<preset>/saves/`, per the README), so they are covered by `out`.

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

`.github/workflows/tests.yml` runs on every pull request and on pushes to `master`, on `windows-latest`, with read-only repository permissions. It runs the version-constant check, pytest, the launcher tests and the native tests. **It never builds the game**, so CI needs no game files and none ever reach a runner.

### Only real discs are accepted

`constants/game_versions.json` lists the supported executables by SHA-256, size, media ID and required files (GOTY USA/Europe, German GOTY and compatible variants). The README says the launcher's **Extract from ISO** checks the disc image against this list before it extracts `default.xex` and `data/`. **Inference:** this ties every install to a retail disc the player owns.

### Debug-only channels

The remote-control server (localhost TCP port 8791, injects gamepad input) is compiled in only when `FABLE2_REMOTE_CONTROL` is defined, and `CMakeLists.txt` defines it only for Debug builds. The README says it "must not ship to players".

### Docs and generated references

- The "Never commit" list in `CLAUDE.md` forbids game files, XEX dumps, saves, shader dumps, game text, dialogue, scripts, asset data, logs with player or local paths, and personal AI files. The docs index (`docs/README.md`) repeats the rule for docs.
- `FABLE2_LUA_API.md` and `docs/reference/lua_call_shapes.md` were generated from the script banks, but they hold names, call counts and argument shapes, not script code. A skim of the quoted values in `lua_call_shapes.md` found key codes and file names, not dialogue.
- The README's opening note says the project doesn't condone piracy, and that mentions or links lead to removal and a ban.

### Licences

- `LICENSE` (repo root): MIT, copyright 2026 Himdo. It covers the project's own code, not the game.
- `thirdparty/XenosTiling/LICENSE.txt` and `thirdparty/tomlplusplus/LICENSE` are the bundled third-party licences. The release ships the XenosTiling one, and the keycap font ships with its OFL text.

### Gaps (Inference)

- No test checks that no game file is tracked in git. `.gitignore` only covers the usual names, and `git add -f` or an oddly named rip folder would get past it. A small pytest that fails if `git ls-files` lists `*.xex`, `*.bnk` or anything under `data/` would close that gap.
- `tools/xex_image.py` contains the retail XEX AES key in plain text. It is the key the Xbox 360 emulators already publish, but it is worth knowing it's there if the repo is ever reviewed.
- The README links an external extractor tool. That's fine, but any link to disc images would break the project's own rule.

## Design (proposed)

- Keep `generated/` out of releases and forks, as now.
- Never ship assets. Anything restored (see [cut_content](../cut_content/technical.md)) must be read from the player's own `data/` files at runtime.
- Don't target unreleased or current IP.
- Add the git-tracked-files test described under Gaps.

## What changed

Nothing. This is research only.

## Verification

- **Verified in this repo:** `.gitignore`, `tools/package_release.py` and `tests/tools/test_package_release.py`, `.github/workflows/tests.yml`, `constants/game_versions.json`, the `FABLE2_REMOTE_CONTROL` gate in `CMakeLists.txt`, the licence files, the README notes and the "Never commit" list in `CLAUDE.md`.

## Not tested

- No legal review. The Microsoft position rests on one secondary source.
- The packager test was read, not run, for this doc. `git ls-files` showed no game files tracked at 7863041.

## Sources

1. [Mario Odyssey And Skyward Sword Decomps Taken Down By Nintendo](https://www.thegamer.com/nintendo-takes-down-four-fan-ported-pc-games-dmca-wave/)
2. [GitLab Is Not Purging Recomp Projects. Two Throwaway Accounts Vanished and the Scene Could Not Tell the Difference](https://heldgames.com/guides/gitlab-recomp-takedowns)
3. [Nintendo DMCA Hits Odyssey & Zelda PC Ports - CDKeyPrices.com News](https://cdkeyprices.com/news/nintendo-takes-down-mario-odyssey-and-skyward-sword-recomp-ports)
4. [Ocarina of Time Remake on PC: DMCA Risks for Downloads and Tools](https://finalboss.io/ocarina-of-time-remake-on-pc-what-nintendos-dmca-claims-mean)
5. [Xbox Labs: Microsoft Backs AI Game Ports (Oct 2026)](https://www.explainx.ai/blog/xbox-labs-ai-game-decompiling-recompiling-ports-asha-sharma-2026)
6. [GitHub - andrewnakas/gtav-recomp: GTA V (Xbox 360, TU27) static recompilation with ReXGlue · GitHub](https://github.com/andrewnakas/gtav-recomp)
7. [\[Project\]: Fable-2-Recomp — Fable 2 Xbox 360 static recomp · C++ · Issue #192 · kaixinbaba/awesome-rea · GitHub](https://github.com/kaixinbaba/awesome-rea/issues/192)
