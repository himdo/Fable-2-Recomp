@echo off
rem ===========================================================================
rem Build the texture export tool.
rem Usage: tools\texture_export\build.cmd
rem Output: tools\texture_export\texture_export.exe
rem ===========================================================================
setlocal
cd /d "%~dp0..\.."

rem Find clang++
set "CLANG="
for /f "delims=" %%i in ('where clang++ 2^>nul') do if not defined CLANG set "CLANG=%%i"
if not defined CLANG if exist "C:\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin\clang++.exe" set "CLANG=C:\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin\clang++.exe"
if not defined CLANG (
    echo Error: clang++ not found on PATH or at default location. 1>&2
    exit /b 1
)

rem Find zlib-ng build (from the main project build)
set "ZB=out\build\win-amd64-release\_deps\zlibng-build"
if not exist "%ZB%\z-ng.lib" (
    echo Error: %ZB%\z-ng.lib not found. 1>&2
    echo        Build the main project first: build.cmd -release fable_2 1>&2
    exit /b 1
)

"%CLANG%" -std=c++23 -O2 ^
  -Isrc/gui -Isrc -Ithirdparty/stb ^
  -I%ZB% -Ithirdparty/rexglue-sdk/include ^
  -o tools\texture_export\texture_export.exe ^
  tools\texture_export\main.cpp src\gui\keycap_render.cpp ^
  %ZB%\z-ng.lib

if errorlevel 1 (
    echo Build failed. 1>&2
    exit /b 1
)
echo Build OK: tools\texture_export\texture_export.exe
endlocal
