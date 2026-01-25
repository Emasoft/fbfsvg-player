/**
 * Vulkan Graphite Context for Skia GPU Rendering (Linux/Windows)
 *
 * This file provides Skia Graphite GPU-accelerated rendering using Vulkan.
 * Graphite is Skia's next-generation GPU backend that replaces Ganesh.
 *
 * Requires:
 * - Vulkan SDK with vulkan-1.lib (Windows) or libvulkan.so (Linux)
 * - GPU with Vulkan 1.1+ support
 */

#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

// Skia Graphite headers
#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Recording.h"
#include "include/gpu/graphite/Surface.h"
#include "include/gpu/graphite/BackendTexture.h"
#include "include/gpu/graphite/vk/VulkanGraphiteUtils.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include "include/core/SkSurface.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"

#include "graphite_context.h"

namespace svgplayer {

// Vulkan debug callback for validation layers (debug builds only)
#ifndef NDEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    const char* severity = "UNKNOWN";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        severity = "ERROR";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        severity = "WARNING";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        severity = "INFO";
    }

    fprintf(stderr, "[Vulkan %s] %s\n", severity, pCallbackData->pMessage);
    return VK_FALSE;
}
#endif

/**
 * Vulkan Graphite context implementation.
 * Uses Vulkan swapchain for presentation and Skia Graphite for rendering.
 */
class VulkanGraphiteContext : public GraphiteContext {
public:
    VulkanGraphiteContext() = default;
    ~VulkanGraphiteContext() override { destroy(); }

    bool initialize(SDL_Window* window) override {
        if (!window) {
            fprintf(stderr, "[Graphite Vulkan] Error: NULL window\n");
            return false;
        }

        window_ = window;

        // Check if Vulkan is supported by SDL
        if (SDL_Vulkan_LoadLibrary(nullptr) != 0) {
            fprintf(stderr, "[Graphite Vulkan] Error: Failed to load Vulkan library: %s\n", SDL_GetError());
            return false;
        }

        // Create Vulkan instance
        if (!createInstance()) {
            destroy();
            return false;
        }

        // Create surface
        if (!SDL_Vulkan_CreateSurface(window, instance_, &surface_)) {
            fprintf(stderr, "[Graphite Vulkan] Error: Failed to create Vulkan surface: %s\n", SDL_GetError());
            destroy();
            return false;
        }

        // Pick physical device
        if (!pickPhysicalDevice()) {
            destroy();
            return false;
        }

        // Create logical device
        if (!createLogicalDevice()) {
            destroy();
            return false;
        }

        // Create swapchain
        if (!createSwapchain()) {
            destroy();
            return false;
        }

        // Create Skia Graphite context
        if (!createGraphiteContext()) {
            destroy();
            return false;
        }

        initialized_ = true;
        printf("[Graphite Vulkan] Successfully initialized Vulkan Graphite backend\n");
        return true;
    }

    void destroy() override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (recorder_) {
            recorder_.reset();
        }

        if (context_) {
            context_->submit(skgpu::graphite::SyncToCpu::kYes);
            context_.reset();
        }

        destroySwapchain();

        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }

        if (surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }

