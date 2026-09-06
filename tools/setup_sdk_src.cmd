@echo off
rem ===========================================================================
rem Fetch the ReXGlue SDK *source* into thirdparty\rexglue-sdk-src\ (relative
rem to the repo root) at the exact commit this project was built with, pin
rem its submodules to the recorded SHAs, and apply this project's local SDK
rem patch. No-op if already present.
rem
rem This is only needed for the optional Vulkan GPU plugin (see
rem tools/build_sdk_vulkan.cmd); the prebuilt-SDK game build does not use it.
rem
rem WARNING: this is a LONG clone (several GB of submodules).
rem
rem Pinned inputs (all in thirdparty/, tracked in this repo):
rem   rexglue-sdk-local.patch            - local SDK modifications (main-menu
rem                                        crash fix, Vulkan + FPS work)
rem   rexglue-sdk-submodule-pins.txt     - submodule SHAs the current DLLs
rem                                        were built against
rem ===========================================================================
setlocal
set "REPO=%~dp0.."
set "SRC=%REPO%\thirdparty\rexglue-sdk-src"
set "PATCH=%REPO%\thirdparty\rexglue-sdk-local.patch"
set "PINS=%REPO%\thirdparty\rexglue-sdk-submodule-pins.txt"
set "PIN_COMMIT=f5337cdc947ff6d4c4196737e2c807a48f2a1fc2"

if exist "%SRC%\.git" (
    echo ReXGlue SDK source already present: %SRC%
    exit /b 0
)

where git >nul 2>nul
if errorlevel 1 (
    echo Error: git not found on PATH. Install Git for Windows first. 1>&2
    exit /b 1
)
if not exist "%PATCH%" (
    echo Error: missing %PATCH%; this file is tracked in the repo. 1>&2
    exit /b 1
)
if not exist "%PINS%" (
    echo Error: missing %PINS%; this file is tracked in the repo. 1>&2
    exit /b 1
)

if not exist "%REPO%\thirdparty" mkdir "%REPO%\thirdparty"

echo Cloning rexglue/rexglue-sdk (LONG: several GB of submodules)...
git clone https://github.com/rexglue/rexglue-sdk.git "%SRC%"
if errorlevel 1 (
    echo Error: SDK source clone failed. 1>&2
    exit /b 1
)

pushd "%SRC%"
git checkout -q %PIN_COMMIT%
if errorlevel 1 (
    echo Error: checkout of pinned commit %PIN_COMMIT% failed. 1>&2
    popd
    exit /b 1
)
git submodule update --init --recursive
if errorlevel 1 (
    echo Error: submodule update failed. 1>&2
    popd
    exit /b 1
)
rem Pin each submodule to the exact SHA this project's DLLs were built with.
for /f "usebackq tokens=1,2" %%a in (`findstr /v /c:"#" "%PINS%"`) do (
    echo   pin %%a -^> %%b
    git -C "%%a" checkout -q %%b
    if errorlevel 1 (
        echo Error: cannot pin %%a to %%b. 1>&2
        popd
        exit /b 1
    )
)
git apply --check "%PATCH%"
if errorlevel 1 (
    echo Error: local SDK patch does not apply cleanly. 1>&2
    popd
    exit /b 1
)
git apply "%PATCH%"
if errorlevel 1 (
    echo Error: applying local SDK patch failed. 1>&2
    popd
    exit /b 1
)
popd

echo OK: ReXGlue SDK source -> %SRC% (commit %PIN_COMMIT% + local patch)
endlocal
