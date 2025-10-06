#include "ModelScene.hpp"

#include <array>
#include <stdexcept>

#include "core/utils.hpp"

ModelScene::ModelScene() {
  title = "glTF Model Viewer";
  name = "ModelScene";
}

void ModelScene::loadResources() {
  // Load the glTF model
  scene = modelManager->createModelFromFile(std::string(MODEL_DIR) + "/buster_drone/scene.gltf", 1.0f);

  // Create descriptor pool
  std::array<VkDescriptorPoolSize, 4> poolSizes = {{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 * MAX_FRAMES_IN_FLIGHT},
                                                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
                                                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 * MAX_FRAMES_IN_FLIGHT},
                                                    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 10}}};

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = 100;

  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create descriptor pool!");
  }

  // Create descriptor set layouts
  createDescriptorSetLayouts();

  // Create uniform buffers
  createUniformBuffers();

  // Create material SSBO
  createMaterialBuffer();

  // Create mesh data SSBOs
  createMeshDataBuffers();

  // Allocate and update descriptor sets
  createDescriptorSets();

  // Create skybox descriptor sets
  createSkyboxDescriptorSets();
}

void ModelScene::createDescriptorSetLayouts() {
  // Set 0: Scene layout (matrices + params)
  {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayouts.scene);
  }

  // Set 1: Material textures layout
  {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    for (uint32_t i = 0; i < 5; i++) {
      bindings[i].binding = i;
      bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      bindings[i].descriptorCount = 1;
      bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayouts.material);
  }

  // Set 2: Material buffer (SSBO)
  {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayouts.materialBuffer);
  }

  // Set 3: Mesh data buffer (SSBO)
  {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayouts.meshDataBuffer);
  }
}

void ModelScene::createUniformBuffers() {
  VkDeviceSize sceneBufferSize = sizeof(UBOMatrices);
  VkDeviceSize paramsBufferSize = sizeof(ShaderValuesParams);

  uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    uniformBuffers[i].scene =
        bufferManager->createBuffer(sceneBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    uniformBuffers[i].params =
        bufferManager->createBuffer(paramsBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  }

  // Initialize shader params
  shaderValuesParams.lightDir = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
  shaderValuesParams.exposure = 4.5f;
  shaderValuesParams.gamma = 2.2f;

  // Update params buffers (they don't change per frame)
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    bufferManager->updateBuffer(uniformBuffers[i].params, &shaderValuesParams, sizeof(ShaderValuesParams));
  }
}

