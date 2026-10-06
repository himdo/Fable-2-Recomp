@echo off
rem Matched Windows x64 runtime and dual-backend (D3D12 + Vulkan) GPU plugin. Optional second arg: Debug.
rem Run in a VS x64
rem developer shell with LLVM 20+, Ninja, CMake and Python on PATH.
setlocal
set "ROOT=%~dp0.."
set "SOURCE=%ROOT%\thirdparty\rexglue-sdk"
set "BUILD=%ROOT%\out\build\runtime-sdk"
set "STAGED=%ROOT%\out\tooling\runtime-sdk\win-amd64"
set "OFFICIAL_ARG=%~1"
set "OFFICIAL=%~1"
set "CONFIGURATION=%~2"
if not defined CONFIGURATION set "CONFIGURATION=Release"
if /i "%CONFIGURATION%"=="Debug" (
  set "CONFIGURATION=Debug"
  set "BUILD=%ROOT%\out\build\runtime-sdk-debug"
  set "STAGED=%ROOT%\out\tooling\runtime-sdk-debug\win-amd64"
) else if /i "%CONFIGURATION%"=="Release" (
  set "CONFIGURATION=Release"
) else (
  echo Error: runtime configuration must be Debug or Release. 1>&2
  exit /b 1
)
if not defined OFFICIAL set "OFFICIAL=%ROOT%\out\tooling\rexglue-sdk-0.10.0\win-amd64"
if not exist "%SOURCE%\CMakeLists.txt" (
  git -C "%ROOT%" submodule update --init thirdparty/rexglue-sdk || exit /b 1
)
rem Auto-fetch the official prebuilt SDK only when using the default location.
if not defined OFFICIAL_ARG if not exist "%OFFICIAL%\bin\rexglue.exe" (
  call "%~dp0setup_sdk.cmd" || exit /b 1
)
python "%~dp0prepare_runtime_sdk.py" "%SOURCE%" || exit /b 1
cmake -S "%SOURCE%" -B "%BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=%CONFIGURATION% -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ^
  -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2 ^
  -DREXGLUE_USE_D3D12=ON -DREXGLUE_USE_VULKAN=ON ^
  -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_ENABLE_FIDELITYFX=OFF ^
  -DREXGLUE_BUILD_TESTS=OFF || exit /b 1
cmake --build "%BUILD%" --target rexruntime rexgpu-xenos || exit /b 1
python "%~dp0stage_renderer_sdk.py" "%OFFICIAL%" "%SOURCE%" "%BUILD%" "%STAGED%" --configuration %CONFIGURATION% || exit /b 1
exit /b 0
