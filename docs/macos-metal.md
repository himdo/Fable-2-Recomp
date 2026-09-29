# Apple Silicon Metal backend

The optional Metal backend provides native Metal rendering through a ReXGlue
plugin. It requires Apple Silicon and an Xcode installation with the Metal
Toolchain component. See [backend build details](../backends/metal/README.md)
for dependencies, source provenance and licenses.

## Build and launch

Supply the extracted game content described in the main README, then run:

```sh
python3 tools/build_macos_metal.py
open 'out/build/mac-arm64-metal-release/Fable II.app'
```

The full build generates guest code, builds the converter, helper shaders and
renderer, and creates or refreshes the app. `--prepare-only` and `--backend-only`
do not produce a game app. Existing app output is replaced only after the new
bundle has been assembled and signature-verified.

The app includes its runtime libraries and can be moved to Applications. It
uses the source checkout as the initial game-content location. If that location
moves, the app asks for the folder containing `default.xex` and `data/` and
remembers the selection. Game content is never copied into the app.

For command-line use, `./run_macos_metal.command` starts Metal directly.
`./run_macos.command` prefers a built Metal runtime on Apple Silicon and otherwise
uses Vulkan. Set `FABLE2_MACOS_BACKEND=metal` to require Metal, or `vulkan` to
select Vulkan. Intel Macs use Vulkan.

## Saves and configuration

The app stores saves in `~/Library/Application Support/Fable II Recomp/saves`,
with settings, logs and caches in the parent directory. Metal and Vulkan apps
share this state and refuse concurrent game instances. Updating the app does
not replace user state.

Command launchers retain separate state beside their executables:

- Metal: `out/build/mac-arm64-metal-release/saves/`
- Vulkan: `out/build/mac-arm64-release/saves/`

There is no automatic save migration or merging. To migrate, close the game and
copy the entire existing `saves` folder into a new app state folder before first
launch. Preserve a backup; do not merge two existing save trees.
`FABLE2_GAME_DATA_ROOT` selects external game content for command launches without
changing their save location.

## Renderer configuration and limitations

The Metal launchers enable sequential register batching and two background
shader compilation workers. Constant-payload caching and forced block-compressed
texture expansion are disabled. The `[patches] hero_dog_texture_readback` setting
in `fable2_config.toml` controls morph readback (default `true`); explicit SDK or
command-line settings such as `--metal_fable_morph_readback=false` take priority.

Morph readback copies native-resolution RGBA8 color resolves at guest destination
`0x12704000` into guest RAM after GPU completion. The selection criteria derive
from [Femtofork for Fable II](https://github.com/just-harry/unofficial-xenia-femtofork-for-fable-ii).
The address is game-version-specific; scaled-resolution readback is unsupported.
Asynchronous shader compilation can skip pending draws. Failed geometry pipelines
are retried with backoff. Complete-playthrough coverage is not established.

The build applies the private SDK graphics patch from `backends/metal/` without
modifying the installed runtime or Windows SDK submodule. Generated-code hooks
in `tools/apply_startup_handoff.py` target a specific startup lock and validate
function shape before patching. The Metal launcher enables that workaround with
`FABLE_STARTUP_LOCK_HANDOFF=1`; it is not a general SDK locking fix. Diagnostic
render-loop cadence logging is opt-in with `FABLE2_FPS_METER=1` and does not
measure displayed frames.

## App packaging and testing

Standalone packaging is available through `python3 tools/package_macos_app.py`.
It creates `out/app/Fable II.app`; `--replace` permits replacing a generated app.
Packaging includes only selected runtime libraries, default settings and an icon.
Lua helpers are loaded from the external content folder's `data/scripts/recomp`.
No game content, saves, caches or generated guest source are packaged.

Apps are ad-hoc signed for local use, not Apple-notarized for distribution.
Their minimum macOS version is derived from the compiled binaries. Packaging
cannot lower that version. Omit `--game-data-root` when packaging for distribution
to avoid embedding a local checkout path.

Run the [Mac regression tests](macos.md#tests-and-current-limits) and the backend's
[renderer tests](../backends/metal/README.md#tests). For a bounded app startup check
using a **new disposable state folder**:

```sh
python3 tests/macos_app_tests.py --app 'out/build/mac-arm64-metal-release/Fable II.app' \
  --state out/validation/fresh-app-state --game-data-root "$PWD" \
  --report out/validation/app-startup.json
```

This requires desktop access, verifies Metal/input initialization, preserves
preexisting settings, checks bundle signatures and immutability, and stops only
the process it starts. Gameplay testing remains a separate manual check.
