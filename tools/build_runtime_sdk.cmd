@echo off
rem Build the SDK from the thirdparty\rexglue-sdk fork and stage it: runtime,
rem D3D12 plugin, codegen tool and CMake package. Optional arg: Debug or Release.
rem Debug needs the Release SDK too, because only that one ships the tool.
rem Run in a VS x64 developer shell with LLVM 20+, Ninja, CMake and Python on PATH.
setlocal
set "ROOT=%~dp0.."
set "SOURCE=%ROOT%\thirdparty\rexglue-sdk"
set "CONFIGURATION=%~1"
if not defined CONFIGURATION set "CONFIGURATION=Release"
if /i not "%CONFIGURATION%"=="Debug" if /i not "%CONFIGURATION%"=="Release" (
  echo Error: runtime configuration must be Debug or Release. 1>&2
  exit /b 1
)
if not exist "%SOURCE%\CMakeLists.txt" (
  git -C "%ROOT%" submodule update --init thirdparty/rexglue-sdk || exit /b 1
)
python "%~dp0prepare_runtime_sdk.py" "%SOURCE%" || exit /b 1
call :build_sdk Release || exit /b 1
if /i "%CONFIGURATION%"=="Debug" call :build_sdk Debug || exit /b 1
exit /b 0

:build_sdk
set "BUILD=%ROOT%\out\build\runtime-sdk"
set "STAGED=%ROOT%\out\tooling\runtime-sdk\win-amd64"
if "%~1"=="Debug" (
  set "BUILD=%ROOT%\out\build\runtime-sdk-debug"
  set "STAGED=%ROOT%\out\tooling\runtime-sdk-debug\win-amd64"
)
rem Older builds staged on top of the official SDK. Don't leave its files behind.
findstr /c:"rexglue-sdk-0.10.0" "%STAGED%\..\renderer-sdk-provenance.json" "%STAGED%\renderer-sdk-provenance.json" >nul 2>nul
if not errorlevel 1 rmdir /s /q "%STAGED%"
rem Only configure when needed: it stamps the SDK's version header, which would
rem rebuild the SDK on every run. The build reconfigures itself if CMake files change.
rem -g: always emit ReXGlue PDBs (Release + -g keeps production codegen but
rem produces full-symbol PDBs the fable_2 build stages next to the exe).
findstr /c:"REXGLUE_REGISTER_PACKAGE:BOOL=OFF" "%BUILD%\CMakeCache.txt" >nul 2>nul
if errorlevel 1 (
  cmake -S "%SOURCE%" -B "%BUILD%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%~1 -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ^
    -DCMAKE_C_FLAGS="-march=x86-64-v2 -g" -DCMAKE_CXX_FLAGS="-march=x86-64-v2 -g" ^
    -DREXGLUE_USE_D3D12=ON -DREXGLUE_USE_VULKAN=OFF ^
    -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_ENABLE_FIDELITYFX=OFF ^
    -DREXGLUE_BUILD_TESTS=OFF -DREXGLUE_REGISTER_PACKAGE=OFF ^
    -DCMAKE_INSTALL_MESSAGE=NEVER || exit /b 1
)
cmake --build "%BUILD%" || exit /b 1
cmake --install "%BUILD%" --prefix "%STAGED%" || exit /b 1
python "%~dp0stage_renderer_sdk.py" "%SOURCE%" "%STAGED%" --configuration %~1 --codegen-stamp "%ROOT%\generated\default\codegen.stamp" || exit /b 1
exit /b 0
