@echo off
rem ===========================================================================
rem Fetch the prebuilt ReXGlue SDK (v0.10.0, win-amd64) into
rem thirdparty\rexglue-sdk\ (relative to the repo root). No-op if already
rem present.
rem
rem The release zip contains a top-level "win-amd64" folder, so the SDK root
rem (bin/, include/, lib/) ends up at:
rem   thirdparty\rexglue-sdk\win-amd64
rem
rem Run automatically by build.cmd when the SDK is missing; can also be run
rem manually.
rem ===========================================================================
setlocal
set "REPO=%~dp0.."
set "VER=0.10.0"
set "DEST=%REPO%\thirdparty\rexglue-sdk"
set "ZIPNAME=rexglue-sdk-%VER%-win-amd64.zip"
set "URL=https://github.com/rexglue/rexglue-sdk/releases/download/v%VER%/%ZIPNAME%"

if exist "%DEST%\win-amd64\lib\cmake\rexglue\rexglueConfig.cmake" (
    echo ReXGlue SDK v%VER% already present: %DEST%\win-amd64
    exit /b 0
)

where curl >nul 2>nul
if errorlevel 1 (
    echo Error: curl not found on PATH; built into Windows 10+. 1>&2
    exit /b 1
)

if not exist "%REPO%\thirdparty" mkdir "%REPO%\thirdparty"
if exist "%DEST%" rmdir /s /q "%DEST%"

echo Downloading %URL%
curl -L --fail -sS -o "%TEMP%\%ZIPNAME%" "%URL%"
if errorlevel 1 (
    echo Error: SDK download failed. 1>&2
    exit /b 1
)

echo Extracting to %DEST%
powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath '%TEMP%\%ZIPNAME%' -DestinationPath '%DEST%' -Force"
if errorlevel 1 (
    echo Error: SDK extraction failed. 1>&2
    exit /b 1
)
del /q "%TEMP%\%ZIPNAME%" 2>nul

if not exist "%DEST%\win-amd64\lib\cmake\rexglue\rexglueConfig.cmake" (
    echo Error: SDK incomplete after extract; expected %DEST%\win-amd64. 1>&2
    exit /b 1
)
echo OK: ReXGlue SDK v%VER% -> %DEST%\win-amd64
endlocal
