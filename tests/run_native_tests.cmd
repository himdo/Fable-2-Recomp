@echo off
rem Standalone synthetic tests: no game files, GPU, SDL or audio device needed.
rem They use Catch2 (the SDK's thirdparty\catch2), and print each TEST_CASE with
rem its SECTIONs marked ok or FAIL, like pytest-spec (tests\native\catch_spec.cpp).
rem Set FABLE2_COVERAGE=1 to build them with clang's source coverage and print a
rem report of the src/ code they run (plus an HTML report), like pytest --cov.
setlocal
set "ROOT=%~dp0.."
set "SDK=%~1"
if not defined SDK set "SDK=%ROOT%\thirdparty\rexglue-sdk"
set "OUT=%ROOT%\out\tests\runtime-unit-tests"
set "COV="
if "%FABLE2_COVERAGE%"=="1" (
  rem Separate folder, so no uninstrumented executable from a plain run is reported.
  set "OUT=%ROOT%\out\tests\runtime-unit-tests-coverage"
  set "COV=-fprofile-instr-generate -fcoverage-mapping"
)
if not exist "%OUT%" mkdir "%OUT%"
if defined COV (
  if exist "%OUT%\cov" rmdir /s /q "%OUT%\cov"
  mkdir "%OUT%\cov"
  rem %%p: one raw profile per test process.
  set "LLVM_PROFILE_FILE=%OUT%\cov\%%p.profraw"
)
python "%ROOT%\tools\generate_game_versions.py" --check || exit /b 1

rem Catch2, its spec-style listener and the default main, built once per run
rem (Catch2 without coverage: it's not the project's code).
set "CATCH=%SDK%\thirdparty\catch2\extras"
if not exist "%CATCH%\catch_amalgamated.cpp" (
  echo Catch2 is missing: run git -C thirdparty/rexglue-sdk submodule update --init --depth 1 thirdparty/catch2
  exit /b 1
)
if not exist "%OUT%\catch2" mkdir "%OUT%\catch2"
clang++ -std=c++23 -O1 -DCATCH_AMALGAMATED_CUSTOM_MAIN -c "%CATCH%\catch_amalgamated.cpp" -o "%OUT%\catch2\catch2.o" || exit /b 1
clang++ -std=c++23 -I"%CATCH%" -c "%~dp0native\catch_spec.cpp" -o "%OUT%\catch2\spec.o" || exit /b 1
clang++ -std=c++23 -I"%CATCH%" -c "%~dp0native\catch_main.cpp" -o "%OUT%\catch2\main.o" || exit /b 1
set "CXX=clang++ -std=c++23 %COV% -I"%CATCH%""
set CATCH_OBJS="%OUT%\catch2\catch2.o" "%OUT%\catch2\spec.o"
set CATCH_MAIN=%CATCH_OBJS% "%OUT%\catch2\main.o"
rem Declaration order, not Catch2's default random order, so the output reads
rem like the source.
set "RUN=--order decl"
rem The SDK's own thirdparty headers, pulled in by its cvar and ppc/context includes.
set SDK_DEPS=-I"%SDK%\thirdparty\fmt\include" -I"%SDK%\thirdparty\simde" -I"%SDK%\thirdparty\spdlog\include"

%CXX% -I"%SDK%\include" "%~dp0native\test_clocked_audio_sink.cpp" %CATCH_MAIN% -o "%OUT%\audio.exe" || exit /b 1
"%OUT%\audio.exe" %RUN% || exit /b 1
%CXX% -I"%SDK%\include" "%~dp0native\test_frame_limiter.cpp" "%SDK%\src\graphics\frame_limiter.cpp" %CATCH_MAIN% -o "%OUT%\limiter.exe" || exit /b 1
"%OUT%\limiter.exe" %RUN% || exit /b 1
%CXX% -I"%SDK%\include" "%~dp0native\test_present_rate_limiter.cpp" %CATCH_MAIN% -o "%OUT%\present-limiter.exe" || exit /b 1
"%OUT%\present-limiter.exe" %RUN% || exit /b 1
%CXX% -I"%SDK%\include" "%~dp0native\test_guest_frame_meter.cpp" %CATCH_MAIN% -o "%OUT%\meter.exe" || exit /b 1
"%OUT%\meter.exe" %RUN% || exit /b 1
rem Keyboard/mouse -> gamepad map parsing (links the SDK's real keybinds.cpp).
%CXX% -I"%SDK%\include" %SDK_DEPS% -I"%ROOT%" -I"%ROOT%\src\core" -I"%ROOT%\src\input" "%~dp0native\test_keyboard_gamepad_map.cpp" "%SDK%\src\ui\keybinds.cpp" %CATCH_MAIN% -o "%OUT%\map.exe" || exit /b 1
"%OUT%\map.exe" %RUN% || exit /b 1
rem Mouse-look helpers (stick mapping, inject plan, diagnostics summary).
%CXX% -I"%SDK%\include" %SDK_DEPS% -I"%ROOT%" -I"%ROOT%\src\core" -I"%ROOT%\src\input" "%~dp0native\test_mouse_look.cpp" "%SDK%\src\ui\keybinds.cpp" %CATCH_MAIN% -o "%OUT%\mouse.exe" || exit /b 1
"%OUT%\mouse.exe" %RUN% || exit /b 1
rem Direct mouse camera (accumulator, route, Lua return values) and inject profiles.
%CXX% -I"%SDK%\include" %SDK_DEPS% -I"%ROOT%" -I"%ROOT%\src\input" "%~dp0native\test_mouse_direct.cpp" %CATCH_MAIN% -o "%OUT%\mouse-direct.exe" || exit /b 1
"%OUT%\mouse-direct.exe" %RUN% || exit /b 1
rem Keycap prompt textures (memory scan, raw BC3 path, labels).
%CXX% -I"%ROOT%\src" "%~dp0native\test_keycap_textures.cpp" %CATCH_MAIN% -o "%OUT%\keycap-textures.exe" || exit /b 1
"%OUT%\keycap-textures.exe" %RUN% || exit /b 1
rem The RunScript gate: host scripts only run inside the world-update service.
%CXX% -I"%ROOT%" "%~dp0native\test_world_thread.cpp" %CATCH_MAIN% -o "%OUT%\world-thread.exe" || exit /b 1
"%OUT%\world-thread.exe" %RUN% || exit /b 1
rem The version check, once per build profile. It has its own main (--xex).
for %%P in (goty-compatible goty-us-eu goty-german) do (
  %CXX% -DFABLE2_BUILD_PROFILE=\"%%P\" "%~dp0native\test_xex_verify.cpp" %CATCH_OBJS% -o "%OUT%\xex-%%P.exe" || exit /b 1
  "%OUT%\xex-%%P.exe" %RUN% || exit /b 1
)
if defined COV call :coverage_report || exit /b 1
exit /b 0

rem Merges the raw profiles and reports the project's own code: the SDK and its
rem thirdparty headers, and the tests themselves, are left out.
rem llvm-cov warns that some functions "have mismatched data": each test's main
rem and Catch2's generated test functions (same names, different bodies), and
rem some inline header functions built into several test executables, get a
rem different profile hash in each. Such a function is reported from the
rem executable whose build matches, so runs in the others aren't added.
:coverage_report
dir /b /s "%OUT%\cov\*.profraw" > "%OUT%\cov\inputs.txt"
llvm-profdata merge -sparse -f "%OUT%\cov\inputs.txt" -o "%OUT%\cov\native.profdata" || exit /b 1
setlocal EnableDelayedExpansion
set "BINS="
for %%E in ("%OUT%\*.exe") do (
  if defined BINS (set BINS=!BINS! -object "%%~fE") else (set BINS="%%~fE")
)
echo.
echo Native test coverage of the project's own code:
llvm-cov report !BINS! -instr-profile="%OUT%\cov\native.profdata" "-ignore-filename-regex=(thirdparty|tests)[\\/]" || exit /b 1
llvm-cov show !BINS! -instr-profile="%OUT%\cov\native.profdata" "-ignore-filename-regex=(thirdparty|tests)[\\/]" -format=html -output-dir="%OUT%\coverage-html" || exit /b 1
echo HTML report: %OUT%\coverage-html\index.html
exit /b 0
