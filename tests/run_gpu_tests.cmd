@echo off
rem GPU backend tests: need real GPU drivers (not run by run_native_tests.cmd).
rem Run in a VS x64 developer shell with LLVM, Ninja and CMake on PATH, after
rem build.cmd has staged the runtime SDK for that configuration.
rem   run_gpu_tests.cmd           Release (out\tooling\runtime-sdk)
rem   run_gpu_tests.cmd Debug     Debug   (out\tooling\runtime-sdk-debug)
setlocal
set "ROOT=%~dp0.."
set "CONFIG=Release"
set "SDK=%ROOT%\out\tooling\runtime-sdk\win-amd64"
if /i "%~1"=="Debug" (
  set "CONFIG=Debug"
  set "SDK=%ROOT%\out\tooling\runtime-sdk-debug\win-amd64"
)
set "OUT=%ROOT%\out\tests\gpu-%CONFIG%"
cmake -S "%~dp0gpu" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% ^
  -DCMAKE_CXX_COMPILER=clang++ ^
  -DCMAKE_PREFIX_PATH="%SDK%" -Drexglue_DIR="%SDK%\lib\cmake\rexglue" || exit /b 1
cmake --build "%OUT%" || exit /b 1
set "FAILED=0"
for %%M in (d3d12 vulkan nodriver) do (
  "%OUT%\test_gpu_backends.exe" %%M || set "FAILED=1"
)
exit /b %FAILED%