#ifndef NDEBUG
        if (debugMessenger_ != VK_NULL_HANDLE) {
            auto destroyFunc = (PFN_vkDestroyDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
            if (destroyFunc) {
                destroyFunc(instance_, debugMessenger_, nullptr);
            }
            debugMessenger_ = VK_NULL_HANDLE;
        }
#endif

        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }

        SDL_Vulkan_UnloadLibrary();
        initialized_ = false;

        printf("[Graphite Vulkan] Destroyed Vulkan Graphite context\n");
    }

    bool isInitialized() const override {
        return initialized_;
    }

    void updateDrawableSize(int width, int height) override {
        if (!initialized_) return;
        if (width <= 0 || height <= 0) return;

        std::lock_guard<std::mutex> lock(mutex_);

        // Check if size actually changed
        if (width == swapchainExtent_.width && height == swapchainExtent_.height) {
            return;
        }

        // Wait for device to be idle before recreating swapchain
        vkDeviceWaitIdle(device_);

        // Recreate swapchain with new size
        destroySwapchain();
        swapchainExtent_.width = width;
        swapchainExtent_.height = height;
        createSwapchain();
    }

    sk_sp<SkSurface> createSurface(int width, int height) override {
        if (!initialized_) {
            fprintf(stderr, "[Graphite Vulkan] createSurface: context not initialized\n");
            return nullptr;
        }

        if (width <= 0 || height <= 0) {
            fprintf(stderr, "[Graphite Vulkan] createSurface: invalid dimensions %dx%d\n", width, height);
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Check if we need to recreate swapchain
        if (width != (int)swapchainExtent_.width || height != (int)swapchainExtent_.height) {
            vkDeviceWaitIdle(device_);
            destroySwapchain();
            swapchainExtent_.width = width;
            swapchainExtent_.height = height;
            if (!createSwapchain()) {
                return nullptr;
            }
        }

        // Acquire next swapchain image
        VkResult result = vkAcquireNextImageKHR(
            device_, swapchain_, UINT64_MAX,
            imageAvailableSemaphore_, VK_NULL_HANDLE,
            &currentImageIndex_);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            // Swapchain out of date, recreate
            vkDeviceWaitIdle(device_);
            destroySwapchain();
            if (!createSwapchain()) {
                return nullptr;
            }
            result = vkAcquireNextImageKHR(
                device_, swapchain_, UINT64_MAX,
                imageAvailableSemaphore_, VK_NULL_HANDLE,
                &currentImageIndex_);
        }

        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            fprintf(stderr, "[Graphite Vulkan] createSurface: Failed to acquire swapchain image\n");
            return nullptr;
        }

        // Create BackendTexture from swapchain image
        skgpu::graphite::VulkanTextureInfo textureInfo;
        textureInfo.fSampleCount = 1;
        textureInfo.fMipmapped = skgpu::Mipmapped::kNo;
        textureInfo.fFormat = VK_FORMAT_B8G8R8A8_UNORM;
        textureInfo.fImageTiling = VK_IMAGE_TILING_OPTIMAL;
        textureInfo.fImageUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        textureInfo.fSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        textureInfo.fAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        skgpu::graphite::BackendTexture backendTexture(
            {(uint32_t)width, (uint32_t)height},
            textureInfo,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_QUEUE_FAMILY_IGNORED,
            swapchainImages_[currentImageIndex_],
            VK_NULL_HANDLE  // memory
        );

        // Create SkSurface wrapping the swapchain image
        SkImageInfo imageInfo = SkImageInfo::Make(
            width, height,
            kBGRA_8888_SkColorType,
            kPremul_SkAlphaType,
            SkColorSpace::MakeSRGB()
        );

        sk_sp<SkSurface> surface = SkSurfaces::WrapBackendTexture(
            recorder_.get(),
            backendTexture,
            kTopLeft_GrSurfaceOrigin,
            imageInfo.colorInfo(),
            nullptr,  // surfaceProps
            nullptr   // textureReleaseProc
        );

        if (!surface) {
            fprintf(stderr, "[Graphite Vulkan] createSurface: Failed to create surface from swapchain image\n");
            return nullptr;
        }

        return surface;
    }

    bool submitFrame() override {
        if (!initialized_ || !recorder_) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Snap the recording
        std::unique_ptr<skgpu::graphite::Recording> recording = recorder_->snap();
        if (!recording) {
            fprintf(stderr, "[Graphite Vulkan] submitFrame: Failed to snap recording\n");
            return false;
        }

        // Insert the recording
        skgpu::graphite::InsertRecordingInfo insertInfo;
        insertInfo.fRecording = recording.get();

        if (!context_->insertRecording(insertInfo)) {
            fprintf(stderr, "[Graphite Vulkan] submitFrame: Failed to insert recording\n");
            return false;
        }

        // Submit GPU work
        context_->submit(skgpu::graphite::SyncToCpu::kNo);

        return true;
    }

    void present() override {
        if (!initialized_) return;

        std::lock_guard<std::mutex> lock(mutex_);

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphore_;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain_;
        presentInfo.pImageIndices = &currentImageIndex_;

        VkResult result = vkQueuePresentKHR(presentQueue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            // Swapchain needs recreation - will happen on next createSurface
        } else if (result != VK_SUCCESS) {
            fprintf(stderr, "[Graphite Vulkan] present: Failed to present swapchain image\n");
        }
    }

    void setVSyncEnabled(bool enabled) override {
        if (!initialized_) return;

        // VSync is controlled by swapchain present mode
        // Would need to recreate swapchain to change this
        vsyncEnabled_ = enabled;
        // Note: Actual swapchain recreation would happen here
    }

    const char* getBackendName() const override {
        return "Vulkan Graphite";
    }

