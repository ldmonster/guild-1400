// OPTIONAL Vulkan graphics backend — compiled to nothing unless
// GUILD_HAVE_VULKAN is set. See vulkan_backend.h for the design / enable docs.
#ifdef GUILD_HAVE_VULKAN

#include "shim_impl/vulkan_backend.h"
#include "render/scene_drawlist.h"
#include "render/types.h"   // render::Surface (the 3D render target)

#include <cstddef>
#include <cstring>
#include <vector>

#ifdef GUILD_HAVE_SCENE_SHADERS
static const std::uint32_t kSceneVertSpv[] =
#include "shaders/scene_vert_spv.inc"
;
static const std::uint32_t kSceneFragSpv[] =
#include "shaders/scene_frag_spv.inc"
;
#endif

namespace guild::shim {

// --------------------------- format conversion -----------------------------
// Byte-for-byte identical to Sdl2GraphicsDevice::present(); see that file.
void ConvertToXRGB8888(const std::uint8_t* src, int width, int height, int bpp,
                       const std::uint32_t* palette, std::uint32_t* out) {
    if (!src || !out || width <= 0 || height <= 0)
        return;
    const int n = width * height;
    for (int i = 0; i < n; ++i) {
        std::uint32_t argb;
        if (bpp == 8) {
            const std::uint32_t pal = palette ? palette[src[i]] : 0u;
            argb = pal | 0xFF000000u;
        } else if (bpp == 16) {
            std::uint16_t v =
                static_cast<std::uint16_t>(src[i * 2] | (src[i * 2 + 1] << 8));
            std::uint32_t r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
            std::uint32_t r = (r5 << 3) | (r5 >> 2);
            std::uint32_t g = (g6 << 2) | (g6 >> 4);
            std::uint32_t b = (b5 << 3) | (b5 >> 2);
            argb = 0xFF000000u | (r << 16) | (g << 8) | b;
        } else { // 32bpp XRGB (engine stores B,G,R,X in memory)
            const std::uint8_t* p = src + i * 4;
            argb = 0xFF000000u | (static_cast<std::uint32_t>(p[2]) << 16) |
                   (static_cast<std::uint32_t>(p[1]) << 8) | p[0];
        }
        out[i] = argb;
    }
}

// ------------------------------- lifecycle ----------------------------------

bool VulkanGraphicsDevice::createInstance() {
    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "guild";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.pEngineName = "guild";
    ai.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &ai;
    // Swapchain mode needs the surface (+ platform/headless surface) instance
    // extensions; offscreen mode leaves instanceExts_ empty.
    if (!instanceExts_.empty()) {
        ci.enabledExtensionCount = static_cast<std::uint32_t>(instanceExts_.size());
        ci.ppEnabledExtensionNames = instanceExts_.data();
    }
    return vkCreateInstance(&ci, nullptr, &instance_) == VK_SUCCESS;
}

void VulkanGraphicsDevice::configureSwapchain(
    std::function<VkSurfaceKHR(VkInstance)> surfaceFactory,
    std::vector<const char*> instanceExtensions) {
    surfaceFactory_ = std::move(surfaceFactory);
    instanceExts_ = std::move(instanceExtensions);
}

bool VulkanGraphicsDevice::pickPhysicalDevice() {
    std::uint32_t n = 0;
    if (vkEnumeratePhysicalDevices(instance_, &n, nullptr) != VK_SUCCESS || n == 0)
        return false;
    std::vector<VkPhysicalDevice> devs(n);
    if (vkEnumeratePhysicalDevices(instance_, &n, devs.data()) != VK_SUCCESS)
        return false;

    // For each candidate, require a queue family with GRAPHICS|TRANSFER.
    // Prefer a CPU/software device (lavapipe) since the env may have no usable
    // GPU/display, but fall back to anything that has a suitable queue.
    auto queueFor = [](VkPhysicalDevice pd, std::uint32_t* outFamily) -> bool {
        std::uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
        if (qn == 0)
            return false;
        std::vector<VkQueueFamilyProperties> q(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, q.data());
        for (std::uint32_t i = 0; i < qn; ++i) {
            if (q[i].queueCount > 0 &&
                (q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                (q[i].queueFlags & VK_QUEUE_TRANSFER_BIT)) {
                *outFamily = i;
                return true;
            }
        }
        return false;
    };

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    std::uint32_t chosenFamily = 0;
    int chosenScore = -1;
    for (auto pd : devs) {
        std::uint32_t fam = 0;
        if (!queueFor(pd, &fam))
            continue;
        // Swapchain mode: the chosen queue family must also support present to
        // our surface, or we can't vkQueuePresentKHR on it.
        if (vkSurface_ != VK_NULL_HANDLE) {
            VkBool32 sup = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, fam, vkSurface_, &sup);
            if (!sup)
                continue;
        }
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(pd, &p);
        // Score: CPU (software) highest for headless robustness, then anything.
        int score = (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) ? 2 : 1;
        if (score > chosenScore) {
            chosenScore = score;
            chosen = pd;
            chosenFamily = fam;
        }
    }
    if (chosen == VK_NULL_HANDLE)
        return false;

    phys_ = chosen;
    queueFamily_ = chosenFamily;

    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(phys_, &p);
    deviceName_ = p.deviceName;
    apiVersion_ = std::to_string(VK_VERSION_MAJOR(p.apiVersion)) + "." +
                  std::to_string(VK_VERSION_MINOR(p.apiVersion)) + "." +
                  std::to_string(VK_VERSION_PATCH(p.apiVersion));
    return true;
}

bool VulkanGraphicsDevice::createDevice() {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = queueFamily_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    // Swapchain mode needs VK_KHR_swapchain on the device.
    std::vector<const char*> devExts;
    if (vkSurface_ != VK_NULL_HANDLE)
        devExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (!devExts.empty()) {
        dci.enabledExtensionCount = static_cast<std::uint32_t>(devExts.size());
        dci.ppEnabledExtensionNames = devExts.data();
    }
    // Enable anisotropic filtering when available — fixes the grazing-angle texture
    // shimmer (the walls) that isotropic trilinear mips still alias on.
    VkPhysicalDeviceFeatures avail{}; vkGetPhysicalDeviceFeatures(phys_, &avail);
    VkPhysicalDeviceFeatures want{};
    if (avail.samplerAnisotropy) {
        want.samplerAnisotropy = VK_TRUE;
        VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(phys_, &props);
        maxAnisotropy_ = props.limits.maxSamplerAnisotropy;
        if (maxAnisotropy_ > 16.0f) maxAnisotropy_ = 16.0f;
        if (maxAnisotropy_ < 1.0f) maxAnisotropy_ = 1.0f;
        dci.pEnabledFeatures = &want;
    }
    if (vkCreateDevice(phys_, &dci, nullptr, &device_) != VK_SUCCESS)
        return false;
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    return queue_ != VK_NULL_HANDLE;
}

std::int32_t VulkanGraphicsDevice::findMemoryType(std::uint32_t typeBits,
                                                  VkMemoryPropertyFlags want) const {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys_, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & want) == want)
            return static_cast<std::int32_t>(i);
    }
    return -1;
}

