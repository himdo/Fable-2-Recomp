@echo off
rem ===========================================================================
rem Fetch the prebuilt ReXGlue SDK (v0.10.0, win-amd64) into
rem out\tooling\rexglue-sdk-0.10.0\ (relative to the repo root). No-op if already
rem present.
rem
rem The release zip contains a top-level "win-amd64" folder, so the SDK root
rem (bin/, include/, lib/) ends up at:
rem   out\tooling\rexglue-sdk-0.10.0\win-amd64
rem
rem Run automatically by build.cmd when the SDK is missing; can also be run
rem manually.
rem ===========================================================================
setlocal
set "REPO=%~dp0.."
set "VER=0.10.0"
set "DEST=%REPO%\out\tooling\rexglue-sdk-0.10.0"
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

rem Keep the source submodule and any local edits separate from downloaded SDKs.
if not exist "%DEST%" mkdir "%DEST%"

echo Downloading %URL%
curl -L --fail -sS -o "%TEMP%\%ZIPNAME%" "%URL%"
if errorlevel 1 (
    echo Error: SDK download failed. 1>&2
    exit /b 1
)

echo Extracting to %DEST%
rem Windows' bsdtar first: Expand-Archive -Force fails on this zip's directory
rem entries (and on DLLs an antivirus scan still holds open).
"%SystemRoot%\System32\tar.exe" -xf "%TEMP%\%ZIPNAME%" -C "%DEST%" 2>nul
if errorlevel 1 powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath '%TEMP%\%ZIPNAME%' -DestinationPath '%DEST%' -Force"
if errorlevel 1 (
    echo Error: SDK extraction failed. 1>&2
    exit /b 1
)
del /q "%TEMP%\%ZIPNAME%" 2>nul

if not exist "%DEST%\win-amd64\lib\cmake\rexglue\rexglueConfig.cmake" (
    echo Error: SDK incomplete after extract; expected %DEST%\win-amd64. 1>&2
    exit /b 1
)
if not exist "%DEST%\win-amd64\bin\rexruntime.dll" (
    echo Error: bin\rexruntime.dll was not extracted; antivirus may be blocking it. 1>&2
    exit /b 1
)
echo OK: ReXGlue SDK v%VER% -> %DEST%\win-amd64
endlocal
