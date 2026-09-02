@echo off
rem Build the Fable 2 ReXGlue project.
rem
rem Toolchain: clang + lld + Ninja (the SDK headers use clang builtins, so
rem plain MSVC cl cannot compile the generated code). Preset: win-amd64-debug
rem (out\build\win-amd64-debug). An MSVC-only preset (win-msvc-debug) exists
rem in CMakePresets.json but only works for targets that don't compile the
rem generated code.
rem
rem Usage:
rem   build.cmd                  build fable_2_codegen (runs codegen from fable_2_manifest.toml)
rem   build.cmd fable_2          build the full recompiled executable
rem   build.cmd <other target>   build any other CMake target
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

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=fable_2_codegen"

cmake --preset win-amd64-debug -DCMAKE_PREFIX_PATH="%REXSDK%" || exit /b 1
cmake --build out\build\win-amd64-debug --target %TARGET%
