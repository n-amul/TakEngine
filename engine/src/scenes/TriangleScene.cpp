#include "TriangleScene.hpp"

#include <spdlog/spdlog.h>

#include <cstring>
#include <filesystem>
#include <stdexcept>

#include "core/utils.hpp"

TriangleScene::TriangleScene() {
  window_width = 1920;
  window_height = 1080;
  title = "Vulkan Triangle Scene";
  name = "TriangleScene";
}
void TriangleScene::loadResources() {
  initializePBREnvironment();
  loadEnvironment(std::string(TEXTURE_DIR) + "/skybox/workshop.hdr");
  skybox = modelManager->createModelFromFile(std::string(MODEL_DIR) + "/box/box.gltf");
  spdlog::info("Loading triangle resources");
  // scene objs
  createDescriptorSetLayout();
  createTextures();
  createVertexBuffer();
  createIndexBuffer();
  createUniformBuffers();
  createDescriptorPool();
  createDescriptorSets();
}

void TriangleScene::createPipeline() {
  spdlog::info("Creating triangle pipeline");
  VkPipelineShaderStageCreateInfo shaderStages[] = {
      loadShader(std::string(SHADER_DIR) + "/triangle.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
      loadShader(std::string(SHADER_DIR) + "/triangle.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)};

  // Vertex input configuration
  auto bindingDescription = Vertex::getBindingDescription();
  auto attributeDescriptions = Vertex::getAttributeDescriptions();

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

  // Input assembly
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  // Viewport state (dynamic)
  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  // Rasterizer
  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  rasterizer.depthBiasConstantFactor = 0.0f;
  rasterizer.depthBiasClamp = 0.0f;
  rasterizer.depthBiasSlopeFactor = 0.0f;

  // Multisampling
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  if (this->multisampling) {
    multisampling.rasterizationSamples = this->msaaSamples;
  }

  // Color blending
  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;
  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = VK_LOGIC_OP_COPY;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;
  colorBlending.blendConstants[0] = 0.0f;
  colorBlending.blendConstants[1] = 0.0f;
  colorBlending.blendConstants[2] = 0.0f;
  colorBlending.blendConstants[3] = 0.0f;

  // Dynamic state - viewport and scissor will be set at draw time
  std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  // depth and stencil
  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  // Pipeline layout
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 0;  // No push constants for now
  pipelineLayoutInfo.pPushConstantRanges = nullptr;

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create pipeline layout!");
  }

  // Create graphics pipeline
  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = pipelineLayout;
  pipelineInfo.renderPass = renderPass;  // Use base class render pass
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineInfo.basePipelineIndex = -1;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create graphics pipeline!");
  }

  // Cleanup shader modules
  vkDestroyShaderModule(device, shaderStages[0].module, nullptr);
  vkDestroyShaderModule(device, shaderStages[1].module, nullptr);

  spdlog::info("Triangle pipeline created successfully");
  createSkyboxPipeline();
}

void TriangleScene::createSkyboxPipeline() {
  spdlog::info("Creating skybox pipeline");
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
  blendAttachmentState.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
  colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendStateCI.attachmentCount = 1;
  colorBlendStateCI.pAttachments = &blendAttachmentState;

  VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
  depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencilStateCI.depthTestEnable = VK_FALSE;
  depthStencilStateCI.depthWriteEnable = VK_FALSE;
  depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  depthStencilStateCI.depthBoundsTestEnable = VK_FALSE;
  depthStencilStateCI.stencilTestEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewportStateCI{};
  viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportStateCI.viewportCount = 1;
  viewportStateCI.scissorCount = 1;

  VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
  multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  if (multisampling) {
    multisampleStateCI.rasterizationSamples = msaaSamples;
  }

  std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicStateCI{};
  dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
  dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

  VkPipelineLayoutCreateInfo pipelineLayoutCI{};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount = 1;
  pipelineLayoutCI.pSetLayouts = &skyboxDescriptorSetLayout;
  pipelineLayoutCI.pushConstantRangeCount = 0;
  pipelineLayoutCI.pPushConstantRanges = nullptr;
  VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &skyboxPipelineLayout));

  // Vertex bindings and attributes
  VkVertexInputBindingDescription vertexInputBinding = tak::Vertex::getBindingDescription();
  std::array<VkVertexInputAttributeDescription, 8> vertexInputAttributes = tak::Vertex::getAttributeDescriptions();

  VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
  vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputStateCI.vertexBindingDescriptionCount = 1;
  vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
  vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
  vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

  // Pipelines
  std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
  shaderStages[0] = loadShader(std::string(SHADER_DIR) + "/skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
  shaderStages[1] = loadShader(std::string(SHADER_DIR) + "/skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

  VkGraphicsPipelineCreateInfo pipelineCI{};
  pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineCI.layout = skyboxPipelineLayout;
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
  pipelineCI.subpass = 0;
  pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
  pipelineCI.basePipelineIndex = -1;

  // Fixed: Use pipelineCI instead of pipelineInfo
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &skyboxPipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create skybox graphics pipeline!");
  }

  // Cleanup shader modules
  vkDestroyShaderModule(device, shaderStages[0].module, nullptr);
  vkDestroyShaderModule(device, shaderStages[1].module, nullptr);

  spdlog::info("Skybox pipeline created successfully");
}

