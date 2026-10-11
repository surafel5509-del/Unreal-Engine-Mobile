// Nova Engine — Vulkan 1.3 device, swapchain and frame loop.
//
// Structure of this translation unit:
//   BackbufferTexture   — ITexture view over a swapchain image.
//   VulkanCommandBuffer— ICommandBuffer recording (dynamic rendering + sync2).
//   VulkanDevice        — IDevice implementation (everything else).
//   createVulkanDevice  — factory used by rhi/DeviceFactory.cpp.
//
// Frames-in-flight: 2 (double buffering). Per-frame: command pool/buffer,
// imageAvailable semaphore, descriptor pool, in-flight fence. Per-swapchain-
// image: renderFinished semaphore + images-in-flight fence (standard
// acquire/submit/present handshake).

#include "rhi/vulkan/VulkanDevice.h"

#include "core/Assert.h"
#include "rhi/vulkan/VulkanResources.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace nova::rhi {
namespace {

constexpr u32 kMaxFramesInFlight = 2;
constexpr u32 kDescriptorSetsPerFrame = 64;

/// Swapchain image wrapper exposed to passes as ITexture.
class BackbufferTexture final : public VulkanTextureBase {
public:
    BackbufferTexture(VkImage image, VkImageView view, Format format, u32 width, u32 height)
        : m_image(image), m_view(view), m_format(format), m_width(width), m_height(height) {}

    Format format() const override { return m_format; }
    u32 width() const override { return m_width; }
    u32 height() const override { return m_height; }
    VkImage imageHandle() const override { return m_image; }
    VkImageView viewHandle() const override { return m_view; }

    void setExtent(u32 width, u32 height) {
        m_width = width;
        m_height = height;
    }

private:
    VkImage m_image = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    Format m_format = Format::Unknown;
    u32 m_width = 0;
    u32 m_height = 0;
};

}  // namespace

// =========================================================== VulkanCommandBuffer

class VulkanCommandBuffer final : public ICommandBuffer {
public:
    /// Stable per-frame storage owned by VulkanDevice (set 0 layout + pool).
    struct FrameContext {
        VkDescriptorSetLayout uboSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    };

    VulkanCommandBuffer(VkDevice device, VkCommandBuffer commands, FrameContext* frame)
        : m_device(device), m_commands(commands), m_frame(frame) {}

    void beginRecording() {
        m_renderPassOpen = false;
        m_lastColorTexture = nullptr;
        m_boundPipeline = nullptr;

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkutil::checkVk(vkBeginCommandBuffer(m_commands, &beginInfo),
                        "vkBeginCommandBuffer");
    }

