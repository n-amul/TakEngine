#pragma once
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>

#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "TextureManager.hpp"
#include "core/utils.hpp"
#include "defines.hpp"

struct UI {
  std::shared_ptr<TextureManager> textureManager;
  VkDevice device{VK_NULL_HANDLE};

 public:
  BufferManager::Buffer vertexBuffer, indexBuffer;
  TextureManager::Texture fontTexture;
  VkPipelineLayout pipelineLayout;
  VkPipeline pipeline;
  VkDescriptorPool descriptorPool;
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorSet descriptorSet;
  float updateTimer = 0.0f;
  int32_t vertexCount = 0;
  int32_t indexCount = 0;

  std::unordered_map<VkImageView, VkDescriptorSet> textureDescriptorSets;

  struct PushConstBlock {
    glm::vec2 scale;
    glm::vec2 translate;
  } pushConstBlock;

  UI(std::shared_ptr<TextureManager> textureManager, VkRenderPass renderPass, VkSampleCountFlagBits multiSampleCount,
     const std::string& shaderDir, GLFWwindow* window)  // Added window parameter
      : textureManager(textureManager) {
    device = textureManager->context->device;

    // Create ImGui context
    ImGui::CreateContext();

    // Initialize ImGui for GLFW - this handles ALL input automatically
    ImGui_ImplGlfw_InitForVulkan(window, true);  // true = install callbacks

    // Configure ImGui
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Font texture loading
    unsigned char* fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    fontTexture = textureManager->createTextureFromBuffer(fontData, texWidth * texHeight * 4 * sizeof(char),
                                                          VK_FORMAT_R8G8B8A8_UNORM, texWidth, texHeight);

    // Setup style
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameBorderSize = 0.0f;
    style.WindowBorderSize = 0.0f;

    // Descriptor pool - increased for multiple textures
    std::vector<VkDescriptorPoolSize> poolSizes = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100}};
    VkDescriptorPoolCreateInfo descriptorPoolCI{};
    descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptorPoolCI.poolSizeCount = 1;
    descriptorPoolCI.pPoolSizes = poolSizes.data();
    descriptorPoolCI.maxSets = 100;
    vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool);

    // Descriptor set layout
    VkDescriptorSetLayoutBinding setLayoutBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                  VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
    descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCI.pBindings = &setLayoutBinding;
    descriptorSetLayoutCI.bindingCount = 1;
    vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayout);

    // Descriptor set for font
    VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
    descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocInfo.descriptorPool = descriptorPool;
    descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
    descriptorSetAllocInfo.descriptorSetCount = 1;
    vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSet);

    VkWriteDescriptorSet writeDescriptorSet{};
    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.dstSet = descriptorSet;
    writeDescriptorSet.dstBinding = 0;
    writeDescriptorSet.pImageInfo = &fontTexture.descriptor;
    vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

    textureDescriptorSets[fontTexture.imageView] = descriptorSet;

    // Pipeline layout
    VkPushConstantRange pushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock)};

    VkPipelineLayoutCreateInfo pipelineLayoutCI{};
    pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCI.pushConstantRangeCount = 1;
    pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
    pipelineLayoutCI.setLayoutCount = 1;
    pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
    vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout);

    // Pipeline creation (same as before)
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
    inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
    rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
    rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationStateCI.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState blendAttachmentState{};
    blendAttachmentState.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachmentState.blendEnable = VK_TRUE;
    blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
    colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendStateCI.attachmentCount = 1;
    colorBlendStateCI.pAttachments = &blendAttachmentState;

    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
    depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilStateCI.depthTestEnable = VK_FALSE;
    depthStencilStateCI.depthWriteEnable = VK_FALSE;
    depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencilStateCI.front = depthStencilStateCI.back;
    depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineViewportStateCreateInfo viewportStateCI{};
    viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportStateCI.viewportCount = 1;
    viewportStateCI.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
    multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleStateCI.rasterizationSamples =
        (multiSampleCount > VK_SAMPLE_COUNT_1_BIT) ? multiSampleCount : VK_SAMPLE_COUNT_1_BIT;

    std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicStateCI{};
    dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
    dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

    VkVertexInputBindingDescription vertexInputBinding = {0, 20, VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * 4},
    };
    VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
    vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputStateCI.vertexBindingDescriptionCount = 1;
    vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
    vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
    vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

    VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.layout = pipelineLayout;
    pipelineCI.renderPass = renderPass;
    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    pipelineCI.pVertexInputState = &vertexInputStateCI;
    pipelineCI.pRasterizationState = &rasterizationStateCI;
    pipelineCI.pColorBlendState = &colorBlendStateCI;
    pipelineCI.pMultisampleState = &multisampleStateCI;
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pDepthStencilState = &depthStencilStateCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();

    auto vertCode = readFile(shaderDir + "/ui.vert.spv");
    auto fragCode = readFile(shaderDir + "/ui.frag.spv");

    auto createShaderModule = [&](const std::vector<char>& code) -> VkShaderModule {
      VkShaderModuleCreateInfo createInfo{};
      createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      createInfo.codeSize = code.size();
      createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
      VkShaderModule shaderModule;
      if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
      }
      return shaderModule;
    };
    VkShaderModule vertShaderModule = createShaderModule(vertCode);
    VkShaderModule fragShaderModule = createShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    shaderStages = {vertShaderStageInfo, fragShaderStageInfo};
    vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineCI, nullptr, &pipeline);

    for (auto shaderStage : shaderStages) {
      vkDestroyShaderModule(device, shaderStage.module, nullptr);
    }
  }

  ~UI() {
    // Shutdown ImGui GLFW backend
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    textureManager->bufferManager->destroyBuffer(vertexBuffer);
    textureManager->bufferManager->destroyBuffer(indexBuffer);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
  }

  // Call this at the start of each frame before any ImGui calls
  void newFrame() {
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }

  ImTextureID addTexture(VkSampler sampler, VkImageView imageView) {
    auto it = textureDescriptorSets.find(imageView);
    if (it != textureDescriptorSets.end()) {
      return (ImTextureID)it->second;
    }

    VkDescriptorSet texDescSet;
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.pSetLayouts = &descriptorSetLayout;
    allocInfo.descriptorSetCount = 1;
    vkAllocateDescriptorSets(device, &allocInfo, &texDescSet);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writeDesc{};
    writeDesc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDesc.dstSet = texDescSet;
    writeDesc.dstBinding = 0;
    writeDesc.descriptorCount = 1;
    writeDesc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDesc.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &writeDesc, 0, nullptr);

    textureDescriptorSets[imageView] = texDescSet;
    return (ImTextureID)texDescSet;
  }

  void draw(VkCommandBuffer cmdBuffer) {
    ImDrawData* imDrawData = ImGui::GetDrawData();
    if (!imDrawData || imDrawData->TotalVtxCount == 0 || vertexBuffer.buffer == VK_NULL_HANDLE) {
      return;
    }

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    const VkDeviceSize offsets[1] = {0};
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vertexBuffer.buffer, offsets);
    vkCmdBindIndexBuffer(cmdBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(UI::PushConstBlock),
                       &pushConstBlock);

    int32_t vertexOffset = 0;
    int32_t indexOffset = 0;
    for (int32_t j = 0; j < imDrawData->CmdListsCount; j++) {
      const ImDrawList* cmd_list = imDrawData->CmdLists[j];
      for (int32_t k = 0; k < cmd_list->CmdBuffer.Size; k++) {
        const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[k];

        VkDescriptorSet texDescSet = pcmd->TextureId ? (VkDescriptorSet)pcmd->TextureId : descriptorSet;
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &texDescSet, 0, nullptr);

        VkRect2D scissorRect;
        scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
        scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
        scissorRect.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
        scissorRect.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
        vkCmdSetScissor(cmdBuffer, 0, 1, &scissorRect);
        vkCmdDrawIndexed(cmdBuffer, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
        indexOffset += pcmd->ElemCount;
      }
      vertexOffset += cmd_list->VtxBuffer.Size;
    }
  }

  template <typename T>
  bool checkbox(const char* caption, T* value) {
    bool val = (*value == 1);
    bool res = ImGui::Checkbox(caption, &val);
    *value = val;
    return res;
  }
  bool header(const char* caption) { return ImGui::CollapsingHeader(caption, ImGuiTreeNodeFlags_DefaultOpen); }
  bool slider(const char* caption, float* value, float min, float max) {
    return ImGui::SliderFloat(caption, value, min, max);
  }
  bool combo(const char* caption, int32_t* itemindex, std::vector<std::string> items) {
    if (items.empty()) return false;
    std::vector<const char*> charitems;
    charitems.reserve(items.size());
    for (size_t i = 0; i < items.size(); i++) {
      charitems.push_back(items[i].c_str());
    }
    uint32_t itemCount = static_cast<uint32_t>(charitems.size());
    return ImGui::Combo(caption, itemindex, &charitems[0], itemCount, itemCount);
  }
  bool button(const char* caption) { return ImGui::Button(caption); }
  void text(const char* formatstr, ...) {
    va_list args;
    va_start(args, formatstr);
    ImGui::TextV(formatstr, args);
    va_end(args);
  }

  void updateBuffers() {
    ImDrawData* imDrawData = ImGui::GetDrawData();
    if (!imDrawData || imDrawData->TotalVtxCount == 0) {
      return;
    }
    VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
    VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

    if (vertexBuffer.buffer == VK_NULL_HANDLE || vertexCount < imDrawData->TotalVtxCount) {
      if (vertexBuffer.buffer != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        textureManager->bufferManager->destroyBuffer(vertexBuffer);
      }
      vertexBuffer = textureManager->bufferManager->createBuffer(
          vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
      vertexCount = imDrawData->TotalVtxCount;
    }

    if (indexBuffer.buffer == VK_NULL_HANDLE || indexCount < imDrawData->TotalIdxCount) {
      if (indexBuffer.buffer != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        textureManager->bufferManager->destroyBuffer(indexBuffer);
      }
      indexBuffer = textureManager->bufferManager->createBuffer(
          indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
      indexCount = imDrawData->TotalIdxCount;
    }

    VkDeviceSize offsetVert = 0, offsetIdx = 0;
    for (int n = 0; n < imDrawData->CmdListsCount; n++) {
      const ImDrawList* cmdList = imDrawData->CmdLists[n];
      VkDeviceSize vtxSize = cmdList->VtxBuffer.Size * sizeof(ImDrawVert);
      VkDeviceSize idxSize = cmdList->IdxBuffer.Size * sizeof(ImDrawIdx);
      textureManager->bufferManager->updateBuffer(vertexBuffer, cmdList->VtxBuffer.Data, vtxSize, offsetVert);
      textureManager->bufferManager->updateBuffer(indexBuffer, cmdList->IdxBuffer.Data, idxSize, offsetIdx);
      offsetVert += vtxSize;
      offsetIdx += idxSize;
    }
  }
};