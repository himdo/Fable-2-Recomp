// fable_2_vulkan_smoke.cpp
//
// Stage 1 of the Vulkan renderer for the Fable 2 port.
//
// A self-contained Vulkan pipeline that:
//   1. Creates a Win32 window.
//   2. Initializes Vulkan (instance, physical/logical device, graphics queue).
//   3. Creates a swapchain bound to the window.
//   4. Renders a solid clear color and presents, each frame, until the window
//      is closed.
//
// This proves the Vulkan foundation works in this repo before Stage 2 builds
// the Xenos -> Vulkan command processor so the game itself renders:
//   - headers are vendored in thirdparty/vulkan (Khronos, Apache-2.0);
//   - the loader is dynamically loaded from vulkan-1.dll (System32), so no
//     vulkan-1.lib import library is needed; every command is resolved through
//     vkGetInstanceProcAddr.
//
// Build:  build.cmd fable_2_vulkan_smoke
// Run:    out\build\win-amd64-<cfg>\fable_2_vulkan_smoke.exe

// Prevent windows.h from defining the min/max macros, which collide with
// std::min / std::numeric_limits<T>::max() used in the SDK's math.h.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>

#include <vulkan.h>

#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context_sdl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <io.h>

// Global log file. A GUI (no-console) launch has no usable stdout/stderr, so all
// diagnostics go straight to this FILE* (robust regardless of launch context).
static FILE* g_log = nullptr;
#define LOG(...) \
  do { \
    if (g_log) { \
      fprintf(g_log, __VA_ARGS__); \
      fflush(g_log); \
    } \
  } while (0)

// ---------------------------------------------------------------------------
// Dynamic Vulkan loader: resolve the global entry points from vulkan-1.dll,
// then every other command through vkGetInstanceProcAddr.
// ---------------------------------------------------------------------------
namespace {

HMODULE g_loader = nullptr;
PFN_vkCreateInstance g_vkCreateInstance = nullptr;
PFN_vkEnumerateInstanceExtensionProperties g_vkEnumerateInstanceExtensionProperties = nullptr;
PFN_vkEnumerateInstanceVersion g_vkEnumerateInstanceVersion = nullptr;
PFN_vkGetInstanceProcAddr g_vkGetInstanceProcAddr = nullptr;

bool LoadVulkan() {
  g_loader = LoadLibraryA("vulkan-1.dll");
  if (!g_loader) {
    LOG("FAIL: LoadLibrary(vulkan-1.dll) failed (error %lu)\n", GetLastError());
    return false;
  }
  g_vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
      GetProcAddress(g_loader, "vkCreateInstance"));
  g_vkEnumerateInstanceExtensionProperties =
      reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
          GetProcAddress(g_loader, "vkEnumerateInstanceExtensionProperties"));
  g_vkEnumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
      GetProcAddress(g_loader, "vkEnumerateInstanceVersion"));
  g_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
      GetProcAddress(g_loader, "vkGetInstanceProcAddr"));
  if (!g_vkCreateInstance || !g_vkEnumerateInstanceExtensionProperties ||
      !g_vkGetInstanceProcAddr) {
    LOG("FAIL: missing Vulkan loader entry points\n");
    return false;
  }
  uint32_t api_version = 0;
  if (g_vkEnumerateInstanceVersion) g_vkEnumerateInstanceVersion(&api_version);
 LOG("OK: loaded vulkan-1.dll (loader API %d.%d.%d)\n", VK_VERSION_MAJOR(api_version),
         VK_VERSION_MINOR(api_version), VK_VERSION_PATCH(api_version));
  return true;
}

bool HaveInstanceExtension(const char* name) {
  uint32_t count = 0;
  if (g_vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) {
    return false;
  }
  std::vector<VkExtensionProperties> props(count);
  if (count == 0 ||
      g_vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data()) != VK_SUCCESS) {
    return false;
  }
  for (const auto& p : props) {
    if (strcmp(p.extensionName, name) == 0) return true;
  }
  return false;
}

}  // namespace