bool VulkanGraphicsDevice::createTargetImage() {
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_B8G8R8A8_UNORM; // matches XRGB8888 byte order
    ici.extent = {static_cast<std::uint32_t>(width_),
                  static_cast<std::uint32_t>(height_), 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;  // also a renderScene3D target
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ici, nullptr, &target_) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(device_, target_, &mr);
    std::int32_t mt =
        findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt < 0) // software ICDs may not flag DEVICE_LOCAL; accept anything
        mt = findMemoryType(mr.memoryTypeBits, 0);
    if (mt < 0)
        return false;

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = static_cast<std::uint32_t>(mt);
    if (vkAllocateMemory(device_, &mai, nullptr, &targetMem_) != VK_SUCCESS)
        return false;
    return vkBindImageMemory(device_, target_, targetMem_, 0) == VK_SUCCESS;
}

bool VulkanGraphicsDevice::createStaging() {
    stagingSize_ = static_cast<VkDeviceSize>(width_) * height_ * 4;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = stagingSize_;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bci, nullptr, &staging_) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(device_, staging_, &mr);
    std::int32_t mt = findMemoryType(mr.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt < 0)
        mt = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (mt < 0)
        return false;

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = static_cast<std::uint32_t>(mt);
    if (vkAllocateMemory(device_, &mai, nullptr, &stagingMem_) != VK_SUCCESS)
        return false;
    return vkBindBufferMemory(device_, staging_, stagingMem_, 0) == VK_SUCCESS;
}

bool VulkanGraphicsDevice::createCommandPool() {
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queueFamily_;
    if (vkCreateCommandPool(device_, &pci, nullptr, &cmdPool_) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbi.commandPool = cmdPool_;
    cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbi.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &cbi, &cmd_) != VK_SUCCESS)
        return false;

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    return vkCreateFence(device_, &fci, nullptr, &fence_) == VK_SUCCESS;
}

bool VulkanGraphicsDevice::init(int width, int height, int bpp, bool /*fullscreen*/) {
    if (inited_)
        shutdown();
    if (width <= 0 || height <= 0 || (bpp != 8 && bpp != 16 && bpp != 32))
        return false;

    width_ = width;
    height_ = height;
    bpp_ = bpp;

    const int bpx = bpp / 8;
    framebuffer_.assign(static_cast<std::size_t>(width) * height * bpx, 0);
    convert_.assign(static_cast<std::size_t>(width) * height, 0);
    surface_.pixels = framebuffer_.data();
    surface_.width = width;
    surface_.height = height;
    surface_.pitch = width * bpx;
    surface_.bpp = bpp;

    if (!createInstance()) {
        destroyAll();
        return false;
    }
    // Swapchain mode: create the surface from the just-created instance BEFORE
    // picking the device (so we can require present support). A null factory or
    // a failed surface leaves vkSurface_ == VK_NULL_HANDLE -> offscreen path.
    if (surfaceFactory_) {
        vkSurface_ = surfaceFactory_(instance_);
    }
    if (!pickPhysicalDevice() || !createDevice() || !createTargetImage() ||
        !createStaging() || !createCommandPool()) {
        destroyAll();
        return false;
    }
    // Build the swapchain on the surface. If it fails, fall back to offscreen
    // (destroy the surface) so the device still works headless.
    if (vkSurface_ != VK_NULL_HANDLE) {
        if (!createSwapchain()) {
            destroySwapchain();
            vkDestroySurfaceKHR(instance_, vkSurface_, nullptr);
            vkSurface_ = VK_NULL_HANDLE;
        }
    }
    targetLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    inited_ = true;
    return true;
}

void VulkanGraphicsDevice::destroyAll() {
    if (device_ != VK_NULL_HANDLE)
        vkDeviceWaitIdle(device_);
    // Reverse creation order. Swapchain + its semaphores are device-owned, so
    // destroy them before the device; the surface is instance-owned, so destroy
    // it after the device but before the instance.
    destroySwapchain();
    destroyScenePipeline();
    if (fence_ != VK_NULL_HANDLE) { vkDestroyFence(device_, fence_, nullptr); fence_ = VK_NULL_HANDLE; }
    if (cmdPool_ != VK_NULL_HANDLE) { vkDestroyCommandPool(device_, cmdPool_, nullptr); cmdPool_ = VK_NULL_HANDLE; cmd_ = VK_NULL_HANDLE; }
    if (staging_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, staging_, nullptr); staging_ = VK_NULL_HANDLE; }
    if (stagingMem_ != VK_NULL_HANDLE) { vkFreeMemory(device_, stagingMem_, nullptr); stagingMem_ = VK_NULL_HANDLE; }
    if (target_ != VK_NULL_HANDLE) { vkDestroyImage(device_, target_, nullptr); target_ = VK_NULL_HANDLE; }
    if (targetMem_ != VK_NULL_HANDLE) { vkFreeMemory(device_, targetMem_, nullptr); targetMem_ = VK_NULL_HANDLE; }
    if (device_ != VK_NULL_HANDLE) { vkDestroyDevice(device_, nullptr); device_ = VK_NULL_HANDLE; }
    if (vkSurface_ != VK_NULL_HANDLE) { vkDestroySurfaceKHR(instance_, vkSurface_, nullptr); vkSurface_ = VK_NULL_HANDLE; }
    if (instance_ != VK_NULL_HANDLE) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
    queue_ = VK_NULL_HANDLE;
    phys_ = VK_NULL_HANDLE;
}

// destroySwapchain: device-owned swapchain + sync semaphores. Safe to call when
// inactive. The surface itself is destroyed in destroyAll (instance-owned).
void VulkanGraphicsDevice::destroySwapchain() {
    if (device_ == VK_NULL_HANDLE)
        return;
    if (imageAvailable_ != VK_NULL_HANDLE) { vkDestroySemaphore(device_, imageAvailable_, nullptr); imageAvailable_ = VK_NULL_HANDLE; }
    if (renderFinished_ != VK_NULL_HANDLE) { vkDestroySemaphore(device_, renderFinished_, nullptr); renderFinished_ = VK_NULL_HANDLE; }
    if (swapchain_ != VK_NULL_HANDLE) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
    swapImages_.clear();
    swapFormat_ = VK_FORMAT_UNDEFINED;
    swapExtent_ = {0, 0};
}

// createSwapchain: build the swapchain on surface_ + acquire/present semaphores.
bool VulkanGraphicsDevice::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, vkSurface_, &caps) != VK_SUCCESS)
        return false;

    // Extent: honor the surface's fixed extent, else clamp our framebuffer size.
    VkExtent2D extent;
    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        extent = caps.currentExtent;
    } else {
        auto clampu = [](std::uint32_t v, std::uint32_t lo, std::uint32_t hi) {
            return v < lo ? lo : (v > hi ? hi : v);
        };
        extent.width = clampu(static_cast<std::uint32_t>(width_),
                              caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = clampu(static_cast<std::uint32_t>(height_),
                               caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0)
        return false;

    // Format: prefer B8G8R8A8_UNORM (matches the target image), else take [0].
    std::uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, vkSurface_, &fn, nullptr);
    if (fn == 0)
        return false;
    std::vector<VkSurfaceFormatKHR> formats(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, vkSurface_, &fn, formats.data());
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = f; break; }
    swapFormat_ = chosen.format;
    swapExtent_ = extent;

    // We blit into the swapchain images -> they must support TRANSFER_DST.
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return false; // can't blit into the swapchain; fall back to offscreen
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    std::uint32_t minImages = caps.minImageCount;
    if (caps.maxImageCount > 0 && minImages > caps.maxImageCount)
        minImages = caps.maxImageCount;

    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha)) {
        for (std::uint32_t b = 1; b; b <<= 1)
            if (caps.supportedCompositeAlpha & b) { alpha = (VkCompositeAlphaFlagBitsKHR)b; break; }
    }

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = vkSurface_;
    sci.minImageCount = minImages;
    sci.imageFormat = chosen.format;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = usage;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = alpha;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // always supported
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain_) != VK_SUCCESS)
        return false;

    std::uint32_t ic = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &ic, nullptr);
    if (ic == 0)
        return false;
    swapImages_.resize(ic);
    vkGetSwapchainImagesKHR(device_, swapchain_, &ic, swapImages_.data());

    VkSemaphoreCreateInfo semci{};
    semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(device_, &semci, nullptr, &imageAvailable_) != VK_SUCCESS ||
        vkCreateSemaphore(device_, &semci, nullptr, &renderFinished_) != VK_SUCCESS)
        return false;
    return true;
}