void TriangleScene::createVertexBuffer() {
  VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
  vertexBuffer = bufferManager->createGPULocalBuffer(vertices.data(), bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void TriangleScene::createIndexBuffer() {
  VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
  indexBuffer = bufferManager->createGPULocalBuffer(indices.data(), bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}
void TriangleScene::createDescriptorPool() {
  std::array<VkDescriptorPoolSize, 2> poolSizes = {
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 4)},
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 4)}};

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  // set will contain ubo and sampler we need for each frame
  poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 4);

  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor pool!");
  }
}

void TriangleScene::createDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding uboLayoutBinding{};
  uboLayoutBinding.binding = 0;
  uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboLayoutBinding.descriptorCount = 1;
  uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  uboLayoutBinding.pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutBinding samplerLayoutBinding{};
  samplerLayoutBinding.binding = 1;
  samplerLayoutBinding.descriptorCount = 1;
  samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  samplerLayoutBinding.pImmutableSamplers = nullptr;
  samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};
  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
  // skybox
  std::array<VkDescriptorSetLayoutBinding, 3> bindingsSkybox = {
      VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
      VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
      VkDescriptorSetLayoutBinding{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};

  VkDescriptorSetLayoutCreateInfo layoutInfoSkybox{};
  layoutInfoSkybox.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfoSkybox.bindingCount = static_cast<uint32_t>(bindingsSkybox.size());
  layoutInfoSkybox.pBindings = bindingsSkybox.data();

  if (vkCreateDescriptorSetLayout(device, &layoutInfoSkybox, nullptr, &skyboxDescriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}

void TriangleScene::createDescriptorSets() {
  // scene obj
  {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      VkDescriptorBufferInfo bufferInfo{};
      bufferInfo.buffer = uniformBuffers[i].buffer;
      bufferInfo.offset = 0;
      bufferInfo.range = sizeof(UniformBufferObject);

      VkDescriptorImageInfo imageInfo{};
      imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      // texture passing
      imageInfo.imageView = rectTexture.imageView;
      imageInfo.sampler = rectTexture.sampler;

      std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

      descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[0].dstSet = descriptorSets[i];
      descriptorWrites[0].dstBinding = 0;
      descriptorWrites[0].dstArrayElement = 0;
      descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptorWrites[0].descriptorCount = 1;
      descriptorWrites[0].pBufferInfo = &bufferInfo;

      descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[1].dstSet = descriptorSets[i];
      descriptorWrites[1].dstBinding = 1;
      descriptorWrites[1].dstArrayElement = 0;
      descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      descriptorWrites[1].descriptorCount = 1;
      descriptorWrites[1].pImageInfo = &imageInfo;

      vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
  }
  // skybox
  {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, skyboxDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    skyboxDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, skyboxDescriptorSets.data()) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      VkDescriptorBufferInfo skyboxUBOBufferInfo{};
      skyboxUBOBufferInfo.buffer = skyboxUniformBuffers[i].buffer;  // per-frame skybox UBO
      skyboxUBOBufferInfo.offset = 0;
      skyboxUBOBufferInfo.range = sizeof(UniformBufferSkybox);
      VkDescriptorBufferInfo skyboxParamBufferInfo{};
      skyboxParamBufferInfo.buffer = skyBoxParamBuffer.buffer;
      skyboxParamBufferInfo.offset = 0;
      skyboxParamBufferInfo.range = sizeof(uboParamsSkybox);

      VkDescriptorImageInfo skyboxImageInfo{};
      skyboxImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      skyboxImageInfo.imageView = pbrEnvironment.environmentCube.imageView;
      skyboxImageInfo.sampler = pbrEnvironment.environmentCube.sampler;

      // --- Write descriptors (binding 0 = UBO, 1 = params UBO, 2 = combined image sampler) ---
      std::array<VkWriteDescriptorSet, 3> descriptorWrites{};
      // binding 0 : per-frame skybox UBO (vertex)
      descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[0].dstSet = skyboxDescriptorSets[i];
      descriptorWrites[0].dstBinding = 0;
      descriptorWrites[0].dstArrayElement = 0;
      descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptorWrites[0].descriptorCount = 1;
      descriptorWrites[0].pBufferInfo = &skyboxUBOBufferInfo;
      // binding 1 : params UBO (fragment)
      descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[1].dstSet = skyboxDescriptorSets[i];
      descriptorWrites[1].dstBinding = 1;
      descriptorWrites[1].dstArrayElement = 0;
      descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptorWrites[1].descriptorCount = 1;
      descriptorWrites[1].pBufferInfo = &skyboxParamBufferInfo;
      // binding 2 : cubemap sampler (fragment)
      descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[2].dstSet = skyboxDescriptorSets[i];
      descriptorWrites[2].dstBinding = 2;
      descriptorWrites[2].dstArrayElement = 0;
      descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      descriptorWrites[2].descriptorCount = 1;
      descriptorWrites[2].pImageInfo = &skyboxImageInfo;

      vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
  }
}

void TriangleScene::createUniformBuffers() {
  VkDeviceSize bufferSize = sizeof(UniformBufferObject);
  uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    uniformBuffers[i] =
        bufferManager->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkMapMemory(device, uniformBuffers[i].memory, 0, bufferSize, 0, &uniformBuffersMapped[i]);
  }
  // skybox
  VkDeviceSize skyBoxBufferSize = sizeof(UniformBufferSkybox);
  skyboxUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  skyboxUniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    skyboxUniformBuffers[i] =
        bufferManager->createBuffer(skyBoxBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
  }
  VkDeviceSize skyBoxParamSize = sizeof(uboParamsSkybox);
  skyBoxParamBuffer = bufferManager->createGPULocalBuffer(
      &skyBoxParamSize, skyBoxParamSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
}
// +Z = up, +X = right, +Y = forward
void TriangleScene::updateUniformBuffer(f32 deltatime) {
  // TODO: need to move this to push constant
  totalTime += deltatime;  // accumlate time
  UniformBufferObject ubo{};
  // ubo.model = glm::rotate(glm::mat4(1.0f), totalTime * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  ubo.model = glm::mat4(1.0f);
  ubo.view = camera.getViewMatrix();
  float aspectRatio = swapChainExtent.width / static_cast<float>(swapChainExtent.height);
  ubo.proj = camera.getProjectionMatrix(aspectRatio);

  memcpy(uniformBuffersMapped[currentFrame], &ubo, sizeof(ubo));

  UniformBufferSkybox skyboxUBO{};
  skyboxUBO.proj = ubo.proj;  // Same projection as main scene
  // For skybox, we want the view matrix without translation
  // This keeps the skybox centered around the camera
  glm::mat4 viewWithoutTranslation = glm::mat4(glm::mat3(camera.getViewMatrix()));
  skyboxUBO.model = viewWithoutTranslation;

  // Write to skybox uniform buffer using staged update
  bufferManager->updateBuffer(skyboxUniformBuffers[currentFrame], &skyboxUBO, sizeof(skyboxUBO));
}

void TriangleScene::createTextures() {
  rectTexture = textureManager->createTextureFromFile(std::string(TEXTURE_DIR) + "/cuteCat.jpg");
}

void TriangleScene::recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
  // Note: Render pass is already begun in base class recordCommandBuffer()
  // We just need to bind pipeline and draw
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(swapChainExtent.width);
  viewport.height = static_cast<float>(swapChainExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = swapChainExtent;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
  VkDeviceSize offsets[] = {0};

  // draw skybox:
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipelineLayout, 0, 1,
                          &skyboxDescriptorSets[currentFrame], 0, nullptr);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
  const VkDeviceSize offsetSkybox[1] = {0};
  for (tak::Node* node : skybox.nodes) {
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &skybox.vertices.buffer, offsetSkybox);
    vkCmdBindIndexBuffer(commandBuffer, skybox.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    modelManager->drawNode(node, commandBuffer);
  }
  // scene obj
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

  VkBuffer vertexBuffers[] = {vertexBuffer.buffer};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
  vkCmdBindIndexBuffer(commandBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                          &descriptorSets[currentFrame], 0, nullptr);
  vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
}