// Every Vulkan command we use, resolved after instance creation.
struct VkFns {
  PFN_vkDestroyInstance destroy_instance = nullptr;
  PFN_vkEnumeratePhysicalDevices enumerate_physical_devices = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties get_queue_family_properties = nullptr;
  PFN_vkGetPhysicalDeviceProperties get_properties = nullptr;
  PFN_vkCreateDevice create_device = nullptr;
  PFN_vkDestroyDevice destroy_device = nullptr;
  PFN_vkGetDeviceQueue get_device_queue = nullptr;
  PFN_vkCreateWin32SurfaceKHR create_win32_surface = nullptr;
  PFN_vkDestroySurfaceKHR destroy_surface = nullptr;
  PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR get_surface_capabilities = nullptr;
  PFN_vkGetPhysicalDeviceSurfaceFormatsKHR get_surface_formats = nullptr;
  PFN_vkGetPhysicalDeviceSurfacePresentModesKHR get_surface_present_modes = nullptr;
  PFN_vkGetPhysicalDeviceSurfaceSupportKHR get_surface_support = nullptr;
  PFN_vkCreateSwapchainKHR create_swapchain = nullptr;
  PFN_vkDestroySwapchainKHR destroy_swapchain = nullptr;
  PFN_vkGetSwapchainImagesKHR get_swapchain_images = nullptr;
  PFN_vkAcquireNextImageKHR acquire_next_image = nullptr;
  PFN_vkQueueSubmit queue_submit = nullptr;
  PFN_vkQueuePresentKHR queue_present = nullptr;
  PFN_vkCreateRenderPass create_render_pass = nullptr;
  PFN_vkDestroyRenderPass destroy_render_pass = nullptr;
  PFN_vkCreateFramebuffer create_framebuffer = nullptr;
  PFN_vkDestroyFramebuffer destroy_framebuffer = nullptr;
  PFN_vkCreateImageView create_image_view = nullptr;
  PFN_vkDestroyImageView destroy_image_view = nullptr;
  PFN_vkCreateCommandPool create_command_pool = nullptr;
  PFN_vkDestroyCommandPool destroy_command_pool = nullptr;
  PFN_vkAllocateCommandBuffers allocate_command_buffers = nullptr;
  PFN_vkFreeCommandBuffers free_command_buffers = nullptr;
  PFN_vkBeginCommandBuffer begin_command_buffer = nullptr;
  PFN_vkEndCommandBuffer end_command_buffer = nullptr;
  PFN_vkCmdBeginRenderPass cmd_begin_render_pass = nullptr;
  PFN_vkCmdEndRenderPass cmd_end_render_pass = nullptr;
  PFN_vkCreateSemaphore create_semaphore = nullptr;
  PFN_vkDestroySemaphore destroy_semaphore = nullptr;
  PFN_vkCreateFence create_fence = nullptr;
  PFN_vkDestroyFence destroy_fence = nullptr;
  PFN_vkWaitForFences wait_for_fences = nullptr;
  PFN_vkResetFences reset_fences = nullptr;
  PFN_vkDeviceWaitIdle device_wait_idle = nullptr;

  template <typename T>
  bool Get(VkInstance instance, const char* name, T* out) {
    *out = reinterpret_cast<T>(g_vkGetInstanceProcAddr(instance, name));
    return *out != nullptr;
  }