private:
    bool createInstance() {
        // Get required extensions from SDL
        unsigned int extensionCount = 0;
        if (!SDL_Vulkan_GetInstanceExtensions(window_, &extensionCount, nullptr)) {
            fprintf(stderr, "[Graphite Vulkan] Error: Failed to get required extension count\n");
            return false;
        }

        std::vector<const char*> extensions(extensionCount);
        if (!SDL_Vulkan_GetInstanceExtensions(window_, &extensionCount, extensions.data())) {
            fprintf(stderr, "[Graphite Vulkan] Error: Failed to get required extensions\n");
            return false;
        }

        // Add debug extension in debug builds
#ifndef NDEBUG
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "FBF SVG Player";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Skia Graphite";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = (uint32_t)extensions.size();
        createInfo.ppEnabledExtensionNames = extensions.data();

        // Enable validation layers in debug builds
#ifndef NDEBUG
        const char* validationLayers[] = { "VK_LAYER_KHRONOS_validation" };
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = validationLayers;
#endif

        VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "[Graphite Vulkan] Error: Failed to create Vulkan instance (VkResult: %d)\n", result);
            return false;
        }

        // Setup debug messenger in debug builds
#ifndef NDEBUG
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = vulkanDebugCallback;

        auto createFunc = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
        if (createFunc) {
            createFunc(instance_, &debugCreateInfo, nullptr, &debugMessenger_);
        }
