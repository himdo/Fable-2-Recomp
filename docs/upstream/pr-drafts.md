# PR drafts for himdo/rexglue-sdk (nothing pushed or opened)

Local branches live in /Volumes/fable2build/pr-wt, all on top of origin/main (a728090) unless noted.

## Already open

- Presenter paint pipeline churn: https://github.com/himdo/rexglue-sdk/pull/11 (branch `fix-vulkan-paint-pipeline-churn`, commit 1ffc649). Upstream issue: rexglue/rexglue-sdk#507.

## T1 - branch `t1-upload-first` (on top of 1ffc649)

**Title:** vulkan: upload shared memory before the submission's render passes

Adds the `vulkan_shared_memory_upload_first` cvar (default false). Pages that no GPU command of the current
submission has accessed yet are uploaded into a second deferred command buffer, replayed before the main
one, instead of ending the current render pass. Pages already accessed keep the in-order path. The upload
buffer has its own barriers, so usage tracking of the main command buffer is unaffected.

Fable 2 on MoltenVK (Apple M3 Pro), 3D scene, off -> on: inline shared memory copies 431 -> ~8 per frame,
render passes 409 -> ~145 per frame, 7.9 -> 9.7 FPS.

## T2 - branch `t2-memexport-no-drain` (on top of T1)

**Title:** vulkan: skip the memexport readback instead of draining the GPU (readback_memexport_no_drain)

When the previous readback slot for a memexport (address, size) key is still in flight, the fast path falls
back to the full path, which waits for all queue operations and serializes CPU and GPU once per frame. With
`readback_memexport_no_drain` the read for that draw is skipped and the slot still alternates; the next draw
with the same key copies it once its submission has completed. Hot reload.

**Warning (data loss):** skipped reads mean the guest sometimes sees data from an earlier draw, or none.
On Fable 2 this gained only about +0.9 FPS, so we keep it off by default on our side; the PR should make
the cvar default off everywhere (the commit currently says "default on for macOS"; change before opening) and
state the trade-off clearly.

## fix-pacer-members-non-win32

**Title:** ui: declare the presenter pacer counters on all platforms

The guest vblank/frame counters and pacer state in `presenter.h` were declared inside
`#if REX_PLATFORM_WIN32`, but the pacer code that uses them is built everywhere, so the build failed
outside Windows. Move the members out of the Win32 block. No behaviour change on Windows.

## fix-def-only-windows

**Title:** cmake: pass -Xlinker /DEF: only on Windows

`src/system/CMakeLists.txt` passed `-Xlinker /DEF:<rexruntime.def>` unconditionally. That is an lld-link
option; on ELF/Mach-O the link fails. Guard it with `if(WIN32)`; other platforms export with default visibility.

## fix-posix-unhandled-signal

**Title:** core: restore the default action when no exception handler takes a signal

`ExceptionHandlerCallback` returned when no registered handler handled SIGSEGV/SIGBUS/SIGILL, so the CPU
re-ran the faulting instruction forever (silent hang at 100% CPU, no crash report). Reset the signal to
`SIG_DFL` so the process terminates with a normal crash report. Found debugging a loader crash on macOS.

## fix-ui-paint-spin-when-paced

**Title:** vulkan: do not re-request a UI paint when the pacer suppressed it

When `GuestVblankPaceAllows()` suppressed a paint, `PaintAndPresentImpl` immediately requested another UI
paint. `RefreshGuestOutput` already requests one when the next guest frame lands, and on platforms where no
vblank tick throttles paints (everything but Win32) the extra request spun the UI thread. Keep the immediate
re-request only for the vsync gate. Depends on nothing; touches the same function as #11 but a different hunk.
