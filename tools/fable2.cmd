@echo off
rem ===========================================================================
rem Fable 2 launcher - picks the GPU backend and stages the matching
rem rexruntime.dll + rexgpu-xenos.dll before launching fable_2.exe.
rem
rem   fable2.cmd            D3D12 (source-built plugin + runtime; F3 FPS works)
rem   fable2.cmd vulkan     Vulkan (source-built plugin + runtime; F3 FPS works)
rem   fable2.cmd prebuilt   D3D12 (prebuilt 0.10.0 plugin + runtime; no FPS)
rem
rem Why a launcher? The runtime file name is fixed (rexruntime.dll), so the
rem prebuilt D3D12-only runtime and the source runtime can't coexist. The
rem source plugin (rexgpu-xenos-vulkan.dll) is built with BOTH backends
rem enabled: "any" (the default) picks D3D12, "vulkan" picks Vulkan. The
rem prebuilt 0.10.0 runtime is D3D12-only and is missing the Vulkan symbols
rem the source plugin imports, so the source plugin must run against the
rem source runtime. This copies the right runtime + plugin into place, then
rem launches. The game is compiled against the prebuilt runtime's import lib;
rem both runtimes export the game's symbols, so it loads against either.
rem
rem   d3d12    -> rexruntime.dll    = source nightly (both backends)
rem              + rexgpu-xenos.dll = source nightly (both backends, "any"=D3D12)
rem              -> F3 debug overlay shows Guest FPS (source plugin records
rem                 per-swap frame timing in the shared perf registry).
rem   vulkan   -> rexruntime.dll    = source nightly (both backends)
rem              + rexgpu-xenos.dll = source nightly, forced Vulkan backend
rem   prebuilt -> rexruntime.dll    = prebuilt 0.10.0 (unchanged from a plain
rem              + rexgpu-xenos.dll = prebuilt D3D12 plugin (0.10.0)
rem              -> baseline behavior; no FPS counter (0.10.0 plugin was
rem                 compiled without perf counter support).
rem ===========================================================================
setlocal
cd /d "%~dp0"
set "MODE=d3d12"
if /i "%~1"=="vulkan" set "MODE=vulkan"
if /i "%~1"=="prebuilt" set "MODE=prebuilt"

if /i "%MODE%"=="vulkan" (
    if not exist "rexruntime-vulkan.dll" (
        echo Error: rexruntime-vulkan.dll not found. Build the Vulkan SDK first: 1>&2
        echo   tools\build_sdk_vulkan.cmd 1>&2
        exit /b 1
    )
    if not exist "rexgpu-xenos-vulkan.dll" (
        echo Error: rexgpu-xenos-vulkan.dll not found next to fable_2.exe. 1>&2
        exit /b 1
    )
    copy /y "rexruntime-vulkan.dll" "rexruntime.dll" >nul
    copy /y "rexgpu-xenos-vulkan.dll" "rexgpu-xenos.dll" >nul
    "fable_2.exe" --gpu_plugin=xenos-vulkan
) else if /i "%MODE%"=="d3d12" (
    if exist "rexgpu-xenos-vulkan.dll" (
        rem Source dual-backend plugin: default backend "any" picks D3D12.
        copy /y "rexruntime-vulkan.dll" "rexruntime.dll" >nul
        copy /y "rexgpu-xenos-vulkan.dll" "rexgpu-xenos.dll" >nul
        "fable_2.exe" --gpu_plugin=xenos
    ) else (
        rem Source plugin not built yet - fall back to the prebuilt 0.10.0 pair.
        echo Note: source GPU plugin not found; using prebuilt 0.10.0 D3D12. 1>&2
        echo       Run tools\build_sdk_vulkan.cmd to enable FPS in the F3 view. 1>&2
        if exist "rexruntime-d3d12.dll" copy /y "rexruntime-d3d12.dll" "rexruntime.dll" >nul
        "fable_2.exe" --gpu_plugin=xenos
    )
) else (
    rem prebuilt
    if exist "rexruntime-d3d12.dll" copy /y "rexruntime-d3d12.dll" "rexruntime.dll" >nul
    if exist "rexgpu-xenos-prebuilt.dll" copy /y "rexgpu-xenos-prebuilt.dll" "rexgpu-xenos.dll" >nul
    "fable_2.exe" --gpu_plugin=xenos
)
endlocal
