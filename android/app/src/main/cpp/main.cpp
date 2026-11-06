#define VK_USE_PLATFORM_ANDROID_KHR

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include "android_pad_bridge.h"
#include "android_cdroot_bridge.h"

#include <android/log.h>
#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include <jni.h>

class PS2Runtime;
extern "C" PS2Runtime *gowCreatePs2Runtime() noexcept;
extern "C" void gowDestroyPs2Runtime(PS2Runtime *runtime) noexcept;
extern "C" bool gowInitializePs2RuntimeCoreHeadless(
    PS2Runtime *runtime, char *message, size_t capacity) noexcept;
extern "C" void gowAndroidDiscCloseImage() noexcept;

namespace {

constexpr char kTag[] = "GowAndroid";
constexpr uint32_t kVulkanApiVersion = VK_API_VERSION_1_0;

void logMessage(int priority, const char* message) {
    __android_log_write(priority, kTag, message);
}

void reportRuntimeStatus(android_app *app, const char *message, bool failed) {
    if (app == nullptr || app->activity == nullptr ||
        app->activity->vm == nullptr || app->activity->clazz == nullptr ||
        message == nullptr) {
        return;
    }

    JavaVM *vm = app->activity->vm;
    JNIEnv *env = nullptr;
    bool attachedHere = false;
    const jint envResult = vm->GetEnv(
        reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (envResult == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            logMessage(ANDROID_LOG_WARN,
                "Could not attach native thread to report runtime status");
            return;
        }
        attachedHere = true;
    } else if (envResult != JNI_OK || env == nullptr) {
        logMessage(ANDROID_LOG_WARN,
            "Could not obtain JNIEnv to report runtime status");
        return;
    }

    jclass activityClass = env->GetObjectClass(app->activity->clazz);
    jmethodID callback = activityClass == nullptr ? nullptr :
        env->GetMethodID(activityClass, "onNativeRuntimeStatus",
                         "(Ljava/lang/String;Z)V");
    jstring javaMessage = callback == nullptr ? nullptr :
        env->NewStringUTF(message);

    if (callback != nullptr && javaMessage != nullptr) {
        env->CallVoidMethod(
            app->activity->clazz, callback, javaMessage,
            static_cast<jboolean>(failed ? JNI_TRUE : JNI_FALSE));
    }

    const bool javaFailed = env->ExceptionCheck() == JNI_TRUE;
    if (javaFailed) {
        env->ExceptionClear();
        logMessage(ANDROID_LOG_WARN,
            "Java runtime-status callback failed; native startup will continue");
    }

    if (javaMessage != nullptr) {
        env->DeleteLocalRef(javaMessage);
    }
    if (activityClass != nullptr) {
        env->DeleteLocalRef(activityClass);
    }
    if (attachedHere) {
        vm->DetachCurrentThread();
    }
}

void logVkError(const char* operation, VkResult result) {
    __android_log_print(
        ANDROID_LOG_ERROR, kTag, "%s failed with VkResult=%d",
        operation, static_cast<int>(result));
}

bool isSuccess(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) {
        return true;
    }
    logVkError(operation, result);
    return false;
}

struct QueueFamilies {
    uint32_t graphics = UINT32_MAX;
    uint32_t present = UINT32_MAX;

    bool complete() const {
        return graphics != UINT32_MAX && present != UINT32_MAX;
    }
};

class VulkanSmokeRenderer {
public:
    bool initialize(ANativeWindow* window) {
        shutdown();

        if (window == nullptr) {
            logMessage(ANDROID_LOG_ERROR, "Cannot initialize Vulkan: Android window is null");
            return false;
        }

        window_ = window;

        if (!createInstance() ||
            !createSurface() ||
            !selectPhysicalDevice() ||
            !createDevice() ||
            !createSwapchainResources()) {
            shutdown();
            return false;
        }

        ready_ = true;
        logMessage(
            ANDROID_LOG_INFO,
            "Vulkan surface + swapchain initialized; clear/present test is active");
        return true;
    }