    void beginRendering(u32 width, u32 height,
                        std::span<const ColorAttachment> colorAttachments) override {
        NOVA_ENSURE(!m_renderPassOpen, "beginRendering called twice");
        NOVA_ENSURE(!colorAttachments.empty(), "beginRendering needs at least one attachment");

        // UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL, gated by the acquire wait
        // (stage COLOR_ATTACHMENT_OUTPUT on the wait semaphore).
        for (const ColorAttachment& attachment : colorAttachments) {
            auto* texture = static_cast<VulkanTextureBase*>(attachment.texture);
            NOVA_ENSURE(texture != nullptr, "color attachment texture is null");

            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.srcAccessMask = 0;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture->imageHandle();
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(m_commands, &dependency);
        }

        std::array<VkRenderingAttachmentInfo, kMaxColorAttachments> colors{};
        const u32 attachmentCount = static_cast<u32>(std::min<size_t>(
            colorAttachments.size(), kMaxColorAttachments));
        for (u32 i = 0; i < attachmentCount; ++i) {
            const ColorAttachment& attachment = colorAttachments[i];
            auto* texture = static_cast<VulkanTextureBase*>(attachment.texture);
            colors[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colors[i].imageView = texture->viewHandle();
            colors[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colors[i].loadOp = vkutil::toVkLoadOp(attachment.loadOp);
            colors[i].storeOp = vkutil::toVkStoreOp(attachment.storeOp);
            colors[i].clearValue.color =
                {{attachment.clearColor.r, attachment.clearColor.g,
                  attachment.clearColor.b, attachment.clearColor.a}};
        }

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = {{0, 0}, {width, height}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = attachmentCount;
        renderingInfo.pColorAttachments = colors.data();

        vkCmdBeginRendering(m_commands, &renderingInfo);
        m_renderPassOpen = true;
        m_lastColorTexture = static_cast<VulkanTextureBase*>(colorAttachments[0].texture);
        setViewportScissor(width, height);
    }

    void endRendering() override {
        NOVA_ENSURE(m_renderPassOpen, "endRendering without beginRendering");
        vkCmdEndRendering(m_commands);
        m_renderPassOpen = false;

        // COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR. The present wait
        // semaphore is the memory dependency for the presentation engine.
        if (m_lastColorTexture != nullptr) {
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            barrier.dstAccessMask = 0;
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = m_lastColorTexture->imageHandle();
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(m_commands, &dependency);
        }
        m_lastColorTexture = nullptr;
    }

    void setViewportScissor(u32 width, u32 height) override {
        // Positive-height viewport; Y is flipped in the projection matrix.
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(width);
        viewport.height = static_cast<float>(height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(m_commands, 0, 1, &viewport);

        VkRect2D scissor{{0, 0}, {width, height}};
        vkCmdSetScissor(m_commands, 0, 1, &scissor);
    }

    void bindPipeline(const IPipeline* pipeline) override {
        auto* vulkanPipeline = static_cast<const VulkanPipeline*>(pipeline);
        NOVA_ENSURE(vulkanPipeline != nullptr && vulkanPipeline->isValid(),
                    "bindPipeline: invalid pipeline");
        vkCmdBindPipeline(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          vulkanPipeline->handle());
        m_boundPipeline = vulkanPipeline;
    }

    void bindVertexBuffer(const IBuffer* buffer) override {
        auto* vulkanBuffer = static_cast<const VulkanBuffer*>(buffer);
        NOVA_ENSURE(vulkanBuffer != nullptr && vulkanBuffer->isValid(),
                    "bindVertexBuffer: invalid buffer");
        const VkDeviceSize offset = 0;
        const VkBuffer bufferHandle = vulkanBuffer->handle();
        vkCmdBindVertexBuffers(m_commands, 0, 1, &bufferHandle, &offset);
    }

    void bindUniformBuffer(u32 slot, const IBuffer* buffer, usize offset,
                           usize range) override {
        NOVA_ENSURE(m_frame != nullptr, "bindUniformBuffer: no frame context");
        NOVA_ENSURE(slot == 0, "M1 exposes a single uniform slot (got %u)", slot);
        auto* vulkanBuffer = static_cast<const VulkanBuffer*>(buffer);
        NOVA_ENSURE(vulkanBuffer != nullptr && vulkanBuffer->isValid(),
                    "bindUniformBuffer: invalid buffer");
        NOVA_ENSURE(m_boundPipeline != nullptr, "bindUniformBuffer before bindPipeline");

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_frame->descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_frame->uboSetLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (!vkutil::checkVk(vkAllocateDescriptorSets(m_device, &allocInfo, &set),
                             "vkAllocateDescriptorSets")) {
            return;
        }

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = vulkanBuffer->handle();
        bufferInfo.offset = offset;
        bufferInfo.range = range;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

        vkCmdBindDescriptorSets(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_boundPipeline->layout(), 0, 1, &set, 0, nullptr);
    }

    void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex,
              u32 firstInstance) override {
        NOVA_ENSURE(m_renderPassOpen, "draw outside of a rendering scope");
        vkCmdDraw(m_commands, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    VkCommandBuffer handle() const { return m_commands; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkCommandBuffer m_commands = VK_NULL_HANDLE;
    FrameContext* m_frame = nullptr;
    const VulkanPipeline* m_boundPipeline = nullptr;
    VulkanTextureBase* m_lastColorTexture = nullptr;
    bool m_renderPassOpen = false;
};

// ================================================================ VulkanDevice

class VulkanDevice final : public IDevice {
public:
    explicit VulkanDevice(const DeviceDesc& desc);
    ~VulkanDevice() override;

    bool isValid() const { return m_device != VK_NULL_HANDLE; }

    // -- IDevice ------------------------------------------------------------
    Unique<IBuffer> createBuffer(const BufferDesc& desc) override;
    Unique<IShaderModule> createShaderModule(const ShaderModuleDesc& desc) override;
    Unique<IPipeline> createPipeline(const PipelineDesc& desc) override;

    void setSurface(void* nativeWindow, u32 width, u32 height) override;
    void setSurfaceSize(u32 width, u32 height) override;
    void setPresentMode(PresentMode mode) override;
    PresentMode presentMode() const override { return m_requestedPresentMode; }

    bool beginFrame(FrameInfo& out) override;
    void endFrame() override;
    void waitIdle() override;
    bool hasSurface() const override { return m_swapchain != VK_NULL_HANDLE; }
    const DeviceStats& stats() const override { return m_stats; }

private:
    // setup / teardown
    bool createInstance(const DeviceDesc& desc);
    bool pickPhysicalDevice();
    bool createDevice();
    bool createFrameObjects();
    void destroyFrameObjects();
    bool createSurface();
    void destroySurface();
    bool createSwapchain(u32 width, u32 height);
    void destroySwapchain();
    bool recreateSwapchain();
    bool acquireNextImage(u32& imageIndex);

    static VkBool32 VKAPI_PTR debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT,
                                            const VkDebugUtilsMessengerCallbackDataEXT* data,
                                            void* userData);

    /// Attach a debug-utils name to an object when validation is active.
    void setDebugName(VkObjectType type, u64 handle, const char* name) {
        if (m_setObjectName == nullptr || handle == 0) return;
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = type;
        nameInfo.objectHandle = handle;
        nameInfo.pObjectName = name;
        m_setObjectName(m_device, &nameInfo);
    }

    // ---- instance level ----
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    PFN_vkSetDebugUtilsObjectNameEXT m_setObjectName = nullptr;
    bool m_validationEnabled = false;

    // ---- device level ----
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    u32 m_queueFamily = 0;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_deviceProperties{};
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_uboSetLayout = VK_NULL_HANDLE;

    // ---- surface / swapchain ----
    void* m_window = nullptr;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapchainExtent{};
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainViews;
    std::vector<Unique<BackbufferTexture>> m_backbuffers;
    PresentMode m_requestedPresentMode = PresentMode::Fifo;
    bool m_swapchainDirty = false;
    bool m_resizePending = false;
    u32 m_pendingWidth = 0;
    u32 m_pendingHeight = 0;
    bool m_deviceLost = false;

    // ---- frame synchronization ----
    struct FrameObjects {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        Unique<VulkanCommandBuffer> recorder;
    };
    std::array<VulkanCommandBuffer::FrameContext, kMaxFramesInFlight> m_frameContexts{};
    std::array<FrameObjects, kMaxFramesInFlight> m_frames{};
    std::vector<VkSemaphore> m_renderFinished;  // per swapchain image
    std::vector<VkFence> m_imagesInFlight;      // per swapchain image
    u64 m_frameIndex = 0;
    u32 m_activeRing = 0;
    u32 m_activeImage = 0;
    bool m_frameOpen = false;

    // ---- stats ----
    DeviceStats m_stats{};
};

// -------------------------------------------------------------- debug callback

VkBool32 VKAPI_PTR VulkanDevice::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*types*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*userData*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        NOVA_LOG_ERROR("validation: %s", data->pMessage);
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        NOVA_LOG_WARN("validation: %s", data->pMessage);
    } else {
        NOVA_LOG_DEBUG("validation: %s", data->pMessage);
    }
    return VK_FALSE;
}

// ------------------------------------------------------------------ lifecycle

VulkanDevice::VulkanDevice(const DeviceDesc& desc) {
    m_requestedPresentMode = desc.presentMode;
    if (!createInstance(desc) || !pickPhysicalDevice() || !createDevice() ||
        !createFrameObjects()) {
        NOVA_LOG_ERROR("vulkan: device initialization failed");
        destroyFrameObjects();
        if (m_allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(m_allocator);
            m_allocator = VK_NULL_HANDLE;
        }
        if (m_uboSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_uboSetLayout, nullptr);
            m_uboSetLayout = VK_NULL_HANDLE;
        }
        if (m_device != VK_NULL_HANDLE) {
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
        m_physicalDevice = VK_NULL_HANDLE;
        if (m_debugMessenger != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
            auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroyMessenger != nullptr) {
                destroyMessenger(m_instance, m_debugMessenger, nullptr);
            }
            m_debugMessenger = VK_NULL_HANDLE;
        }
        if (m_instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
        return;
    }

    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_deviceProperties);
    m_stats.validationEnabled = m_validationEnabled;
    m_stats.presentMode = m_requestedPresentMode;
    m_stats.framesInFlight = kMaxFramesInFlight;
    NOVA_LOG_INFO("vulkan: device ready — %s, API %u.%u.%u, validation=%s",
                  m_deviceProperties.deviceName,
                  VK_API_VERSION_MAJOR(m_deviceProperties.apiVersion),
                  VK_API_VERSION_MINOR(m_deviceProperties.apiVersion),
                  VK_API_VERSION_PATCH(m_deviceProperties.apiVersion),
                  m_validationEnabled ? "on" : "off");
}

VulkanDevice::~VulkanDevice() {
    if (m_device == VK_NULL_HANDLE) {
        // Partial construction failure: only instance-level leftovers remain.
        if (m_debugMessenger != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
            auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroyMessenger != nullptr) {
                destroyMessenger(m_instance, m_debugMessenger, nullptr);
            }
        }
        if (m_instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_instance, nullptr);
        }
        return;
    }