void VulkanGraphicsDevice::shutdown() {
    destroyAll();
    framebuffer_.clear();
    convert_.clear();
    surface_ = Surface{};
    width_ = height_ = bpp_ = 0;
    presentCount_ = 0;
    swapchainPresentCount_ = 0;
    targetLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    inited_ = false;
}

Surface* VulkanGraphicsDevice::backbuffer() {
    if (!inited_)
        return nullptr;
    surface_.pixels = framebuffer_.data();
    return &surface_;
}

void VulkanGraphicsDevice::setPalette(const std::uint32_t* argb256) {
    if (!argb256)
        return;
    for (int i = 0; i < 256; ++i)
        palette_[i] = argb256[i];
}

void VulkanGraphicsDevice::present() {
    if (!inited_)
        return;

    // 1. Convert the CPU framebuffer to XRGB8888 and copy into staging memory.
    ConvertToXRGB8888(framebuffer_.data(), width_, height_, bpp_, palette_,
                      convert_.data());

    void* mapped = nullptr;
    if (vkMapMemory(device_, stagingMem_, 0, stagingSize_, 0, &mapped) != VK_SUCCESS)
        return;
    std::memcpy(mapped, convert_.data(), static_cast<std::size_t>(stagingSize_));
    vkUnmapMemory(device_, stagingMem_);

    // 2. Record: transition target -> TRANSFER_DST, copy buffer->image,
    //    transition -> TRANSFER_SRC (so readback can copy back out).
    vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd_, &bi) != VK_SUCCESS)
        return;

    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = targetLayout_;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = target_;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &toDst);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;   // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<std::uint32_t>(width_),
                          static_cast<std::uint32_t>(height_), 1};
    vkCmdCopyBufferToImage(cmd_, staging_, target_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toSrc{};
    toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image = target_;
    toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toSrc.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &toSrc);

    if (vkEndCommandBuffer(cmd_) != VK_SUCCESS)
        return;

    // 3. Submit + fence-wait.
    vkResetFences(device_, 1, &fence_);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    if (vkQueueSubmit(queue_, 1, &si, fence_) != VK_SUCCESS)
        return;
    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);

    targetLayout_ = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ++presentCount_;

    // On-screen path: blit the freshly-uploaded target image into the next
    // swapchain image and vkQueuePresentKHR it. Offscreen mode skips this.
    if (swapchain_ != VK_NULL_HANDLE)
        presentSwapchain();
}

// rebuildSwapchainAfterLoss: idle the device, drop the current swapchain (which
// also destroys imageAvailable_/renderFinished_), and build a fresh one on the
// same surface. On failure the surface is released and the device stays
// offscreen. Recreating the semaphores here is what makes the post-acquire error
// paths safe: a binary semaphore that vkAcquireNextImageKHR already signaled but
// that no submit consumed would otherwise be re-signaled next frame (a spec
// violation); destroying + recreating it guarantees a fresh unsignaled state.
void VulkanGraphicsDevice::rebuildSwapchainAfterLoss() {
    vkDeviceWaitIdle(device_);
    destroySwapchain();
    if (!createSwapchain()) {
        vkDestroySurfaceKHR(instance_, vkSurface_, nullptr);
        vkSurface_ = VK_NULL_HANDLE; // give up on-screen, stay offscreen
    }
}

// presentSwapchain: acquire -> blit target_ (TRANSFER_SRC) into the acquired
// swapchain image -> transition to PRESENT_SRC -> vkQueuePresentKHR. CPU-synced
// with fence_; the two binary semaphores are reused each frame (fully serialized).
bool VulkanGraphicsDevice::presentSwapchain() {
    std::uint32_t idx = 0;
    VkResult acq = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                         imageAvailable_, VK_NULL_HANDLE, &idx);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        // Window resized/lost: rebuild the swapchain, skip this frame. (Acquire
        // failed, so imageAvailable_ was not signaled; the rebuild still resets
        // both semaphores for good measure.)
        rebuildSwapchainAfterLoss();
        return false;
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR)
        return false;
    // From here on vkAcquireNextImageKHR HAS signaled imageAvailable_. Any bailout
    // before the submit consumes it must rebuild (recreating the semaphore), or the
    // next frame's acquire would re-signal an already-signaled binary semaphore.

    vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd_, &bi) != VK_SUCCESS) {
        rebuildSwapchainAfterLoss(); // drains the signaled imageAvailable_
        return false;
    }

    VkImage dst = swapImages_[idx];
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    // swapchain image: (undefined/present) -> TRANSFER_DST
    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = dst;
    toDst.subresourceRange = range;
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &toDst);

    // Blit target_ (TRANSFER_SRC, width_ x height_) -> swapchain (swapExtent_).
    // vkCmdBlitImage scales + converts format with NEAREST filtering.
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {width_, height_, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {static_cast<std::int32_t>(swapExtent_.width),
                          static_cast<std::int32_t>(swapExtent_.height), 1};
    vkCmdBlitImage(cmd_, target_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_NEAREST);

    // swapchain image: TRANSFER_DST -> PRESENT_SRC
    VkImageMemoryBarrier toPresent{};
    toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = dst;
    toPresent.subresourceRange = range;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toPresent);

    if (vkEndCommandBuffer(cmd_) != VK_SUCCESS) {
        rebuildSwapchainAfterLoss(); // drains the signaled imageAvailable_
        return false;
    }

    vkResetFences(device_, 1, &fence_);
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &imageAvailable_;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderFinished_;
    if (vkQueueSubmit(queue_, 1, &si, fence_) != VK_SUCCESS) {
        // Submit never ran, so imageAvailable_ stays signaled; rebuild to drain it.
        rebuildSwapchainAfterLoss();
        return false;
    }
    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderFinished_;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &idx;
    VkResult pr = vkQueuePresentKHR(queue_, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
        rebuildSwapchainAfterLoss();
        return false;
    }
    if (pr != VK_SUCCESS)
        return false;
    // target_ was the blit source -> still TRANSFER_SRC_OPTIMAL; readback works.
    ++swapchainPresentCount_;
    return true;
}

std::vector<std::uint32_t> VulkanGraphicsDevice::readback() {
    std::vector<std::uint32_t> out;
    if (!inited_ || presentCount_ == 0)
        return out;

    // Copy target image -> staging buffer, fence-wait, then map.
    vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd_, &bi) != VK_SUCCESS)
        return out;

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<std::uint32_t>(width_),
                          static_cast<std::uint32_t>(height_), 1};
    vkCmdCopyImageToBuffer(cmd_, target_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging_, 1, &region);

    if (vkEndCommandBuffer(cmd_) != VK_SUCCESS)
        return out;

    vkResetFences(device_, 1, &fence_);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    if (vkQueueSubmit(queue_, 1, &si, fence_) != VK_SUCCESS)
        return out;
    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);

    void* mapped = nullptr;
    if (vkMapMemory(device_, stagingMem_, 0, stagingSize_, 0, &mapped) != VK_SUCCESS)
        return out;
    out.resize(static_cast<std::size_t>(width_) * height_);
    std::memcpy(out.data(), mapped, static_cast<std::size_t>(stagingSize_));
    vkUnmapMemory(device_, stagingMem_);
    return out;
}

