@echo off
rem Wrapper to run the baseline build and capture the true exit code.
cd /d "D:\projects\hacking\Windows\Fable 2 Rexglue"
if exist build_baseline.log del build_baseline.log
call build.cmd fable_2 > build_baseline.log 2>&1
echo BUILD_EXIT_CODE=%ERRORLEVEL% > build_baseline.status
type build_baseline.status