    vkDeviceWaitIdle(m_device);
    destroySwapchain();
    destroySurface();
    destroyFrameObjects();

    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
    if (m_uboSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_uboSetLayout, nullptr);
        m_uboSetLayout = VK_NULL_HANDLE;
    }
    vkDestroyDevice(m_device, nullptr);
    m_device = VK_NULL_HANDLE;

    if (m_debugMessenger != VK_NULL_HANDLE) {
        auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyMessenger != nullptr) {
            destroyMessenger(m_instance, m_debugMessenger, nullptr);
        }
        m_debugMessenger = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    NOVA_LOG_INFO("vulkan: device destroyed");
}

// ---------------------------------------------------------------- instance

bool VulkanDevice::createInstance(const DeviceDesc& desc) {
    // --- validation layer (optional; warn and continue when absent) ---
    if (desc.enableValidation) {
        u32 layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> layers(layerCount);
        if (layerCount > 0) {
            vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
        }
        const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
        const bool present =
            std::any_of(layers.begin(), layers.end(), [&](const VkLayerProperties& layer) {
                return std::strcmp(layer.layerName, kValidationLayer) == 0;
            });
        if (present) {
            m_validationEnabled = true;
        } else {
            NOVA_LOG_WARN("vulkan: validation requested but %s is not available on this "
                          "device (retail builds usually omit it)",
                          kValidationLayer);
        }
    }

    std::vector<const char*> extensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
    if (m_validationEnabled) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // Drop extensions the loader does not know (defensive on odd OEM builds).
    u32 availableCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    if (availableCount > 0) {
        vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());
    }
    extensions.erase(
        std::remove_if(extensions.begin(), extensions.end(),
                       [&](const char* name) {
                           return !std::any_of(available.begin(), available.end(),
                                               [&](const VkExtensionProperties& props) {
                                                   return std::strcmp(props.extensionName,
                                                                      name) == 0;
                                               });
                       }),
        extensions.end());
    for (const char* required :
         {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME}) {
        if (std::none_of(extensions.begin(), extensions.end(),
                         [&](const char* name) { return std::strcmp(name, required) == 0; })) {
            NOVA_LOG_ERROR("vulkan: required instance extension %s not supported", required);
            return false;
        }
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = desc.appName;
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "NovaEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> layers;
    if (m_validationEnabled) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<u32>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<u32>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
        NOVA_LOG_ERROR(
            "vulkan: loader rejected API 1.3 — Nova Engine M1 requires a Vulkan 1.3 "
            "device (Android 12+ with up-to-date GPU drivers)");
        return false;
    }
    if (!vkutil::checkVk(result, "vkCreateInstance")) {
        return false;
    }

    if (m_validationEnabled) {
        auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
        if (createMessenger != nullptr) {
            VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
            messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            messengerInfo.pfnUserCallback = debugCallback;
            createMessenger(m_instance, &messengerInfo, nullptr, &m_debugMessenger);
        }
        m_setObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(m_instance, "vkSetDebugUtilsObjectNameEXT"));
    }
    return true;
}