// =========================================================================
// 3D scene GPU pipeline (rule 3: the engine's fixed-function 3D raster, GPU side
// swapped to Vulkan). renderScene3D draws play::BuildSceneDrawList output into an
// offscreen colour+depth image with scene.vert/scene.frag, then reads it back into
// the caller's CPU surface so the 2D overlay + present path are untouched.
// =========================================================================
#ifdef GUILD_HAVE_SCENE_SHADERS

namespace {
VkShaderModule MakeModule(VkDevice dev, const std::uint32_t* code, std::size_t bytes) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}
} // namespace

void VulkanGraphicsDevice::destroyScenePipeline() {
    if (device_ == VK_NULL_HANDLE) return;
    destroySceneFrameResources();
    if (scenePipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, scenePipeline_, nullptr); scenePipeline_ = VK_NULL_HANDLE; }
    if (scenePipelineShadow_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, scenePipelineShadow_, nullptr); scenePipelineShadow_ = VK_NULL_HANDLE; }
    if (scenePipelineAdditive_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, scenePipelineAdditive_, nullptr); scenePipelineAdditive_ = VK_NULL_HANDLE; }
    if (scenePipelineAlpha_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, scenePipelineAlpha_, nullptr); scenePipelineAlpha_ = VK_NULL_HANDLE; }
    if (sceneFb_ != VK_NULL_HANDLE) { vkDestroyFramebuffer(device_, sceneFb_, nullptr); sceneFb_ = VK_NULL_HANDLE; }
    if (sceneColorView_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, sceneColorView_, nullptr); sceneColorView_ = VK_NULL_HANDLE; }
    if (sceneColor_ != VK_NULL_HANDLE) { vkDestroyImage(device_, sceneColor_, nullptr); sceneColor_ = VK_NULL_HANDLE; }
    if (sceneColorMem_ != VK_NULL_HANDLE) { vkFreeMemory(device_, sceneColorMem_, nullptr); sceneColorMem_ = VK_NULL_HANDLE; }
    if (sceneDepthView_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, sceneDepthView_, nullptr); sceneDepthView_ = VK_NULL_HANDLE; }
    if (sceneDepth_ != VK_NULL_HANDLE) { vkDestroyImage(device_, sceneDepth_, nullptr); sceneDepth_ = VK_NULL_HANDLE; }
    if (sceneDepthMem_ != VK_NULL_HANDLE) { vkFreeMemory(device_, sceneDepthMem_, nullptr); sceneDepthMem_ = VK_NULL_HANDLE; }
    if (sceneStaging_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, sceneStaging_, nullptr); sceneStaging_ = VK_NULL_HANDLE; }
    if (sceneStagingMem_ != VK_NULL_HANDLE) { vkFreeMemory(device_, sceneStagingMem_, nullptr); sceneStagingMem_ = VK_NULL_HANDLE; }
    sceneRW_ = 0; sceneRH_ = 0; sceneStagingSize_ = 0;
    if (sceneSampler_ != VK_NULL_HANDLE) { vkDestroySampler(device_, sceneSampler_, nullptr); sceneSampler_ = VK_NULL_HANDLE; }
    if (sceneSamplerLinear_ != VK_NULL_HANDLE) { vkDestroySampler(device_, sceneSamplerLinear_, nullptr); sceneSamplerLinear_ = VK_NULL_HANDLE; }
    if (scenePipeLayout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device_, scenePipeLayout_, nullptr); scenePipeLayout_ = VK_NULL_HANDLE; }
    if (sceneDescLayout_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device_, sceneDescLayout_, nullptr); sceneDescLayout_ = VK_NULL_HANDLE; }
    if (scenePass_ != VK_NULL_HANDLE) { vkDestroyRenderPass(device_, scenePass_, nullptr); scenePass_ = VK_NULL_HANDLE; }
    sceneReady_ = false; scenePipelineCull_ = -1;
}

bool VulkanGraphicsDevice::ensureScenePipeline() {
    if (sceneFailed_) return false;
    if (sceneReady_) return true;

    sceneDepthFormat_ = VK_FORMAT_D32_SFLOAT;
    // Render pass: colour (clear -> store, finalLayout TRANSFER_SRC for readback) + depth.
    {
        VkAttachmentDescription att[2]{};
        att[0].format = VK_FORMAT_B8G8R8A8_UNORM;
        att[0].samples = VK_SAMPLE_COUNT_1_BIT;
        att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        att[1].format = sceneDepthFormat_;
        att[1].samples = VK_SAMPLE_COUNT_1_BIT;
        att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference colRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1; sub.pColorAttachments = &colRef;
        sub.pDepthStencilAttachment = &depRef;
        VkRenderPassCreateInfo rpi{}; rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpi.attachmentCount = 2; rpi.pAttachments = att;
        rpi.subpassCount = 1; rpi.pSubpasses = &sub;
        if (vkCreateRenderPass(device_, &rpi, nullptr, &scenePass_) != VK_SUCCESS) { sceneFailed_ = true; return false; }
    }

    // Sampler: NEAREST + REPEAT (== render::SampleTexel per-axis floor(uv*w) mod w).
    {
        VkSamplerCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_NEAREST; si.minFilter = VK_FILTER_NEAREST;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        if (vkCreateSampler(device_, &si, nullptr, &sceneSampler_) != VK_SUCCESS) { sceneFailed_ = true; return false; }
        // Trilinear variant: LINEAR mag/min + LINEAR mip mode + full LOD range, so the
        // GPU mip-samples minified surfaces (anti-aliases the camera-motion shimmer).
        si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; si.maxLod = VK_LOD_CLAMP_NONE;
        if (maxAnisotropy_ > 1.0f) { si.anisotropyEnable = VK_TRUE; si.maxAnisotropy = maxAnisotropy_; }
        if (vkCreateSampler(device_, &si, nullptr, &sceneSamplerLinear_) != VK_SUCCESS) { sceneFailed_ = true; return false; }
    }

    // Descriptor set layout (binding 0 = combined image sampler, fragment) + push constants.
    {
        VkDescriptorSetLayoutBinding b{}; b.binding = 0; b.descriptorCount = 1;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo li{}; li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 1; li.pBindings = &b;
        if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &sceneDescLayout_) != VK_SUCCESS) { sceneFailed_ = true; return false; }
        VkPushConstantRange pcr{}; pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0; pcr.size = 128;  // 8 * vec4
        VkPipelineLayoutCreateInfo pli{}; pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &sceneDescLayout_;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
        if (vkCreatePipelineLayout(device_, &pli, nullptr, &scenePipeLayout_) != VK_SUCCESS) { sceneFailed_ = true; return false; }
    }

    sceneReady_ = true;
    return true;
}

