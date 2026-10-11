#include "rhi/vulkan/VulkanResources.h"

#include "core/Assert.h"

namespace nova::rhi {
namespace {

VkBufferUsageFlags toVkBufferUsage(BufferUsage usage) {
    switch (usage) {
        case BufferUsage::Vertex: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        case BufferUsage::Index: return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        case BufferUsage::Uniform: return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        case BufferUsage::Storage: return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    return 0;
}

VkShaderStageFlags toVkShaderStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return 0;
}

}  // namespace

// ---------------------------------------------------------------- VulkanBuffer

VulkanBuffer::VulkanBuffer(VmaAllocator allocator, const BufferDesc& desc)
    : m_allocator(allocator), m_desc(desc) {
    NOVA_ENSURE(allocator != VK_NULL_HANDLE, "VulkanBuffer: allocator is null");
    NOVA_ENSURE(desc.size > 0, "VulkanBuffer: size must be > 0 (%s)",
                desc.debugName != nullptr ? desc.debugName : "?");

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = toVkBufferUsage(desc.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    if (desc.domain == BufferDomain::HostVisible) {
        // Persistently mapped, CPU-written every frame (frame UBOs).
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    } else {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocInfo.flags = 0;
    }

    VmaAllocationInfo allocationInfo{};
    const VkResult result =
        vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &m_buffer, &m_allocation,
                        &allocationInfo);
    if (!vkutil::checkVk(result, "vmaCreateBuffer")) {
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        return;
    }
    m_mapped = allocationInfo.pMappedData;
}

VulkanBuffer::~VulkanBuffer() {
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    }
}

// ---------------------------------------------------------- VulkanShaderModule

VulkanShaderModule::VulkanShaderModule(VkDevice device, const ShaderModuleDesc& desc)
    : m_device(device), m_desc(desc) {
    NOVA_ENSURE(device != VK_NULL_HANDLE, "VulkanShaderModule: device is null");
    NOVA_ENSURE(!desc.spirv.empty(), "VulkanShaderModule: empty SPIR-V (%s)",
                desc.debugName != nullptr ? desc.debugName : "?");

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = desc.spirv.size() * sizeof(nova::core::u32);
    info.pCode = desc.spirv.data();
    const VkResult result = vkCreateShaderModule(device, &info, nullptr, &m_module);
    if (!vkutil::checkVk(result, "vkCreateShaderModule")) {
        m_module = VK_NULL_HANDLE;
    }
}

VulkanShaderModule::~VulkanShaderModule() {
    if (m_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device, m_module, nullptr);
    }
}

// ------------------------------------------------------------- VulkanPipeline