void TriangleScene::updateScene(float deltaTime) { updateUniformBuffer(deltaTime); }

void TriangleScene::onResize(int width, int height) {
  // The base class already handles swapchain recreation
  spdlog::info("Triangle scene resized to {}x{}", width, height);
}

void TriangleScene::cleanupResources() {
  spdlog::info("Cleaning up triangle resources");
  // clean up texture resources
  textureManager->destroyTexture(rectTexture);
  modelManager->destroyModel(skybox);

  vkDestroyDescriptorPool(device, descriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
  vkDestroyDescriptorSetLayout(device, skyboxDescriptorSetLayout, nullptr);

  // unmap the uniformbuffer pointers
  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    if (uniformBuffers[i].memory != VK_NULL_HANDLE) {
      vkUnmapMemory(device, uniformBuffers[i].memory);  // uniformBuffersMapped
      uniformBuffersMapped[i] = nullptr;
    }
    bufferManager->destroyBuffer(uniformBuffers[i]);
    bufferManager->destroyBuffer(skyboxUniformBuffers[i]);
  }
  // Clean up buffers
  bufferManager->destroyBuffer(vertexBuffer);
  bufferManager->destroyBuffer(indexBuffer);
  bufferManager->destroyBuffer(skyBoxParamBuffer);

  // Clean up pipeline
  vkDestroyPipeline(device, graphicsPipeline, nullptr);
  graphicsPipeline = VK_NULL_HANDLE;
  vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  pipelineLayout = VK_NULL_HANDLE;
  vkDestroyPipeline(device, skyboxPipeline, nullptr);
  skyboxPipeline = VK_NULL_HANDLE;
  vkDestroyPipelineLayout(device, skyboxPipelineLayout, nullptr);
  skyboxPipelineLayout = VK_NULL_HANDLE;

  spdlog::info("Triangle resources cleaned up");
  cleanupPBREnvironment();
}