// ------------------------------------------------------------- physical device

bool VulkanDevice::pickPhysicalDevice() {
    u32 count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) {
        NOVA_LOG_ERROR("vulkan: no physical devices found");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(candidate, &props);
        if (props.apiVersion < VK_API_VERSION_1_3) {
            NOVA_LOG_INFO("vulkan: skipping %s (API %u.%u < 1.3)", props.deviceName,
                          VK_API_VERSION_MAJOR(props.apiVersion),
                          VK_API_VERSION_MINOR(props.apiVersion));
            continue;
        }

        // Core-1.3 features are guaranteed but must be reported and enabled.
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(candidate, &features2);
        if (!features13.dynamicRendering || !features13.synchronization2) {
            NOVA_LOG_INFO("vulkan: skipping %s (dynamicRendering/synchronization2 off)",
                          props.deviceName);
            continue;
        }

        u32 queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
        for (u32 i = 0; i < queueCount; ++i) {
            if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                m_physicalDevice = candidate;
                m_queueFamily = i;
                m_deviceProperties = props;
                return true;
            }
        }
    }
    NOVA_LOG_ERROR("vulkan: no device exposes a graphics queue with Vulkan 1.3");
    return false;
}

bool VulkanDevice::createDevice() {
    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features13;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;

    if (!vkutil::checkVk(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device),
                         "vkCreateDevice")) {
        return false;
    }
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);

    VmaVulkanFunctions vmaFunctions{};
    vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;
    allocatorInfo.pVulkanFunctions = &vmaFunctions;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    if (!vkutil::checkVk(vmaCreateAllocator(&allocatorInfo, &m_allocator),
                         "vmaCreateAllocator")) {
        return false;
    }

    // Shared frame-UBO descriptor set layout (set 0, binding 0, VS|FS).
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;
    return vkutil::checkVk(
        vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_uboSetLayout),
        "vkCreateDescriptorSetLayout");
}

