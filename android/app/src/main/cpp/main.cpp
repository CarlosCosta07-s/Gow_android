#define VK_USE_PLATFORM_ANDROID_KHR

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include "android_pad_bridge.h"
#include "android_cdroot_bridge.h"
#include "android_frame_scaler.h"
#include "android_gs_vulkan_backend.h"
#include "android_gs_scene_capture.h"
#include "android_gs_geometry.h"
#include "gs_vulkan_primitive_shaders.h"
#include "ps2_runtime.h"
#include "android_runtime_log.h"

#include <android/log.h>
#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <condition_variable>
#include <string_view>
#include <vector>
#include <mutex>
#include <utility>
#include <jni.h>

class PS2Runtime;
extern "C" PS2Runtime *gowCreatePs2Runtime() noexcept;
extern "C" void gowDestroyPs2Runtime(PS2Runtime *runtime) noexcept;
extern "C" bool gowInitializePs2RuntimeCoreHeadless(
    PS2Runtime *runtime, char *message, size_t capacity) noexcept;
extern "C" bool gowRuntimeLoadElfAndCheckAot(
    PS2Runtime *runtime, const char *bootRelativePath,
    char *message, size_t capacity) noexcept;
extern "C" bool gowRuntimeStartGuestExecutionAndroid(
    PS2Runtime *runtime, char *message, size_t capacity) noexcept;
extern "C" bool gowRuntimeGuestExecutionFinishedAndroid(
    PS2Runtime *runtime) noexcept;
extern "C" void gowAndroidDiscCloseImage() noexcept;

namespace {

constexpr char kTag[] = "GowAndroid";
constexpr uint32_t kVulkanApiVersion = VK_API_VERSION_1_0;

void logMessage(int priority, const char* message) {
    __android_log_write(priority, kTag, message);
    const char *level = priority >= ANDROID_LOG_ERROR ? "ERROR" :
        (priority >= ANDROID_LOG_WARN ? "WARN" :
         (priority >= ANDROID_LOG_INFO ? "INFO" : "DEBUG"));
    gow::android::appendPrivateRuntimeLog(
        level, message == nullptr ? std::string_view{} : std::string_view(message));
}

bool configurePrivateAotCaptureDirectories(const std::string &privateCdRoot) {
    try {
        const std::filesystem::path discRoot(privateCdRoot);
        const std::filesystem::path installationsDir =
            discRoot.parent_path().parent_path();
        const std::filesystem::path appFilesDir = installationsDir.parent_path();
        if (!discRoot.is_absolute() || discRoot.filename() != "disc" ||
            installationsDir.filename() != "installations" ||
            appFilesDir.empty()) {
            logMessage(
                ANDROID_LOG_WARN,
                "Private AOT capture disabled: cdRoot is outside the expected app-private installation layout");
            return false;
        }

        if (gow::android::configurePrivateRuntimeLog(appFilesDir)) {
            gow::android::appendPrivateRuntimeLog(
                "INFO", "Private runtime diagnostic log configured (2 MiB maximum).");
        } else {
            logMessage(
                ANDROID_LOG_WARN,
                "Private runtime log could not be configured; Logcat remains available");
        }

        const std::filesystem::path captureRoot =
            appFilesDir / "gow-aot-captures";
        const std::filesystem::path vu1Dir = captureRoot / "vu1";
        const std::filesystem::path iopDir = captureRoot / "iop";
        std::filesystem::create_directories(vu1Dir);
        std::filesystem::create_directories(iopDir);

        const std::string vu1Path = vu1Dir.string();
        const std::string iopPath = iopDir.string();
        const std::string vu1MissPath = (vu1Dir / "vu1n_heads.txt").string();
        if (::setenv("GOW_VU1_CAPTURA", vu1Path.c_str(), 1) != 0 ||
            ::setenv("GOW_VU1N_MISS_LOG", vu1MissPath.c_str(), 1) != 0 ||
            ::setenv("PS2X_IOP_DUMP_MODULES", iopPath.c_str(), 1) != 0) {
            logMessage(
                ANDROID_LOG_WARN,
                "Private AOT capture disabled: could not configure runtime capture paths");
            return false;
        }

        logMessage(
            ANDROID_LOG_INFO,
            "Private AOT capture enabled under app files: gow-aot-captures/{vu1,iop}");
        return true;
    } catch (const std::exception &exception) {
        __android_log_print(
            ANDROID_LOG_WARN, kTag,
            "Private AOT capture setup failed: %s", exception.what());
        return false;
    } catch (...) {
        logMessage(
            ANDROID_LOG_WARN,
            "Private AOT capture setup failed with an unknown error");
        return false;
    }
}

std::mutex gActivityReferenceMutex;
JavaVM *gActivityVm = nullptr;
jobject gActivityReference = nullptr;
std::mutex gBootPathMutex;
std::condition_variable gBootPathCondition;
std::string gBootRelativePath;
bool gBootPathConfigurationReceived = false;
PS2Runtime *gActiveRuntime = nullptr;
bool gGsBackendAttached = false;
bool gPrivateAotElfReady = false;
bool gGuestExecutionStartAttempted = false;
bool gGuestExecutionStarted = false;
bool gGuestExecutionFinishReported = false;
std::atomic<bool> gGsUnsupportedStateWarned{false};
std::atomic<bool> gGsRenderTargetWarned{false};
bool gGsSceneCaptureAttempted = false;
std::atomic<bool> gGsSceneCaptureRequested{false};
std::atomic<uint32_t> gGsSceneCaptureSequence{0u};
constexpr uint32_t kMaxRequestedGsSceneCaptures = 32u;

void reportRuntimeStatus(android_app *app, const char *message, bool failed) {
    if (message != nullptr) {
        gow::android::appendPrivateRuntimeLog(
            failed ? "ERROR" : "STATUS", message);
    }
    if (app == nullptr || message == nullptr) {
        return;
    }

    JavaVM *vm = nullptr;
    {
        std::lock_guard<std::mutex> lock(gActivityReferenceMutex);
        vm = gActivityVm;
    }
    if (vm == nullptr) {
        return;
    }

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

    jobject activity = nullptr;
    {
        std::lock_guard<std::mutex> lock(gActivityReferenceMutex);
        if (gActivityReference != nullptr) {
            activity = env->NewLocalRef(gActivityReference);
        }
    }

    if (activity == nullptr) {
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
        }
        if (attachedHere) {
            vm->DetachCurrentThread();
        }
        return;
    }

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID callback = activityClass == nullptr ? nullptr :
        env->GetMethodID(activityClass, "onNativeRuntimeStatus",
                         "(Ljava/lang/String;Z)V");
    jstring javaMessage = callback == nullptr ? nullptr :
        env->NewStringUTF(message);

