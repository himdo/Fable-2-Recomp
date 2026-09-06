@echo off
rem ===========================================================================
rem Crash capture: run fable_2.exe under LLDB and dump full backtraces when
rem the process stops (unhandled access violation or rex::debug::Break).
rem Usage: tools\crash_capture.cmd   (from the build dir, next to fable_2.exe)
rem Output: crash_capture_lldb.log next to the exe.
rem ===========================================================================
setlocal
cd /d "%~dp0.."
rem lldb: prefer one on PATH, else the default LLVM install location
set "LLDB="
for /f "delims=" %%i in ('where lldb 2^>nul') do if not defined LLDB set "LLDB=%%i"
if not defined LLDB if exist "C:\Program Files\LLVM\bin\lldb.exe" set "LLDB=C:\Program Files\LLVM\bin\lldb.exe"
if not defined LLDB (
    echo Error: lldb not found on PATH or at C:\Program Files\LLVM\bin 1>&2
    echo        Install LLVM - https://llvm.org - or add its bin\ to PATH. 1>&2
    exit /b 1
)
"%LLDB%" -b ^
  -o "command script import %~dp0..\crash_bt" ^
  -o "breakpoint set -n rex::debug::Break()" ^
  -o "run" ^
  -o "echo === STOP REASONS ===" ^
  -o "thread list -b" ^
  -o "bt all" ^
  -o "detach" ^
  -o "quit" fable_2.exe > crash_capture_lldb.log 2>&1
echo === LLDB capture done, log: crash_capture_lldb.log
type crash_capture_lldb.log | more
endlocal
