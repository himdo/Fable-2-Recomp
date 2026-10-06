@echo off
rem Build x360extract as a single-file exe. Optional first arg: self-contained.
rem Cleans the staged output and intermediate build dirs, then publishes.
setlocal
set "ROOT=%~dp0..\.."
set "PROJECT=%~dp0X360Extract.csproj"
set "OUTPUT=%ROOT%\out\tooling\x360extract"

rem Clean: drop the staged output (hard) and intermediate build artifacts
rem (best effort - editors like VS Code/C# Dev Kit may hold bin\ handles).
if exist "%OUTPUT%" rmdir /s /q "%OUTPUT%"
if exist "%~dp0bin" rmdir /s /q "%~dp0bin" 2>nul
if exist "%~dp0obj" rmdir /s /q "%~dp0obj" 2>nul

if /i "%~1"=="self-contained" (
  dotnet publish "%PROJECT%" -c Release -r win-x64 --self-contained true ^
    -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true ^
    -o "%OUTPUT%"
) else (
  dotnet publish "%PROJECT%" -c Release -r win-x64 --self-contained false ^
    -p:PublishSingleFile=true -o "%OUTPUT%"
)

if errorlevel 1 exit /b 1
echo x360extract built: %OUTPUT%\x360extract.exe
