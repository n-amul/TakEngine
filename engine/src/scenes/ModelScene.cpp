#include "ModelScene.hpp"

#include <spdlog/spdlog.h>

#include <cstring>
#include <filesystem>
#include <stdexcept>

#include "core/utils.hpp"

ModelScene::ModelScene() {
  window_width = 1920;
  window_height = 1080;
  title = "Vulkan Model Scene";
  name = "ModelScene";
}

void ModelScene::loadResources() {
  spdlog::info("Loading model scene resources");
  // Create descriptor set layout first
  createDescriptorSetLayout();

  // Load models
  loadModel(std::string(MODEL_DIR) + "/buster_drone/scene.gltf", 1.0f);
  // You can load additional models

  // Create uniform buffers
  createUniformBuffers();

  // Create descriptor resources
  createDescriptorPool();
  createDescriptorSets();

  spdlog::info("Model scene resources loaded successfully");
}

void ModelScene::loadModel(const std::string& path, float scale) {
  spdlog::info("Loading model from: {}", path);
  models.push_back(modelManager->createModelFromFile(path, scale));
  spdlog::info("Model loaded with {} textures and {} materials", models.back().textures.size(), models.back().materials.size());
}

void ModelScene::createDescriptorSetLayout() {
  // UBO binding for vertex shader
  VkDescriptorSetLayoutBinding uboLayoutBinding{};
  uboLayoutBinding.binding = 0;
  uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboLayoutBinding.descriptorCount = 1;
  uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  uboLayoutBinding.pImmutableSamplers = nullptr;

  // Material textures bindings for fragment shader
  std::vector<VkDescriptorSetLayoutBinding> bindings = {uboLayoutBinding};

  // Add bindings for PBR textures
  uint32_t bindingIndex = 1;

  // Base color texture
  VkDescriptorSetLayoutBinding baseColorBinding{};
  baseColorBinding.binding = bindingIndex++;
  baseColorBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  baseColorBinding.descriptorCount = 1;
  baseColorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings.push_back(baseColorBinding);

  // Metallic-roughness texture
  VkDescriptorSetLayoutBinding metallicRoughnessBinding{};
  metallicRoughnessBinding.binding = bindingIndex++;
  metallicRoughnessBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  metallicRoughnessBinding.descriptorCount = 1;
  metallicRoughnessBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings.push_back(metallicRoughnessBinding);

  // Normal map texture
  VkDescriptorSetLayoutBinding normalBinding{};
  normalBinding.binding = bindingIndex++;
  normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  normalBinding.descriptorCount = 1;
  normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings.push_back(normalBinding);

  // Occlusion texture
  VkDescriptorSetLayoutBinding occlusionBinding{};
  occlusionBinding.binding = bindingIndex++;
  occlusionBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  occlusionBinding.descriptorCount = 1;
  occlusionBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings.push_back(occlusionBinding);

  // Emissive texture
  VkDescriptorSetLayoutBinding emissiveBinding{};
  emissiveBinding.binding = bindingIndex++;
  emissiveBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  emissiveBinding.descriptorCount = 1;
  emissiveBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings.push_back(emissiveBinding);

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create descriptor set layout!");
  }
}

void ModelScene::createDescriptorPool() {
  std::vector<VkDescriptorPoolSize> poolSizes;

  // UBO descriptors
  VkDescriptorPoolSize uboSize{};
  uboSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * models.size());
  poolSizes.push_back(uboSize);

  // Combined image samplers for textures
  VkDescriptorPoolSize samplerSize{};
  samplerSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  // 5 textures per material, multiple materials per model
  samplerSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * models.size() * 5);
  poolSizes.push_back(samplerSize);

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * models.size());

  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create descriptor pool!");
  }
}

void ModelScene::createDescriptorSets() {
  descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

  for (size_t frameIdx = 0; frameIdx < MAX_FRAMES_IN_FLIGHT; frameIdx++) {
    descriptorSets[frameIdx].resize(models.size());

    std::vector<VkDescriptorSetLayout> layouts(models.size(), descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(models.size());
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets[frameIdx].data()) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate descriptor sets!");
    }

    // Update descriptor sets for each model
    for (size_t modelIdx = 0; modelIdx < models.size(); modelIdx++) {
      VkDescriptorBufferInfo bufferInfo{};
      bufferInfo.buffer = uniformBuffers[frameIdx].buffer;
      bufferInfo.offset = 0;
      bufferInfo.range = sizeof(UniformBufferObject);

      std::vector<VkWriteDescriptorSet> descriptorWrites;

      // UBO write
      VkWriteDescriptorSet uboWrite{};
      uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      uboWrite.dstSet = descriptorSets[frameIdx][modelIdx];
      uboWrite.dstBinding = 0;
      uboWrite.dstArrayElement = 0;
      uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      uboWrite.descriptorCount = 1;
      uboWrite.pBufferInfo = &bufferInfo;
      descriptorWrites.push_back(uboWrite);

      // Create default white texture if model doesn't have textures
      // Note: In production, you'd want to create a default white texture once and reuse it
      if (!models[modelIdx].textures.empty()) {
        // Use first texture as default for simplicity
        // In a real implementation, you'd map these based on material properties
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = models[modelIdx].textures[0].imageView;
        imageInfo.sampler = models[modelIdx].textures[0].sampler;

        // Add texture bindings (simplified - using same texture for all slots)
        for (uint32_t binding = 1; binding <= 5; binding++) {
          VkWriteDescriptorSet textureWrite{};
          textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
          textureWrite.dstSet = descriptorSets[frameIdx][modelIdx];
          textureWrite.dstBinding = binding;
          textureWrite.dstArrayElement = 0;
          textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
          textureWrite.descriptorCount = 1;
          textureWrite.pImageInfo = &imageInfo;
          descriptorWrites.push_back(textureWrite);
        }
      }

      vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
  }
}