// (Re)create the scene colour+depth images + framebuffer + readback staging at rW x rH
// (the draw-list resolution, possibly supersampled above the device size).
bool VulkanGraphicsDevice::ensureSceneTargets(int rW, int rH) {
    if (rW == sceneRW_ && rH == sceneRH_ && sceneFb_ != VK_NULL_HANDLE) return true;
    vkDeviceWaitIdle(device_);
    if (sceneFb_ != VK_NULL_HANDLE) { vkDestroyFramebuffer(device_, sceneFb_, nullptr); sceneFb_ = VK_NULL_HANDLE; }
    if (sceneColorView_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, sceneColorView_, nullptr); sceneColorView_ = VK_NULL_HANDLE; }
    if (sceneColor_ != VK_NULL_HANDLE) { vkDestroyImage(device_, sceneColor_, nullptr); sceneColor_ = VK_NULL_HANDLE; }
    if (sceneColorMem_ != VK_NULL_HANDLE) { vkFreeMemory(device_, sceneColorMem_, nullptr); sceneColorMem_ = VK_NULL_HANDLE; }
    if (sceneDepthView_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, sceneDepthView_, nullptr); sceneDepthView_ = VK_NULL_HANDLE; }
    if (sceneDepth_ != VK_NULL_HANDLE) { vkDestroyImage(device_, sceneDepth_, nullptr); sceneDepth_ = VK_NULL_HANDLE; }
    if (sceneDepthMem_ != VK_NULL_HANDLE) { vkFreeMemory(device_, sceneDepthMem_, nullptr); sceneDepthMem_ = VK_NULL_HANDLE; }

    auto mkImage = [&](VkFormat fmt, VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem) {
        VkImageCreateInfo ici{}; ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D; ici.format = fmt;
        ici.extent = {(std::uint32_t)rW, (std::uint32_t)rH, 1};
        ici.mipLevels = 1; ici.arrayLayers = 1; ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL; ici.usage = usage; ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_, &ici, nullptr, &img) != VK_SUCCESS) return false;
        VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(device_, img, &mr);
        std::int32_t mt = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mt < 0) mt = findMemoryType(mr.memoryTypeBits, 0);
        VkMemoryAllocateInfo mai{}; mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size; mai.memoryTypeIndex = (std::uint32_t)mt;
        if (mt < 0 || vkAllocateMemory(device_, &mai, nullptr, &mem) != VK_SUCCESS) return false;
        return vkBindImageMemory(device_, img, mem, 0) == VK_SUCCESS;
    };
    if (!mkImage(VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                 sceneColor_, sceneColorMem_)) { sceneFailed_ = true; return false; }
    if (!mkImage(sceneDepthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, sceneDepth_, sceneDepthMem_)) { sceneFailed_ = true; return false; }
    VkImageViewCreateInfo cv{}; cv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    cv.image = sceneColor_; cv.viewType = VK_IMAGE_VIEW_TYPE_2D; cv.format = VK_FORMAT_B8G8R8A8_UNORM;
    cv.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &cv, nullptr, &sceneColorView_) != VK_SUCCESS) { sceneFailed_ = true; return false; }
    VkImageViewCreateInfo dv{}; dv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    dv.image = sceneDepth_; dv.viewType = VK_IMAGE_VIEW_TYPE_2D; dv.format = sceneDepthFormat_;
    dv.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &dv, nullptr, &sceneDepthView_) != VK_SUCCESS) { sceneFailed_ = true; return false; }
    VkImageView views[2] = {sceneColorView_, sceneDepthView_};
    VkFramebufferCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = scenePass_; fci.attachmentCount = 2; fci.pAttachments = views;
    fci.width = (std::uint32_t)rW; fci.height = (std::uint32_t)rH; fci.layers = 1;
    if (vkCreateFramebuffer(device_, &fci, nullptr, &sceneFb_) != VK_SUCCESS) { sceneFailed_ = true; return false; }

    // Readback staging sized to the scene resolution.
    const VkDeviceSize need = (VkDeviceSize)rW * rH * 4;
    if (need > sceneStagingSize_) {
        if (sceneStaging_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, sceneStaging_, nullptr); sceneStaging_ = VK_NULL_HANDLE; }
        if (sceneStagingMem_ != VK_NULL_HANDLE) { vkFreeMemory(device_, sceneStagingMem_, nullptr); sceneStagingMem_ = VK_NULL_HANDLE; }
        VkBufferCreateInfo bci{}; bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = need; bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (vkCreateBuffer(device_, &bci, nullptr, &sceneStaging_) != VK_SUCCESS) { sceneFailed_ = true; return false; }
        VkMemoryRequirements mr{}; vkGetBufferMemoryRequirements(device_, sceneStaging_, &mr);
        std::int32_t mt = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (mt < 0) mt = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        VkMemoryAllocateInfo mai{}; mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size; mai.memoryTypeIndex = (std::uint32_t)mt;
        if (mt < 0 || vkAllocateMemory(device_, &mai, nullptr, &sceneStagingMem_) != VK_SUCCESS) { sceneFailed_ = true; return false; }
        vkBindBufferMemory(device_, sceneStaging_, sceneStagingMem_, 0);
        sceneStagingSize_ = need;
    }
    sceneRW_ = rW; sceneRH_ = rH;
    return true;
}

bool VulkanGraphicsDevice::createSceneTexture(int w, int h, const std::uint32_t* argb,
                                              const std::vector<std::vector<std::uint32_t>>* mips,
                                              VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
    if (w <= 0 || h <= 0) return false;
    const std::uint32_t levels = 1u + (mips ? (std::uint32_t)mips->size() : 0u);
    VkImageCreateInfo ici{}; ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D; ici.format = VK_FORMAT_B8G8R8A8_UNORM;
    ici.extent = {(std::uint32_t)w, (std::uint32_t)h, 1};
    ici.mipLevels = levels; ici.arrayLayers = 1; ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ici, nullptr, &img) != VK_SUCCESS) return false;
    VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(device_, img, &mr);
    std::int32_t mt = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt < 0) mt = findMemoryType(mr.memoryTypeBits, 0);
    VkMemoryAllocateInfo mai{}; mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size; mai.memoryTypeIndex = (std::uint32_t)mt;
    if (mt < 0 || vkAllocateMemory(device_, &mai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindImageMemory(device_, img, mem, 0);

    // Pack all mip levels into one host-visible staging buffer.
    std::vector<std::pair<const std::uint32_t*, std::uint64_t>> srcs;   // ptr, byteOffset
    auto levelDim = [&](std::uint32_t L, int& lw, int& lh){ lw = w >> L; if (lw<1) lw=1; lh = h >> L; if (lh<1) lh=1; };
    std::uint64_t total = 0;
    for (std::uint32_t L = 0; L < levels; ++L) {
        int lw, lh; levelDim(L, lw, lh);
        const std::uint32_t* p = (L == 0) ? argb : (*mips)[L - 1].data();
        srcs.push_back({p, total}); total += (std::uint64_t)lw * lh * 4;
    }
    VkBuffer sb = VK_NULL_HANDLE; VkDeviceMemory sm = VK_NULL_HANDLE;
    VkBufferCreateInfo bci{}; bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = total; bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(device_, &bci, nullptr, &sb) != VK_SUCCESS) return false;
    VkMemoryRequirements bmr{}; vkGetBufferMemoryRequirements(device_, sb, &bmr);
    std::int32_t bmt = findMemoryType(bmr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (bmt < 0) bmt = findMemoryType(bmr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    VkMemoryAllocateInfo bmai{}; bmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bmai.allocationSize = bmr.size; bmai.memoryTypeIndex = (std::uint32_t)bmt;
    if (bmt < 0 || vkAllocateMemory(device_, &bmai, nullptr, &sm) != VK_SUCCESS) { vkDestroyBuffer(device_, sb, nullptr); return false; }
    vkBindBufferMemory(device_, sb, sm, 0);
    void* mapped = nullptr; vkMapMemory(device_, sm, 0, total, 0, &mapped);
    for (std::uint32_t L = 0; L < levels; ++L) {
        int lw, lh; levelDim(L, lw, lh);
        std::memcpy((std::uint8_t*)mapped + srcs[L].second, srcs[L].first, (std::size_t)lw * lh * 4);
    }
    vkUnmapMemory(device_, sm);

    vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo cbi{}; cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd_, &cbi);
    VkImageMemoryBarrier toDst{}; toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = img; toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);
    std::vector<VkBufferImageCopy> copies(levels);
    for (std::uint32_t L = 0; L < levels; ++L) {
        int lw, lh; levelDim(L, lw, lh);
        copies[L] = {}; copies[L].bufferOffset = srcs[L].second;
        copies[L].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, L, 0, 1};
        copies[L].imageExtent = {(std::uint32_t)lw, (std::uint32_t)lh, 1};
    }
    vkCmdCopyBufferToImage(cmd_, sb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels, copies.data());
    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
    vkEndCommandBuffer(cmd_);
    vkResetFences(device_, 1, &fence_);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount = 1; si.pCommandBuffers = &cmd_;
    vkQueueSubmit(queue_, 1, &si, fence_);
    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkDestroyBuffer(device_, sb, nullptr); vkFreeMemory(device_, sm, nullptr);

    VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_B8G8R8A8_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
    return vkCreateImageView(device_, &vi, nullptr, &view) == VK_SUCCESS;
}

