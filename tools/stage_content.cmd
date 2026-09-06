@echo off
rem ===========================================================================
rem Stage the game content next to a built exe so the build directory is fully
rem self-contained:  <dest>\default.xex, data\, nxeart\, $SystemUpdate\
rem (plus saves\ and cache\ created by the game at runtime).
rem
rem Incremental: xcopy /D copies only newer or missing files, so re-running
rem after a rebuild is fast; only the first run copies the full ~6.5 GB.
rem
rem Usage:  stage_content.cmd <content_root> <dest_dir>
rem ===========================================================================
setlocal
set "ROOT=%~1"
set "DEST=%~2"
if "%ROOT%"=="" (
    echo Usage: stage_content.cmd ^<content_root^> ^<dest_dir^> 1>&2
    exit /b 1
)
if "%DEST%"=="" (
    echo Usage: stage_content.cmd ^<content_root^> ^<dest_dir^> 1>&2
    exit /b 1
)
if not exist "%ROOT%\default.xex" (
    echo Error: no default.xex in %ROOT% - extract the game content there first. 1>&2
    exit /b 1
)

copy /Y "%ROOT%\default.xex" "%DEST%\" >nul
if errorlevel 1 (
    echo Error: copying default.xex to %DEST% failed. 1>&2
    exit /b 1
)
for %%d in (data nxeart $SystemUpdate) do (
    if exist "%ROOT%\%%d\" xcopy /D /E /I /Q /Y "%ROOT%\%%d" "%DEST%\%%d\" >nul
    if errorlevel 1 (
        echo Error: staging %%d to %DEST% failed. 1>&2
        exit /b 1
    )
)
rem Note: saves\ and cache\ are created by the game itself at startup.
endlocal
