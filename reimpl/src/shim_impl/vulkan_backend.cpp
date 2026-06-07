// OPTIONAL Vulkan graphics backend — compiled to nothing unless
// GUILD_HAVE_VULKAN is set. See vulkan_backend.h for the design / enable docs.
#ifdef GUILD_HAVE_VULKAN

#include "shim_impl/vulkan_backend.h"

#include <cstring>

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
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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

} // namespace guild::shim

#endif // GUILD_HAVE_VULKAN
