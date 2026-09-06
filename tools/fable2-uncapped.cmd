@echo off
rem ===========================================================================
rem Fable 2 - UNCAPPED launcher (60fps+, no frame cap).
rem
rem This is a thin wrapper around fable2.cmd that sets REX_VSYNC=0 before
rem launching. With vsync off, the ReXGlue vblank worker runs at ~1000 Hz
rem instead of 60 Hz, so the guest's per-frame "wait for the vblank counter
rem to advance 2 units" gate (2 x 16.6 ms = 33 ms = 30 fps) no longer paces
rem the game. The result is an uncapped frame rate (observed 20-100 fps
rem depending on scene).
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
