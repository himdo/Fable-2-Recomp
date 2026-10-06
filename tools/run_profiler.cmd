@echo off
rem ===========================================================================
rem Fable 2 - RELEASE-PROFILING launcher (frame rate = actual guest vblank).
rem
rem Launches this build's fable_2.exe with the flags that make the actual
rem (host) frame rate track the *actual* internal guest vblank, so a profiler
rem sees the game's real vblank cadence (not a fixed constant):
rem
rem   REX_PACE_TO_GUEST_VBLANK=1   gate the host present to the guest vblank -
rem                                at most one present per guest vblank
rem   REX_VIDEO_MODE_REFRESH_RATE  the configured guest vblank refresh rate, Hz
rem                                (the actual FPS follows the real vblank, which
rem                                 tracks this in guest time - it will be lower
rem                                 in real time if the guest runs slower than
rem                                 real time)
rem   REX_VSYNC=1                  guest waits on vblank (paces the game logic)
rem
rem Usage:
rem   run_profiler.cmd                 D3D12, 60 Hz (native NTSC) guest vblank
rem   run_profiler.cmd 30              D3D12, 30 Hz guest vblank
rem   run_profiler.cmd 50              D3D12, 50 Hz (PAL) guest vblank
rem   run_profiler.cmd vulkan          Vulkan, 60 Hz guest vblank
rem   run_profiler.cmd vulkan 30       Vulkan, 30 Hz guest vblank
rem
rem The refresh rate is the first non-backend argument (default 60). "vulkan"
rem requires the Vulkan SDK to be built and staged (tools\build_sdk_vulkan.cmd)
rem so rexgpu-xenos-vulkan.dll is present next to the exe; the default backend
rem is D3D12 ("xenos"), which is always available in the profiler build.
rem
rem Any further command-line arguments after the backend/rate are ignored.
rem ===========================================================================
setlocal
cd /d "%~dp0"

set "BACKEND=xenos"
set "RATE=60"
for %%A in (%*) do (
    if /i "%%A"=="vulkan" (
        set "BACKEND=xenos-vulkan"
    ) else if /i "%%A"=="d3d12" (
        set "BACKEND=xenos"
    ) else (
        rem First non-backend argument is the guest vblank refresh rate (Hz).
        set "RATE=%%A"
    )
)

set "REX_PACE_TO_GUEST_VBLANK=1"
set "REX_VIDEO_MODE_REFRESH_RATE=%RATE%"
set "REX_VSYNC=1"

echo run_profiler: guest vblank = %RATE% Hz, pacing = on, backend = %BACKEND% 1>&2

if not exist "fable_2.exe" (
    echo Error: fable_2.exe not found next to this script. 1>&2
    exit /b 1
)
"fable_2.exe" --gpu_plugin=%BACKEND%
endlocal