bool VulkanDevice::createFrameObjects() {
    for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
        FrameObjects& frame = m_frames[i];

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_queueFamily;
        if (!vkutil::checkVk(vkCreateCommandPool(m_device, &poolInfo, nullptr,
                                                 &frame.commandPool),
                             "vkCreateCommandPool")) {
            return false;
        }

        VkCommandBufferAllocateInfo cmdAlloc{};
        cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlloc.commandPool = frame.commandPool;
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandBufferCount = 1;
        if (!vkutil::checkVk(
                vkAllocateCommandBuffers(m_device, &cmdAlloc, &frame.commandBuffer),
                "vkAllocateCommandBuffers")) {
            return false;
        }

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (!vkutil::checkVk(vkCreateSemaphore(m_device, &semInfo, nullptr,
                                               &frame.imageAvailable),
                             "vkCreateSemaphore(imageAvailable)")) {
            return false;
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (!vkutil::checkVk(vkCreateFence(m_device, &fenceInfo, nullptr, &frame.inFlight),
                             "vkCreateFence")) {
            return false;
        }

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                      kDescriptorSetsPerFrame};
        VkDescriptorPoolCreateInfo descPoolInfo{};
        descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descPoolInfo.maxSets = kDescriptorSetsPerFrame;
        descPoolInfo.poolSizeCount = 1;
        descPoolInfo.pPoolSizes = &poolSize;
        if (!vkutil::checkVk(vkCreateDescriptorPool(m_device, &descPoolInfo, nullptr,
                                                    &frame.descriptorPool),
                             "vkCreateDescriptorPool")) {
            return false;
        }

        m_frameContexts[i] = {m_uboSetLayout, frame.descriptorPool};
        frame.recorder =
            makeUnique<VulkanCommandBuffer>(m_device, frame.commandBuffer,
                                            &m_frameContexts[i]);
        char name[48];
        std::snprintf(name, sizeof(name), "nova.frame%u.cmd", i);
        setDebugName(VK_OBJECT_TYPE_COMMAND_BUFFER,
                     reinterpret_cast<u64>(frame.commandBuffer), name);
    }
    setDebugName(VK_OBJECT_TYPE_DEVICE, reinterpret_cast<u64>(m_device), "nova.device");
    return true;
}

void VulkanDevice::destroyFrameObjects() {
    for (FrameObjects& frame : m_frames) {
        if (m_device != VK_NULL_HANDLE) {
            frame.recorder.reset();
            if (frame.descriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(m_device, frame.descriptorPool, nullptr);
            }
            if (frame.inFlight != VK_NULL_HANDLE) {
                vkDestroyFence(m_device, frame.inFlight, nullptr);
            }
            if (frame.imageAvailable != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_device, frame.imageAvailable, nullptr);
            }
            if (frame.commandPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(m_device, frame.commandPool, nullptr);
            }
        }
        frame = FrameObjects{};
    }
    for (VkSemaphore& semaphore : m_renderFinished) {
        if (m_device != VK_NULL_HANDLE && semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, semaphore, nullptr);
        }
    }
    m_renderFinished.clear();
    m_imagesInFlight.clear();
}

// ------------------------------------------------------------- surface / swapchain

bool VulkanDevice::createSurface() {
    NOVA_ENSURE(m_window != nullptr, "createSurface without a window");

    // The window must support presentation on our graphics queue (always
    // true on Android, checked for correctness on other platforms later).
    VkAndroidSurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.window = reinterpret_cast<ANativeWindow*>(m_window);
    if (!vkutil::checkVk(vkCreateAndroidSurfaceKHR(m_instance, &surfaceInfo, nullptr,
                                                   &m_surface),
                         "vkCreateAndroidSurfaceKHR")) {
        m_surface = VK_NULL_HANDLE;
        return false;
    }

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_queueFamily, m_surface,
                                         &supported);
    if (!supported) {
        NOVA_LOG_ERROR("vulkan: graphics queue family %u cannot present to this surface",
                       m_queueFamily);
        destroySurface();
        return false;
    }
    return true;
}

