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
rem   build.cmd -clean [t]       wipe the build dir for the config instead of
rem   build.cmd -c [t]             building. E.g. "build.cmd -clean
rem                                fable_2_profiler" deletes
rem                                out\build\win-amd64-release-profiling
setlocal
cd /d "%~dp0"

rem LLVM: prefer clang++ already on PATH, else the default install location
where clang++ >nul 2>nul
if errorlevel 1 (
    if exist "C:\Program Files\LLVM\bin\clang++.exe" set "LLVM=C:\Program Files\LLVM\bin"
    if not defined LLVM (
        echo Error: LLVM not found on PATH and no clang++.exe at C:\Program Files\LLVM\bin 1>&2
        echo        Install LLVM - https://llvm.org - or add its bin\ to PATH. 1>&2
        exit /b 1
    )
)
if defined LLVM set "PATH=%LLVM%;%PATH%"
rem Ninja: prefer one on PATH, else the user bin, else the WinGet package dir
where ninja >nul 2>nul
if errorlevel 1 (
    if exist "%USERPROFILE%\bin\ninja.exe" set "PATH=%USERPROFILE%\bin;%PATH%"
    for %%d in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\Ninja-build.Ninja_*\ninja.exe") do set "NINJADIR=%%~dpd"
    if defined NINJADIR set "PATH=%NINJADIR%;%PATH%"
)

rem ReXGlue SDK: prefer a rexglue.exe found on PATH (SDK root = <bin>\..),
rem then thirdparty\rexglue-sdk in this repo (fetched by tools\setup_sdk.cmd),
rem then a sibling install, else auto-download.
set "REXSDK="
for /f "delims=" %%f in ('where rexglue.exe 2^>nul') do (
    if not defined REXSDK for %%d in ("%%~dpf..") do set "REXSDK=%%~fdd"
)
if not defined REXSDK set "REXSDK=%~dp0thirdparty\rexglue-sdk\win-amd64"
if not exist "%REXSDK%\lib\cmake\rexglue\rexglueConfig.cmake" (
    set "REXSDK=%~dp0..\rexglue-sdk-0.10.0.9-dev.g923c1a5-win-amd64\win-amd64"
)
if not exist "%REXSDK%\lib\cmake\rexglue\rexglueConfig.cmake" (
    echo ReXGlue SDK not found; downloading via tools\setup_sdk.cmd ...
    call "%~dp0tools\setup_sdk.cmd" || exit /b 1
    set "REXSDK=%~dp0thirdparty\rexglue-sdk\win-amd64"
)
if not exist "%REXSDK%\lib\cmake\rexglue\rexglueConfig.cmake" (
    echo Error: ReXGlue SDK not found under %REXSDK% 1>&2
    exit /b 1
)

rem Argument parsing: -release / -r select the Release preset, -clean / -c
rem wipes the build dir instead of building, the first non-flag argument is
rem the CMake target (default: fable_2_codegen).
set "CONFIG=win-amd64-debug"
set "TARGET="
set "CLEAN=0"
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
if /i "%~1"=="-clean" (
    set "CLEAN=1"
    shift
    goto parse_args
)
if /i "%~1"=="-c" (
    set "CLEAN=1"
    shift
    goto parse_args
)
set "TARGET=%~1"
shift
goto parse_args
:args_done
if "%TARGET%"=="" set "TARGET=fable_2_codegen"

rem Special target: fable_2_profiler = Release (-O3) + symbols, but WITHOUT the
rem post-build stable-hash PE cleaning, so the exe keeps the linker's real PDB
rem UUID/Age and debuggers/profilers (VS, WinDbg, VTune) load fable_2.pdb
rem directly. Builds into out\build\win-amd64-release-profiling.
if /i "%TARGET%"=="fable_2_profiler" (
    set "CONFIG=win-amd64-release-profiling"
    set "TARGET=fable_2"
)

rem Clean: wipe the whole build dir for the selected config (a full clean is
rem just deleting the tree for a Ninja + CMake build) and stop.
if "%CLEAN%"=="1" (
    if exist "out\build\%CONFIG%" (
        rmdir /s /q "out\build\%CONFIG%"
        echo Cleaned out\build\%CONFIG%
    ) else (
        echo Nothing to clean: out\build\%CONFIG% does not exist
    )
    exit /b 0
)

rem SDK source (optional, Vulkan plugin): thirdparty, then a sibling install.
set "SDKSRC=%~dp0thirdparty\rexglue-sdk-src"
if not exist "%SDKSRC%\CMakeLists.txt" set "SDKSRC=%~dp0..\rexglue-sdk-src"
rem (inline -D with quotes at the call site: cmd cannot carry a quoted value
rem in a variable for paths with spaces)
if exist "%SDKSRC%\CMakeLists.txt" (
    cmake --preset %CONFIG% -DCMAKE_PREFIX_PATH="%REXSDK%" -DREXGLUE_SDK_ROOT="%REXSDK%" -DREXGLUE_SDK_SOURCE="%SDKSRC%" || exit /b 1
) else (
    cmake --preset %CONFIG% -DCMAKE_PREFIX_PATH="%REXSDK%" -DREXGLUE_SDK_ROOT="%REXSDK%" || exit /b 1
)
cmake --build out\build\%CONFIG% --target %TARGET%