  bool Resolve(VkInstance instance) {
#define R(n, m) \
  if (!Get(instance, n, &m)) { \
    LOG("FAIL: cannot resolve " n "\n"); \
    return false; \
  }
    R("vkDestroyInstance", destroy_instance)
    R("vkEnumeratePhysicalDevices", enumerate_physical_devices)
    R("vkGetPhysicalDeviceQueueFamilyProperties", get_queue_family_properties)
    R("vkGetPhysicalDeviceProperties", get_properties)
    R("vkCreateDevice", create_device)
    R("vkDestroyDevice", destroy_device)
    R("vkGetDeviceQueue", get_device_queue)
    R("vkCreateWin32SurfaceKHR", create_win32_surface)
    R("vkDestroySurfaceKHR", destroy_surface)
    R("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", get_surface_capabilities)
    R("vkGetPhysicalDeviceSurfaceFormatsKHR", get_surface_formats)
    R("vkGetPhysicalDeviceSurfacePresentModesKHR", get_surface_present_modes)
    R("vkGetPhysicalDeviceSurfaceSupportKHR", get_surface_support)
    R("vkCreateSwapchainKHR", create_swapchain)
    R("vkDestroySwapchainKHR", destroy_swapchain)
    R("vkGetSwapchainImagesKHR", get_swapchain_images)
    R("vkAcquireNextImageKHR", acquire_next_image)
    R("vkQueueSubmit", queue_submit)
    R("vkQueuePresentKHR", queue_present)
    R("vkCreateRenderPass", create_render_pass)
    R("vkDestroyRenderPass", destroy_render_pass)
    R("vkCreateFramebuffer", create_framebuffer)
    R("vkDestroyFramebuffer", destroy_framebuffer)
    R("vkCreateImageView", create_image_view)
    R("vkDestroyImageView", destroy_image_view)
    R("vkCreateCommandPool", create_command_pool)
    R("vkDestroyCommandPool", destroy_command_pool)
    R("vkAllocateCommandBuffers", allocate_command_buffers)
    R("vkFreeCommandBuffers", free_command_buffers)
    R("vkBeginCommandBuffer", begin_command_buffer)
    R("vkEndCommandBuffer", end_command_buffer)
    R("vkCmdBeginRenderPass", cmd_begin_render_pass)
    R("vkCmdEndRenderPass", cmd_end_render_pass)
    R("vkCreateSemaphore", create_semaphore)
    R("vkDestroySemaphore", destroy_semaphore)
    R("vkCreateFence", create_fence)
    R("vkDestroyFence", destroy_fence)
    R("vkWaitForFences", wait_for_fences)
    R("vkResetFences", reset_fences)
    R("vkDeviceWaitIdle", device_wait_idle)
#undef R
    return true;
  }
};

// The whole pipeline, owned by a single struct with ordered teardown.
struct VulkanApp {
  HWND hwnd = nullptr;
  UINT width = 1280, height = 720;

  VkInstance instance = VK_NULL_HANDLE;
  VkFns fns{};
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  char gpu_name[256] = {};
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queue_family = 0;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  std::vector<VkImage> images;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent = {};
  VkRenderPass render_pass = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers;
  std::vector<VkImageView> image_views;  // must outlive the framebuffers
  VkCommandPool command_pool = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> command_buffers;

  static constexpr int kFrames = 3;
  std::vector<VkSemaphore> image_available;
  std::vector<VkSemaphore> render_finished;
  std::vector<VkFence> in_flight;
  uint32_t frame = 0;

  bool CreateInstance() {
    if (!HaveInstanceExtension(VK_KHR_SURFACE_EXTENSION_NAME) ||
        !HaveInstanceExtension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
      LOG("FAIL: instance surface extensions not available\n");
      return false;
    }
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "fable_2_vulkan_smoke";
    app_info.apiVersion = VK_MAKE_VERSION(1, 1, 0);
    const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app_info;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = exts;
    VkResult r = g_vkCreateInstance(&info, nullptr, &instance);
    if (r != VK_SUCCESS) {
      LOG("FAIL: vkCreateInstance = %d\n", r);
      return false;
    }
    if (!fns.Resolve(instance)) return false;
   LOG("OK: created VkInstance (1.1)\n");
    return true;
  }

  bool CreateSurface() {
    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = GetModuleHandleW(nullptr);
    info.hwnd = hwnd;
    VkResult r = fns.create_win32_surface(instance, &info, nullptr, &surface);
    if (r != VK_SUCCESS) {
      LOG("FAIL: vkCreateWin32SurfaceKHR = %d\n", r);
      return false;
    }
   LOG("OK: created Win32 surface\n");
    return true;
  }

