# Hero/Dog Realtime Texture Morphing - Black Texture Fix

## Problem Statement

With `realtime_texture_morphing=true` (r9.u64=1 hook), the game renders morphed hero/dog
skin textures directly to a guest texture at `0x12704000`. On the Vulkan path, the hero/dog
mesh samples this texture and gets **black** (zeros). The D3D12 path works correctly.

## Root Cause Analysis

### D3D12 Path (Working)
1. Game renders morphed texture → EDRAM (render target)
2. EDRAM resolve → writes to **scaled resolve buffer** (GPU buffer)
3. Texture cache `RequestTextures()` → compute shader reads from scaled resolve buffer →
   writes to scratch buffer → `D3DCopyTextureRegion` to D3D12 texture resource
4. Hero/dog mesh samples the D3D12 texture resource → **correct data**

Key: The D3D12 texture cache's upload path uses a **compute shader** that reads from the
scaled resolve buffer (GPU-to-GPU), does format conversion, and writes to the texture.

### Vulkan Path (Broken)
1. Game renders morphed texture → EDRAM (render target) ✓
2. EDRAM resolve → writes to **scaled resolve buffer** (GPU buffer) ✓
3. Texture cache `RequestTextures()` → compute shader reads from scaled resolve buffer →
   writes to scratch buffer → `vkCmdCopyBufferToImage` to VkImage **???**
4. Hero/dog mesh samples the VkImage → **BLACK**

The question is: **WHY is step 3 not producing correct data in Vulkan?**

### Key Differences D3D12 vs Vulkan
| Aspect | D3D12 | Vulkan |
|--------|-------|--------|
| Shared memory | GPU buffer (CPU-accessible via aliasing) | GPU buffer (device-local, NOT CPU-accessible) |
| Scaled resolve buffer | GPU buffer (tiled heap) | GPU buffer (device-local) |
| Texture upload source | Scaled resolve buffer (GPU→GPU) | Scaled resolve buffer (GPU→GPU) |
| Texture upload method | Compute shader + D3DCopyTextureRegion | Compute shader + vkCmdCopyBufferToImage |
| Texture re-upload trigger | `RequestRange` after resolve | `MarkRangeAsResolved` → `RangeWrittenByGpu` |
| Texture invalidation | Watch callback on memory range | Watch callback on memory range |

## Critical Question

Does the Vulkan texture cache's compute shader actually execute for the hero/dog texture?
If the texture was already uploaded (from the initial CPU upload of zeros), does it get
re-uploaded after the EDRAM resolve?

The `MarkRangeAsResolved` → `RangeWrittenByGpu` path marks the range as GPU-written.
This should trigger the texture cache to re-upload when `RequestTextures` is next called.
But does `RangeWrittenByGpu` actually mark the texture as "dirty" for re-upload?

**This is the key thing to verify.**

## Approaches Tried (All Failed)

### 1. GPU-to-GPU Buffer-to-Image Copy from Scaled Resolve Buffer
- **What**: After EDRAM resolve, `vkCmdCopyBufferToImage` from scaled resolve buffer → texture VkImage
- **Result**: Hero still black. Source data in scaled resolve buffer is all zeros (verified by readback).
- **Why it failed**: The EDRAM resolve compute shader produces zeros because the EDRAM is empty in realtime morphing mode.

### 2. Texture Override (GetView returns render target VkImageView)
- **What**: `VulkanTexture::GetView` checks for override and returns the host render target's VkImageView
- **Result**: Hero still black + tree edge artifacts
- **Why it failed**: The render target's VkImage was either empty or in wrong layout/format. Also affected other textures with same base_page.

### 3. Image-to-Image Copy (Render Target → Texture VkImage)
- **What**: `vkCmdCopyImage` from host render target's VkImage → texture cache's VkImage
- **Result**: Hero still black (no crash after fixing ABI issue)
- **Why it failed**: Either the source VkImage is empty, or the copy layout transitions are wrong, or the texture cache re-upload overwrites the copy.

### 4. Buffer-to-Buffer Copy (Scaled Resolve → Shared Memory)
- **What**: `vkCmdCopyBuffer` from scaled resolve buffer → shared memory buffer at guest address
- **Result**: Hero still black
- **Why it failed**: The shared memory buffer gets the data, but the texture cache doesn't re-upload from it (no dirty flag set).

### 5. MarkAllTexturesOutdated + Image-to-Image Copy
- **What**: Image-to-image copy + set `texture_became_outdated_` flag
- **Result**: Hero still black
- **Why it failed**: The re-upload (triggered by the flag) reads from the scaled resolve buffer (which has zeros) and overwrites the image-to-image copy.

### 6. Image-to-Image Copy Only (no MarkAllTexturesOutdated)
- **What**: Just the image-to-image copy without triggering re-upload
- **Result**: Hero still black
- **Why it failed**: Unknown. Possibly the VkImage is not the one being sampled, or the layout is wrong, or the copy is overwritten by the normal texture cache path.

## Approaches NOT Yet Tried