VulkanPipeline::VulkanPipeline(const CreateInfo& info) {
    NOVA_ENSURE(info.device != VK_NULL_HANDLE, "VulkanPipeline: device is null");
    NOVA_ENSURE(info.desc != nullptr, "VulkanPipeline: desc is null");
    NOVA_ENSURE(info.desc->vertexShader != nullptr && info.desc->fragmentShader != nullptr,
                "VulkanPipeline: both shader stages are required");
    NOVA_ENSURE(info.uboSetLayout != VK_NULL_HANDLE, "VulkanPipeline: ubo set layout is null");

    m_device = info.device;
    m_desc = *info.desc;

    // --- pipeline layout (set 0 = frame UBO, shared by all M1 pipelines) ---
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &info.uboSetLayout;
    if (!vkutil::checkVk(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_layout),
                         "vkCreatePipelineLayout")) {
        m_layout = VK_NULL_HANDLE;
        return;
    }

    // --- shader stages ---
    const auto* vsModule = static_cast<const VulkanShaderModule*>(m_desc.vertexShader);
    const auto* fsModule = static_cast<const VulkanShaderModule*>(m_desc.fragmentShader);
    NOVA_ENSURE(vsModule->stage() == ShaderStage::Vertex,
                "VulkanPipeline: vertexShader is not a vertex stage module");
    NOVA_ENSURE(fsModule->stage() == ShaderStage::Fragment,
                "VulkanPipeline: fragmentShader is not a fragment stage module");

    // Stage bits come from the modules themselves so a swapped or compute
    // module is reported by the ensures above instead of silently producing a
    // broken pipeline.
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = static_cast<VkShaderStageFlagBits>(toVkShaderStage(vsModule->stage()));
    stages[0].module = vsModule->handle();
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = static_cast<VkShaderStageFlagBits>(toVkShaderStage(fsModule->stage()));
    stages[1].module = fsModule->handle();
    stages[1].pName = "main";

    // --- vertex input ---
    VkVertexInputBindingDescription binding{};
    std::array<VkVertexInputAttributeDescription, 8> attributes{};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (m_desc.vertexBinding.attributeCount > 0) {
        binding.binding = m_desc.vertexBinding.binding;
        binding.stride = m_desc.vertexBinding.stride;
        binding.inputRate = m_desc.vertexBinding.rate == VertexRate::PerInstance
                                ? VK_VERTEX_INPUT_RATE_INSTANCE
                                : VK_VERTEX_INPUT_RATE_VERTEX;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        for (u32 i = 0; i < m_desc.vertexBinding.attributeCount; ++i) {
            const VertexAttribute& attr = m_desc.vertexBinding.attributes[i];
            attributes[i].location = attr.location;
            attributes[i].binding = m_desc.vertexBinding.binding;
            attributes[i].format = vkutil::toVkFormat(attr.format);
            attributes[i].offset = attr.offset;
        }
        vertexInput.vertexAttributeDescriptionCount = m_desc.vertexBinding.attributeCount;
        vertexInput.pVertexAttributeDescriptions = attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = vkutil::toVkTopology(m_desc.topology);

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = vkutil::toVkCullMode(m_desc.cull);
    // The projection flips Y (nova::math::perspectiveVk), which inverts
    // winding in clip space: front faces of Y-up content are clockwise.
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    const bool withDepth = m_desc.depthFormat != Format::Unknown;
    if (withDepth) {
        depthStencil.depthTestEnable = m_desc.depthTest ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = m_desc.depthWrite ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = vkutil::toVkCompareOp(m_desc.depthCompare);
    }

    std::array<VkPipelineColorBlendAttachmentState, kMaxColorAttachments> blendAttachments{};
    for (u32 i = 0; i < m_desc.colorAttachmentCount; ++i) {
        auto& blend = blendAttachments[i];
        blend.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (m_desc.blend == BlendMode::AlphaBlend) {
            blend.blendEnable = VK_TRUE;
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.colorBlendOp = VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alphaBlendOp = VK_BLEND_OP_ADD;
        } else {
            blend.blendEnable = VK_FALSE;
        }
    }

    VkPipelineColorBlendStateCreateInfo blendState{};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount = m_desc.colorAttachmentCount;
    blendState.pAttachments = blendAttachments.data();

    const VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT,
                                             VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // --- dynamic rendering: formats instead of a render pass ---
    std::array<VkFormat, kMaxColorAttachments> colorFormats{};
    for (u32 i = 0; i < m_desc.colorAttachmentCount; ++i) {
        colorFormats[i] = vkutil::toVkFormat(m_desc.colorFormats[i]);
    }
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = m_desc.colorAttachmentCount;
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    renderingInfo.depthAttachmentFormat =
        withDepth ? vkutil::toVkFormat(m_desc.depthFormat) : VK_FORMAT_UNDEFINED;
    renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    createInfo.pNext = &renderingInfo;
    createInfo.stageCount = 2;
    createInfo.pStages = stages;
    createInfo.pVertexInputState = &vertexInput;
    createInfo.pInputAssemblyState = &inputAssembly;
    createInfo.pViewportState = &viewport;
    createInfo.pRasterizationState = &raster;
    createInfo.pMultisampleState = &multisample;
    createInfo.pDepthStencilState = withDepth ? &depthStencil : nullptr;
    createInfo.pColorBlendState = &blendState;
    createInfo.pDynamicState = &dynamicState;
    createInfo.layout = m_layout;
    createInfo.renderPass = VK_NULL_HANDLE;

    const VkResult result =
        vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &createInfo, nullptr,
                                  &m_pipeline);
    if (!vkutil::checkVk(result, "vkCreateGraphicsPipelines")) {
        m_pipeline = VK_NULL_HANDLE;
    }
}

VulkanPipeline::~VulkanPipeline() {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    }
}

}  // namespace nova::rhi
