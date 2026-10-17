# Draft issue for KhronosGroup/MoltenVK (not filed)

**Title:** `getVkHandle()` rewrites `ICD_LOADER_MAGIC` over the loader's dispatch pointer when a message is reported to a debug messenger, crashing the next loader trampoline call

## Summary

`MVKDispatchableVulkanAPIObject::getVkHandle()` calls `set_loader_magic_value(&_icdRef)` on every call
(`MoltenVK/GPUObjects/MVKVulkanAPIObject.h`, ~lines 90-122). `MVKInstance::debugReportMessage()`
(`GPUObjects/MVKInstance.mm`, ~lines 209-235) calls `getVkHandle()` to fill
`VkDebugUtilsObjectNameInfoEXT::objectHandle` (and the `VK_EXT_debug_report` `object`). For a dispatchable
object that the loader has already initialised, the first pointer-sized field of the handle is the loader's
dispatch-table pointer, not the magic any more. The call overwrites it with `ICD_LOADER_MAGIC` (`0x01CDC0DE`).
The next call on that object that goes through the loader trampoline dereferences `0x01CDC0DE`.

## Observed

Vulkan app with a `VK_EXT_debug_utils` messenger, MoltenVK 1.4.1 (commit 701747d), Vulkan loader from the
ReXGlue SDK 0.10.0, macOS 27, Apple M3 Pro. After MoltenVK emitted some warning/info messages the app
crashed in the loader:

```
Exception Type:  EXC_BAD_ACCESS (SIGSEGV)
Exception Codes: KERN_INVALID_ADDRESS at 0x0000000001cdc0de
Thread (command processor):
 0  libvulkan.1.dylib  vkAllocateCommandBuffers + 28
 1  ... EndSubmission(bool) + 936
```

`vkAllocateCommandBuffers(device, ...)` is a loader trampoline: it reads the dispatch table from the first
field of `VkDevice`, which now holds `0x01CDC0DE`. It happened in ~2 of 3 runs, only when a messenger was
registered. Without a messenger (`_hasDebugUtilsMessengers == false`) it never happened.

## Which object

Note on the message we initially suspected, "Metal does not support disabling primitive restart"
(`GPUObjects/MVKPipeline.mm:~895`, `reportWarning` from `MVKGraphicsPipeline`): `getVulkanAPIObject()` there
resolves to the pipeline itself (`MVKVulkanAPIObject::getVulkanAPIObject() { return this; }`), which is not
dispatchable, so its `getVkHandle()` is the base `return this;` and does not write the magic. That message
alone cannot corrupt a handle. Any message whose API object is dispatchable can (`MVKDevice`, `MVKQueue`,
`MVKCommandBuffer`, `MVKPhysicalDevice`, `MVKInstance`, via `getVulkanAPIObject()` overrides that return
`_device`, `_physicalDevice`, `_queue`, `_cmdBuffer`). The crash report shows the `VkDevice` was the one
corrupted; we did not trace which specific message hit it.

## History

The always-rewrite behaviour was added in c857014b, "Ensure Vulkan loader magic number persists in objects
pooled by MoltenVK" (for pooled objects the loader may consider freed). No later commit touching
`MVKVulkanAPIObject.h` changes it (`git log` of the file: only copyright and minor debugging edits since).

## Reproduction (generic)

1. Create an instance with `VK_EXT_debug_utils`, register a messenger (any severity mask that includes
   warnings), create a device.
2. Do anything that makes MoltenVK report a message against a dispatchable object (device, queue, command
   buffer), e.g. a validation-style warning or a feature-not-present warning from a code path using the device.
3. Call any entry point on that object through the loader, e.g. `vkAllocateCommandBuffers(device, ...)`,
   `vkQueueSubmit(queue, ...)`.
4. Crash at `0x01CDC0DE` in the loader trampoline.

## Workaround

Do not register a debug messenger / debug report callback (we default it off on macOS).

## Proposed fix

- In `debugReportMessage()` (and any other internal caller) do not use `getVkHandle()` for the reported
  handle. Return the handle without touching the field, e.g. add a `peekVkHandle()` / `getVkHandleNoInit()`
  that returns `&_icdRef` and use it for diagnostics; or
- Make `getVkHandle()` write the magic only when the object is first handed out by the creating entry point
  (or when the field is not already a valid loader dispatch pointer), instead of on every call. The pooled
  case of c857014b can re-establish it where objects are re-issued from the pool, not on every query.