void ModelScene::createUniformBuffers() {
  VkDeviceSize bufferSize = sizeof(UniformBufferObject);
  uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    uniformBuffers[i] =
        bufferManager->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkMapMemory(device, uniformBuffers[i].memory, 0, bufferSize, 0, &uniformBuffersMapped[i]);
  }
}

void ModelScene::createPipeline() {
  spdlog::info("Creating model pipeline");

  // Load shaders
  std::string vertPath = std::string(SHADER_DIR) + "/model.vert.spv";
  std::string fragPath = std::string(SHADER_DIR) + "/model.frag.spv";
  auto vertShaderCode = readFile(vertPath);
  auto fragShaderCode = readFile(fragPath);

  VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
  VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

  // Shader stages
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

  VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

  // Vertex input for model vertices
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(tak::Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::vector<VkVertexInputAttributeDescription> attributeDescriptions(7);
  // Position
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(tak::Vertex, pos);
  // Normal
  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(tak::Vertex, normal);
  // UV0
  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(tak::Vertex, uv0);
  // UV1
  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[3].offset = offsetof(tak::Vertex, uv1);
  // Joint indices
  attributeDescriptions[4].binding = 0;
  attributeDescriptions[4].location = 4;
  attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_UINT;
  attributeDescriptions[4].offset = offsetof(tak::Vertex, joint0);
  // Joint weights
  attributeDescriptions[5].binding = 0;
  attributeDescriptions[5].location = 5;
  attributeDescriptions[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[5].offset = offsetof(tak::Vertex, weight0);
  // Color
  attributeDescriptions[6].binding = 0;
  attributeDescriptions[6].location = 6;
  attributeDescriptions[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[6].offset = offsetof(tak::Vertex, color);

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

  // Viewport state
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

  // Multisampling
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Depth and stencil
  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  // Color blending
  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  // Dynamic state
  std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  // Push constant range for model matrix and material index
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstantData);

  // Pipeline layout
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

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
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create graphics pipeline!");
  }

  // Cleanup shader modules
  vkDestroyShaderModule(device, fragShaderModule, nullptr);
  vkDestroyShaderModule(device, vertShaderModule, nullptr);

  spdlog::info("Model pipeline created successfully");
}

void ModelScene::updateScene(float deltaTime) {
  totalTime += deltaTime;
  updateUniformBuffer(currentFrame);

  // Update animations for all models
  if (animationEnabled) {
    for (auto& model : models) {
      if (!model.animations.empty() && currentAnimationIndex < model.animations.size()) {
        // Animation update logic would go here
        // This would involve updating node transforms based on animation data
      }
    }
  }
}

void ModelScene::updateUniformBuffer(uint32_t frameIndex) {
  UniformBufferObject ubo{};

  // Simple rotation for demonstration
  ubo.model = glm::rotate(glm::mat4(1.0f), totalTime * glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  ubo.view = camera.getViewMatrix();

  float aspectRatio = swapChainExtent.width / static_cast<float>(swapChainExtent.height);
  ubo.proj = camera.getProjectionMatrix(aspectRatio);

  // Simple light setup
  ubo.lightPos = glm::vec3(5.0f, 5.0f, 5.0f);
  ubo.viewPos = camera.getPosition();

  memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
}

void ModelScene::recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
  // Bind pipeline
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

  // Set viewport
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(swapChainExtent.width);
  viewport.height = static_cast<float>(swapChainExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

  // Set scissor
  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = swapChainExtent;
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

  // Draw each model
  for (size_t modelIdx = 0; modelIdx < models.size(); modelIdx++) {
    const auto& model = models[modelIdx];

    // Bind vertex and index buffers
    VkBuffer vertexBuffers[] = {model.vertices.buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

    // Bind descriptor set for this model
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame][modelIdx], 0, nullptr);

    // Draw all nodes in the model
    for (auto* node : model.nodes) {
      drawNode(commandBuffer, node, currentFrame, model);
    }
  }
}

void ModelScene::drawNode(VkCommandBuffer commandBuffer, tak::Node* node, uint32_t frameIndex, const ModelManager::Model& model) {
  if (node->mesh) {
    // Update push constants with node transform
    pushConstantData.model = node->getMatrix();

    for (tak::Primitive* primitive : node->mesh->primitives) {
      if (primitive->indexCount > 0) {
        // Set material index in push constants
        pushConstantData.materialIndex = primitive->material.materialIndex;

        // Push the constants
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &pushConstantData);

        // Draw indexed
        vkCmdDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, 0, 0);
      }
    }
  }

  // Recursively draw children
  for (auto* child : node->children) {
    drawNode(commandBuffer, child, frameIndex, model);
  }
}

void ModelScene::onResize(int width, int height) {}

void ModelScene::cleanupResources() {
  spdlog::info("Cleaning up model scene resources");

  // Clean up models
  for (auto& model : models) {
    modelManager->destroyModel(model);
  }
  models.clear();

  // Unmap uniform buffers
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    if (uniformBuffers[i].memory != VK_NULL_HANDLE) {
      vkUnmapMemory(device, uniformBuffers[i].memory);
      uniformBuffersMapped[i] = nullptr;
    }
    bufferManager->destroyBuffer(uniformBuffers[i]);
  }

  // Clean up descriptor resources
  if (descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    descriptorPool = VK_NULL_HANDLE;
  }

  if (descriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    descriptorSetLayout = VK_NULL_HANDLE;
  }

  // Clean up pipeline
  if (graphicsPipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    graphicsPipeline = VK_NULL_HANDLE;
  }

  if (pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    pipelineLayout = VK_NULL_HANDLE;
  }

  spdlog::info("Model scene resources cleaned up");
}