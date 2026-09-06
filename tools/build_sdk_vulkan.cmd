@echo off
rem ===========================================================================
rem Build the ReXGlue SDK's Vulkan GPU plugin from the SDK source + this
rem project's local patch (thirdparty/rexglue-sdk-local.patch).
rem
rem Uses a thin CMake wrapper (tools/vulkan_sdk) that add_subdirectory()s the
rem SDK, which skips the SDK's top-level install/export step (it fails on a
rem missing raw "SPIRV" target for the Vulkan-on-Windows config). The upstream
rem SDK repo is not forked; local changes live only in the tracked patch.
rem
rem Result: rexgpu-xenos.dll compiled with REXGLUE_USE_VULKAN=ON (D3D12 off),
rem plus its deps (rexruntime, glslang, VMA, SPIRV-Tools, Vulkan loader). The
rem SDK stages its outputs under its own out/ tree (REXGLUE_ROOT based).
rem
rem The game selects the renderer at launch via --gpu_plugin:
rem   --gpu_plugin=xenos           -> D3D12 (prebuilt plugin, default)
rem   --gpu_plugin=xenos-vulkan    -> Vulkan (this source-built plugin)
rem
rem Toolchain: clang + lld + Ninja Multi-Config (matches the SDK win-amd64
rem preset). This is a LONG build (fetches + compiles ~6 pinned deps).
rem ===========================================================================
setlocal
set "REPO=%~dp0.."
set "WRAPPER=%REPO%\tools\vulkan_sdk"
set "OUT=%REPO%\out\build\vulkan-sdk"
rem SDK source: thirdparty\rexglue-sdk-src in this repo (fetched + patched by
rem tools\setup_sdk_src.cmd if missing), else a sibling rexglue-sdk-src
rem install; its build outputs land in SDKOUT (under the SDK source root).
set "SDKSRC=%REPO%\thirdparty\rexglue-sdk-src"
if not exist "%SDKSRC%\CMakeLists.txt" set "SDKSRC=%REPO%\..\rexglue-sdk-src"
set "SDKOUT=%SDKSRC%\out"
if not exist "%SDKSRC%\CMakeLists.txt" (
    echo SDK source not found; fetching via tools\setup_sdk_src.cmd - LONG...
    call "%~dp0setup_sdk_src.cmd" || exit /b 1
    set "SDKSRC=%REPO%\thirdparty\rexglue-sdk-src"
    set "SDKOUT=%SDKSRC%\out"
)
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
set "PATH=%USERPROFILE%\bin;%PATH%"
where ninja >nul 2>nul
if errorlevel 1 (
    for %%d in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\Ninja-build.Ninja_*\ninja.exe") do set "PATH=%%~dpd;%PATH%"
)
echo === Wrapper: %WRAPPER%
echo === Build dir: %OUT%

echo === Configuring (reuses already-fetched deps; fast unless deps changed) ===
cmake -S "%WRAPPER%" -B "%OUT%" -G "Ninja Multi-Config" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_STANDARD=23 -DCMAKE_CONFIGURATION_TYPES=Release -DREXGLUE_SDK_SOURCE="%SDKSRC%"
if errorlevel 1 exit /b 1

echo === Building rexgpu-xenos + rexruntime Release (with PDBs) ===
cmake --build "%OUT%" --config Release --target rexgpu-xenos --target rexruntime
if errorlevel 1 (
    echo BUILD FAILED 1>&2
    exit /b 1
)
echo === Build OK. Locating plugin:
dir /s /b "%SDKOUT%\*rexgpu-xenos.dll" 2>nul
dir /s /b "%OUT%\*rexgpu-xenos.dll" 2>nul
endlocal