void ModelScene::createMaterialBuffer() {
  size_t materialCount = scene.materials.size();
  if (materialCount == 0) return;

  VkDeviceSize bufferSize = sizeof(ShaderMaterial) * materialCount;
  std::vector<ShaderMaterial> shaderMaterials(materialCount);

  for (size_t i = 0; i < materialCount; i++) {
    const auto& mat = scene.materials[i];
    shaderMaterials[i].baseColorFactor = mat.baseColorFactor;
    shaderMaterials[i].emissiveFactor = mat.emissiveFactor;
    shaderMaterials[i].workflow = mat.pbrWorkflows.metallicRoughness ? 0.0f : 1.0f;
    shaderMaterials[i].colorTextureSet = mat.texCoordSets.baseColor;
    shaderMaterials[i].physicalDescriptorTextureSet = mat.texCoordSets.metallicRoughness;
    shaderMaterials[i].normalTextureSet = mat.texCoordSets.normal;
    shaderMaterials[i].occlusionTextureSet = mat.texCoordSets.occlusion;
    shaderMaterials[i].emissiveTextureSet = mat.texCoordSets.emissive;
    shaderMaterials[i].metallicFactor = mat.metallicFactor;
    shaderMaterials[i].roughnessFactor = mat.roughnessFactor;
    shaderMaterials[i].alphaMask = mat.alphaMode == tak::Material::ALPHAMODE_MASK ? 1.0f : 0.0f;
    shaderMaterials[i].alphaMaskCutoff = mat.alphaCutoff;
  }

  shaderMaterialBuffer = bufferManager->createGPULocalBuffer(shaderMaterials.data(), bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

void ModelScene::createMeshDataBuffers() {
  VkDeviceSize bufferSize = sizeof(ShaderMeshData) * scene.linearNodes.size();
  shaderMeshDataBuffers.resize(MAX_FRAMES_IN_FLIGHT);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    shaderMeshDataBuffers[i] =
        bufferManager->createBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  }
}

void ModelScene::createDescriptorSets() {
  // Allocate scene descriptor sets (one per frame)
  {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayouts.scene);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSetsScene.resize(MAX_FRAMES_IN_FLIGHT);
    vkAllocateDescriptorSets(device, &allocInfo, descriptorSetsScene.data());

    // Update scene descriptor sets
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

      VkDescriptorBufferInfo sceneInfo{};
      sceneInfo.buffer = uniformBuffers[i].scene.buffer;
      sceneInfo.offset = 0;
      sceneInfo.range = sizeof(UBOMatrices);

      descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[0].dstSet = descriptorSetsScene[i];
      descriptorWrites[0].dstBinding = 0;
      descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptorWrites[0].descriptorCount = 1;
      descriptorWrites[0].pBufferInfo = &sceneInfo;

      VkDescriptorBufferInfo paramsInfo{};
      paramsInfo.buffer = uniformBuffers[i].params.buffer;
      paramsInfo.offset = 0;
      paramsInfo.range = sizeof(ShaderValuesParams);

      descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[1].dstSet = descriptorSetsScene[i];
      descriptorWrites[1].dstBinding = 1;
      descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptorWrites[1].descriptorCount = 1;
      descriptorWrites[1].pBufferInfo = &paramsInfo;

      vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
  }

  // Allocate and update material buffer descriptor set
  if (scene.materials.size() > 0) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayouts.materialBuffer;

    vkAllocateDescriptorSets(device, &allocInfo, &descriptorSetMaterials);

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = shaderMaterialBuffer.buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSetMaterials;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
  }

  // Allocate and update mesh data descriptor sets
  {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayouts.meshDataBuffer);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSetsMeshData.resize(MAX_FRAMES_IN_FLIGHT);
    vkAllocateDescriptorSets(device, &allocInfo, descriptorSetsMeshData.data());

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      VkDescriptorBufferInfo bufferInfo{};
      bufferInfo.buffer = shaderMeshDataBuffers[i].buffer;
      bufferInfo.offset = 0;
      bufferInfo.range = VK_WHOLE_SIZE;

      VkWriteDescriptorSet descriptorWrite{};
      descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrite.dstSet = descriptorSetsMeshData[i];
      descriptorWrite.dstBinding = 0;
      descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptorWrite.descriptorCount = 1;
      descriptorWrite.pBufferInfo = &bufferInfo;

      vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }
  }

  // Allocate descriptor sets for material textures
  for (auto& material : scene.materials) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayouts.material;

    vkAllocateDescriptorSets(device, &allocInfo, &material.descriptorSet);

    // Update material texture descriptor sets
    std::vector<VkWriteDescriptorSet> descriptorWrites;
    std::vector<VkDescriptorImageInfo> imageInfos(5);

    // Default white texture for missing textures
    VkDescriptorImageInfo defaultImageInfo{};
    defaultImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    defaultImageInfo.imageView = textureManager->getDefaultTexture().imageView;
    defaultImageInfo.sampler = textureManager->getDefaultSampler();

    // Base color
    imageInfos[0] = defaultImageInfo;
    if (material.baseColorTextureIndex != UINT32_MAX) {
      imageInfos[0].imageView = scene.textures[material.baseColorTextureIndex].imageView;
      imageInfos[0].sampler = scene.textures[material.baseColorTextureIndex].sampler;
    }

    // Metallic roughness
    imageInfos[1] = defaultImageInfo;
    if (material.metallicRoughnessTextureIndex != UINT32_MAX) {
      imageInfos[1].imageView = scene.textures[material.metallicRoughnessTextureIndex].imageView;
      imageInfos[1].sampler = scene.textures[material.metallicRoughnessTextureIndex].sampler;
    }

    // Normal
    imageInfos[2] = defaultImageInfo;
    if (material.normalTextureIndex != UINT32_MAX) {
      imageInfos[2].imageView = scene.textures[material.normalTextureIndex].imageView;
      imageInfos[2].sampler = scene.textures[material.normalTextureIndex].sampler;
    }

    // Occlusion
    imageInfos[3] = defaultImageInfo;
    if (material.occlusionTextureIndex != UINT32_MAX) {
      imageInfos[3].imageView = scene.textures[material.occlusionTextureIndex].imageView;
      imageInfos[3].sampler = scene.textures[material.occlusionTextureIndex].sampler;
    }

    // Emissive
    imageInfos[4] = defaultImageInfo;
    if (material.emissiveTextureIndex != UINT32_MAX) {
      imageInfos[4].imageView = scene.textures[material.emissiveTextureIndex].imageView;
      imageInfos[4].sampler = scene.textures[material.emissiveTextureIndex].sampler;
    }

    for (uint32_t i = 0; i < 5; i++) {
      VkWriteDescriptorSet write{};
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = material.descriptorSet;
      write.dstBinding = i;
      write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.descriptorCount = 1;
      write.pImageInfo = &imageInfos[i];
      descriptorWrites.push_back(write);
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
  }
}

