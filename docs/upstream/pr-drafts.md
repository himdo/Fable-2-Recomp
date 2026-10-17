# PRs for himdo/rexglue-sdk

All branches pushed to `rasty94/rexglue-sdk` and PRs opened against `himdo/rexglue-sdk:main`.

## Open Pull Requests

- **#11** Presenter paint pipeline churn: https://github.com/himdo/rexglue-sdk/pull/11 (branch `fix-vulkan-paint-pipeline-churn`). Upstream issue: rexglue/rexglue-sdk#507.
- **#13** ui: declare the presenter pacer counters on all platforms: https://github.com/himdo/rexglue-sdk/pull/13 (branch `fix-pacer-members-non-win32`).
- **#14** cmake: pass -Xlinker /DEF: only on Windows: https://github.com/himdo/rexglue-sdk/pull/14 (branch `fix-def-only-windows`).
- **#15** core: restore default action when no exception handler takes a signal: https://github.com/himdo/rexglue-sdk/pull/15 (branch `fix-posix-unhandled-signal`).
- **#16** vulkan: do not re-request a UI paint when the pacer suppressed it: https://github.com/himdo/rexglue-sdk/pull/16 (branch `fix-ui-paint-spin-when-paced`).
- **#17** vulkan: upload shared memory before submission render passes (T1): https://github.com/himdo/rexglue-sdk/pull/17 (branch `t1-upload-first`).

## Notes on remaining candidates

- **T2 (`readback_memexport_no_drain`):** skipped reads mean the guest sometimes sees data from an earlier draw. On Fable 2 it gained only ~0.9 FPS; kept internal/off.
- **T3 (tessellation emulated in VS) & T10 (vulkan_host_msaa_1x) & T11 (native point sprites):** living in `rasty94/rexglue-sdk:macos`. Can be proposed upstream if himdo wants macOS/TBDR optimizations.