    void shutdown() {
        ready_ = false;

        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
            destroySwapchainResources();

            if (imageAvailable_ != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, imageAvailable_, nullptr);
                imageAvailable_ = VK_NULL_HANDLE;
            }
            if (frameFence_ != VK_NULL_HANDLE) {
                vkDestroyFence(device_, frameFence_, nullptr);
                frameFence_ = VK_NULL_HANDLE;
            }
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }

        if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }

        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }

        physicalDevice_ = VK_NULL_HANDLE;
        graphicsQueue_ = VK_NULL_HANDLE;
        presentQueue_ = VK_NULL_HANDLE;
        window_ = nullptr;
        queueFamilies_ = {};
    }

    bool isReady() const {
        return ready_;
    }

    void drawFrame() {
        if (!ready_ || device_ == VK_NULL_HANDLE) {
            return;
        }

        if (!isSuccess(
                vkWaitForFences(device_, 1, &frameFence_, VK_TRUE, UINT64_MAX),
                "vkWaitForFences")) {
            ready_ = false;
            return;
        }

        uint32_t imageIndex = 0;
        const VkResult acquire = vkAcquireNextImageKHR(
            device_,
            swapchain_,
            UINT64_MAX,
            imageAvailable_,
            VK_NULL_HANDLE,
            &imageIndex);

        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
            logVkError("vkAcquireNextImageKHR", acquire);
            ready_ = false;
            return;
        }

        if (imageIndex >= commandBuffers_.size() ||
            imageIndex >= renderFinished_.size()) {
            logMessage(ANDROID_LOG_ERROR, "Swapchain returned an invalid image index");
            ready_ = false;
            return;
        }

        if (!isSuccess(vkResetFences(device_, 1, &frameFence_), "vkResetFences") ||
            !isSuccess(
                vkResetCommandBuffer(commandBuffers_[imageIndex], 0),
                "vkResetCommandBuffer")) {
            ready_ = false;
            return;
        }

        VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (!isSuccess(
                vkBeginCommandBuffer(commandBuffers_[imageIndex], &beginInfo),
                "vkBeginCommandBuffer")) {
            ready_ = false;
            return;
        }

        // This clear-only pass proves that an Android surface, swapchain image,
        // graphics queue and presentation queue are connected. It is not the
        // PlayStation 2 Graphics Synthesizer renderer.
        VkClearValue clearValue{};
        clearValue.color = {{0.025f, 0.018f, 0.035f, 1.0f}};

        VkRenderPassBeginInfo renderPassInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderPassInfo.renderPass = renderPass_;
        renderPassInfo.framebuffer = framebuffers_[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchainExtent_;
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(
            commandBuffers_[imageIndex],
            &renderPassInfo,
            VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(commandBuffers_[imageIndex]);

        if (!isSuccess(
                vkEndCommandBuffer(commandBuffers_[imageIndex]),
                "vkEndCommandBuffer")) {
            ready_ = false;
            return;
        }

        const VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable_;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished_[imageIndex];

        if (!isSuccess(
                vkQueueSubmit(graphicsQueue_, 1, &submitInfo, frameFence_),
                "vkQueueSubmit")) {
            ready_ = false;
            return;
        }

        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished_[imageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain_;
        presentInfo.pImageIndices = &imageIndex;

        const VkResult present = vkQueuePresentKHR(presentQueue_, &presentInfo);
        if (present == VK_ERROR_OUT_OF_DATE_KHR ||
            present == VK_SUBOPTIMAL_KHR ||
            acquire == VK_SUBOPTIMAL_KHR) {
            recreateSwapchain();
        } else if (present != VK_SUCCESS) {
            logVkError("vkQueuePresentKHR", present);
            ready_ = false;
        }
    }

    void recreateSwapchain() {
        if (device_ == VK_NULL_HANDLE || surface_ == VK_NULL_HANDLE) {
            return;
        }

        if (!isSuccess(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(resize)")) {
            ready_ = false;
            return;
        }

        destroySwapchainResources();
        ready_ = createSwapchainResources();
        if (ready_) {
            logMessage(ANDROID_LOG_INFO, "Vulkan swapchain recreated");
        }
    }

private:
    bool createInstance() {
        const std::array<const char*, 2> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
        };

        VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        applicationInfo.pApplicationName = "Gow_android";
        applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        applicationInfo.pEngineName = "Gow_android";
        applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        applicationInfo.apiVersion = kVulkanApiVersion;

        VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInfo.pApplicationInfo = &applicationInfo;
        createInfo.enabledExtensionCount =
            static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        return isSuccess(
            vkCreateInstance(&createInfo, nullptr, &instance_),
            "vkCreateInstance");
    }

    bool createSurface() {
        VkAndroidSurfaceCreateInfoKHR surfaceInfo{
            VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
        surfaceInfo.window = window_;

        return isSuccess(
            vkCreateAndroidSurfaceKHR(
                instance_, &surfaceInfo, nullptr, &surface_),
            "vkCreateAndroidSurfaceKHR");
    }

    bool selectPhysicalDevice() {
        uint32_t deviceCount = 0;
        if (!isSuccess(
                vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr),
                "vkEnumeratePhysicalDevices(count)") ||
            deviceCount == 0) {
            logMessage(ANDROID_LOG_ERROR, "No Vulkan physical device is available");
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        if (!isSuccess(
                vkEnumeratePhysicalDevices(
                    instance_, &deviceCount, devices.data()),
                "vkEnumeratePhysicalDevices(list)")) {
            return false;
        }

        for (VkPhysicalDevice candidate : devices) {
            uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(
                candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(
                candidate, &familyCount, families.data());

            QueueFamilies candidateFamilies;
            for (uint32_t i = 0; i < familyCount; ++i) {
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                    candidateFamilies.graphics = i;
                }

                VkBool32 supportsPresent = VK_FALSE;
                const VkResult supportResult =
                    vkGetPhysicalDeviceSurfaceSupportKHR(
                        candidate, i, surface_, &supportsPresent);
                if (supportResult == VK_SUCCESS && supportsPresent == VK_TRUE) {
                    candidateFamilies.present = i;
                }

                if (candidateFamilies.complete()) {
                    break;
                }
            }

            if (candidateFamilies.complete()) {
                physicalDevice_ = candidate;
                queueFamilies_ = candidateFamilies;

                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(candidate, &properties);
                __android_log_print(
                    ANDROID_LOG_INFO, kTag,
                    "Selected Vulkan device: %s (vendor=0x%04x device=0x%04x api=%u.%u.%u)",
                    properties.deviceName,
                    properties.vendorID,
                    properties.deviceID,
                    VK_VERSION_MAJOR(properties.apiVersion),
                    VK_VERSION_MINOR(properties.apiVersion),
                    VK_VERSION_PATCH(properties.apiVersion));
                return true;
            }
        }

        logMessage(
            ANDROID_LOG_ERROR,
            "No Vulkan device has both graphics and Android presentation queues");
        return false;
    }

    bool createDevice() {
        const float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;

        VkDeviceQueueCreateInfo graphicsInfo{
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        graphicsInfo.queueFamilyIndex = queueFamilies_.graphics;
        graphicsInfo.queueCount = 1;
        graphicsInfo.pQueuePriorities = &priority;
        queueInfos.push_back(graphicsInfo);

        if (queueFamilies_.present != queueFamilies_.graphics) {
            VkDeviceQueueCreateInfo presentInfo{
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            presentInfo.queueFamilyIndex = queueFamilies_.present;
            presentInfo.queueCount = 1;
            presentInfo.pQueuePriorities = &priority;
            queueInfos.push_back(presentInfo);
        }

        const char* deviceExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount =
            static_cast<uint32_t>(queueInfos.size());
        deviceInfo.pQueueCreateInfos = queueInfos.data();
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = &deviceExtension;

        if (!isSuccess(
                vkCreateDevice(
                    physicalDevice_, &deviceInfo, nullptr, &device_),
                "vkCreateDevice")) {
            return false;
        }

        vkGetDeviceQueue(
            device_, queueFamilies_.graphics, 0, &graphicsQueue_);
        vkGetDeviceQueue(
            device_, queueFamilies_.present, 0, &presentQueue_);

        VkSemaphoreCreateInfo semaphoreInfo{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (!isSuccess(
                vkCreateSemaphore(
                    device_, &semaphoreInfo, nullptr, &imageAvailable_),
                "vkCreateSemaphore(imageAvailable)")) {
            return false;
        }

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        return isSuccess(
            vkCreateFence(device_, &fenceInfo, nullptr, &frameFence_),
            "vkCreateFence");
    }

    bool createSwapchainResources() {
        VkSurfaceCapabilitiesKHR capabilities{};
        if (!isSuccess(
                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                    physicalDevice_, surface_, &capabilities),
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
            return false;
        }

        uint32_t formatCount = 0;
        if (!isSuccess(
                vkGetPhysicalDeviceSurfaceFormatsKHR(
                    physicalDevice_, surface_, &formatCount, nullptr),
                "vkGetPhysicalDeviceSurfaceFormatsKHR(count)") ||
            formatCount == 0) {
            logMessage(ANDROID_LOG_ERROR, "Android surface exposes no Vulkan formats");
            return false;
        }

        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        if (!isSuccess(
                vkGetPhysicalDeviceSurfaceFormatsKHR(
                    physicalDevice_, surface_, &formatCount, formats.data()),
                "vkGetPhysicalDeviceSurfaceFormatsKHR(list)")) {
            return false;
        }

        VkSurfaceFormatKHR selectedFormat = formats.front();
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_R8G8B8A8_UNORM ||
                format.format == VK_FORMAT_B8G8R8A8_UNORM) {
                selectedFormat = format;
                break;
            }
        }
        if (formats.size() == 1 &&
            formats.front().format == VK_FORMAT_UNDEFINED) {
            selectedFormat.format = VK_FORMAT_B8G8R8A8_UNORM;
            selectedFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        }
        swapchainFormat_ = selectedFormat.format;

        VkExtent2D extent = capabilities.currentExtent;
        if (extent.width == std::numeric_limits<uint32_t>::max()) {
            const uint32_t width = static_cast<uint32_t>(
                std::max(1, ANativeWindow_getWidth(window_)));
            const uint32_t height = static_cast<uint32_t>(
                std::max(1, ANativeWindow_getHeight(window_)));
            extent.width = std::clamp(
                width,
                capabilities.minImageExtent.width,
                capabilities.maxImageExtent.width);
            extent.height = std::clamp(
                height,
                capabilities.minImageExtent.height,
                capabilities.maxImageExtent.height);
        }
        swapchainExtent_ = extent;

        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0) {
            imageCount = std::min(imageCount, capabilities.maxImageCount);
        }

        VkCompositeAlphaFlagBitsKHR compositeAlpha =
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if ((capabilities.supportedCompositeAlpha & compositeAlpha) == 0) {
            const std::array<VkCompositeAlphaFlagBitsKHR, 4> options = {
                VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
            };
            bool found = false;
            for (const auto option : options) {
                if ((capabilities.supportedCompositeAlpha & option) != 0) {
                    compositeAlpha = option;
                    found = true;
                    break;
                }
            }
            if (!found) {
                logMessage(ANDROID_LOG_ERROR, "No supported Vulkan composite alpha mode");
                return false;
            }
        }

        VkSwapchainCreateInfoKHR swapchainInfo{
            VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        swapchainInfo.surface = surface_;
        swapchainInfo.minImageCount = imageCount;
        swapchainInfo.imageFormat = selectedFormat.format;
        swapchainInfo.imageColorSpace = selectedFormat.colorSpace;
        swapchainInfo.imageExtent = extent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const std::array<uint32_t, 2> queueIndices = {
            queueFamilies_.graphics, queueFamilies_.present
        };
        if (queueFamilies_.graphics != queueFamilies_.present) {
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            swapchainInfo.queueFamilyIndexCount = 2;
            swapchainInfo.pQueueFamilyIndices = queueIndices.data();
        } else {
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        swapchainInfo.preTransform = capabilities.currentTransform;
        swapchainInfo.compositeAlpha = compositeAlpha;
        swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchainInfo.clipped = VK_TRUE;

        if (!isSuccess(
                vkCreateSwapchainKHR(
                    device_, &swapchainInfo, nullptr, &swapchain_),
                "vkCreateSwapchainKHR")) {
            return false;
        }

        uint32_t actualImageCount = 0;
        if (!isSuccess(
                vkGetSwapchainImagesKHR(
                    device_, swapchain_, &actualImageCount, nullptr),
                "vkGetSwapchainImagesKHR(count)") ||
            actualImageCount == 0) {
            return false;
        }

        swapchainImages_.resize(actualImageCount);
        if (!isSuccess(
                vkGetSwapchainImagesKHR(
                    device_, swapchain_, &actualImageCount,
                    swapchainImages_.data()),
                "vkGetSwapchainImagesKHR(list)")) {
            return false;
        }

        imageViews_.reserve(actualImageCount);
        for (const VkImage image : swapchainImages_) {
            VkImageViewCreateInfo viewInfo{
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = swapchainFormat_;
            viewInfo.components = {
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY
            };
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            VkImageView view = VK_NULL_HANDLE;
            if (!isSuccess(
                    vkCreateImageView(
                        device_, &viewInfo, nullptr, &view),
                    "vkCreateImageView")) {
                return false;
            }
            imageViews_.push_back(view);
        }

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchainFormat_;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorReference{};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (!isSuccess(
                vkCreateRenderPass(
                    device_, &renderPassInfo, nullptr, &renderPass_),
                "vkCreateRenderPass")) {
            return false;
        }

        framebuffers_.reserve(actualImageCount);
        for (const VkImageView view : imageViews_) {
            VkFramebufferCreateInfo framebufferInfo{
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebufferInfo.renderPass = renderPass_;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &view;
            framebufferInfo.width = swapchainExtent_.width;
            framebufferInfo.height = swapchainExtent_.height;
            framebufferInfo.layers = 1;

            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            if (!isSuccess(
                    vkCreateFramebuffer(
                        device_, &framebufferInfo, nullptr, &framebuffer),
                    "vkCreateFramebuffer")) {
                return false;
            }
            framebuffers_.push_back(framebuffer);
        }

        VkCommandPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilies_.graphics;
        if (!isSuccess(
                vkCreateCommandPool(
                    device_, &poolInfo, nullptr, &commandPool_),
                "vkCreateCommandPool")) {
            return false;
        }

        commandBuffers_.resize(actualImageCount);
        VkCommandBufferAllocateInfo commandInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandInfo.commandPool = commandPool_;
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = actualImageCount;

        if (!isSuccess(
                vkAllocateCommandBuffers(
                    device_, &commandInfo, commandBuffers_.data()),
                "vkAllocateCommandBuffers")) {
            return false;
        }

        renderFinished_.resize(actualImageCount, VK_NULL_HANDLE);
        VkSemaphoreCreateInfo semaphoreInfo{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (auto& semaphore : renderFinished_) {
            if (!isSuccess(
                    vkCreateSemaphore(
                        device_, &semaphoreInfo, nullptr, &semaphore),
                    "vkCreateSemaphore(renderFinished)")) {
                return false;
            }
        }

        __android_log_print(
            ANDROID_LOG_INFO, kTag,
            "Swapchain ready: %ux%u, format=%d, images=%u",
            swapchainExtent_.width,
            swapchainExtent_.height,
            static_cast<int>(swapchainFormat_),
            actualImageCount);
        return true;
    }

    void destroySwapchainResources() {
        if (device_ == VK_NULL_HANDLE) {
            return;
        }

        for (const VkSemaphore semaphore : renderFinished_) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, semaphore, nullptr);
            }
        }
        renderFinished_.clear();

        if (!commandBuffers_.empty() && commandPool_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(
                device_,
                commandPool_,
                static_cast<uint32_t>(commandBuffers_.size()),
                commandBuffers_.data());
        }
        commandBuffers_.clear();

        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }

        for (const VkFramebuffer framebuffer : framebuffers_) {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
        framebuffers_.clear();

        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }

        for (const VkImageView view : imageViews_) {
            vkDestroyImageView(device_, view, nullptr);
        }
        imageViews_.clear();
        swapchainImages_.clear();

        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    ANativeWindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    QueueFamilies queueFamilies_{};

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> imageViews_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    std::vector<VkSemaphore> renderFinished_;
    VkFence frameFence_ = VK_NULL_HANDLE;
    bool ready_ = false;
};

VulkanSmokeRenderer gRenderer;

void processInputEvents(android_app *app) {
    android_input_buffer *input = android_app_swap_input_buffers(app);
    if (input == nullptr) {
        return;
    }

    for (uint64_t i = 0; i < input->keyEventsCount; ++i) {
        gowAndroidPadHandleKeyEvent(&input->keyEvents[i]);
    }
    for (uint64_t i = 0; i < input->motionEventsCount; ++i) {
        gowAndroidPadHandleMotionEvent(&input->motionEvents[i]);
    }

    android_app_clear_key_events(input);
    android_app_clear_motion_events(input);
}

void handleAppCommand(android_app* app, int32_t command) {
    switch (command) {
        case APP_CMD_INIT_WINDOW:
            if (app->window != nullptr) {
#if defined(GOW_ANDROID_ENABLE_VULKAN_SMOKE)
                gRenderer.initialize(app->window);
#else
                logMessage(ANDROID_LOG_INFO,
                    "GameActivity surface available; PS2 GS/Vulkan renderer is not integrated yet");
#endif
            }
            break;

        case APP_CMD_TERM_WINDOW:
#if defined(GOW_ANDROID_ENABLE_VULKAN_SMOKE)
            gRenderer.shutdown();
#endif
            break;

        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONTENT_RECT_CHANGED:
#if defined(GOW_ANDROID_ENABLE_VULKAN_SMOKE)
            gRenderer.recreateSwapchain();
#endif
            break;

        case APP_CMD_LOST_FOCUS:
        case APP_CMD_PAUSE:
            gowAndroidPadResetAll();
            break;

        case APP_CMD_GAINED_FOCUS:
            break;

        case APP_CMD_LOW_MEMORY:
            logMessage(ANDROID_LOG_WARN, "Android reported low memory");
            break;

        default:
            break;
    }
}

} // namespace

extern "C" void android_main(android_app* app) {
    if (app == nullptr) {
        return;
    }

    app->userData = &gRenderer;
    app->onAppCmd = handleAppCommand;

    // GameActivity delivers controls through android_input_buffer, not an
    // android_app::onInputEvent callback. Disable the touch-only default filter
    // so external gamepad and joystick motion events are also buffered.
    gowAndroidPadEnableGamepadAxes();
    android_app_set_motion_event_filter(app, nullptr);

    logMessage(ANDROID_LOG_INFO, "Gow_android native shell started");

    // Initialize only the platform-independent EE/IOP/VU core. The Android GS
    // backend is intentionally null and no Raylib window/audio device is touched.
    // ELF loading and guest execution remain gated on the Android GS/audio adapters
    // and private recompilation output.
    reportRuntimeStatus(
        app, "Preparando o núcleo PS2 (memória, EE, IOP e VU)...", false);
    PS2Runtime *runtime = gowCreatePs2Runtime();
    if (runtime != nullptr) {
        std::array<char, 512> coreMessage{};
        if (gowInitializePs2RuntimeCoreHeadless(
                runtime, coreMessage.data(), coreMessage.size())) {
            __android_log_print(
                ANDROID_LOG_INFO, kTag,
                "PS2Runtime core ready: %s", coreMessage.data());
            reportRuntimeStatus(
                app,
                "Núcleo PS2 inicializado. O jogo ainda não iniciou: o código AOT do ELF, o GS/Vulkan Android e o áudio nativo continuam pendentes.",
                false);
        } else {
            __android_log_print(
                ANDROID_LOG_ERROR, kTag,
                "PS2Runtime core initialization failed: %s", coreMessage.data());
            std::string status =
                "Falha ao inicializar o núcleo PS2: ";
            status += coreMessage[0] == '\0'
                ? "motivo não informado."
                : coreMessage.data();
            status += " Consulte o Logcat com a etiqueta GowAndroid.";
            reportRuntimeStatus(app, status.c_str(), true);
        }
    } else {
        logMessage(ANDROID_LOG_ERROR, "PS2Runtime construction failed");
        reportRuntimeStatus(
            app,
            "Não foi possível criar PS2Runtime. O jogo não foi iniciado; consulte o Logcat com a etiqueta GowAndroid.",
            true);
    }

    while (app->destroyRequested == 0) {
        int events = 0;
        android_poll_source* source = nullptr;
#if defined(GOW_ANDROID_ENABLE_VULKAN_SMOKE)
        const int timeout = gRenderer.isReady() ? 0 : -1;
#else
        const int timeout = -1;
#endif

        int pollTimeout = timeout;
        int pollResult = 0;
        while ((pollResult = ALooper_pollOnce(
                    pollTimeout,
                    nullptr,
                    &events,
                    reinterpret_cast<void**>(&source))) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
            if (app->destroyRequested != 0) {
                break;
            }
            // Block only for the first event when no surface exists. Once an
            // event is processed, drain the queue without blocking so input
            // buffers and lifecycle changes are serviced every outer frame.
            pollTimeout = 0;
        }

        if (app->destroyRequested == 0) {
            processInputEvents(app);
        }

#if defined(GOW_ANDROID_ENABLE_VULKAN_SMOKE)
        if (gRenderer.isReady()) {
            gRenderer.drawFrame();
        }
#endif
    }

#if defined(GOW_ANDROID_ENABLE_VULKAN_SMOKE)
    gRenderer.shutdown();
#endif
    if (runtime != nullptr) {
        gowDestroyPs2Runtime(runtime);
        runtime = nullptr;
        logMessage(ANDROID_LOG_INFO, "PS2Runtime destroyed cleanly");
    }
    gowAndroidPadResetAll();
    gowAndroidDiscCloseImage();
    gowAndroidCdRootClose();
    logMessage(ANDROID_LOG_INFO, "Gow_android native shell stopped");
}
