@echo off
rem Build the Fable 2 ReXGlue project.
rem
rem Toolchain: clang + lld + Ninja (the SDK headers use clang builtins, so
rem plain MSVC cl cannot compile the generated code). Preset: win-amd64-debug
rem (out\build\win-amd64-debug) by default, win-amd64-release with -release.
rem An MSVC-only preset (win-msvc-debug) exists in CMakePresets.json but only
rem works for targets that don't compile the generated code.
rem
rem Usage:
rem   build.cmd                  build fable_2_codegen (runs codegen from fable_2_manifest.toml)
rem   build.cmd fable_2          build the full recompiled executable
rem   build.cmd <other target>   build any other CMake target
rem   build.cmd -release [t]     build as Release (-O3) instead of Debug
rem   build.cmd -r [t]           (same, short form)
rem                              (out\build\win-amd64-release; stages the
rem                              release rexruntime/rexgpu-xenos plugins)
setlocal
cd /d "%~dp0"

set "LLVM=C:\Program Files\LLVM\bin"
if not exist "%LLVM%\clang++.exe" (
    echo Error: LLVM not found at %LLVM% 1>&2
    exit /b 1
)
rem Ninja: prefer one on PATH, else the user bin, else the WinGet package dir
where ninja >nul 2>nul
if errorlevel 1 (
    if exist "%USERPROFILE%\bin\ninja.exe" set "PATH=%USERPROFILE%\bin;%PATH%"
    for %%d in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\Ninja-build.Ninja_*\ninja.exe") do set "NINJADIR=%%~dpd"
    if defined NINJADIR set "PATH=%NINJADIR%;%PATH%"
)
set "PATH=%LLVM%;%PATH%"

set "REXSDK=%~dp0..\rexglue-sdk-0.10.0-win-amd64\win-amd64"
if not exist "%REXSDK%\lib\cmake\rexglue\rexglueConfig.cmake" (
    echo Error: ReXGlue SDK not found under %REXSDK% 1>&2
    exit /b 1
)

rem Argument parsing: -release / -r select the Release preset, the first
rem non-flag argument is the CMake target (default: fable_2_codegen).
set "CONFIG=win-amd64-debug"
set "TARGET="
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-release" (
    set "CONFIG=win-amd64-release"
    shift
    goto parse_args
)
if /i "%~1"=="-r" (
    set "CONFIG=win-amd64-release"
    shift
    goto parse_args
)
set "TARGET=%~1"
shift
goto parse_args
:args_done
if "%TARGET%"=="" set "TARGET=fable_2_codegen"

cmake --preset %CONFIG% -DCMAKE_PREFIX_PATH="%REXSDK%" || exit /b 1
cmake --build out\build\%CONFIG% --target %TARGET%
