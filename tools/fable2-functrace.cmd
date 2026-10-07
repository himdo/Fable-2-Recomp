@echo off
rem ===========================================================================
rem Fable 2 - GUEST FUNCTION TRACING launcher.
rem
rem Thin wrapper around fable2.cmd that enables the guest function-call
rem tracer before launching (see README "Guest function-call tracing").
rem Every guest function entry is logged by name to fable2_func_trace.log
rem next to the exe; consecutive calls of the same function collapse into
rem one "name x N" line. Call counts are also accumulated and written to
rem fable2_func_summary.log (one "N x name" line per function, sorted by
rem count, refreshed every 5 s while tracing) - check it at a glance to see
rem what's being called.
rem
rem Arguments are keywords in any order:
rem   fable2-functrace.cmd                  configured backend, trace every call
rem   fable2-functrace.cmd vulkan           Vulkan, trace every call
rem   fable2-functrace.cmd subs             only sub_* functions - naming mode
rem   fable2-functrace.cmd subs LoadingScreen   naming mode + substring filter
rem   fable2-functrace.cmd d3d12 82B9 subs  D3D12, address-range naming mode
rem   fable2-functrace.cmd summary          summary file only, no trace log
rem   fable2-functrace.cmd trace            trace log only, no summary file
rem
rem   <backend>  d3d12 | vulkan  (default: fable2_config.toml [graphics] backend)
rem   <filter>   any other argument: sets FABLE2_FUNC_TRACE_FILTER (substring
rem              match; e.g. Story_, SwitchDispatch, or 82B9 with subs)
rem   subs       keyword: sets FABLE2_FUNC_TRACE_SUBS_ONLY=1, dropping
rem              everything that isn't a sub_<hex> name (named functions,
rem              __savegprlr_*/__restgprlr_* helpers, xstart)
rem   trace      keyword: write only fable2_func_trace.log
rem   summary    keyword: write only fable2_func_summary.log
rem              (no keyword = both, the default)
rem
rem A previous session's logs are renamed to fable2_func_trace_prev.log /
rem fable2_func_summary_prev.log first, so each launch starts fresh (the
rem old ones are kept for reference).
rem
rem WARNING: unfiltered tracing writes FAST (several GB per minute of
rem gameplay). Use a <filter> for anything longer than a quick check, and
rem watch fable2_func_trace.log's size.
rem ===========================================================================
setlocal
cd /d "%~dp0"

rem --- Parse keyword arguments in any order -------------------------------
set "BACKEND="
set "FILTER="
set "SUBSONLY=0"
set "WANTLOG=0"
set "WANTSUMMARY=0"
for %%A in (%*) do (
    if /i "%%A"=="subs" (
        set "SUBSONLY=1"
    ) else if /i "%%A"=="trace" (
        set "WANTLOG=1"
    ) else if /i "%%A"=="summary" (
        set "WANTSUMMARY=1"
    ) else if /i "%%A"=="d3d12" (
        if not defined BACKEND set "BACKEND=d3d12"
    ) else if /i "%%A"=="vulkan" (
        if not defined BACKEND set "BACKEND=vulkan"
    ) else if not defined FILTER (
        set "FILTER=%%A"
    )
)

rem trace/summary output selection: no keyword = both, one keyword = just
rem that output, both keywords = both.
set "FABLE2_FUNC_TRACE=1"
set "FABLE2_FUNC_TRACE_LOG=1"
set "FABLE2_FUNC_TRACE_SUMMARY=1"
set "OUTMODE=trace + summary"
if "%WANTLOG%"=="1" if not "%WANTSUMMARY%"=="1" (
    set "FABLE2_FUNC_TRACE_SUMMARY=0"
    set "OUTMODE=trace only"
)
if "%WANTSUMMARY%"=="1" if not "%WANTLOG%"=="1" (
    set "FABLE2_FUNC_TRACE_LOG=0"
    set "OUTMODE=summary only"
)
if defined FILTER set "FABLE2_FUNC_TRACE_FILTER=%FILTER%"
if "%SUBSONLY%"=="1" set "FABLE2_FUNC_TRACE_SUBS_ONLY=1"

rem Keep the previous session's logs, start fresh.
if exist "fable2_func_trace.log" (
    del /f /q "fable2_func_trace_prev.log" >nul 2>&1
    move /y "fable2_func_trace.log" "fable2_func_trace_prev.log" >nul
)
if exist "fable2_func_summary.log" (
    del /f /q "fable2_func_summary_prev.log" >nul 2>&1
    move /y "fable2_func_summary.log" "fable2_func_summary_prev.log" >nul
)

rem NOTE: no literal parens in these echo strings - a ")" inside a
rem parenthesized if block makes cmd close the block early.
if "%SUBSONLY%"=="1" (
    if defined FILTER (
        echo fable2-functrace: tracing sub_* functions only, filter %FILTER%, %OUTMODE% 1>&2
    ) else (
        echo fable2-functrace: tracing sub_* functions only - naming mode, %OUTMODE% 1>&2
    )
) else if defined FILTER (
    echo fable2-functrace: tracing all functions, filter %FILTER%, %OUTMODE% 1>&2
) else (
    echo fable2-functrace: tracing all functions - log grows by GBs per minute, %OUTMODE% 1>&2
)

if defined BACKEND (
    "fable_2.exe" --gpu_backend=%BACKEND%
) else (
    "fable_2.exe"
)
endlocal