void ModelScene::createSkyboxDescriptorSets() {
  // Allocate skybox descriptor sets
  std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, skyboxDescriptorSetLayout);

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
  allocInfo.pSetLayouts = layouts.data();

  skyboxDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
  if (vkAllocateDescriptorSets(device, &allocInfo, skyboxDescriptorSets.data()) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate skybox descriptor sets!");
  }

  // Update skybox descriptor sets
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = skyboxUniformBuffers[i].buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(SkyboxUniformBufferObject);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = skyboxTexture.imageView;
    imageInfo.sampler = skyboxTexture.sampler;

    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = skyboxDescriptorSets[i];
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = skyboxDescriptorSets[i];
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
  }
}

void ModelScene::createPipeline() {
  // Create pipeline layout
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(MeshPushConstantBlock);

  std::array<VkDescriptorSetLayout, 4> setLayouts = {descriptorSetLayouts.scene, descriptorSetLayouts.material, descriptorSetLayouts.materialBuffer,
                                                     descriptorSetLayouts.meshDataBuffer};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create pipeline layout!");
  }

  // Load shaders
  auto vertShaderCode = readFile(std::string(SHADER_DIR) + "/model.vert.spv");
  auto fragShaderCode = readFile(std::string(SHADER_DIR) + "/model.frag.spv");

  VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
  VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

  // Create pipelines for different rendering modes
  createPipelineVariant(vertShaderModule, fragShaderModule, "pbr", VK_CULL_MODE_BACK_BIT, false);
  createPipelineVariant(vertShaderModule, fragShaderModule, "pbr_double_sided", VK_CULL_MODE_NONE, false);
  createPipelineVariant(vertShaderModule, fragShaderModule, "pbr_alpha_blending", VK_CULL_MODE_NONE, true);

  vkDestroyShaderModule(device, fragShaderModule, nullptr);
  vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void ModelScene::createPipelineVariant(VkShaderModule vertModule, VkShaderModule fragModule, const std::string& name, VkCullModeFlags cullMode, bool alphaBlending) {
  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = vertModule;
  vertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = fragModule;
  fragShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

  // Vertex input
  auto bindingDescription = tak::Vertex::getBindingDescription();
  auto attributeDescriptions = tak::Vertex::getAttributeDescriptions();

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
  rasterizer.cullMode = cullMode;
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

  if (alphaBlending) {
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  } else {
    colorBlendAttachment.blendEnable = VK_FALSE;
  }

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

  // Create the graphics pipeline
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

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipelines[name]) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create graphics pipeline!");
  }
}

