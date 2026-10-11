#pragma once

// Nova Engine — Vulkan resource implementations (internal backend header).
//
// These classes wrap raw Vulkan objects with RAII and implement the public
// RHI interfaces. Creation failures leave the object invalid (isValid() ==
// false) and log the VkResult; the owning device never hands out objects it
// did not validate.

#include "rhi/RhiResources.h"
#include "rhi/vulkan/VulkanCommon.h"

#include <vk_mem_alloc.h>

#include <vector>

namespace nova::rhi {

/// Backend-internal texture interface: exposes raw image handles to the
/// command recorder without leaking them into the public RHI headers.
class VulkanTextureBase : public ITexture {
public:
    virtual VkImage imageHandle() const = 0;
    virtual VkImageView viewHandle() const = 0;
};

/// RAII VkBuffer + VMA allocation. HostVisible buffers stay persistently
/// mapped for the buffer's lifetime.
class VulkanBuffer final : public IBuffer {
public:
    VulkanBuffer(VmaAllocator allocator, const BufferDesc& desc);
    ~VulkanBuffer() override;

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    bool isValid() const { return m_buffer != VK_NULL_HANDLE; }
    VkBuffer handle() const { return m_buffer; }

    const BufferDesc& desc() const override { return m_desc; }
    void* mappedData() const override { return m_mapped; }

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    void* m_mapped = nullptr;
    BufferDesc m_desc{};
};

/// RAII VkShaderModule. The SPIR-V bytes are only needed during creation.
class VulkanShaderModule final : public IShaderModule {
public:
    VulkanShaderModule(VkDevice device, const ShaderModuleDesc& desc);
    ~VulkanShaderModule() override;

    VulkanShaderModule(const VulkanShaderModule&) = delete;
    VulkanShaderModule& operator=(const VulkanShaderModule&) = delete;

    bool isValid() const { return m_module != VK_NULL_HANDLE; }
    VkShaderModule handle() const { return m_module; }

    ShaderStage stage() const override { return m_desc.stage; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkShaderModule m_module = VK_NULL_HANDLE;
    ShaderModuleDesc m_desc{};
};

/// RAII graphics pipeline created for a specific set of attachment formats
/// (dynamic rendering: renderPass == VK_NULL_HANDLE).
class VulkanPipeline final : public IPipeline {
public:
    struct CreateInfo {
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorSetLayout uboSetLayout = VK_NULL_HANDLE;  // set 0, binding 0
        const PipelineDesc* desc = nullptr;
    };

    explicit VulkanPipeline(const CreateInfo& info);
    ~VulkanPipeline() override;

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    bool isValid() const { return m_pipeline != VK_NULL_HANDLE; }
    VkPipeline handle() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout; }

    const PipelineDesc& desc() const override { return m_desc; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    PipelineDesc m_desc{};
};

}  // namespace nova::rhi
