@echo off
rem Run the Fable 2 recomp with the CUSTOM D3D12 renderer (this repo's
rem src/renderer/ plugin, staged as rexgpu-custom[d].dll next to the exe).
rem See plans/custom-d3d12-renderer.md.
rem
rem Usage:
rem   run_custom_renderer.cmd [extra args passed to fable_2.exe]
rem
rem Progress: window title (2 Hz), the top-left HUD overlay (cvar
rem custom_renderer_hud), and logs\renderer-custom.log.
rem The built-in renderer stays available: run fable_2.exe as usual
rem (--gpu_plugin=xenos) for A/B comparison.
cd /d "%~dp0"
fable_2.exe --gpu_plugin=custom %*