void ModelScene::recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
  // Set viewport and scissor
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(swapChainExtent.width);
  viewport.height = static_cast<float>(swapChainExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = swapChainExtent;
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

  // First, render the skybox with its own pipeline and descriptor sets
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipelineLayout, 0, 1, &skyboxDescriptorSets[currentFrame], 0, nullptr);

  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &skyboxVertexBuffer.buffer, offsets);
  vkCmdBindIndexBuffer(commandBuffer, skyboxIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);
  vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(skyboxIndices.size()), 1, 0, 0, 0);

  // Then render the model with its own pipeline and descriptor sets
  if (scene.vertices.buffer != VK_NULL_HANDLE) {
    // Reset pipeline state for model rendering
    // First bind the pipeline for models (start with default)
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines["pbr"]);
    boundPipeline = pipelines["pbr"];

    // Bind vertex and index buffers for the model
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &scene.vertices.buffer, offsets);
    if (scene.indices.buffer != VK_NULL_HANDLE) {
      vkCmdBindIndexBuffer(commandBuffer, scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    }

    // Bind descriptor sets for model rendering
    // Set 0: Scene uniforms
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSetsScene[currentFrame], 0, nullptr);

    // Set 2: Material buffer (if exists)
    if (descriptorSetMaterials != VK_NULL_HANDLE) {
      vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 2, 1, &descriptorSetMaterials, 0, nullptr);
    }

    // Set 3: Mesh data buffer
    if (descriptorSetsMeshData.size() > currentFrame) {
      vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 3, 1, &descriptorSetsMeshData[currentFrame], 0, nullptr);
    }

    // Draw ALL root nodes in the scene
    for (auto& node : scene.nodes) {
      drawNode(commandBuffer, node);
    }
  }
}

void ModelScene::drawNode(VkCommandBuffer commandBuffer, tak::Node* node) {
  if (!node) return;  // Safety check

  // Draw this node's mesh if it exists
  if (node->mesh) {
    // Draw all primitives in this mesh
    for (tak::Primitive* primitive : node->mesh->primitives) {
      if (!primitive || primitive->indexCount == 0) continue;

      const tak::Material& material = primitive->material;

      // Select and bind appropriate pipeline based on material properties
      VkPipeline selectedPipeline = pipelines["pbr"];
      if (material.alphaMode == tak::Material::ALPHAMODE_BLEND) {
        selectedPipeline = pipelines["pbr_alpha_blending"];
      } else if (material.doubleSided) {
        selectedPipeline = pipelines["pbr_double_sided"];
      }

      // Only rebind pipeline if it's different
      if (boundPipeline != selectedPipeline) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, selectedPipeline);
        boundPipeline = selectedPipeline;
      }

      // Bind material descriptor set (Set 1)
      if (material.descriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &material.descriptorSet, 0, nullptr);
      }

      // Push constants for this draw call
      MeshPushConstantBlock pushConstants{};
      pushConstants.meshIndex = node->mesh->index;
      pushConstants.materialIndex = material.materialIndex;

      vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPushConstantBlock), &pushConstants);

      // Draw the primitive
      if (primitive->hasIndices) {
        vkCmdDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, 0, 0);
      } else {
        vkCmdDraw(commandBuffer, primitive->vertexCount, 1, 0, 0);
      }
    }
  }

  // Recursively draw all children
  for (auto& child : node->children) {
    drawNode(commandBuffer, child);
  }
}

