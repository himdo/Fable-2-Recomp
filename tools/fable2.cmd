@echo off
rem ===========================================================================
rem Fable 2 launcher - picks the GPU backend, then launches fable_2.exe.
rem
rem   fable2.cmd            backend from fable2_config.toml [graphics] backend
rem   fable2.cmd d3d12      D3D12
rem   fable2.cmd vulkan     Vulkan
rem
rem rexgpu-xenos.dll is built with both backends (tools\build_runtime_sdk.cmd),
rem so this only passes --gpu_backend; no DLLs are copied or swapped. If the
rem chosen backend cannot start, the game falls back to the other one.
rem ===========================================================================
setlocal
cd /d "%~dp0"
if /i "%~1"=="d3d12" (
    "fable_2.exe" --gpu_backend=d3d12
) else if /i "%~1"=="vulkan" (
    "fable_2.exe" --gpu_backend=vulkan
) else (
    "fable_2.exe"
)
endlocal
