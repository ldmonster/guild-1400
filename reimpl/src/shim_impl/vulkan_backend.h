#pragma once
// OPTIONAL real Vulkan graphics backend for IGraphicsDevice.
//
// This entire file is a no-op unless GUILD_HAVE_VULKAN is defined at compile
// time. It replaces the original DirectDraw/Direct3D present path: the engine
// still renders into a CPU "Surface" (the software rasterizer, 8/16/32 bpp) and
// VulkanGraphicsDevice::present() uploads that framebuffer to a Vulkan image.
//
// HEADLESS by design. This environment has no GPU/display guaranteed, so the
// default path is fully OFFSCREEN: VkInstance + a VkDevice on a picked physical
// device + a transfer/graphics queue, an offscreen target VkImage (the
// "screen"), and present() = convert Surface -> XRGB8888 into a host-visible
// staging buffer, vkCmdCopyBufferToImage into the target, submit, fence-wait.
// No swapchain, no surface, no graphics pipeline, no shaders are required.
//
// ---------------------------------------------------------------------------
// How to enable Vulkan
// ---------------------------------------------------------------------------
//   1. Install the Vulkan loader + headers (e.g. `apt install libvulkan-dev`).
//      A software ICD such as lavapipe (lvp) lets it run with no real GPU.
//   2. Compile with the macro, e.g.:
//        g++ -std=c++17 -DGUILD_HAVE_VULKAN src/shim_impl/vulkan_backend.cpp
//            ...  -lvulkan
//
//   Without -DGUILD_HAVE_VULKAN the .cpp compiles to nothing and no Vulkan
//   symbols are referenced, so the default build links cleanly with zero deps.
//
// Pixel formats uploaded: 8bpp is expanded through the palette to 32-bit XRGB
// each present(); 16bpp is treated as RGB565; 32bpp is taken as XRGB8888. The
// target image format is VK_FORMAT_B8G8R8A8_UNORM so each texel matches the
// 0xAARRGGBB byte order (B,G,R,A in memory) used by the engine's XRGB8888.
// ---------------------------------------------------------------------------
#ifdef GUILD_HAVE_VULKAN

#include "shim/IGraphicsDevice.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace guild::shim {

// Convert one row-major framebuffer (8/16/32 bpp) to XRGB8888 (0xFFRRGGBB),
// exactly mirroring Sdl2GraphicsDevice::present(). Exposed free function so the
// unit tier can golden-test it with no Vulkan device. `palette` must point at
// 256 ARGB entries when bpp==8 (may be null otherwise). `out` must hold w*h u32.
void ConvertToXRGB8888(const std::uint8_t* src, int width, int height, int bpp,
                       const std::uint32_t* palette, std::uint32_t* out);

// IGraphicsDevice backed by a headless offscreen Vulkan target image.
class VulkanGraphicsDevice : public IGraphicsDevice {
public:
    VulkanGraphicsDevice() = default;
    ~VulkanGraphicsDevice() override { shutdown(); }

    bool init(int width, int height, int bpp, bool fullscreen) override;
    void shutdown() override;
    Surface* backbuffer() override;
    void present() override;
    void setPalette(const std::uint32_t* argb256) override;

    // --- swapchain (on-screen) present path ------------------------------
    // Opt in to presenting to a real VkSurfaceKHR (a window) instead of the
    // offscreen-only target. Call BEFORE init(). `surfaceFactory` receives the
    // VkInstance this device creates and returns a surface created from it
    // (e.g. SdlVulkanPlatform::createSurface, or vkCreateHeadlessSurfaceEXT for
    // headless testing). `instanceExtensions` are the extra instance extensions
    // the surface needs (e.g. SDL_Vulkan_GetInstanceExtensions, or
    // {VK_KHR_surface, VK_EXT_headless_surface}). When set, init() builds a
    // VkSwapchainKHR on that surface and present() does:
    //   convert framebuffer -> staging -> target image -> vkCmdBlitImage into the
    //   acquired swapchain image -> vkQueuePresentKHR.
    // If swapchain setup fails, init() falls back to the offscreen path so the
    // device still works headless. readback() always returns the target image,
    // so it works in both modes.
    void configureSwapchain(std::function<VkSurfaceKHR(VkInstance)> surfaceFactory,
                            std::vector<const char*> instanceExtensions);

    // --- test / introspection accessors ---------------------------------
    bool inited() const { return inited_; }
    int presentCount() const { return presentCount_; }
    bool swapchainActive() const { return swapchain_ != VK_NULL_HANDLE; }
    int swapchainImageCount() const { return static_cast<int>(swapImages_.size()); }
    int swapchainPresentCount() const { return swapchainPresentCount_; }

    // Real device name reported by the picked VkPhysicalDevice (e.g. "llvmpipe
    // (LLVM ...)"), and the negotiated API version as "major.minor.patch".
    const std::string& deviceName() const { return deviceName_; }
    const std::string& apiVersion() const { return apiVersion_; }

    // Read back the last presented target image as width*height XRGB8888
    // (0xFFRRGGBB) texels by copying the device image to a host buffer. Returns
    // empty if nothing has been presented yet or the device is down.
    std::vector<std::uint32_t> readback();

private:
    // helpers (defined in the .cpp)
    bool createInstance();
    bool pickPhysicalDevice();
    bool createDevice();
    bool createTargetImage();
    bool createStaging();
    bool createCommandPool();
    bool createSwapchain();      // builds swapchain + sync on surface_
    void destroySwapchain();
    // Tear down + rebuild the swapchain (and its binary semaphores) after a
    // lost/out-of-date/failed frame. Idles the device first. If the rebuild
    // fails the surface is dropped and the device reverts to offscreen. Always
    // leaves both binary semaphores in a fresh, unsignaled state so a later
    // vkAcquireNextImageKHR can never re-signal an already-signaled semaphore.
    void rebuildSwapchainAfterLoss();
    bool presentSwapchain();     // the on-screen present path (called by present())
    std::int32_t findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags want) const;
    void destroyAll();

    // config
    int width_ = 0;
    int height_ = 0;
    int bpp_ = 0;
    bool inited_ = false;
    int presentCount_ = 0;

    // CPU framebuffer the engine draws into + palette + XRGB scratch.
    std::vector<std::uint8_t> framebuffer_;
    std::vector<std::uint32_t> convert_;
    Surface surface_;
    std::uint32_t palette_[256] = {};

    // Vulkan objects.
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;

    // Offscreen "screen" image (device-local, the present target).
    VkImage target_ = VK_NULL_HANDLE;
    VkDeviceMemory targetMem_ = VK_NULL_HANDLE;
    VkImageLayout targetLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    // Host-visible staging buffer (XRGB8888 upload source / readback dest).
    VkBuffer staging_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem_ = VK_NULL_HANDLE;
    VkDeviceSize stagingSize_ = 0;

    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;

    // Swapchain (on-screen) present path. Inactive (all VK_NULL_HANDLE) unless
    // configureSwapchain() was called and init() built it successfully.
    std::function<VkSurfaceKHR(VkInstance)> surfaceFactory_;
    std::vector<const char*> instanceExts_;
    VkSurfaceKHR vkSurface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapImages_;
    VkFormat swapFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapExtent_ = {0, 0};
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    int swapchainPresentCount_ = 0;

    std::string deviceName_;
    std::string apiVersion_;
};

} // namespace guild::shim

#endif // GUILD_HAVE_VULKAN