void VulkanDevice::destroySurface() {
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
}

bool VulkanDevice::createSwapchain(u32 width, u32 height) {
    NOVA_ENSURE(m_surface != VK_NULL_HANDLE, "createSwapchain without a surface");

    VkSurfaceCapabilitiesKHR caps{};
    if (!vkutil::checkVk(
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
        m_swapchainDirty = true;
        return false;
    }

    // --- surface format: prefer BGRA8 UNORM (typical on Android) ---
    u32 formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount > 0 ? formatCount : 1);
    if (formatCount > 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount,
                                             formats.data());
    } else {
        formats[0] = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }
    VkSurfaceFormatKHR chosen = formats[0];
    for (const VkSurfaceFormatKHR& candidate : formats) {
        if ((candidate.format == VK_FORMAT_B8G8R8A8_UNORM ||
             candidate.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = candidate;
            break;
        }
    }

    // --- present mode: requested, then fallbacks (FIFO always available) ---
    u32 modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount,
                                              nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    if (modeCount > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount,
                                                  modes.data());
    }
    const VkPresentModeKHR requested = vkutil::toVkPresentMode(m_requestedPresentMode);
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (std::find(modes.begin(), modes.end(), requested) != modes.end()) {
        presentMode = requested;
    } else if (requested != VK_PRESENT_MODE_FIFO_KHR &&
               std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_FIFO_KHR) !=
                   modes.end()) {
        NOVA_LOG_WARN("vulkan: present mode unavailable, falling back to FIFO");
        presentMode = VK_PRESENT_MODE_FIFO_KHR;
    }

    // --- extent ---
    VkExtent2D extent{};
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width = std::clamp(width, caps.minImageExtent.width,
                                  caps.maxImageExtent.width);
        extent.height = std::clamp(height, caps.minImageExtent.height,
                                   caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        // Window not laid out yet (e.g. mid-rotation): retry next frame.
        m_swapchainDirty = true;
        return false;
    }

    u32 imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        for (u32 bit = 0; bit < 32; ++bit) {
            const auto flag = static_cast<VkCompositeAlphaFlagBitsKHR>(1u << bit);
            if (caps.supportedCompositeAlpha & flag) {
                compositeAlpha = flag;
                break;
            }
        }
    }

    // Android exposes a single graphics+present family, so EXCLUSIVE sharing
    // is correct here (multi-family CONCURRENT is handled when the desktop
    // backend lands).
    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = m_surface;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = chosen.format;
    swapchainInfo.imageColorSpace = chosen.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.preTransform = caps.currentTransform;
    swapchainInfo.compositeAlpha = compositeAlpha;
    swapchainInfo.presentMode = presentMode;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = m_swapchain;  // optimized replace when recreating

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    if (!vkutil::checkVk(vkCreateSwapchainKHR(m_device, &swapchainInfo, nullptr,
                                              &newSwapchain),
                         "vkCreateSwapchainKHR")) {
        m_swapchainDirty = true;
        return false;
    }

    // Success: retire the previous swapchain (if any) and adopt the new one.
    destroySwapchain();
    m_swapchain = newSwapchain;
    m_swapchainFormat = chosen.format;
    m_swapchainExtent = extent;

    u32 imageTotal = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageTotal, nullptr);
    m_swapchainImages.resize(imageTotal);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageTotal, m_swapchainImages.data());

    m_swapchainViews.resize(imageTotal, VK_NULL_HANDLE);
    m_backbuffers.clear();
    m_backbuffers.reserve(imageTotal);
    m_renderFinished.resize(imageTotal, VK_NULL_HANDLE);
    m_imagesInFlight.assign(imageTotal, VK_NULL_HANDLE);

    const Format rhiFormat = vkutil::fromVkFormat(m_swapchainFormat);
    for (u32 i = 0; i < imageTotal; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_swapchainFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        if (!vkutil::checkVk(
                vkCreateImageView(m_device, &viewInfo, nullptr, &m_swapchainViews[i]),
                "vkCreateImageView(swapchain)")) {
            destroySwapchain();
            m_swapchainDirty = true;
            return false;
        }
        m_backbuffers.push_back(makeUnique<BackbufferTexture>(
            m_swapchainImages[i], m_swapchainViews[i], rhiFormat, extent.width,
            extent.height));

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (!vkutil::checkVk(
                vkCreateSemaphore(m_device, &semInfo, nullptr, &m_renderFinished[i]),
                "vkCreateSemaphore(renderFinished)")) {
            destroySwapchain();
            m_swapchainDirty = true;
            return false;
        }
    }

    m_swapchainDirty = false;
    m_resizePending = false;
    m_stats.width = extent.width;
    m_stats.height = extent.height;
    m_stats.imageCount = imageTotal;
    m_stats.presentMode = presentMode == VK_PRESENT_MODE_MAILBOX_KHR
                              ? PresentMode::Mailbox
                              : (presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR
                                     ? PresentMode::Immediate
                                     : PresentMode::Fifo);
    m_stats.validationEnabled = m_validationEnabled;

    NOVA_LOG_INFO("vulkan: swapchain %ux%u, %u images, format %s, present %s",
                  extent.width, extent.height, imageTotal,
                  formatName(vkutil::fromVkFormat(m_swapchainFormat)),
                  presentMode == VK_PRESENT_MODE_FIFO_KHR
                      ? "FIFO"
                      : (presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX"
                                                                    : "IMMEDIATE"));
    return true;
}