// Also update your updateScene to handle linearNodes properly
void ModelScene::updateScene(float deltaTime) {
  // Update skybox uniform buffer
  updateSkyboxUniformBuffer();

  // Update scene matrices
  uboMatrices.projection = camera.getProjectionMatrix(swapChainExtent.width / static_cast<float>(swapChainExtent.height));
  uboMatrices.view = camera.getViewMatrix();
  uboMatrices.model = glm::mat4(1.0f);
  uboMatrices.camPos = camera.getPosition();

  // Update uniform buffer
  bufferManager->updateBuffer(uniformBuffers[currentFrame].scene, &uboMatrices, sizeof(UBOMatrices));

  // Update all nodes first (for animations and transforms)
  for (auto& node : scene.nodes) {
    node->update();
  }

  // Update mesh data for ALL nodes (not just linearNodes)
  // LinearNodes contains all nodes in a flat array for easy iteration
  if (scene.linearNodes.size() > 0) {
    std::vector<ShaderMeshData> meshData(scene.linearNodes.size());

    for (size_t i = 0; i < scene.linearNodes.size(); i++) {
      tak::Node* node = scene.linearNodes[i];
      if (node && node->mesh) {
        // Each mesh needs its index to match its position in linearNodes
        node->mesh->index = i;  // Ensure the index is correct
        meshData[i].matrix = node->mesh->matrix;
        meshData[i].jointcount = node->mesh->jointcount;
        for (uint32_t j = 0; j < node->mesh->jointcount && j < MAX_NUM_JOINTS; j++) {
          meshData[i].jointMatrix[j] = node->mesh->jointMatrix[j];
        }
      } else {
        meshData[i].matrix = glm::mat4(1.0f);
        meshData[i].jointcount = 0;
      }
    }

    bufferManager->updateBuffer(shaderMeshDataBuffers[currentFrame], meshData.data(), sizeof(ShaderMeshData) * meshData.size());
  }

  // Update animations if any
  static float animationTime = 0.0f;
  animationTime += deltaTime;

  if (!scene.animations.empty()) {
    float animDuration = scene.animations[0].end;
    float currentTime = fmod(animationTime, animDuration);

    for (auto& channel : scene.animations[0].channels) {
      tak::AnimationSampler& sampler = scene.animations[0].samplers[channel.samplerIndex];

      for (size_t i = 0; i < sampler.inputs.size() - 1; i++) {
        if (currentTime >= sampler.inputs[i] && currentTime <= sampler.inputs[i + 1]) {
          float u = (currentTime - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);

          switch (channel.path) {
            case tak::AnimationChannel::PathType::TRANSLATION:
              sampler.translate(i, u, channel.node);
              break;
            case tak::AnimationChannel::PathType::ROTATION:
              sampler.rotate(i, u, channel.node);
              break;
            case tak::AnimationChannel::PathType::SCALE:
              sampler.scale(i, u, channel.node);
              break;
          }
          break;
        }
      }
    }

    // Update all nodes after animation
    for (auto& node : scene.nodes) {
      node->update();
    }
  }
}
void ModelScene::cleanupResources() {
  // Cleanup pipelines
  for (auto& [name, pipeline] : pipelines) {
    vkDestroyPipeline(device, pipeline, nullptr);
  }

  if (pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  }

  // Cleanup descriptor set layouts
  if (descriptorSetLayouts.scene != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
  if (descriptorSetLayouts.material != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.material, nullptr);
  if (descriptorSetLayouts.materialBuffer != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.materialBuffer, nullptr);
  if (descriptorSetLayouts.meshDataBuffer != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.meshDataBuffer, nullptr);

  // Cleanup skybox resources
  if (skyboxPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, skyboxPipeline, nullptr);
  if (skyboxPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, skyboxPipelineLayout, nullptr);
  if (skyboxDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, skyboxDescriptorSetLayout, nullptr);

  // Cleanup descriptor pool
  if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);

  // Destroy model
  modelManager->destroyModel(scene);
}