bool VulkanGraphicsDevice::renderScene3D(const render::Scene3DDrawList& dl, render::Surface* target) {
    if (!inited_ || !target || !target->pixels) return false;
    if (dl.width <= 0 || dl.height <= 0) return false;
    if (!ensureScenePipeline()) return false;
    if (!ensureSceneTargets(dl.width, dl.height)) return false;   // render at the draw-list res
    if (dl.verts.empty()) return false;

    // (Re)build the graphics pipeline if the requested backface-cull changed.
    if (scenePipeline_ == VK_NULL_HANDLE || scenePipelineCull_ != dl.backfaceCull) {
        if (scenePipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device_, scenePipeline_, nullptr); scenePipeline_ = VK_NULL_HANDLE; }
        VkShaderModule vsm = MakeModule(device_, kSceneVertSpv, sizeof(kSceneVertSpv));
        VkShaderModule fsm = MakeModule(device_, kSceneFragSpv, sizeof(kSceneFragSpv));
        if (!vsm || !fsm) { if (vsm) vkDestroyShaderModule(device_, vsm, nullptr); if (fsm) vkDestroyShaderModule(device_, fsm, nullptr); sceneFailed_ = true; return false; }
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = vsm; st[0].pName = "main";
        st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fsm; st[1].pName = "main";

        VkVertexInputBindingDescription bind{}; bind.binding = 0;
        bind.stride = sizeof(render::SceneDrawVertex); bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attr[3]{};
        attr[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, (std::uint32_t)offsetof(render::SceneDrawVertex, pos)};
        attr[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, (std::uint32_t)offsetof(render::SceneDrawVertex, color)};
        attr[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, (std::uint32_t)offsetof(render::SceneDrawVertex, uv)};
        VkPipelineVertexInputStateCreateInfo vin{}; vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vin.vertexBindingDescriptionCount = 1; vin.pVertexBindingDescriptions = &bind;
        vin.vertexAttributeDescriptionCount = 3; vin.pVertexAttributeDescriptions = attr;
        VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vps{}; vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vps.viewportCount = 1; vps.scissorCount = 1;   // viewport/scissor are dynamic (set per render res)
        const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dsi{}; dsi.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dsi.dynamicStateCount = 2; dsi.pDynamicStates = dyn;
        VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
        // CPU area == 2*Vulkan signed area (same sign): mode 1 culls area<0 == back faces
        // with CCW front; mode 2 culls area>0 == front faces (== back with CW front).
        if (dl.backfaceCull == 0) { rs.cullMode = VK_CULL_MODE_NONE; }
        else { rs.cullMode = VK_CULL_MODE_BACK_BIT;
               // Empirically (lavapipe, y-down viewport): CPU "cull area<0" (mode 1)
               // == cull BACK with CW front; mode 2 == CCW front.
               rs.frontFace = (dl.backfaceCull == 1) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE; }
        VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState cba{}; cba.colorWriteMask = 0xF; cba.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;
        VkGraphicsPipelineCreateInfo gp{}; gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2; gp.pStages = st; gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vps; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds; gp.pColorBlendState = &cb; gp.pDynamicState = &dsi;
        gp.layout = scenePipeLayout_; gp.renderPass = scenePass_; gp.subpass = 0;
        VkResult pr = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &scenePipeline_);

        // Shadow pipeline: two-sided + multiply blend (dst = dst * src) so a shadow
        // batch (white texture * the 0.5 vertex colour) darkens the receiver.
        if (pr == VK_SUCCESS && scenePipelineShadow_ == VK_NULL_HANDLE) {
            VkPipelineRasterizationStateCreateInfo rss = rs; rss.cullMode = VK_CULL_MODE_NONE;
            VkPipelineColorBlendAttachmentState sba{}; sba.colorWriteMask = 0xF; sba.blendEnable = VK_TRUE;
            sba.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO; sba.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
            sba.colorBlendOp = VK_BLEND_OP_ADD;
            sba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; sba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            sba.alphaBlendOp = VK_BLEND_OP_ADD;
            VkPipelineColorBlendStateCreateInfo scb = cb; scb.pAttachments = &sba;
            VkGraphicsPipelineCreateInfo sgp = gp; sgp.pRasterizationState = &rss; sgp.pColorBlendState = &scb;
            vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &sgp, nullptr, &scenePipelineShadow_);
        }

        // Transparent pipelines (gilde.exe 0x5e0358 VIBE_Render_SetBlendMode): depth-
        // TESTED but NO depth write (the glow layers without occluding). The frag shader
        // outputs alpha = the per-batch opacity (pc.fwd.w), so SRC_ALPHA scales the source.
        //   additive (material mode 2): dst += src*opacity   (window light shaft / flame)
        //   alpha    (material mode 1): dst = dst*(1-op) + src*op
        if (pr == VK_SUCCESS) {
            VkPipelineDepthStencilStateCreateInfo tds = ds; tds.depthWriteEnable = VK_FALSE;
            VkPipelineColorBlendAttachmentState tba{}; tba.colorWriteMask = 0xF; tba.blendEnable = VK_TRUE;
            tba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            tba.colorBlendOp = VK_BLEND_OP_ADD;
            tba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; tba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            tba.alphaBlendOp = VK_BLEND_OP_ADD;
            // additive: dst factor ONE
            if (scenePipelineAdditive_ == VK_NULL_HANDLE) {
                VkPipelineColorBlendAttachmentState aba = tba; aba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                VkPipelineColorBlendStateCreateInfo acb = cb; acb.pAttachments = &aba;
                VkGraphicsPipelineCreateInfo agp = gp; agp.pDepthStencilState = &tds; agp.pColorBlendState = &acb;
                vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &agp, nullptr, &scenePipelineAdditive_);
            }
            // alpha: dst factor ONE_MINUS_SRC_ALPHA
            if (scenePipelineAlpha_ == VK_NULL_HANDLE) {
                VkPipelineColorBlendAttachmentState lba = tba; lba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                VkPipelineColorBlendStateCreateInfo lcb = cb; lcb.pAttachments = &lba;
                VkGraphicsPipelineCreateInfo lgp = gp; lgp.pDepthStencilState = &tds; lgp.pColorBlendState = &lcb;
                vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &lgp, nullptr, &scenePipelineAlpha_);
            }
        }
        vkDestroyShaderModule(device_, vsm, nullptr); vkDestroyShaderModule(device_, fsm, nullptr);
        if (pr != VK_SUCCESS) { sceneFailed_ = true; return false; }
        scenePipelineCull_ = dl.backfaceCull;
    }

    // Upload (or reuse cached) per-geometry GPU resources. A camera-only frame keeps
    // the same geometryId, so the vertex buffer + textures + descriptors are reused and
    // only the push constants change — the real per-frame FPS win.
    const VkDeviceSize vbSize = (VkDeviceSize)dl.verts.size() * sizeof(render::SceneDrawVertex);
    const bool needUpload = (dl.geometryId == 0) || (dl.geometryId != sceneGeomId_) ||
                            sceneVbuf_ == VK_NULL_HANDLE;
    bool texOk = true;
    if (needUpload) {
        destroySceneFrameResources();
        // Vertex buffer (host-visible).
        VkBufferCreateInfo bci{}; bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = vbSize; bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (vkCreateBuffer(device_, &bci, nullptr, &sceneVbuf_) != VK_SUCCESS) return false;
        VkMemoryRequirements mr{}; vkGetBufferMemoryRequirements(device_, sceneVbuf_, &mr);
        std::int32_t mt = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (mt < 0) mt = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        VkMemoryAllocateInfo mai{}; mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size; mai.memoryTypeIndex = (std::uint32_t)mt;
        if (mt < 0 || vkAllocateMemory(device_, &mai, nullptr, &sceneVmem_) != VK_SUCCESS) { destroySceneFrameResources(); return false; }
        vkBindBufferMemory(device_, sceneVbuf_, sceneVmem_, 0);
        void* mp = nullptr; vkMapMemory(device_, sceneVmem_, 0, vbSize, 0, &mp);
        std::memcpy(mp, dl.verts.data(), (std::size_t)vbSize); vkUnmapMemory(device_, sceneVmem_);

        // Textures (one per draw-list texture) + a 1x1 white for untextured batches.
        sceneTexImg_.assign(dl.textures.size(), VK_NULL_HANDLE);
        sceneTexMem_.assign(dl.textures.size(), VK_NULL_HANDLE);
        sceneTexView_.assign(dl.textures.size(), VK_NULL_HANDLE);
        for (std::size_t i = 0; i < dl.textures.size(); ++i) {
            const auto& t = dl.textures[i];
            const auto* mips = (dl.bilinear && !t.mips.empty()) ? &t.mips : nullptr;
            if (!createSceneTexture(t.w, t.h, t.argb.data(), mips, sceneTexImg_[i], sceneTexMem_[i], sceneTexView_[i])) { texOk = false; break; }
        }
        const std::uint32_t whitePix = 0xFFFFFFFFu;
        if (texOk) texOk = createSceneTexture(1, 1, &whitePix, nullptr, sceneWhiteImg_, sceneWhiteMem_, sceneWhiteView_);

        // Descriptor pool + a set per texture (+ white).
        if (texOk) {
            const std::uint32_t n = (std::uint32_t)dl.textures.size() + 1;
            VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, n};
            VkDescriptorPoolCreateInfo pci{}; pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pci.maxSets = n; pci.poolSizeCount = 1; pci.pPoolSizes = &ps;
            if (vkCreateDescriptorPool(device_, &pci, nullptr, &sceneDescPool_) != VK_SUCCESS) texOk = false;
            if (texOk) {
                std::vector<VkDescriptorSetLayout> layouts(n, sceneDescLayout_);
                VkDescriptorSetAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                ai.descriptorPool = sceneDescPool_; ai.descriptorSetCount = n; ai.pSetLayouts = layouts.data();
                sceneSets_.resize(n);
                if (vkAllocateDescriptorSets(device_, &ai, sceneSets_.data()) != VK_SUCCESS) texOk = false;
            }
            if (texOk) {
                const std::uint32_t n2 = (std::uint32_t)sceneSets_.size();
                std::vector<VkDescriptorImageInfo> imgs(n2);
                std::vector<VkWriteDescriptorSet> writes(n2);
                for (std::uint32_t i = 0; i < n2; ++i) {
                    imgs[i].sampler = dl.bilinear ? sceneSamplerLinear_ : sceneSampler_;
                    imgs[i].imageView = (i < dl.textures.size()) ? sceneTexView_[i] : sceneWhiteView_;
                    imgs[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[i].dstSet = sceneSets_[i]; writes[i].dstBinding = 0; writes[i].descriptorCount = 1;
                    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[i].pImageInfo = &imgs[i];
                }
                vkUpdateDescriptorSets(device_, n2, writes.data(), 0, nullptr);
            }
        }
        if (!texOk) { destroySceneFrameResources(); return false; }
        sceneGeomId_ = dl.geometryId;
    }
    const std::uint32_t whiteSet = (std::uint32_t)dl.textures.size();

    bool ok = texOk;
    if (ok) {
        // Push constants (8 vec4).
        float pc[32] = {0};
        const auto& c = dl.cam;
        pc[0]=c.eye[0]; pc[1]=c.eye[1]; pc[2]=c.eye[2];
        pc[4]=c.right[0]; pc[5]=c.right[1]; pc[6]=c.right[2];
        pc[8]=c.up[0]; pc[9]=c.up[1]; pc[10]=c.up[2];
        pc[12]=c.fwd[0]; pc[13]=c.fwd[1]; pc[14]=c.fwd[2];
        pc[16]=c.nearZ; pc[17]=c.q; pc[18]=c.clipX; pc[19]=c.clipWidth;
        pc[20]=c.clipY; pc[21]=c.clipHeight; pc[22]=c.originX; pc[23]=c.originY;
        pc[24]=c.width; pc[25]=c.height; pc[26]=(float)dl.width; pc[27]=(float)dl.height;
        pc[28]=c.engineProjection?1.0f:0.0f; pc[29]=c.fproj; pc[30]=c.aspect; pc[31]=c.farZ;
        pc[15]=1.0f;   // fwd.w = per-batch transparency opacity (1 = opaque; overridden below)

        vkResetCommandBuffer(cmd_, 0);
        VkCommandBufferBeginInfo cbi{}; cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_, &cbi);
        VkClearValue clears[2];
        clears[0].color = {{ dl.clearR / 255.0f, dl.clearG / 255.0f, dl.clearB / 255.0f, 1.0f }};
        clears[1].depthStencil = {1.0f, 0};
        const int rW = dl.width, rH = dl.height;
        VkRenderPassBeginInfo rbi{}; rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rbi.renderPass = scenePass_; rbi.framebuffer = sceneFb_;
        rbi.renderArea = {{0, 0}, {(std::uint32_t)rW, (std::uint32_t)rH}};
        rbi.clearValueCount = 2; rbi.pClearValues = clears;
        vkCmdBeginRenderPass(cmd_, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0, 0, (float)rW, (float)rH, 0.0f, 1.0f};
        VkRect2D scr{{0, 0}, {(std::uint32_t)rW, (std::uint32_t)rH}};
        vkCmdSetViewport(cmd_, 0, 1, &vp); vkCmdSetScissor(cmd_, 0, 1, &scr);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd_, 0, 1, &sceneVbuf_, &off);
        vkCmdPushConstants(cmd_, scenePipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128, pc);
        VkPipeline boundPipe = VK_NULL_HANDLE;
        for (const auto& b : dl.batches) {
            if (b.vertexCount <= 0) continue;
            const bool shadow = (b.texId == render::kSceneShadowBatch);
            // Shadow batches multiply-blend (white texture * 0.5 colour) -> darken; if the
            // shadow pipeline failed to build, skip them rather than paint grey.
            if (shadow && scenePipelineShadow_ == VK_NULL_HANDLE) continue;
            // Select the blend pipeline (gilde.exe 0x5e0358): opaque / additive / alpha.
            VkPipeline pipe = scenePipeline_; float opacity = 1.0f;
            if (shadow) {
                pipe = scenePipelineShadow_;
            } else if (b.blend == render::kBlendAdditive && scenePipelineAdditive_ != VK_NULL_HANDLE) {
                pipe = scenePipelineAdditive_; opacity = b.opacity;
            } else if (b.blend == render::kBlendAlpha && scenePipelineAlpha_ != VK_NULL_HANDLE) {
                pipe = scenePipelineAlpha_; opacity = b.opacity;
            }
            if (pipe != boundPipe) { vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe); boundPipe = pipe; }
            // Per-batch opacity -> pc.fwd.w (float index 15, byte offset 60).
            vkCmdPushConstants(cmd_, scenePipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               60, 4, &opacity);
            const std::uint32_t setIdx = (b.texId >= 0 && (std::size_t)b.texId < dl.textures.size())
                                             ? (std::uint32_t)b.texId : whiteSet;   // shadows use white
            if (setIdx >= sceneSets_.size()) continue;
            vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipeLayout_, 0, 1, &sceneSets_[setIdx], 0, nullptr);
            vkCmdDraw(cmd_, (std::uint32_t)b.vertexCount, 1, (std::uint32_t)b.firstVertex, 0);
        }
        vkCmdEndRenderPass(cmd_);  // sceneColor_ -> TRANSFER_SRC_OPTIMAL

        // Copy the rendered scene image into its staging buffer for readback.
        VkBufferImageCopy region{}; region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(std::uint32_t)rW, (std::uint32_t)rH, 1};
        vkCmdCopyImageToBuffer(cmd_, sceneColor_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sceneStaging_, 1, &region);
        vkEndCommandBuffer(cmd_);
        vkResetFences(device_, 1, &fence_);
        VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount = 1; si.pCommandBuffers = &cmd_;
        ok = (vkQueueSubmit(queue_, 1, &si, fence_) == VK_SUCCESS);
        if (ok) vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);

        // Box-downsample the scene image (rW x rH) into the caller's CPU surface (W x H).
        if (ok) {
            void* mapped = nullptr;
            if (vkMapMemory(device_, sceneStagingMem_, 0, sceneStagingSize_, 0, &mapped) == VK_SUCCESS) {
                const std::uint32_t* src = reinterpret_cast<const std::uint32_t*>(mapped);
                const int W = target->widthPx ? target->widthPx : target->width, H = target->height;
                const int sx = rW / (W > 0 ? W : 1), sy = rH / (H > 0 ? H : 1);   // supersample factor
                const int fx = sx > 0 ? sx : 1, fy = sy > 0 ? sy : 1;
                for (int y = 0; y < H; ++y) {
                    auto* drow32 = (target->bpp == 32) ? reinterpret_cast<std::uint32_t*>(
                        static_cast<std::uint8_t*>(target->pixels) + (std::size_t)y * target->pitch) : nullptr;
                    auto* drow16 = (target->bpp == 16) ? reinterpret_cast<std::uint16_t*>(
                        static_cast<std::uint8_t*>(target->pixels) + (std::size_t)y * target->pitch) : nullptr;
                    for (int x = 0; x < W; ++x) {
                        int r = 0, g = 0, b = 0, n = 0;
                        for (int j = 0; j < fy; ++j) {
                            const int syy = y * fy + j; if (syy >= rH) break;
                            const std::uint32_t* srow = src + (std::size_t)syy * rW;
                            for (int i = 0; i < fx; ++i) {
                                const int sxx = x * fx + i; if (sxx >= rW) break;
                                const std::uint32_t p = srow[sxx];
                                r += (p >> 16) & 0xFF; g += (p >> 8) & 0xFF; b += p & 0xFF; ++n;
                            }
                        }
                        if (n < 1) n = 1; r /= n; g /= n; b /= n;
                        if (drow32) drow32[x] = 0xFF000000u | ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | b;
                        else if (drow16) drow16[x] = (std::uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                    }
                }
                vkUnmapMemory(device_, sceneStagingMem_);
            }
        }
    }

    // Cached resources persist for the next frame (freed on geometry change / shutdown).
    // A geometryId==0 (one-off) caller leaves nothing reusable behind; drop it now.
    if (dl.geometryId == 0) destroySceneFrameResources();
    return ok;
}