void VulkanDevice::destroySwapchain() {
    if (m_device == VK_NULL_HANDLE) return;
    for (VkImageView view : m_swapchainViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, view, nullptr);
        }
    }
    m_swapchainViews.clear();
    m_swapchainImages.clear();
    m_backbuffers.clear();
    for (VkSemaphore& semaphore : m_renderFinished) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, semaphore, nullptr);
        }
    }
    m_renderFinished.clear();
    m_imagesInFlight.clear();
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

bool VulkanDevice::recreateSwapchain() {
    if (m_surface == VK_NULL_HANDLE || m_device == VK_NULL_HANDLE) return false;
    vkDeviceWaitIdle(m_device);

    u32 width = m_pendingWidth;
    u32 height = m_pendingHeight;
    if (!m_resizePending) {
        // Not a user resize: use the current window metrics as a hint.
        width = m_stats.width;
        height = m_stats.height;
    }
    const bool created = createSwapchain(width, height);
    m_resizePending = false;
    return created;
}

// ------------------------------------------------------------------ surface API

void VulkanDevice::setSurface(void* nativeWindow, u32 width, u32 height) {
    if (m_device == VK_NULL_HANDLE) return;

    if (nativeWindow == nullptr) {
        // Contract: called with no frame in flight (engine serializes).
        waitIdle();
        destroySwapchain();
        destroySurface();
        m_window = nullptr;
        m_stats.width = 0;
        m_stats.height = 0;
        m_stats.imageCount = 0;
        NOVA_LOG_INFO("vulkan: surface detached");
        return;
    }

    if (nativeWindow == m_window && m_surface != VK_NULL_HANDLE) {
        setSurfaceSize(width, height);
        return;
    }

    waitIdle();
    destroySwapchain();
    destroySurface();
    m_window = nativeWindow;
    if (!createSurface()) {
        m_window = nullptr;
        return;
    }
    if (!createSwapchain(width, height)) {
        // Extent not ready yet; beginFrame will retry via m_swapchainDirty.
        NOVA_LOG_WARN("vulkan: initial swapchain creation pending (will retry)");
    }
}

void VulkanDevice::setSurfaceSize(u32 width, u32 height) {
    if (m_surface == VK_NULL_HANDLE || width == 0 || height == 0) return;
    if (width == m_stats.width && height == m_stats.height &&
        !m_swapchainDirty) {
        return;
    }
    m_pendingWidth = width;
    m_pendingHeight = height;
    m_resizePending = true;
    m_swapchainDirty = true;
}

void VulkanDevice::setPresentMode(PresentMode mode) {
    if (mode == m_requestedPresentMode) return;
    m_requestedPresentMode = mode;
    if (m_surface != VK_NULL_HANDLE) {
        m_swapchainDirty = true;  // applied on next beginFrame
    }
}

// ----------------------------------------------------------------- frame loop

bool VulkanDevice::acquireNextImage(u32& imageIndex) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        const VkResult result =
            vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                  m_frames[m_activeRing].imageAvailable, VK_NULL_HANDLE,
                                  &imageIndex);
        if (result == VK_SUCCESS) {
            return true;
        }
        if (result == VK_SUBOPTIMAL_KHR) {
            m_swapchainDirty = true;
            return true;
        }
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            if (!recreateSwapchain()) {
                return false;
            }
            continue;
        }
        vkutil::checkVk(result, "vkAcquireNextImageKHR");
        if (result == VK_ERROR_DEVICE_LOST) {
            m_deviceLost = true;
        }
        return false;
    }
    return false;
}