### A. Verify the Actual Failure Point
Before trying more fixes, we need to determine WHERE exactly the data is lost:
1. **Is the host render target's VkImage non-zero?** (Add readback of the source VkImage)
2. **Is the image-to-image copy actually writing non-zero data?** (Add readback of the dest VkImage after copy)
3. **Is the texture cache's VkImage the one being sampled?** (Log the VkImageView handle used by the hero/dog draw)
4. **Does the texture cache re-upload overwrite the copy?** (Disable the re-upload for this texture)

### B. Disable Texture Cache Re-Upload for the Hero/Dog Texture
- Prevent the texture cache from re-uploading the texture at `0x12704000`
- The texture would only be updated by our image-to-image copy
- **How**: In `RequestTextures`, skip the upload if `key.base_page == 0x12704`
- **Risk**: The texture would never be updated from the initial CPU upload (first frame would be black)

### C. Make the Texture Cache Use the Render Target's VkImage Directly
- Instead of a separate VkImage, make the texture cache's texture point to the render target's VkImage
- **How**: When creating the texture at `0x12704000`, use the render target's VkImage handle
- **Risk**: Layout management conflicts (render target uses COLOR_ATTACHMENT, texture needs SHADER_READ_ONLY)

### D. Use a Compute Shader to Copy the Render Target to the Texture
- Write a simple compute shader that reads from the render target VkImage and writes to the texture VkImage
- Avoids layout issues (the compute shader handles the read in any layout)
- **How**: Create a compute pipeline with the render target as a sampler and the texture as a storage image
- **Risk**: Need to handle format matching, sampling, and layout transitions

### E. Use vkCmdBlitImage Instead of vkCmdCopyImage
- `vkCmdBlitImage` can do format conversion and works with more layout combinations
- **How**: Replace `vkCmdCopyImage` with `vkCmdBlitImage` (REMAIN filter, full image)
- **Risk**: Blit might not be supported for the format combination

### F. Render the Morph Directly to the Texture's VkImage
- Instead of copying after the fact, make the game render directly to the texture's VkImage
- **How**: Override the render target's VkImage with the texture's VkImage
- **Risk**: The render pass expects a specific VkImage (format, samples, layout)

### G. Use a Persistent Compute Shader (Per-Frame Dispatch)
- Add a per-frame compute dispatch that copies the render target to the texture
- Runs every frame, regardless of whether the resolve happens
- **How**: In `BeginSubmission`, dispatch a copy shader for the hero/dog texture
- **Risk**: Performance overhead (small, since it's 256x256)

### H. Fix the EDRAM Resolve to Produce Non-Zero Data
- The root cause might be that the EDRAM is empty in realtime morphing mode
- If the EDRAM had the morphed texture, the normal resolve path would work
- **How**: Make the game's realtime morph rendering go through the EDRAM path
- **Risk**: Requires understanding the game's GPU rendering pipeline

### I. Use vkCmdCopyImageToBuffer + vkCmdCopyBufferToImage (Two-Step)
- Copy render target VkImage → temporary VkBuffer → texture VkImage
- Avoids direct image-to-image copy (which might have layout issues)
- **How**: `vkCmdCopyImageToBuffer` (render target → temp buffer) + `vkCmdCopyBufferToImage` (temp buffer → texture)
- **Risk**: Extra memory allocation, extra copy

### J. Check if the Issue is Swizzle/Format Mismatch
- The render target might have a different format/swizzle than the texture expects
- **How**: Log the format and swizzle of both the render target's VkImageView and the texture's VkImageView
- **Risk**: None (diagnostic only)

### K. Use the D3D12 Approach: Upload via Compute Shader from Scaled Resolve Buffer
- The D3D12 path works because the compute shader reads from the scaled resolve buffer
- Verify that the Vulkan compute shader is actually reading from the correct buffer
- **How**: Add logging to the Vulkan texture cache's upload path to verify the source buffer and offset
- **Risk**: None (diagnostic only)

## Diagnostic Steps (Do These First)

1. **Readback the host render target's VkImage** after the game renders to it
   - Is it non-zero? (Confirms the game IS rendering to it)
   
2. **Readback the texture cache's VkImage** after the image-to-image copy
   - Is it non-zero? (Confirms the copy IS working)
   
3. **Log the VkImageView handle** used by the hero/dog mesh draw
   - Is it the same VkImage we're copying to? (Confirms we're updating the right image)
   
4. **Log the texture cache's upload source** for the hero/dog texture
   - Which buffer is it reading from? (Scaled resolve buffer? Shared memory?)
   - What offset? (Is it the correct offset for `0x12704000`?)
   
5. **Check if the texture cache re-uploads AFTER our copy**
   - If the re-upload happens after our copy, it overwrites with zeros
   - Add a flag to skip the re-upload for this texture

## Recommended Order of Attack

1. **Step A** (diagnostics) - Determine exactly where the data is lost
2. **Step J** (format/swizzle check) - Rule out format mismatch
3. **Step K** (verify upload source) - Confirm the compute shader reads the right buffer
4. **Step B** (disable re-upload) - If the re-upload is overwriting our copy
5. **Step D** (compute shader copy) - If the image-to-image copy has layout issues
6. **Step G** (per-frame compute dispatch) - As a fallback that always works