    if (callback != nullptr && javaMessage != nullptr) {
        env->CallVoidMethod(
            activity, callback, javaMessage,
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
    env->DeleteLocalRef(activity);
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
        {
            std::lock_guard<std::mutex> lock(gsQueueMutex_);
            queuedGsVertices_.clear();
            queuedGsDraws_.clear();
        }

        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
            destroyUploadBuffer();
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

    void enqueueGsBatch(const gow::android::GsGpuBatch &batch) {
        if (!ready_ || batch.vertices.empty() ||
            batch.targetWidth == 0u || batch.targetHeight == 0u) return;

        if ((batch.textured || batch.blend || batch.depthTest ||
             batch.depthWrite || batch.fogEnabled) &&
            !gGsUnsupportedStateWarned.exchange(true)) {
            logMessage(ANDROID_LOG_WARN,
                "GS primitive pipeline is solid-color only: texture sampling, GS blending, depth and fog are not implemented yet");
        }
        if ((batch.frameBase != 0u || batch.framePsm != GS_PSM_CT32) &&
            !gGsRenderTargetWarned.exchange(true)) {
            logMessage(ANDROID_LOG_WARN,
                "GS render-target VRAM/PSM composition is not implemented; experimental primitives are sent directly to the Android surface");
        }

        std::lock_guard<std::mutex> lock(gsQueueMutex_);
        constexpr size_t kMaxQueuedGsVertices = 262144u;
        if (queuedGsVertices_.size() > kMaxQueuedGsVertices ||
            batch.vertices.size() >
                kMaxQueuedGsVertices - queuedGsVertices_.size()) {
            logMessage(ANDROID_LOG_WARN,
                "Android GS Vulkan queue reached its per-frame vertex limit; dropping a batch");
            return;
        }

        QueuedGsDraw draw{};
        draw.firstVertex = static_cast<uint32_t>(queuedGsVertices_.size());
        draw.vertexCount = static_cast<uint32_t>(batch.vertices.size());
        draw.topology = batch.topology;
        draw.targetWidth = batch.targetWidth;
        draw.targetHeight = batch.targetHeight;
        draw.scissorX0 = batch.scissorX0;
        draw.scissorY0 = batch.scissorY0;
        draw.scissorX1 = batch.scissorX1;
        draw.scissorY1 = batch.scissorY1;

        const float width = static_cast<float>(batch.targetWidth);
        const float height = static_cast<float>(batch.targetHeight);
        for (const auto &source : batch.vertices) {
            gow::android::GsGpuVertex vertex = source;
            vertex.x = (vertex.x / width) * 2.0f - 1.0f;
            vertex.y = (vertex.y / height) * 2.0f - 1.0f;
            queuedGsVertices_.push_back(vertex);
        }
        queuedGsDraws_.push_back(draw);
    }

    void submitFrame(
        std::vector<uint8_t> rgbaPixels,
        uint32_t width,
        uint32_t height) {
        if (width == 0u || height == 0u) {
            return;
        }
        const uint64_t required =
            static_cast<uint64_t>(width) * height * 4ull;
        if (required > rgbaPixels.size() ||
            required > 16ull * 1024ull * 1024ull * 4ull) {
            logMessage(ANDROID_LOG_WARN,
                "Rejected invalid or oversized GS presentation frame");
            return;
        }
        pendingFramePixels_ = std::move(rgbaPixels);
        pendingFrameWidth_ = width;
        pendingFrameHeight_ = height;
        hasPendingFrame_ = true;
        frameUploadDirty_ = true;
    }

    void drawFrame() {
        if (!ready_ || device_ == VK_NULL_HANDLE) return;
        if (!isSuccess(
                vkWaitForFences(device_, 1, &frameFence_, VK_TRUE, UINT64_MAX),
                "vkWaitForFences")) {
            ready_ = false;
            return;
        }

        uint32_t imageIndex = 0u;
        const VkResult acquire = vkAcquireNextImageKHR(
            device_, swapchain_, UINT64_MAX, imageAvailable_,
            VK_NULL_HANDLE, &imageIndex);
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
            imageIndex >= renderFinished_.size() ||
            imageIndex >= swapchainImages_.size() ||
            imageIndex >= swapchainLayouts_.size()) {
            logMessage(ANDROID_LOG_ERROR, "Swapchain returned an invalid image index");
            ready_ = false;
            return;
        }

        if (!isSuccess(vkResetFences(device_, 1, &frameFence_), "vkResetFences") ||
            !isSuccess(vkResetCommandBuffer(commandBuffers_[imageIndex], 0),
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

        std::vector<gow::android::GsGpuVertex> frameVertices;
        std::vector<QueuedGsDraw> frameDraws;
        {
            std::lock_guard<std::mutex> lock(gsQueueMutex_);
            frameVertices.swap(queuedGsVertices_);
            frameDraws.swap(queuedGsDraws_);
        }

        const bool drewGsGeometry = !frameDraws.empty() &&
            recordGsGeometry(
                commandBuffers_[imageIndex], imageIndex,
                frameVertices, frameDraws);
        bool uploadedFrame = false;
        if (!drewGsGeometry && hasPendingFrame_ && frameUploadSupported_) {
            uploadedFrame = uploadFrameToSwapchain(
                commandBuffers_[imageIndex], imageIndex);
        }

        if (!drewGsGeometry && !uploadedFrame) {
            VkClearValue clearValue{};
            clearValue.color = {{0.025f, 0.018f, 0.035f, 1.0f}};
            VkRenderPassBeginInfo renderPassInfo{
                VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            renderPassInfo.renderPass = renderPass_;
            renderPassInfo.framebuffer = framebuffers_[imageIndex];
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = swapchainExtent_;
            renderPassInfo.clearValueCount = 1u;
            renderPassInfo.pClearValues = &clearValue;
            vkCmdBeginRenderPass(
                commandBuffers_[imageIndex], &renderPassInfo,
                VK_SUBPASS_CONTENTS_INLINE);
            vkCmdEndRenderPass(commandBuffers_[imageIndex]);
            swapchainLayouts_[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }

        if (!isSuccess(
                vkEndCommandBuffer(commandBuffers_[imageIndex]),
                "vkEndCommandBuffer")) {
            ready_ = false;
            return;
        }

        const VkPipelineStageFlags waitStage = uploadedFrame
            ? VK_PIPELINE_STAGE_TRANSFER_BIT
            : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.waitSemaphoreCount = 1u;
        submitInfo.pWaitSemaphores = &imageAvailable_;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1u;
        submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
        submitInfo.signalSemaphoreCount = 1u;
        submitInfo.pSignalSemaphores = &renderFinished_[imageIndex];
        if (!isSuccess(
                vkQueueSubmit(graphicsQueue_, 1u, &submitInfo, frameFence_),
                "vkQueueSubmit")) {
            ready_ = false;
            return;
        }

        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1u;
        presentInfo.pWaitSemaphores = &renderFinished_[imageIndex];
        presentInfo.swapchainCount = 1u;
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
    struct QueuedGsDraw {
        uint32_t firstVertex = 0u;
        uint32_t vertexCount = 0u;
        gow::android::GsPrimitiveTopology topology =
            gow::android::GsPrimitiveTopology::Triangles;
        uint32_t targetWidth = 640u;
        uint32_t targetHeight = 448u;
        uint16_t scissorX0 = 0u;
        uint16_t scissorY0 = 0u;
        uint16_t scissorX1 = 639u;
        uint16_t scissorY1 = 447u;
    };

    bool createGsGraphicsPipelines() {
        VkShaderModule vertexModule = VK_NULL_HANDLE;
        VkShaderModule fragmentModule = VK_NULL_HANDLE;
        VkShaderModuleCreateInfo vertexInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        vertexInfo.codeSize = gow::android::shaders::kGsPrimitiveVertexSpirvSize;
        vertexInfo.pCode = gow::android::shaders::kGsPrimitiveVertexSpirv;
        if (!isSuccess(
                vkCreateShaderModule(device_, &vertexInfo, nullptr, &vertexModule),
                "vkCreateShaderModule(GS vertex)")) return false;

        VkShaderModuleCreateInfo fragmentInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        fragmentInfo.codeSize = gow::android::shaders::kGsPrimitiveFragmentSpirvSize;
        fragmentInfo.pCode = gow::android::shaders::kGsPrimitiveFragmentSpirv;
        if (!isSuccess(
                vkCreateShaderModule(device_, &fragmentInfo, nullptr, &fragmentModule),
                "vkCreateShaderModule(GS fragment)")) {
            vkDestroyShaderModule(device_, vertexModule, nullptr);
            return false;
        }

        VkPipelineLayoutCreateInfo layoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        if (!isSuccess(
                vkCreatePipelineLayout(
                    device_, &layoutInfo, nullptr, &gsPipelineLayout_),
                "vkCreatePipelineLayout(GS)")) {
            vkDestroyShaderModule(device_, fragmentModule, nullptr);
            vkDestroyShaderModule(device_, vertexModule, nullptr);
            return false;
        }

        const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u,
             VK_SHADER_STAGE_VERTEX_BIT, vertexModule, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u,
             VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule, "main", nullptr}
        }};

        VkVertexInputBindingDescription binding{};
        binding.binding = 0u;
        binding.stride = static_cast<uint32_t>(sizeof(gow::android::GsGpuVertex));
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        const std::array<VkVertexInputAttributeDescription, 2> attributes = {{
            {0u, 0u, VK_FORMAT_R32G32B32_SFLOAT,
             static_cast<uint32_t>(offsetof(gow::android::GsGpuVertex, x))},
            {1u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT,
             static_cast<uint32_t>(offsetof(gow::android::GsGpuVertex, r))}
        }};
        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1u;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1u;
        viewport.scissorCount = 1u;
        VkPipelineRasterizationStateCreateInfo rasterization{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.depthClampEnable = VK_FALSE;
        rasterization.rasterizerDiscardEnable = VK_FALSE;
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.depthBiasEnable = VK_FALSE;
        rasterization.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorAttachment{};
        colorAttachment.blendEnable = VK_FALSE;
        colorAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlend.attachmentCount = 1u;
        colorBlend.pAttachments = &colorAttachment;
        const std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamic.pDynamicStates = dynamicStates.data();

        const std::array<VkPrimitiveTopology, 3> topologies = {
            VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
            VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        for (size_t i = 0u; i < topologies.size(); ++i) {
            VkPipelineInputAssemblyStateCreateInfo assembly{
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            assembly.topology = topologies[i];
            assembly.primitiveRestartEnable = VK_FALSE;
            VkGraphicsPipelineCreateInfo pipelineInfo{
                VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
            pipelineInfo.pStages = stages.data();
            pipelineInfo.pVertexInputState = &vertexInput;
            pipelineInfo.pInputAssemblyState = &assembly;
            pipelineInfo.pViewportState = &viewport;
            pipelineInfo.pRasterizationState = &rasterization;
            pipelineInfo.pMultisampleState = &multisample;
            pipelineInfo.pColorBlendState = &colorBlend;
            pipelineInfo.pDynamicState = &dynamic;
            pipelineInfo.layout = gsPipelineLayout_;
            pipelineInfo.renderPass = renderPass_;
            pipelineInfo.subpass = 0u;
            if (!isSuccess(
                    vkCreateGraphicsPipelines(
                        device_, VK_NULL_HANDLE, 1u, &pipelineInfo,
                        nullptr, &gsPipelines_[i]),
                    "vkCreateGraphicsPipelines(GS primitive)")) {
                vkDestroyShaderModule(device_, fragmentModule, nullptr);
                vkDestroyShaderModule(device_, vertexModule, nullptr);
                return false;
            }
        }
        vkDestroyShaderModule(device_, fragmentModule, nullptr);
        vkDestroyShaderModule(device_, vertexModule, nullptr);
        logMessage(ANDROID_LOG_INFO,
            "Android Vulkan surface and frame presenter ready; GS primitive batches are routed through VRAM before CRTC presentation");
        return true;
    }

    bool recordGsGeometry(
        VkCommandBuffer commandBuffer,
        uint32_t imageIndex,
        const std::vector<gow::android::GsGpuVertex> &vertices,
        const std::vector<QueuedGsDraw> &draws) {
        if (vertices.empty() || draws.empty() ||
            imageIndex >= framebuffers_.size() ||
            gsPipelineLayout_ == VK_NULL_HANDLE) return false;

        const VkDeviceSize requiredBytes =
            static_cast<VkDeviceSize>(vertices.size() * sizeof(vertices[0]));
        if (!ensureUploadBuffer(requiredBytes)) return false;
        std::memcpy(uploadMapped_, vertices.data(),
                    static_cast<size_t>(requiredBytes));
        // The staging buffer is shared with optional CPU frame uploads.
        // Invalidate that cached content after writing geometry into it.
        uploadBufferReady_ = false;
        if ((uploadMemoryFlags_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0u) {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = uploadMemory_;
            range.offset = 0u;
            range.size = VK_WHOLE_SIZE;
            if (!isSuccess(
                    vkFlushMappedMemoryRanges(device_, 1u, &range),
                    "vkFlushMappedMemoryRanges(GS vertices)")) return false;
        }

        VkClearValue clearValue{};
        clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkRenderPassBeginInfo renderPassInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderPassInfo.renderPass = renderPass_;
        renderPassInfo.framebuffer = framebuffers_[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchainExtent_;
        renderPassInfo.clearValueCount = 1u;
        renderPassInfo.pClearValues = &clearValue;
        vkCmdBeginRenderPass(
            commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(swapchainExtent_.width);
        viewport.height = static_cast<float>(swapchainExtent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0u, 1u, &viewport);

        const VkBuffer vertexBuffer = uploadBuffer_;
        const VkDeviceSize vertexOffset = 0u;
        vkCmdBindVertexBuffers(
            commandBuffer, 0u, 1u, &vertexBuffer, &vertexOffset);

        for (const QueuedGsDraw &draw : draws) {
            size_t pipelineIndex = 2u;
            if (draw.topology == gow::android::GsPrimitiveTopology::Points)
                pipelineIndex = 0u;
            else if (draw.topology == gow::android::GsPrimitiveTopology::Lines)
                pipelineIndex = 1u;
            if (gsPipelines_[pipelineIndex] == VK_NULL_HANDLE ||
                draw.vertexCount == 0u ||
                static_cast<uint64_t>(draw.firstVertex) + draw.vertexCount >
                    vertices.size()) continue;

            const uint32_t targetWidth = std::max(1u, draw.targetWidth);
            const uint32_t targetHeight = std::max(1u, draw.targetHeight);
            const uint32_t x0 = std::min(
                targetWidth, static_cast<uint32_t>(draw.scissorX0));
            const uint32_t y0 = std::min(
                targetHeight, static_cast<uint32_t>(draw.scissorY0));
            const uint32_t x1 = std::min(
                targetWidth, static_cast<uint32_t>(draw.scissorX1) + 1u);
            const uint32_t y1 = std::min(
                targetHeight, static_cast<uint32_t>(draw.scissorY1) + 1u);
            if (x0 >= x1 || y0 >= y1) continue;

            const uint32_t left = static_cast<uint32_t>(
                static_cast<uint64_t>(x0) * swapchainExtent_.width / targetWidth);
            const uint32_t top = static_cast<uint32_t>(
                static_cast<uint64_t>(y0) * swapchainExtent_.height / targetHeight);
            const uint32_t right = std::min(
                swapchainExtent_.width,
                static_cast<uint32_t>(
                    (static_cast<uint64_t>(x1) * swapchainExtent_.width +
                     targetWidth - 1u) / targetWidth));
            const uint32_t bottom = std::min(
                swapchainExtent_.height,
                static_cast<uint32_t>(
                    (static_cast<uint64_t>(y1) * swapchainExtent_.height +
                     targetHeight - 1u) / targetHeight));
            if (left >= right || top >= bottom) continue;

            VkRect2D scissor{};
            scissor.offset = {
                static_cast<int32_t>(left), static_cast<int32_t>(top)};
            scissor.extent = {right - left, bottom - top};
            vkCmdSetScissor(commandBuffer, 0u, 1u, &scissor);
            vkCmdBindPipeline(
                commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                gsPipelines_[pipelineIndex]);
            vkCmdDraw(
                commandBuffer, draw.vertexCount, 1u,
                draw.firstVertex, 0u);
        }
        vkCmdEndRenderPass(commandBuffer);
        swapchainLayouts_[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        return true;
    }

    void destroyGsGraphicsPipelines() {
        if (device_ == VK_NULL_HANDLE) return;
        for (VkPipeline &pipeline : gsPipelines_) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device_, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
        }
        if (gsPipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, gsPipelineLayout_, nullptr);
            gsPipelineLayout_ = VK_NULL_HANDLE;
        }
    }

    void destroyUploadBuffer() {
        if (device_ == VK_NULL_HANDLE) return;
        if (uploadMapped_ != nullptr && uploadMemory_ != VK_NULL_HANDLE) {
            vkUnmapMemory(device_, uploadMemory_);
            uploadMapped_ = nullptr;
        }
        if (uploadBuffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, uploadBuffer_, nullptr);
            uploadBuffer_ = VK_NULL_HANDLE;
        }
        if (uploadMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, uploadMemory_, nullptr);
            uploadMemory_ = VK_NULL_HANDLE;
        }
        uploadBufferCapacity_ = 0u;
        uploadMemoryFlags_ = 0u;
        uploadBufferReady_ = false;
    }

    bool ensureUploadBuffer(VkDeviceSize requiredBytes) {
        if (uploadBuffer_ != VK_NULL_HANDLE &&
            uploadBufferCapacity_ >= requiredBytes &&
            uploadMapped_ != nullptr) {
            return true;
        }
        destroyUploadBuffer();

        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = requiredBytes;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!isSuccess(
                vkCreateBuffer(device_, &bufferInfo, nullptr, &uploadBuffer_),
                "vkCreateBuffer(GS frame upload)")) {
            destroyUploadBuffer();
            return false;
        }

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, uploadBuffer_, &requirements);
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);

        uint32_t selectedType = UINT32_MAX;
        VkMemoryPropertyFlags selectedFlags = 0u;
        for (uint32_t i = 0u; i < memoryProperties.memoryTypeCount; ++i) {
            if ((requirements.memoryTypeBits & (1u << i)) == 0u) continue;
            const VkMemoryPropertyFlags flags =
                memoryProperties.memoryTypes[i].propertyFlags;
            if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0u) continue;
            if (selectedType == UINT32_MAX ||
                (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u) {
                selectedType = i;
                selectedFlags = flags;
                if ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u) break;
            }
        }
        if (selectedType == UINT32_MAX) {
            logMessage(ANDROID_LOG_ERROR,
                "No host-visible Vulkan memory type is available for GS frame upload");
            destroyUploadBuffer();
            return false;
        }

        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = selectedType;
        if (!isSuccess(
                vkAllocateMemory(device_, &allocation, nullptr, &uploadMemory_),
                "vkAllocateMemory(GS frame upload)")) {
            destroyUploadBuffer();
            return false;
        }
        if (!isSuccess(
                vkBindBufferMemory(device_, uploadBuffer_, uploadMemory_, 0u),
                "vkBindBufferMemory(GS frame upload)")) {
            destroyUploadBuffer();
            return false;
        }
        if (!isSuccess(
                vkMapMemory(
                    device_, uploadMemory_, 0u, VK_WHOLE_SIZE, 0u,
                    &uploadMapped_),
                "vkMapMemory(GS frame upload)")) {
            destroyUploadBuffer();
            return false;
        }

        uploadBufferCapacity_ = requiredBytes;
        uploadMemoryFlags_ = selectedFlags;
        return true;
    }

    bool uploadFrameToSwapchain(
        VkCommandBuffer commandBuffer,
        uint32_t imageIndex) {
        if (!frameUploadSupported_) return false;

        const bool targetChanged =
            uploadTargetWidth_ != swapchainExtent_.width ||
            uploadTargetHeight_ != swapchainExtent_.height;
        if (frameUploadDirty_ || targetChanged || !uploadBufferReady_) {
            std::string scaleError;
            if (!gow::android::scaleRgbaToFit(
                    pendingFramePixels_.data(),
                    pendingFramePixels_.size(),
                    pendingFrameWidth_,
                    pendingFrameHeight_,
                    swapchainExtent_.width,
                    swapchainExtent_.height,
                    uploadPixels_,
                    scaleError)) {
                __android_log_print(
                    ANDROID_LOG_WARN, kTag,
                    "GS frame upload rejected: %s", scaleError.c_str());
                return false;
            }
            if (swapchainFormat_ == VK_FORMAT_B8G8R8A8_UNORM) {
                gow::android::convertRgbaToBgraInPlace(uploadPixels_);
            }

            const VkDeviceSize requiredBytes =
                static_cast<VkDeviceSize>(uploadPixels_.size());
            if (!ensureUploadBuffer(requiredBytes)) return false;
            std::memcpy(uploadMapped_, uploadPixels_.data(), uploadPixels_.size());

            if ((uploadMemoryFlags_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0u) {
                VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                range.memory = uploadMemory_;
                range.offset = 0u;
                range.size = VK_WHOLE_SIZE;
                if (!isSuccess(
                        vkFlushMappedMemoryRanges(device_, 1u, &range),
                        "vkFlushMappedMemoryRanges(GS frame upload)")) {
                    return false;
                }
            }
            uploadTargetWidth_ = swapchainExtent_.width;
            uploadTargetHeight_ = swapchainExtent_.height;
            frameUploadDirty_ = false;
            uploadBufferReady_ = true;
        }

        VkImageMemoryBarrier toTransfer{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toTransfer.srcAccessMask = 0u;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = swapchainLayouts_[imageIndex];
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = swapchainImages_[imageIndex];
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.baseMipLevel = 0u;
        toTransfer.subresourceRange.levelCount = 1u;
        toTransfer.subresourceRange.baseArrayLayer = 0u;
        toTransfer.subresourceRange.layerCount = 1u;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u, 0u, nullptr, 0u, nullptr, 1u, &toTransfer);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0u;
        copyRegion.bufferRowLength = 0u;
        copyRegion.bufferImageHeight = 0u;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0u;
        copyRegion.imageSubresource.baseArrayLayer = 0u;
        copyRegion.imageSubresource.layerCount = 1u;
        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent = {
            swapchainExtent_.width, swapchainExtent_.height, 1u};
        vkCmdCopyBufferToImage(
            commandBuffer,
            uploadBuffer_,
            swapchainImages_[imageIndex],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1u,
            &copyRegion);

        VkImageMemoryBarrier toPresent{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toPresent.dstAccessMask = 0u;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = swapchainImages_[imageIndex];
        toPresent.subresourceRange = toTransfer.subresourceRange;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0u, 0u, nullptr, 0u, nullptr, 1u, &toPresent);

        swapchainLayouts_[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        return true;
    }

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
        const bool rgbaCompatible =
            selectedFormat.format == VK_FORMAT_R8G8B8A8_UNORM ||
            selectedFormat.format == VK_FORMAT_B8G8R8A8_UNORM;
        frameUploadSupported_ = rgbaCompatible &&
            (capabilities.supportedUsageFlags &
                VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0u;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (frameUploadSupported_) {
            swapchainInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        } else {
            logMessage(ANDROID_LOG_WARN,
                "Vulkan surface does not support RGBA transfer upload; frame presenter remains disabled");
        }
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
        swapchainLayouts_.assign(
            actualImageCount, VK_IMAGE_LAYOUT_UNDEFINED);
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

        if (!createGsGraphicsPipelines()) {
            logMessage(ANDROID_LOG_ERROR,
                "Could not create Android GS primitive graphics pipelines");
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

        destroyGsGraphicsPipelines();

        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }

        for (const VkImageView view : imageViews_) {
            vkDestroyImageView(device_, view, nullptr);
        }
        imageViews_.clear();
        swapchainImages_.clear();
        swapchainLayouts_.clear();
        frameUploadSupported_ = false;
        uploadBufferReady_ = false;
        uploadTargetWidth_ = 0u;
        uploadTargetHeight_ = 0u;
        frameUploadDirty_ = hasPendingFrame_;

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
    std::vector<VkImageLayout> swapchainLayouts_;
    std::vector<VkImageView> imageViews_;
    VkBuffer uploadBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uploadMemory_ = VK_NULL_HANDLE;
    void *uploadMapped_ = nullptr;
    VkDeviceSize uploadBufferCapacity_ = 0u;
    VkMemoryPropertyFlags uploadMemoryFlags_ = 0u;
    std::vector<uint8_t> pendingFramePixels_;
    std::vector<uint8_t> uploadPixels_;
    uint32_t pendingFrameWidth_ = 0u;
    uint32_t pendingFrameHeight_ = 0u;
    uint32_t uploadTargetWidth_ = 0u;
    uint32_t uploadTargetHeight_ = 0u;
    bool hasPendingFrame_ = false;
    bool frameUploadDirty_ = false;
    bool uploadBufferReady_ = false;
    bool frameUploadSupported_ = false;
    std::mutex gsQueueMutex_;
    std::vector<gow::android::GsGpuVertex> queuedGsVertices_;
    std::vector<QueuedGsDraw> queuedGsDraws_;
    VkPipelineLayout gsPipelineLayout_ = VK_NULL_HANDLE;
    std::array<VkPipeline, 3> gsPipelines_ = {
        VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    std::vector<VkSemaphore> renderFinished_;
    VkFence frameFence_ = VK_NULL_HANDLE;
    std::atomic<bool> ready_{false};
};

VulkanSmokeRenderer gRenderer;

bool attachAndroidGsBackend() {
    if (gActiveRuntime == nullptr || !gRenderer.isReady()) return false;
    if (gGsBackendAttached) return true;
    try {
        auto backend = std::make_unique<gow::android::AndroidGSVulkanBackend>(
            nullptr, nullptr);
        gActiveRuntime->gs().setRasterBackend(std::move(backend));
        gGsBackendAttached = true;
        logMessage(ANDROID_LOG_INFO,
            "Android GS CPU rasterizer + Vulkan frame presenter attached for first-boot diagnostics");
        return true;
    } catch (const std::exception &exception) {
        __android_log_print(
            ANDROID_LOG_ERROR, kTag,
            "Could not attach Android GS backend: %s", exception.what());
    } catch (...) {
        logMessage(ANDROID_LOG_ERROR,
            "Could not attach Android GS backend: unknown exception");
    }
    return false;
}

bool tryStartAndroidGuestExecution(android_app *app) {
    if (gGuestExecutionStarted || gGuestExecutionStartAttempted ||
        gActiveRuntime == nullptr || !gPrivateAotElfReady ||
        !gGsBackendAttached || !gRenderer.isReady()) {
        return false;
    }

    // The EE worker is started only after the private ELF passed the AOT
    // entry-point gate and the Android GS/CRTC-to-Vulkan presentation path
    // is attached. Audio output is intentionally silent for this first-boot
    // diagnostic; the IOP audio queue is bounded and drops stale samples.
    gGuestExecutionStartAttempted = true;
    std::array<char, 512> message{};
    if (!gowRuntimeStartGuestExecutionAndroid(
            gActiveRuntime, message.data(), message.size())) {
        const char *detail = message[0] == '\0'
            ? "O worker EE Android não iniciou. Consulte o Logcat GowAndroid."
            : message.data();
        reportRuntimeStatus(app, detail, true);
        logMessage(ANDROID_LOG_ERROR, detail);
        return false;
    }

    gGuestExecutionStarted = true;
    logMessage(
        ANDROID_LOG_INFO,
        "Android AOT guest execution worker started after the strict readiness gates.");
    reportRuntimeStatus(
        app,
        "Tentativa de boot AOT iniciada no Android. O primeiro teste é silencioso; acompanhe o Logcat GowAndroid para o estágio do jogo.",
        false);
    logMessage(
        ANDROID_LOG_INFO,
        "Android AOT guest worker started after private ELF and GS/Vulkan gates passed");
    return true;
}

void reportAndroidGuestCompletion(android_app *app) {
    if (!gGuestExecutionStarted || gGuestExecutionFinishReported ||
        !gowRuntimeGuestExecutionFinishedAndroid(gActiveRuntime)) {
        return;
    }
    gGuestExecutionFinishReported = true;
    logMessage(
        ANDROID_LOG_WARN,
        "Android AOT guest execution worker finished; collecting last known diagnostics.");
    reportRuntimeStatus(
        app,
        "A execução AOT do guest foi encerrada. Consulte o Logcat GowAndroid para o último PC e o motivo; nenhuma cena real foi aprovada automaticamente.",
        true);
    logMessage(ANDROID_LOG_WARN, "Android AOT guest worker finished; inspect the last guest PC and subsystem diagnostics");
}

void detachAndroidGsBackend() {
    if (gGsBackendAttached && gActiveRuntime != nullptr) {
        gActiveRuntime->gs().setRasterBackend(nullptr);
        gGsBackendAttached = false;
        logMessage(ANDROID_LOG_INFO, "Android GS Vulkan backend detached");
    }
}

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
#if defined(GOW_ANDROID_ENABLE_VULKAN_SMOKE) || defined(GOW_ANDROID_ENABLE_GS_VULKAN)
                if (gRenderer.initialize(app->window)) {
#if defined(GOW_ANDROID_ENABLE_GS_VULKAN)
                    attachAndroidGsBackend();
#endif
                }
#else
                logMessage(ANDROID_LOG_INFO,
                    "GameActivity surface available; Android GS Vulkan primitive path is disabled in this build");
#endif
            }
            break;

        case APP_CMD_TERM_WINDOW:
#if defined(GOW_ANDROID_ENABLE_VULKAN_SMOKE) || defined(GOW_ANDROID_ENABLE_GS_VULKAN)
#if defined(GOW_ANDROID_ENABLE_GS_VULKAN)
            detachAndroidGsBackend();
#endif
            gRenderer.shutdown();
#endif
            break;

        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONTENT_RECT_CHANGED:
#if defined(GOW_ANDROID_ENABLE_VULKAN_SMOKE) || defined(GOW_ANDROID_ENABLE_GS_VULKAN)
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

extern "C" JNIEXPORT void JNICALL
Java_org_gowandroid_app_GowGameActivity_nativeSetActivityReference(
    JNIEnv *env, jclass, jobject activity) noexcept {
    if (env == nullptr || activity == nullptr) {
        return;
    }

    JavaVM *vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK || vm == nullptr) {
        return;
    }

    jobject newReference = env->NewGlobalRef(activity);
    if (newReference == nullptr) {
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
        }
        return;
    }

    jobject previousReference = nullptr;
    {
        std::lock_guard<std::mutex> lock(gActivityReferenceMutex);
        previousReference = gActivityReference;
        gActivityReference = newReference;
        gActivityVm = vm;
    }
    if (previousReference != nullptr) {
        env->DeleteGlobalRef(previousReference);
    }
    {
        std::lock_guard<std::mutex> lock(gBootPathMutex);
        gBootRelativePath.clear();
        gBootPathConfigurationReceived = false;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_org_gowandroid_app_GowGameActivity_nativeClearActivityReference(
    JNIEnv *env, jclass, jobject activity) noexcept {
    if (env == nullptr || activity == nullptr) {
        return;
    }

    jobject previousReference = nullptr;
    {
        std::lock_guard<std::mutex> lock(gActivityReferenceMutex);
        if (gActivityReference == nullptr ||
            env->IsSameObject(gActivityReference, activity) != JNI_TRUE) {
            return;
        }
        previousReference = gActivityReference;
        gActivityReference = nullptr;
        gActivityVm = nullptr;
    }
    if (previousReference != nullptr) {
        env->DeleteGlobalRef(previousReference);
    }
    {
        std::lock_guard<std::mutex> lock(gBootPathMutex);
        gBootPathConfigurationReceived = true;
    }
    gBootPathCondition.notify_all();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_gowandroid_app_GowGameActivity_nativeRequestGsSceneCapture(
    JNIEnv *, jclass) noexcept {
    if (gGsSceneCaptureSequence.load(std::memory_order_relaxed) >=
        kMaxRequestedGsSceneCaptures) {
        return JNI_FALSE;
    }
    gGsSceneCaptureRequested.store(true, std::memory_order_release);
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_gowandroid_app_GowGameActivity_nativeConfigurePrivateBoot(
    JNIEnv *env, jclass, jstring privateCdRoot,
    jstring relativePath) noexcept {
    if (env == nullptr) {
        return JNI_FALSE;
    }

    std::string rootPath;
    std::string candidate;
    bool inputsValid = false;
    if (privateCdRoot != nullptr && relativePath != nullptr) {
        const char *rootUtf = env->GetStringUTFChars(privateCdRoot, nullptr);
        const char *bootUtf = env->GetStringUTFChars(relativePath, nullptr);
        if (rootUtf != nullptr && bootUtf != nullptr) {
            rootPath.assign(rootUtf);
            candidate.assign(bootUtf);
            inputsValid = !rootPath.empty() && !candidate.empty()
                && candidate.size() <= 1024u;
        }
        if (rootUtf != nullptr) env->ReleaseStringUTFChars(privateCdRoot, rootUtf);
        if (bootUtf != nullptr) env->ReleaseStringUTFChars(relativePath, bootUtf);
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
            inputsValid = false;
        }
    }

    const bool rootReady = inputsValid && gowAndroidCdRootSetPath(rootPath.c_str());
    if (rootReady) {
        (void)configurePrivateAotCaptureDirectories(rootPath);
    } else {
        ::unsetenv("GOW_VU1_CAPTURA");
        ::unsetenv("GOW_VU1N_MISS_LOG");
        ::unsetenv("PS2X_IOP_DUMP_MODULES");
    }
    {
        std::lock_guard<std::mutex> lock(gBootPathMutex);
        gBootRelativePath = rootReady ? std::move(candidate) : std::string{};
        gBootPathConfigurationReceived = true;
    }
    gBootPathCondition.notify_all();
    return rootReady ? JNI_TRUE : JNI_FALSE;
}

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
    gPrivateAotElfReady = false;
    gGuestExecutionStartAttempted = false;
    gGuestExecutionStarted = false;
    gGuestExecutionFinishReported = false;

    // Initialize the EE/IOP/VU core without Raylib. The experimental Android GS
    // primitive adapter is attached only after the GameActivity Vulkan surface is ready.
    // Guest execution remains blocked until full GS, audio, AOT and boot support exist.
    reportRuntimeStatus(
        app, "Preparando o núcleo PS2 (memória, EE, IOP e VU)...", false);
    PS2Runtime *runtime = gowCreatePs2Runtime();
    gActiveRuntime = runtime;
    if (runtime != nullptr) {
        std::array<char, 512> coreMessage{};
        if (gowInitializePs2RuntimeCoreHeadless(
                runtime, coreMessage.data(), coreMessage.size())) {
            __android_log_print(
                ANDROID_LOG_INFO, kTag,
                "PS2Runtime core ready: %s", coreMessage.data());

            std::string bootPath;
            {
                std::unique_lock<std::mutex> lock(gBootPathMutex);
                gBootPathCondition.wait(
                    lock, [] { return gBootPathConfigurationReceived; });
                bootPath = gBootRelativePath;
            }
            if (!bootPath.empty()) {
                std::array<char, 512> loadMessage{};
                const bool elfReady = gowRuntimeLoadElfAndCheckAot(
                    runtime, bootPath.c_str(),
                    loadMessage.data(), loadMessage.size());
                gPrivateAotElfReady = elfReady;
                __android_log_print(
                    elfReady ? ANDROID_LOG_INFO : ANDROID_LOG_WARN,
                    kTag, "PS2Runtime ELF/AOT gate: %s",
                    loadMessage[0] == '\0'
                        ? (elfReady ? "ELF gate passed." : "ELF gate failed without a diagnostic.")
                        : loadMessage.data());
                reportRuntimeStatus(
                    app,
                    loadMessage[0] == '\0'
                        ? (elfReady
                            ? "ELF e função AOT de entrada validados. O jogo ainda não iniciou: GS/Vulkan e áudio Android continuam pendentes."
                            : "A validação ELF/AOT não passou. Consulte o Logcat GowAndroid.")
                        : loadMessage.data(),
                    !elfReady);
            } else {
                reportRuntimeStatus(
                    app,
                    "Núcleo PS2 inicializado. Nenhum BOOT2 foi encaminhado; o jogo não foi carregado.",
                    false);
            }
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
#if defined(GOW_ANDROID_ENABLE_VULKAN_SMOKE) || defined(GOW_ANDROID_ENABLE_GS_VULKAN)
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

#if defined(GOW_ANDROID_ENABLE_VULKAN_SMOKE) || defined(GOW_ANDROID_ENABLE_GS_VULKAN)
        if (gRenderer.isReady()) {
            if (runtime != nullptr) {
                (void)tryStartAndroidGuestExecution(app);
                reportAndroidGuestCompletion(app);
                std::vector<uint8_t> framePixels;
                uint32_t frameWidth = 0u;
                uint32_t frameHeight = 0u;
                if (runtime->gs().copyLatchedHostPresentationFrame(
                        framePixels, frameWidth, frameHeight)) {
                    const bool firstFrameCapture = !gGsSceneCaptureAttempted;
                    const bool requestedCapture = gGsSceneCaptureRequested.exchange(
                        false, std::memory_order_acq_rel);
                    if (firstFrameCapture || requestedCapture) {
                        if (firstFrameCapture) gGsSceneCaptureAttempted = true;
                        const GameActivity *activity = app->activity;
                        const char *captureDirectory = activity != nullptr
                            ? (activity->externalDataPath != nullptr
                                ? activity->externalDataPath
                                : activity->internalDataPath)
                            : nullptr;
                        if (captureDirectory != nullptr) {
                            std::string capturePath(captureDirectory);
                            if (!capturePath.empty() && capturePath.back() != '/') {
                                capturePath.push_back('/');
                            }
                            std::string captureName = "gow_android_first_gs_frame.ppm";
                            if (!firstFrameCapture) {
                                const uint32_t sequence =
                                    gGsSceneCaptureSequence.fetch_add(
                                        1u, std::memory_order_relaxed) + 1u;
                                if (sequence <= kMaxRequestedGsSceneCaptures) {
                                    char numberedName[64]{};
                                    std::snprintf(
                                        numberedName, sizeof(numberedName),
                                        "gow_android_scene_%02u.ppm", sequence);
                                    captureName = numberedName;
                                } else {
                                    __android_log_print(
                                        ANDROID_LOG_WARN, kTag,
                                        "GS scene capture limit reached (%u).",
                                        kMaxRequestedGsSceneCaptures);
                                }
                            }
                            if (!captureName.empty()) {
                                capturePath += captureName;
                                std::string captureError;
                                if (gow::android::writeRgbaPpmCapture(
                                        capturePath, framePixels, frameWidth,
                                        frameHeight, captureError)) {
                                    __android_log_print(
                                        ANDROID_LOG_INFO, kTag,
                                        "Saved GS scene capture: %s",
                                        capturePath.c_str());
                                } else {
                                    __android_log_print(
                                        ANDROID_LOG_WARN, kTag,
                                        "Could not save GS scene capture: %s",
                                        captureError.c_str());
                                }
                            }
                        } else {
                            logMessage(ANDROID_LOG_WARN,
                                "No app data directory is available for a GS scene capture");
                        }
                    }
                    gRenderer.submitFrame(
                        std::move(framePixels), frameWidth, frameHeight);
                }
            }
            gRenderer.drawFrame();
        }
#endif
    }

#if defined(GOW_ANDROID_ENABLE_VULKAN_SMOKE) || defined(GOW_ANDROID_ENABLE_GS_VULKAN)
    detachAndroidGsBackend();
    gRenderer.shutdown();
#endif
    gActiveRuntime = nullptr;
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