bool VulkanDevice::beginFrame(FrameInfo& out) {
    if (m_deviceLost || m_device == VK_NULL_HANDLE) return false;
    if (m_window == nullptr || m_swapchain == VK_NULL_HANDLE || m_swapchainDirty) {
        if (m_window != nullptr && m_swapchainDirty) {
            if (!recreateSwapchain()) return false;
        } else if (m_swapchain == VK_NULL_HANDLE) {
            return false;
        }
    }

    m_activeRing = static_cast<u32>(m_frameIndex % kMaxFramesInFlight);
    FrameObjects& frame = m_frames[m_activeRing];

    if (!vkutil::checkVk(vkWaitForFences(m_device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX),
                         "vkWaitForFences")) {
        m_deviceLost = true;
        return false;
    }

    // The ring slot is idle: safe to reset its transient state.
    vkResetDescriptorPool(m_device, frame.descriptorPool, 0);
    vkResetCommandPool(m_device, frame.commandPool, 0);

    u32 imageIndex = 0;
    if (!acquireNextImage(imageIndex)) {
        return false;
    }
    if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(m_device, 1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
    m_imagesInFlight[imageIndex] = frame.inFlight;

    m_activeImage = imageIndex;
    frame.recorder->beginRecording();
    m_frameOpen = true;

    out.commands = frame.recorder.get();
    out.backbuffer = m_backbuffers[imageIndex].get();
    out.width = m_swapchainExtent.width;
    out.height = m_swapchainExtent.height;
    out.colorFormat = vkutil::fromVkFormat(m_swapchainFormat);
    out.frameIndex = m_frameIndex;
    out.ringIndex = m_activeRing;
    m_stats.frameIndex = m_frameIndex;
    return true;
}

void VulkanDevice::endFrame() {
    if (!m_frameOpen) return;
    m_frameOpen = false;

    FrameObjects& frame = m_frames[m_activeRing];
    VulkanCommandBuffer* recorder = frame.recorder.get();
    if (vkEndCommandBuffer(recorder->handle()) != VK_SUCCESS) {
        NOVA_LOG_ERROR("vulkan: vkEndCommandBuffer failed");
        return;
    }

    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = recorder->handle();
    commandInfo.deviceMask = 0;

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = m_renderFinished[m_activeImage];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;

    vkResetFences(m_device, 1, &frame.inFlight);
    const VkResult submitResult = vkQueueSubmit2(m_queue, 1, &submitInfo, frame.inFlight);
    if (!vkutil::checkVk(submitResult, "vkQueueSubmit2")) {
        if (submitResult == VK_ERROR_DEVICE_LOST) m_deviceLost = true;
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinished[m_activeImage];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_activeImage;
    const VkResult presentResult = vkQueuePresentKHR(m_queue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        m_swapchainDirty = true;
    } else if (presentResult == VK_ERROR_DEVICE_LOST) {
        NOVA_LOG_ERROR("vulkan: device lost during present");
        m_deviceLost = true;
    } else {
        vkutil::checkVk(presentResult, "vkQueuePresentKHR");
    }

    ++m_frameIndex;
    m_stats.frameIndex = m_frameIndex;
}

void VulkanDevice::waitIdle() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
    m_frameOpen = false;
}

// ------------------------------------------------------------------ resources

Unique<IBuffer> VulkanDevice::createBuffer(const BufferDesc& desc) {
    auto buffer = makeUnique<VulkanBuffer>(m_allocator, desc);
    if (!buffer->isValid()) {
        return nullptr;
    }
    ++m_stats.buffersCreated;
    return buffer;
}

Unique<IShaderModule> VulkanDevice::createShaderModule(const ShaderModuleDesc& desc) {
    auto module = makeUnique<VulkanShaderModule>(m_device, desc);
    if (!module->isValid()) {
        return nullptr;
    }
    return module;
}

Unique<IPipeline> VulkanDevice::createPipeline(const PipelineDesc& desc) {
    NOVA_ENSURE(desc.colorAttachmentCount >= 1, "pipeline needs at least one color target");
    VulkanPipeline::CreateInfo info{};
    info.device = m_device;
    info.uboSetLayout = m_uboSetLayout;
    info.desc = &desc;
    auto pipeline = makeUnique<VulkanPipeline>(info);
    if (!pipeline->isValid()) {
        return nullptr;
    }
    ++m_stats.pipelinesCreated;
    return pipeline;
}

// ------------------------------------------------------------------- factory

nova::Unique<IDevice> createVulkanDevice(const DeviceDesc& desc) {
    auto device = makeUnique<VulkanDevice>(desc);
    if (!device->isValid()) {
        return nullptr;
    }
    return device;
}

}  // namespace nova::rhi
