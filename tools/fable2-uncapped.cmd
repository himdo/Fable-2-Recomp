@echo off
rem ===========================================================================
rem Fable 2 - UNCAPPED launcher (no 30fps cap; 60 fps floor by default).
rem
rem This is a thin wrapper around fable2.cmd that sets REX_VSYNC=0 before
rem launching. With vsync off, the ReXGlue vblank worker ticks at
rem no_vsync_vblank_hz (120 Hz by default) instead of 60 Hz, so the guest's
rem per-frame "wait for the vblank counter to advance 2 units" gate is ~16.7
rem ms (60 fps) instead of 33 ms (30 fps) - 2x native. The host present is
rem also paced to guest frames, so idle screens like the main menu stop at
rem ~60 fps instead of spinning the GPU at 100% forever; gameplay scenes
rem that render slower than 60 fps are unaffected.
rem
rem To restore the original fully-uncapped behavior (free-running menu, ~500
rem fps, GPU pinned), set REX_NO_VSYNC_VBLANK_HZ=1000 in the environment or
rem raise it live in the F3 console (GPU category).
rem
rem Usage (from this folder, next to fable_2.exe):
rem   fable2-uncapped.cmd             D3D12, uncapped
rem   fable2-uncapped.cmd vulkan      Vulkan, uncapped
rem
rem To run CAPPED (native 30 fps) instead, use fable2.cmd directly
rem (it leaves REX_VSYNC unset -> default true).
rem ===========================================================================
setlocal
cd /d "%~dp0"

rem The uncap lever: disable the SDK's vsync pacing.
set "REX_VSYNC=0"

rem fable2.cmd lives next to this file (both are staged together).
if not exist "fable2.cmd" (
    echo Error: fable2.cmd not found next to this launcher. 1>&2
    echo        Rebuild, or copy fable2.cmd into this folder. 1>&2
    exit /b 1
)

call fable2.cmd %*
endlocal
