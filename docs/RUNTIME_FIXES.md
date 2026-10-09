# Windows runtime fixes

The tested source baseline is `himdo/rexglue-sdk` commit
`1338ec1011739c7f00f8df9b9473e34d3dd9f2df` (upstream's dog rendering fix).
`thirdparty/rexglue-sdk-runtime-fixes.patch` carries the additional audio,
frame-pacing, Release FPS-counter and Windows export changes as source.
`thirdparty/rexglue-sdk-debug-exports.patch` adds the performance-counter and cvar
exports needed by Debug's GPU plugin, shutdown path and debug control server.
It applies separately so an SDK checkout already carrying the original patch
can upgrade without resetting it.
It also removes the hardcoded `LIBRARY rexruntime` name from the export table:
the linker must use its actual output name, so Debug imports `rexruntimed.dll`
rather than accidentally loading a leftover Release `rexruntime.dll`.
No SDK fork, binary download of a patched runtime, game files or saves are
required from this contribution. Original GOTY game files are still required
to generate and run the game.

## Build

Use a Visual Studio x64 developer shell with LLVM 20+, Ninja, CMake, Python
and the .NET 8 SDK on PATH. From the repository root:

```cmd
build.cmd -release fable_2
build.cmd launcher
tests\run_native_tests.cmd
dotnet run --project tests/Fable2.Launcher.ConfigTests -c Release
```

Both Debug and Release builds prepare the pinned SDK, apply the patch, build D3D12 runtime
and GPU targets, and stage matched headers, integration sources, import
libraries and DLLs. The host is rebuilt against that staged SDK. The official
0.10.0 codegen executable remains in a separate directory with its original
runtime; mixing newer native DLLs into its directory is not supported.
Patch conflicts and unexpected SDK revisions fail rather than overwrite edits.
`build.cmd fable_2` builds the Debug host and a patched Debug SDK;
`build.cmd -release fable_2` builds the Release pair. Debug SDK build/staging directories
are `out/build/runtime-sdk-debug` and `out/tooling/runtime-sdk-debug/win-amd64`;
Release keeps `out/build/runtime-sdk` and `out/tooling/runtime-sdk/win-amd64`.
Debug stages `rexruntimed.dll` and `rexgpu-xenosd.dll`, never substitutes the
official unpatched Debug pair, and still uses the separate official codegen tool.
After updating an existing checkout, rerun `build.cmd` rather than only building
an old CMake cache. Keep the EXE and DLLs together in the resulting build folder.
The startup log records `[native-build] configuration=... linked_sdk=...`.
This identifies the host's configured SDK, not DLLs manually replaced afterward.
CMake rejects a new staged SDK whose configuration does not match the host,
instead of silently selecting the official package's unpatched other configuration.
The unavailable upstream libmspack pin is replaced by its earlier public
`305907723a4e7ab2018e58040059ffb5e77db837` revision. The Windows helper only
materializes that revision's symlink blobs after validating their targets.

`thirdparty/rexglue-sdk` remains the pinned source SDK and is still built for
the runtime/renderer fixes. `out/tooling/rexglue-sdk-0.10.0` is only the official
prebuilt codegen package, not a replacement source checkout. Keeping it outside
the submodule avoids overwriting source/local edits: the previous download
script could recursively delete that same directory before extraction. The
staged native SDK also lives under `out/tooling`, keeping matched native
headers/libraries separate from the codegen tool's own runtime dependencies.
Put the published launcher beside the Release game EXE, `fable2_build.json`,
`app-icon.png` and its matched DLLs. The previous experimental Vulkan build
path is not the default for these fixes; do not replace this pair with DLLs
left over from that build. Other-platform configurations and Vulkan have not
been validated for this contribution; full Debug gameplay remains unverified.

## Behavior

When SDL cannot initialize or open an audio device, each affected audio client
gets a silent, clocked consumer at 256 samples / 48 kHz. It returns frame
permits at approximately 5.33 ms intervals, performs no guest-memory reads,
does not produce idle permits or catch-up bursts, and joins before shutdown.
Working SDL devices continue using the existing audio path. This is a startup
fallback, not hot-plug recovery or a general sound-quality fix.
`StartWithoutAudio.cmd` forces an invalid SDL3 backend in the launched process
only. It does not change Windows device settings. Start a fresh game/launcher
process for this test, select the game folder if necessary, then load a save
and exercise gameplay. Relaunch normally to test real sound output.

`frame_limit` is a host swap limiter: 0 disables it, with SDK range 0–240.
The launcher offers 30, 60, 120, 144, 165, 240 and unlimited. Fable waits for two guest vblanks;
the old 60-Hz guest pacing therefore held it at 30 even with a 60-FPS limit.
`guest_vblank_pacing` keeps the old SDK behavior by default, but the launcher
disables it for 60/unlimited independently of presentation VSync. The limiter
uses a private high-resolution waitable timer on Windows, with a standard wait
fallback, and does not change the guest clock or advertised video mode.
F3 counts guest swaps in Release, averaged over a short window and including
stalls; it does not count monitor refreshes or establish unique rendered frames.

Hero/dog resolve readback is the upstream fix, not a new implementation here.
Credit for the original approach/address goes to just-harry's Unofficial Xenia
femtofork, with the ReXGlue implementation maintained upstream. Explicit
readback configuration still takes precedence over the application's default.

The SDK's D3D12 renderer dropped draws while their pipeline was still being
created in the background (`async_shader_compilation`, on by default). A
dropped draw leaves its object missing for that frame, and when the dropped
pass renders into a texture, everything sampling that texture shows stale or
uninitialized data: noise bands, colored streaks and short bursts of
corruption, mostly when new effects or areas first appear. The fix is made in
rexglue-sdk itself (himdo/rexglue-sdk, not a patch here): draws wait for their
pipeline instead, with the waiting thread helping to create queued pipelines.
Async compilation stays on; only the first use of a pipeline can stall.
`async_pipeline_wait = false` in the SDK config restores the old skipping, and
`async_pipeline_wait_timeout_ms` (default 5000) bounds a single wait.

## Validation and remaining limits

Both unchanged USA/EU and German GOTY images started with the same native EXE.
On the German GOTY build the tester loaded a Version 1 adult-hero save and
confirmed normal hero and dog rendering. The tester also confirmed up to
60 FPS with the cap and approximately 150 FPS in unlimited mode. Launcher
tests cover edition validation, output/render mappings, FPS mappings,
preferences surviving runtime rewrites, unknown-setting preservation and
dropdown contrast. Synthetic audio, limiter and FPS-meter tests passed, and
the patch applied cleanly and idempotently to a fresh SDK checkout.
Both build paths were subsequently checked with a fresh pinned source SDK
and full `build.cmd fable_2` / `build.cmd -release fable_2` builds.
The Debug EXE and plugin were checked to import `rexruntimed.dll`; staged DLL
code sections match the built SDK after the existing PE metadata normalization.
Staging tests check both configurations
and fail before copying if Debug inputs are missing. The SDK follow-up patch
was also checked on an SDK already carrying the original patch, and reapplies
idempotently. A deliberately mismatched host/SDK configuration was rejected.

The forced missing-audio-backend build started successfully for the tester;
SDL failure-path timing and shutdown were also checked separately. This does
not establish every physical-device failure, audible playback on all devices,
or unplug/replug recovery. There is no claim that long-session simulation,
cutscenes or timing above 60 FPS are fully correct. Frame drops below 20 FPS
were reported and remain unresolved. Other regional/TU revisions, exhaustive
save compatibility and remaster features are outside this change.