#endif

        return true;
    }

    bool pickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
        if (deviceCount == 0) {
            fprintf(stderr, "[Graphite Vulkan] Error: No GPUs with Vulkan support found\n");
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

        // Find a suitable GPU (prefer discrete)
        for (VkPhysicalDevice device : devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);

            // Check for graphics queue family with present support
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

            for (uint32_t i = 0; i < queueFamilyCount; i++) {
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);

                if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport) {
                    physicalDevice_ = device;
                    graphicsQueueFamily_ = i;
                    printf("[Graphite Vulkan] Using GPU: %s\n", props.deviceName);
                    return true;
                }
            }
        }

        fprintf(stderr, "[Graphite Vulkan] Error: No suitable GPU found\n");
        return false;
    }

    bool createLogicalDevice() {
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = graphicsQueueFamily_;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        VkPhysicalDeviceFeatures deviceFeatures = {};

        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = deviceExtensions;
        createInfo.pEnabledFeatures = &deviceFeatures;

        if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
            fprintf(stderr, "[Graphite Vulkan] Error: Failed to create logical device\n");
            return false;
        }

        vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
        presentQueue_ = graphicsQueue_;  // Same queue for now

        return true;
    }

    bool createSwapchain() {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities);

        // Choose extent
        if (capabilities.currentExtent.width != UINT32_MAX) {
            swapchainExtent_ = capabilities.currentExtent;
        } else {
            int width, height;
            SDL_Vulkan_GetDrawableSize(window_, &width, &height);
            swapchainExtent_.width = std::max(capabilities.minImageExtent.width,
                std::min(capabilities.maxImageExtent.width, (uint32_t)width));
            swapchainExtent_.height = std::max(capabilities.minImageExtent.height,
                std::min(capabilities.maxImageExtent.height, (uint32_t)height));
        }

        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
            imageCount = capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface_;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
        createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        createInfo.imageExtent = swapchainExtent_;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = vsyncEnabled_ ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_) != VK_SUCCESS) {
            fprintf(stderr, "[Graphite Vulkan] Error: Failed to create swapchain\n");
            return false;
        }

        // Get swapchain images
        vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
        swapchainImages_.resize(imageCount);
        vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());

        // Create semaphores
        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailableSemaphore_) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinishedSemaphore_) != VK_SUCCESS) {
            fprintf(stderr, "[Graphite Vulkan] Error: Failed to create semaphores\n");
            return false;
        }

        printf("[Graphite Vulkan] Created swapchain: %dx%d with %zu images\n",
               swapchainExtent_.width, swapchainExtent_.height, swapchainImages_.size());

        return true;
    }

    void destroySwapchain() {
        if (imageAvailableSemaphore_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, imageAvailableSemaphore_, nullptr);
            imageAvailableSemaphore_ = VK_NULL_HANDLE;
        }

        if (renderFinishedSemaphore_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, renderFinishedSemaphore_, nullptr);
            renderFinishedSemaphore_ = VK_NULL_HANDLE;
        }

        swapchainImages_.clear();

        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    bool createGraphiteContext() {
        // Get physical device features
        VkPhysicalDeviceFeatures2 features2 = {};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        vkGetPhysicalDeviceFeatures2(physicalDevice_, &features2);

        // Setup Vulkan extensions info for Skia
        skgpu::VulkanExtensions skiaExtensions;
        skiaExtensions.init(
            vkGetInstanceProcAddr,
            instance_,
            physicalDevice_,
            0, nullptr,  // instance extensions (already created)
            1, &(const char*){VK_KHR_SWAPCHAIN_EXTENSION_NAME}  // device extensions
        );

        // Create backend context
        skgpu::VulkanBackendContext backendContext;
        backendContext.fInstance = instance_;
        backendContext.fPhysicalDevice = physicalDevice_;
        backendContext.fDevice = device_;
        backendContext.fQueue = graphicsQueue_;
        backendContext.fGraphicsQueueIndex = graphicsQueueFamily_;
        backendContext.fMaxAPIVersion = VK_API_VERSION_1_1;
        backendContext.fVkExtensions = &skiaExtensions;
        backendContext.fDeviceFeatures2 = &features2;
        backendContext.fGetProc = [](const char* name, VkInstance instance, VkDevice device) {
            if (device) {
                return vkGetDeviceProcAddr(device, name);
            }
            return vkGetInstanceProcAddr(instance, name);
        };

        skgpu::graphite::ContextOptions options;

        context_ = skgpu::graphite::ContextFactory::MakeVulkan(backendContext, options);
        if (!context_) {
            fprintf(stderr, "[Graphite Vulkan] Error: Failed to create Skia Graphite context\n");
            return false;
        }

        recorder_ = context_->makeRecorder();
        if (!recorder_) {
            fprintf(stderr, "[Graphite Vulkan] Error: Failed to create Skia Graphite recorder\n");
            return false;
        }

        return true;
    }

    SDL_Window* window_ = nullptr;

    // Vulkan objects
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;

    // Swapchain
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages_;
    VkExtent2D swapchainExtent_ = {0, 0};
    VkSemaphore imageAvailableSemaphore_ = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore_ = VK_NULL_HANDLE;
    uint32_t currentImageIndex_ = 0;

    // Debug
#ifndef NDEBUG
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
#endif

    // Skia Graphite
    std::unique_ptr<skgpu::graphite::Context> context_;
    std::unique_ptr<skgpu::graphite::Recorder> recorder_;

    bool initialized_ = false;
    bool vsyncEnabled_ = true;
    std::mutex mutex_;
};

// Factory function for Linux/Windows
std::unique_ptr<GraphiteContext> createGraphiteContext(SDL_Window* window) {
    auto context = std::make_unique<VulkanGraphiteContext>();
    if (!context->initialize(window)) {
        return nullptr;
    }
    return context;
}

} // namespace svgplayer
