@echo off
cd /d "D:\projects\hacking\Windows\Fable 2 Rexglue"
call build.cmd -release fable_2 > build_release.log 2>&1
echo BUILD_EXIT_CODE=%ERRORLEVEL% > build_release.status
type build_release.status