void VulkanGraphicsDevice::destroySceneFrameResources() {
    if (device_ == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_);
    if (sceneDescPool_ != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, sceneDescPool_, nullptr); sceneDescPool_ = VK_NULL_HANDLE; }
    sceneSets_.clear();
    if (sceneWhiteView_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, sceneWhiteView_, nullptr); sceneWhiteView_ = VK_NULL_HANDLE; }
    if (sceneWhiteImg_ != VK_NULL_HANDLE) { vkDestroyImage(device_, sceneWhiteImg_, nullptr); sceneWhiteImg_ = VK_NULL_HANDLE; }
    if (sceneWhiteMem_ != VK_NULL_HANDLE) { vkFreeMemory(device_, sceneWhiteMem_, nullptr); sceneWhiteMem_ = VK_NULL_HANDLE; }
    for (std::size_t i = 0; i < sceneTexView_.size(); ++i)
        if (sceneTexView_[i] != VK_NULL_HANDLE) vkDestroyImageView(device_, sceneTexView_[i], nullptr);
    for (std::size_t i = 0; i < sceneTexImg_.size(); ++i)
        if (sceneTexImg_[i] != VK_NULL_HANDLE) vkDestroyImage(device_, sceneTexImg_[i], nullptr);
    for (std::size_t i = 0; i < sceneTexMem_.size(); ++i)
        if (sceneTexMem_[i] != VK_NULL_HANDLE) vkFreeMemory(device_, sceneTexMem_[i], nullptr);
    sceneTexView_.clear(); sceneTexImg_.clear(); sceneTexMem_.clear();
    if (sceneVbuf_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, sceneVbuf_, nullptr); sceneVbuf_ = VK_NULL_HANDLE; }
    if (sceneVmem_ != VK_NULL_HANDLE) { vkFreeMemory(device_, sceneVmem_, nullptr); sceneVmem_ = VK_NULL_HANDLE; }
    sceneGeomId_ = 0;
}

#else  // !GUILD_HAVE_SCENE_SHADERS — no embedded SPIR-V, so no GPU 3D path.
void VulkanGraphicsDevice::destroyScenePipeline() {}
void VulkanGraphicsDevice::destroySceneFrameResources() {}
bool VulkanGraphicsDevice::ensureScenePipeline() { return false; }
bool VulkanGraphicsDevice::createSceneTexture(int, int, const std::uint32_t*, const std::vector<std::vector<std::uint32_t>>*, VkImage&, VkDeviceMemory&, VkImageView&) { return false; }
bool VulkanGraphicsDevice::renderScene3D(const render::Scene3DDrawList&, render::Surface*) { return false; }
#endif // GUILD_HAVE_SCENE_SHADERS

} // namespace guild::shim

#endif // GUILD_HAVE_VULKAN