  bool PickDevice() {
    uint32_t count = 0;
    if (fns.enumerate_physical_devices(instance, &count, nullptr) != VK_SUCCESS || count == 0) {
      LOG("FAIL: no physical devices\n");
      return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    fns.enumerate_physical_devices(instance, &count, devices.data());
    for (auto pd : devices) {
      uint32_t qf_count = 0;
      fns.get_queue_family_properties(pd, &qf_count, nullptr);
      std::vector<VkQueueFamilyProperties> qf(qf_count);
      fns.get_queue_family_properties(pd, &qf_count, qf.data());
      bool ok = false;
      for (uint32_t i = 0; i < qf_count; ++i) {
        if (!(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        VkBool32 present_supported = VK_FALSE;
        fns.get_surface_support(pd, i, surface, &present_supported);
        if (present_supported) {
          queue_family = i;
          ok = true;
          break;
        }
      }
      if (!ok) continue;
      VkPhysicalDeviceProperties props{};
      fns.get_properties(pd, &props);
      snprintf(gpu_name, sizeof(gpu_name), "%s", props.deviceName);
      physical_device = pd;
      break;
    }
    if (physical_device == VK_NULL_HANDLE) {
      LOG("FAIL: no device with a graphics+present queue\n");
      return false;
    }
   LOG("OK: physical device \"%s\" (queue family %u)\n", gpu_name, queue_family);
    return true;
  }

  bool CreateDevice() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    const char* exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &qci;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = exts;
    VkResult r = fns.create_device(physical_device, &info, nullptr, &device);
    if (r != VK_SUCCESS) {
      LOG("FAIL: vkCreateDevice = %d\n", r);
      return false;
    }
    fns.get_device_queue(device, queue_family, 0, &queue);
   LOG("OK: created logical device + graphics/present queue\n");
    return true;
  }

  bool CreateSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    if (fns.get_surface_capabilities(physical_device, surface, &caps) != VK_SUCCESS) {
      LOG("FAIL: get_surface_capabilities\n");
      return false;
    }
    uint32_t fmt_count = 0;
    fns.get_surface_formats(physical_device, surface, &fmt_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmt_count);
    fns.get_surface_formats(physical_device, surface, &fmt_count, formats.data());
    format = formats.empty() ? VK_FORMAT_B8G8R8A8_UNORM : formats[0].format;

    if (caps.currentExtent.width == 0xFFFFFFFFu) {
      extent = {width, height};
    } else {
      extent = caps.currentExtent;
    }

    uint32_t min_images = caps.minImageCount ? caps.minImageCount : 2;
    if (caps.maxImageCount != 0 && min_images > caps.maxImageCount) {
      min_images = caps.maxImageCount;
    }
    const uint32_t count = min_images + 1;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface;
    sci.minImageCount = count;
    sci.imageFormat = format;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;
    VkResult r = fns.create_swapchain(device, &sci, nullptr, &swapchain);
    if (r != VK_SUCCESS) {
      LOG("FAIL: vkCreateSwapchainKHR = %d\n", r);
      return false;
    }

    uint32_t img_count = 0;
    fns.get_swapchain_images(device, swapchain, &img_count, nullptr);
    images.resize(img_count);
    fns.get_swapchain_images(device, swapchain, &img_count, images.data());
    framebuffers.resize(img_count);
    image_views.resize(img_count);
    command_buffers.resize(img_count);
   LOG("OK: swapchain (%u images, format %d, %ux%u)\n", img_count, format, extent.width,
           extent.height);
    return true;
  }

  bool CreateRenderPassAndCommands() {
    LOG("  [rp] start\n");
    VkAttachmentDescription att{};
    att.format = format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    LOG("  [rp] vkCreateRenderPass\n");
    if (fns.create_render_pass(device, &rpci, nullptr, &render_pass) != VK_SUCCESS) {
      LOG("FAIL: create_render_pass\n");
      return false;
    }

    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = queue_family;
    LOG("  [rp] vkCreateCommandPool\n");
    if (fns.create_command_pool(device, &cpci, nullptr, &command_pool) != VK_SUCCESS) {
      LOG("FAIL: create_command_pool\n");
      return false;
    }

    VkClearValue clear{};
    clear.color = {{0.15f, 0.55f, 0.85f, 1.0f}};  // a clear blue
    for (uint32_t i = 0; i < images.size(); ++i) {
      VkImageViewCreateInfo ivci{};
      ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      ivci.image = images[i];
      ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
      ivci.format = format;
      ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      // Keep the image view alive (the framebuffer references it); destroyed in
      // Shutdown after the framebuffers.
      LOG("  [rp] vkCreateImageView %u\n", i);
      if (fns.create_image_view(device, &ivci, nullptr, &image_views[i]) != VK_SUCCESS) {
        LOG("FAIL: create_image_view %u\n", i);
        return false;
      }
      VkFramebufferCreateInfo fci{};
      fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fci.renderPass = render_pass;
      fci.attachmentCount = 1;
      fci.pAttachments = &image_views[i];
      fci.width = extent.width;
      fci.height = extent.height;
      fci.layers = 1;
      LOG("  [rp] vkCreateFramebuffer %u\n", i);
      if (fns.create_framebuffer(device, &fci, nullptr, &framebuffers[i]) != VK_SUCCESS) {
        LOG("FAIL: create_framebuffer %u\n", i);
        return false;
      }

      VkCommandBufferAllocateInfo abci{};
      abci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      abci.commandPool = command_pool;
      abci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      abci.commandBufferCount = 1;
      LOG("  [rp] vkAllocateCommandBuffers %u\n", i);
      if (fns.allocate_command_buffers(device, &abci, &command_buffers[i]) != VK_SUCCESS) {
        LOG("FAIL: allocate_command_buffers %u\n", i);
        return false;
      }

      VkRenderPassBeginInfo rpb{};
      rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rpb.renderPass = render_pass;
      rpb.framebuffer = framebuffers[i];
      rpb.renderArea = {{0, 0}, extent};
      rpb.clearValueCount = 1;
      rpb.pClearValues = &clear;

      VkCommandBufferBeginInfo bci{};
      bci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      LOG("  [rp] vkBeginCommandBuffer %u\n", i);
      if (fns.begin_command_buffer(command_buffers[i], &bci) != VK_SUCCESS) {
        LOG("FAIL: begin_command_buffer %u\n", i);
        return false;
      }
      LOG("  [rp] vkCmdBeginRenderPass %u\n", i);
      fns.cmd_begin_render_pass(command_buffers[i], &rpb, VK_SUBPASS_CONTENTS_INLINE);
      fns.cmd_end_render_pass(command_buffers[i]);
      LOG("  [rp] vkEndCommandBuffer %u\n", i);
      fns.end_command_buffer(command_buffers[i]);
    }

    LOG("  [rp] creating sync objects\n");
    for (int i = 0; i < kFrames; ++i) {
      VkSemaphoreCreateInfo sci{};
      sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
      fns.create_semaphore(device, &sci, nullptr, &image_available[i]);
      fns.create_semaphore(device, &sci, nullptr, &render_finished[i]);
      VkFenceCreateInfo fci{};
      fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
      fns.create_fence(device, &fci, nullptr, &in_flight[i]);
    }
   LOG("OK: render pass + %zu framebuffers/commands + sync objects\n", images.size());
    return true;
  }

  bool Init(HWND handle) {
    hwnd = handle;
    RECT rc;
    GetClientRect(hwnd, &rc);
    width = (UINT)(rc.right - rc.left);
    height = (UINT)(rc.bottom - rc.top);
    image_available.resize(kFrames);
    render_finished.resize(kFrames);
    in_flight.resize(kFrames);
    if (!CreateInstance()) return false;
    if (!CreateSurface()) return false;
    if (!PickDevice()) return false;
    if (!CreateDevice()) return false;
    if (!CreateSwapchain()) return false;
    if (!CreateRenderPassAndCommands()) return false;
   LOG("OK: Vulkan pipeline ready (GPU: %s). Presenting clear color...\n", gpu_name);
    return true;
  }

  void PresentFrame() {
    fns.wait_for_fences(device, 1, &in_flight[frame], VK_TRUE, UINT64_MAX);
    uint32_t image_index = 0;
    VkResult r = fns.acquire_next_image(device, swapchain, UINT64_MAX, image_available[frame],
                                        VK_NULL_HANDLE, &image_index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
      image_index = 0;  // for the smoke test, keep going on a stale surface
    } else if (r != VK_SUCCESS) {
      LOG("WARN: acquire_next_image = %d\n", r);
      return;
    }
    fns.reset_fences(device, 1, &in_flight[frame]);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &image_available[frame];
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &command_buffers[image_index];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &render_finished[frame];
    if (fns.queue_submit(queue, 1, &si, in_flight[frame]) != VK_SUCCESS) {
      LOG("WARN: queue_submit failed\n");
      return;
    }
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &render_finished[frame];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &image_index;
    fns.queue_present(queue, &pi);
    frame = (frame + 1) % kFrames;
  }

  void Shutdown() {
    if (device == VK_NULL_HANDLE) {
      if (surface && fns.destroy_surface) fns.destroy_surface(instance, surface, nullptr);
      if (instance && fns.destroy_instance) fns.destroy_instance(instance, nullptr);
    } else {
      fns.device_wait_idle(device);
      for (auto f : in_flight) fns.destroy_fence(device, f, nullptr);
      for (auto s : image_available) fns.destroy_semaphore(device, s, nullptr);
      for (auto s : render_finished) fns.destroy_semaphore(device, s, nullptr);
      for (auto c : command_buffers) {
        fns.free_command_buffers(device, command_pool, 1, &c);
      }
      fns.destroy_command_pool(device, command_pool, nullptr);
      for (auto fb : framebuffers) fns.destroy_framebuffer(device, fb, nullptr);
      for (auto v : image_views) fns.destroy_image_view(device, v, nullptr);
      fns.destroy_render_pass(device, render_pass, nullptr);
      fns.destroy_swapchain(device, swapchain, nullptr);
      fns.destroy_device(device, nullptr);
      fns.destroy_surface(instance, surface, nullptr);
      fns.destroy_instance(instance, nullptr);
      device = VK_NULL_HANDLE;
      instance = VK_NULL_HANDLE;
    }
    if (g_loader) {
      FreeLibrary(g_loader);
      g_loader = nullptr;
    }
   LOG("OK: Vulkan shut down cleanly\n");
  }
};

// ---------------------------------------------------------------------------
// Win32 window + message loop.
// ---------------------------------------------------------------------------
static VulkanApp g_app;
static bool g_running = false;

// (window proc removed — the window is created by SDL, which provides its own proc)

// GUI subsystem: no console. Show a message box on exit; full log is written
// to vulkan_smoke.log next to the exe.
static int PauseExit(int code) {
  if (g_log) {
    fclose(g_log);
    g_log = nullptr;
  }
  MessageBoxW(nullptr,
              L"Smoke test finished. See vulkan_smoke.log (next to the exe) for the "
              L"full init log.\n\n0 = success, non-zero = failure.",
              L"Fable 2 Vulkan smoke", MB_OK);
  return code;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  // Do NOT AllocConsole: allocating a console can disrupt the process's desktop /
  // window-station association and break interactive top-level window creation
  // (exactly the symptom seen here). Log straight to a file next to the exe.
  wchar_t exepath[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exepath, MAX_PATH);
  if (wchar_t* slash = wcsrchr(exepath, L'\\')) *(slash + 1) = 0;
  wcscat(exepath, L"vulkan_smoke.log");
  g_log = _wfopen(exepath, L"w");
  if (!g_log) {
    MessageBoxW(nullptr, L"Could not open vulkan_smoke.log for writing.",
                L"Fable 2 Vulkan smoke", MB_OK | MB_ICONERROR);
    return 1;
  }
 LOG("=== fable_2 Vulkan smoke test (Stage 1) ===\n");
  if (!LoadVulkan()) return PauseExit(1);

  // Create the window via SDL — the same mechanism the game uses, which is
  // proven to work in this environment (raw CreateWindowExW fails here for
  // reasons that do not affect SDL's window creation).
  // Uses the SHARED SDL baked into rexruntime.dll — the game's proven path.
  rex::ui::SDLWindowedAppContext app_ctx;
  if (!app_ctx.Initialize()) {
    LOG("FAIL: SDLWindowedAppContext::Initialize (shared SDL video init)\n");
    return PauseExit(1);
  }
  LOG("  SDK WindowedAppContext initialized (shared SDL video)\n");
  auto window =
      rex::ui::Window::Create(app_ctx, "Fable 2 - Vulkan (Stage 1)", 1280, 720);
  if (!window) {
    LOG("FAIL: Window::Create\n");
    return PauseExit(1);
  }
  if (!window->Open()) {
    LOG("FAIL: Window::Open\n");
    return PauseExit(1);
  }
  HWND hwnd = (HWND)window->GetNativeWindowHandle();
  LOG("  SDK window opened, hwnd=%p phase=%d\n", (void*)hwnd, (int)window->phase());
  if (!hwnd) {
    LOG("FAIL: no native window handle\n");
    return PauseExit(1);
  }

  if (!g_app.Init(hwnd)) {
    LOG("\nSMOKE TEST FAILED during Vulkan init.\n");
    return PauseExit(1);
  }

  // Present a clear-color frame, then hand control to the SDK's message loop so
  // the window stays open and responsive until the user closes it.
  g_app.PresentFrame();
  LOG("  presented first clear-color frame; entering message loop (close to exit)\n");
  app_ctx.RunMainMessageLoop();

  g_app.Shutdown();
  LOG("\n=== smoke test complete ===\n");
  return PauseExit(0);
}
