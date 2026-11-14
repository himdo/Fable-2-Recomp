@echo off
rem Standalone synthetic tests: no game files, GPU, SDL or audio device needed.
setlocal
set "ROOT=%~dp0.."
set "SDK=%~1"
if not defined SDK set "SDK=%ROOT%\thirdparty\rexglue-sdk"
set "OUT=%ROOT%\out\tests\runtime-unit-tests"
if not exist "%OUT%" mkdir "%OUT%"
python "%ROOT%\tools\generate_game_versions.py" --check || exit /b 1
clang++ -std=c++23 -I"%SDK%\include" "%~dp0native\test_clocked_audio_sink.cpp" -o "%OUT%\audio.exe" || exit /b 1
"%OUT%\audio.exe" || exit /b 1
clang++ -std=c++23 -I"%SDK%\include" "%~dp0native\test_frame_limiter.cpp" "%SDK%\src\graphics\frame_limiter.cpp" -o "%OUT%\limiter.exe" || exit /b 1
"%OUT%\limiter.exe" || exit /b 1
clang++ -std=c++23 -I"%SDK%\include" "%~dp0native\test_present_rate_limiter.cpp" -o "%OUT%\present-limiter.exe" || exit /b 1
"%OUT%\present-limiter.exe" || exit /b 1
clang++ -std=c++23 -I"%SDK%\include" "%~dp0native\test_guest_frame_meter.cpp" -o "%OUT%\meter.exe" || exit /b 1
"%OUT%\meter.exe" || exit /b 1
rem Keyboard/mouse -> gamepad map parsing (links the SDK's real keybinds.cpp;
rem the extra -I dirs are the SDK's own thirdparty headers pulled in by its cvar
rem and ppc/context includes).
clang++ -std=c++23 -I"%SDK%\include" -I"%SDK%\thirdparty\fmt\include" -I"%SDK%\thirdparty\simde" -I"%SDK%\thirdparty\spdlog\include" -I"%ROOT%" -I"%ROOT%\src\core" -I"%ROOT%\src\input" "%~dp0native\test_keyboard_gamepad_map.cpp" "%SDK%\src\ui\keybinds.cpp" -o "%OUT%\map.exe" || exit /b 1
"%OUT%\map.exe" || exit /b 1
rem Mouse-look helpers (stick mapping, inject plan, diagnostics summary).
clang++ -std=c++23 -I"%SDK%\include" -I"%SDK%\thirdparty\fmt\include" -I"%SDK%\thirdparty\simde" -I"%SDK%\thirdparty\spdlog\include" -I"%ROOT%" -I"%ROOT%\src\core" -I"%ROOT%\src\input" "%~dp0native\test_mouse_look.cpp" "%SDK%\src\ui\keybinds.cpp" -o "%OUT%\mouse.exe" || exit /b 1
"%OUT%\mouse.exe" || exit /b 1
rem Direct mouse camera (accumulator, route, Lua return values) and inject profiles.
clang++ -std=c++23 -I"%SDK%\include" -I"%SDK%\thirdparty\fmt\include" -I"%SDK%\thirdparty\simde" -I"%SDK%\thirdparty\spdlog\include" -I"%ROOT%" -I"%ROOT%\src\input" "%~dp0native\test_mouse_direct.cpp" -o "%OUT%\mouse-direct.exe" || exit /b 1
"%OUT%\mouse-direct.exe" || exit /b 1
for %%P in (goty-compatible goty-us-eu goty-german) do (
  clang++ -std=c++23 -DFABLE2_BUILD_PROFILE=\"%%P\" "%~dp0native\test_xex_verify.cpp" -o "%OUT%\xex-%%P.exe" || exit /b 1
  "%OUT%\xex-%%P.exe" || exit /b 1
)
exit /b 0
