#include "PBRIBLScene.hpp"

#include <assert.h>

#include "core/utils.hpp"

void PBRIBLScene::loadResources() {
  uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
  shaderMeshDataBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  descriptorSetsMeshData.resize(MAX_FRAMES_IN_FLIGHT);
  loadAssets();       // Scene and environment loading entry point
  generateBRDFLUT();  // 2D BRDF lookup table generation
  //   prepareUniformBuffers();
  //   setupDescriptors();
  // preparePipelines();
}

void PBRIBLScene::loadAssets() {
  textures.empty = textureManager->createDefault();
  // load scene
  modelManager->destroyModel(models.scene);
  models.scene = modelManager->createModelFromFile(std::string(MODEL_DIR) + "/buster_drone/scene.gltf");
  createMaterialBuffer();
  createMeshDataBuffer();
  // Check and list unsupported extensions
  for (auto& ext : models.scene.extensions) {
    if (std::find(supportedExtensions.begin(), supportedExtensions.end(), ext) == supportedExtensions.end()) {
      spdlog::warn("Unsupported extension {}detected. Scene may not work or display as intended", ext);
    }
  }
  modelManager->destroyModel(models.skybox);
  models.skybox = modelManager->createModelFromFile(std::string(MODEL_DIR) + "/box/box.gltf");
  loadEnviroment(std::string(TEXTURE_DIR) + "/skybox/papermill.ktx");
}

void PBRIBLScene::loadEnviroment(std::string& filename) {
  spdlog::info("Loading environment from {}", filename);
  if (textures.environmentCube.image) {
    textureManager->destroyTexture(textures.environmentCube);
    textureManager->destroyTexture(textures.irradianceCube);
    textureManager->destroyTexture(textures.prefilteredCube);
  }
  textures.environmentCube = textureManager->loadHDRCubemapTexture(filename, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  spdlog::info("papermil loaded:{}", textures.environmentCube.image != VK_NULL_HANDLE);
  spdlog::info("enviroment is cubemap:{}", textures.environmentCube.isCubemap());

  generateCubemaps();
}

void PBRIBLScene::createMaterialBuffer() {
  std::vector<ShaderMaterial> shaderMaterials{};
  for (size_t i = 0; i < models.scene.materials.size(); i++) {
    tak::Material& material = models.scene.materials[i];
    material.materialIndex = i;
    ShaderMaterial shaderMaterial{};

    shaderMaterial.emissiveFactor = material.emissiveFactor;
    // To save space, availabilty and texture coordinate set are combined
    //  -1, UINT32_MAX = no texture
    shaderMaterial.colorTextureSet = material.baseColorTextureIndex != UINT32_MAX ? material.texCoordSets.baseColor : -1;
    shaderMaterial.normalTextureSet = material.normalTextureIndex != UINT32_MAX ? material.texCoordSets.normal : -1;
    shaderMaterial.occlusionTextureSet = material.occlusionTextureIndex != UINT32_MAX ? material.texCoordSets.occlusion : -1;
    shaderMaterial.emissiveTextureSet = material.emissiveTextureIndex != UINT32_MAX ? material.texCoordSets.emissive : -1;
    shaderMaterial.alphaMask = static_cast<float>(material.alphaMode == tak::Material::ALPHAMODE_MASK);
    shaderMaterial.alphaMaskCutoff = material.alphaCutoff;
    shaderMaterial.emissiveStrength = material.emissiveStrength;

    if (material.pbrWorkflows.metallicRoughness) {
      // Metallic roughness workflow
      shaderMaterial.workflow = static_cast<float>(PBR_WORKFLOW_METALLIC_ROUGHNESS);
      shaderMaterial.baseColorFactor = material.baseColorFactor;
      shaderMaterial.metallicFactor = material.metallicFactor;
      shaderMaterial.roughnessFactor = material.roughnessFactor;
      shaderMaterial.physicalDescriptorTextureSet = material.metallicRoughnessTextureIndex != UINT32_MAX ? material.texCoordSets.metallicRoughness : -1;
      shaderMaterial.colorTextureSet = material.baseColorTextureIndex != UINT32_MAX ? material.texCoordSets.baseColor : -1;
    } else {
      if (material.pbrWorkflows.specularGlossiness) {
        // Specular glossiness workflow
        shaderMaterial.workflow = static_cast<float>(PBR_WORKFLOW_SPECULAR_GLOSSINESS);
        shaderMaterial.physicalDescriptorTextureSet = material.extension.specularGlossinessTextureIndex != UINT32_MAX ? material.texCoordSets.specularGlossiness : -1;
        shaderMaterial.colorTextureSet = material.extension.diffuseTextureIndex != UINT32_MAX ? material.texCoordSets.baseColor : -1;
        shaderMaterial.diffuseFactor = material.extension.diffuseFactor;
        shaderMaterial.specularFactor = glm::vec4(material.extension.specularFactor, 1.0f);
      }
    }
    shaderMaterials.push_back(shaderMaterial);
  }
  // init shaderMaterialBuffer
  VkDeviceSize bufferSize = shaderMaterials.size() * sizeof(ShaderMaterial);
  shaderMaterialBuffer = bufferManager->createGPULocalBuffer(shaderMaterials.data(), bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

  // Update descriptor
  shaderMaterialBuffer.descriptor.buffer = shaderMaterialBuffer.buffer;
  shaderMaterialBuffer.descriptor.offset = 0;
  shaderMaterialBuffer.descriptor.range = bufferSize;
  shaderMaterialBuffer.device = device;
}
void PBRIBLScene::createMeshDataBuffer() {
  uint32_t meshIndex = 0;
  std::function<void(tak::Node*)> assignMeshIndices = [&](tak::Node* node) {
    if (node->mesh) {
      node->mesh->index = meshIndex++;
    }
    for (auto& child : node->children) {
      assignMeshIndices(child);
    }
  };
  // Assign indices to ALL meshes
  for (auto& node : models.scene.nodes) {
    assignMeshIndices(node);
  }
  std::map<uint32_t, ShaderMeshData> meshDataMap;
  for (auto& node : models.scene.linearNodes) {
    ShaderMeshData meshData{};
    if (node->mesh) {
      memcpy(meshData.jointMatrix, node->mesh->jointMatrix.data(), sizeof(glm::mat4) * MAX_NUM_JOINTS);
      meshData.jointcount = node->mesh->jointcount;
      meshData.matrix = node->mesh->matrix;
      meshDataMap[node->mesh->index] = meshData;
    }
  }
  // Convert map to vector in index order
  std::vector<ShaderMeshData> shaderMeshData(meshDataMap.size());
  for (const auto& [idx, data] : meshDataMap) {
    shaderMeshData[idx] = data;
  }
  // create buffers
  for (auto& shaderMeshDataBuffer : shaderMeshDataBuffers) {
    VkDeviceSize bufferSize = shaderMeshData.size() * sizeof(ShaderMeshData);
    shaderMeshDataBuffer =
        bufferManager->createBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    bufferManager->updateBuffer(shaderMeshDataBuffer, shaderMeshData.data(), bufferSize, 0);
    // Update descriptor
    shaderMeshDataBuffer.descriptor.buffer = shaderMeshDataBuffer.buffer;
    shaderMeshDataBuffer.descriptor.offset = 0;
    shaderMeshDataBuffer.descriptor.range = bufferSize;
    shaderMeshDataBuffer.device = device;
  }
}

void PBRIBLScene::generateBRDFLUT() {
  // image
  const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
  const int32_t dim = 512;
  textureManager->InitTexture(textures.lutBrdf, dim, dim, format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, VK_SAMPLE_COUNT_1_BIT);
  textureManager->createImageView(textures.lutBrdf.image, format, VK_IMAGE_ASPECT_COLOR_BIT);
  TextureManager::TextureSampler sampler{VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                         VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
  textures.lutBrdf.sampler = textureManager->createTextureSampler(sampler, 1.0f, 1.0f);

  // FB, Att, RP, Pipe, etc.
  VkAttachmentDescription attDesc{};
  // Color attachment
  attDesc.format = format;
  attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
  attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkAttachmentReference colorReference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

  VkSubpassDescription subpassDescription{};
  subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpassDescription.colorAttachmentCount = 1;
  subpassDescription.pColorAttachments = &colorReference;

  // dep0 Wait for any previous work to finish before we start writing to the color attachment
  // dep1 Wait for our color attachment writes to finish before anyone else uses the image
  std::array<VkSubpassDependency, 2> dependencies;
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  // Create the renderpass
  VkRenderPassCreateInfo renderPassCI{};
  renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassCI.attachmentCount = 1;
  renderPassCI.pAttachments = &attDesc;
  renderPassCI.subpassCount = 1;
  renderPassCI.pSubpasses = &subpassDescription;
  renderPassCI.dependencyCount = 2;
  renderPassCI.pDependencies = dependencies.data();

  VkRenderPass renderpass;
  vkCreateRenderPass(device, &renderPassCI, nullptr, &renderpass);

  VkFramebufferCreateInfo framebufferCI{};
  framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferCI.renderPass = renderpass;
  framebufferCI.attachmentCount = 1;
  framebufferCI.pAttachments = &textures.lutBrdf.imageView;
  framebufferCI.width = dim;
  framebufferCI.height = dim;
  framebufferCI.layers = 1;

  VkFramebuffer framebuffer;
  vkCreateFramebuffer(device, &framebufferCI, nullptr, &framebuffer);

  // Desriptors
  VkDescriptorSetLayout descriptorsetlayout;
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
  descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout);

  // Pipeline layout
  VkPipelineLayout pipelinelayout;
  VkPipelineLayoutCreateInfo pipelineLayoutCI{};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount = 1;
  pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
  vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout);

  // Pipeline
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
  blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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
  depthStencilStateCI.front = depthStencilStateCI.back;
  depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

  VkPipelineViewportStateCreateInfo viewportStateCI{};
  viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportStateCI.viewportCount = 1;
  viewportStateCI.scissorCount = 1;

  VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
  multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicStateCI{};
  dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
  dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

  VkPipelineVertexInputStateCreateInfo emptyInputStateCI{};
  emptyInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

  // Look-up-table (from BRDF) pipeline
  auto vertcode = readFile("genbrdflut.vert.spv");
  auto fragcode = readFile("genbrdflut.frag.spv");
  VkShaderModule vertShaderModule = createShaderModule(vertcode);
  VkShaderModule fragShaderModule = createShaderModule(fragcode);

  // Shader stage creation
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

  VkGraphicsPipelineCreateInfo pipelineCI{};
  pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineCI.layout = pipelinelayout;
  pipelineCI.renderPass = renderpass;
  pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
  pipelineCI.pVertexInputState = &emptyInputStateCI;
  pipelineCI.pRasterizationState = &rasterizationStateCI;
  pipelineCI.pColorBlendState = &colorBlendStateCI;
  pipelineCI.pMultisampleState = &multisampleStateCI;
  pipelineCI.pViewportState = &viewportStateCI;
  pipelineCI.pDepthStencilState = &depthStencilStateCI;
  pipelineCI.pDynamicState = &dynamicStateCI;
  pipelineCI.stageCount = 2;
  pipelineCI.pStages = shaderStages.data();

  VkPipeline pipeline;
  vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineCI, nullptr, &pipeline);
  for (auto shaderStage : shaderStages) {
    vkDestroyShaderModule(device, shaderStage.module, nullptr);
  }

  // Render
  VkClearValue clearValues[1];
  clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};

  VkRenderPassBeginInfo renderPassBeginInfo{};
  renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassBeginInfo.renderPass = renderpass;
  renderPassBeginInfo.renderArea.extent.width = dim;
  renderPassBeginInfo.renderArea.extent.height = dim;
  renderPassBeginInfo.clearValueCount = 1;
  renderPassBeginInfo.pClearValues = clearValues;
  renderPassBeginInfo.framebuffer = framebuffer;

  VkCommandBuffer cmdBuf = cmdUtils->beginSingleTimeCommands();
  vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport{};
  viewport.width = (float)dim;
  viewport.height = (float)dim;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.extent.width = dim;
  scissor.extent.height = dim;

  vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
  vkCmdSetScissor(cmdBuf, 0, 1, &scissor);
  vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdDraw(cmdBuf, 3, 1, 0, 0);
  vkCmdEndRenderPass(cmdBuf);
  cmdUtils->endSingleTimeCommands(cmdBuf);

  vkQueueWaitIdle(context->graphicsQueue);

  vkDestroyPipeline(device, pipeline, nullptr);
  vkDestroyPipelineLayout(device, pipelinelayout, nullptr);
  vkDestroyRenderPass(device, renderpass, nullptr);
  vkDestroyFramebuffer(device, framebuffer, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorsetlayout, nullptr);

  textures.lutBrdf.descriptor.imageView = textures.lutBrdf.imageView;
  textures.lutBrdf.descriptor.sampler = textures.lutBrdf.sampler;
  textures.lutBrdf.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  textures.lutBrdf.device = device;
}

void PBRIBLScene::cleanupResources() {
  for (auto& pipeline : pipelines) {
    vkDestroyPipeline(device, pipeline.second, nullptr);
  }

  vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.material, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.materialBuffer, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.meshDataBuffer, nullptr);

  modelManager->destroyModel(models.scene);

  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    bufferManager->destroyBuffer(uniformBuffers[i].params);
    bufferManager->destroyBuffer(uniformBuffers[i].skybox);
    bufferManager->destroyBuffer(uniformBuffers[i].scene);
  }

  textureManager->destroyTexture(textures.environmentCube);
  textureManager->destroyTexture(textures.empty);
  textureManager->destroyTexture(textures.irradianceCube);
  textureManager->destroyTexture(textures.lutBrdf);
  textureManager->destroyTexture(textures.prefilteredCube);
}
/*
  Offline generation for the cube maps used for PBR lighting
  - Irradiance cube map
  - Pre-filterd environment cubemap
*/
void PBRIBLScene::generateCubemaps() {
  enum Target { IRRADIANCE = 0, PREFILTEREDENV = 1 };

  for (uint32_t target = 0; target < PREFILTEREDENV + 1; target++) {
    TextureManager::Texture cubemap;
    auto tStart = std::chrono::high_resolution_clock::now();

    VkFormat format;
    int32_t dim;

    switch (target) {
      case IRRADIANCE:
        format = VK_FORMAT_R32G32B32A32_SFLOAT;
        dim = 64;
        break;
      case PREFILTEREDENV:
        format = VK_FORMAT_R16G16B16A16_SFLOAT;
        dim = 512;
        break;
    };

    const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;
    textureManager->InitCubemapTexture(cubemap, dim, dim, format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, )
    // Create target cubemap
    {
      // Image
      VkImageCreateInfo imageCI{};
      imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      imageCI.imageType = VK_IMAGE_TYPE_2D;
      imageCI.format = format;
      imageCI.extent.width = dim;
      imageCI.extent.height = dim;
      imageCI.extent.depth = 1;
      imageCI.mipLevels = numMips;
      imageCI.arrayLayers = 6;
      imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
      imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
      imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
      VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &cubemap.image));
      VkMemoryRequirements memReqs;
      vkGetImageMemoryRequirements(device, cubemap.image, &memReqs);
      VkMemoryAllocateInfo memAllocInfo{};
      memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      memAllocInfo.allocationSize = memReqs.size;
      memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &cubemap.deviceMemory));
      VK_CHECK_RESULT(vkBindImageMemory(device, cubemap.image, cubemap.deviceMemory, 0));

      // View
      VkImageViewCreateInfo viewCI{};
      viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
      viewCI.format = format;
      viewCI.subresourceRange = {};
      viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      viewCI.subresourceRange.levelCount = numMips;
      viewCI.subresourceRange.layerCount = 6;
      viewCI.image = cubemap.image;
      VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &cubemap.view));

      // Sampler
      VkSamplerCreateInfo samplerCI{};
      samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
      samplerCI.magFilter = VK_FILTER_LINEAR;
      samplerCI.minFilter = VK_FILTER_LINEAR;
      samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      samplerCI.minLod = 0.0f;
      samplerCI.maxLod = static_cast<float>(numMips);
      samplerCI.maxAnisotropy = 1.0f;
      samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
      VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &cubemap.sampler));
    }

    // FB, Att, RP, Pipe, etc.
    VkAttachmentDescription attDesc{};
    // Color attachment
    attDesc.format = format;
    attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorReference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpassDescription{};
    subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.colorAttachmentCount = 1;
    subpassDescription.pColorAttachments = &colorReference;

    // Use subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 2> dependencies;
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Renderpass
    VkRenderPassCreateInfo renderPassCI{};
    renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCI.attachmentCount = 1;
    renderPassCI.pAttachments = &attDesc;
    renderPassCI.subpassCount = 1;
    renderPassCI.pSubpasses = &subpassDescription;
    renderPassCI.dependencyCount = 2;
    renderPassCI.pDependencies = dependencies.data();
    VkRenderPass renderpass;
    VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderpass));

    struct Offscreen {
      VkImage image;
      VkImageView view;
      VkDeviceMemory memory;
      VkFramebuffer framebuffer;
    } offscreen;

    // Create offscreen framebuffer
    {
      // Image
      VkImageCreateInfo imageCI{};
      imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      imageCI.imageType = VK_IMAGE_TYPE_2D;
      imageCI.format = format;
      imageCI.extent.width = dim;
      imageCI.extent.height = dim;
      imageCI.extent.depth = 1;
      imageCI.mipLevels = 1;
      imageCI.arrayLayers = 1;
      imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
      imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
      imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &offscreen.image));
      VkMemoryRequirements memReqs;
      vkGetImageMemoryRequirements(device, offscreen.image, &memReqs);
      VkMemoryAllocateInfo memAllocInfo{};
      memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      memAllocInfo.allocationSize = memReqs.size;
      memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &offscreen.memory));
      VK_CHECK_RESULT(vkBindImageMemory(device, offscreen.image, offscreen.memory, 0));

      // View
      VkImageViewCreateInfo viewCI{};
      viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewCI.format = format;
      viewCI.flags = 0;
      viewCI.subresourceRange = {};
      viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      viewCI.subresourceRange.baseMipLevel = 0;
      viewCI.subresourceRange.levelCount = 1;
      viewCI.subresourceRange.baseArrayLayer = 0;
      viewCI.subresourceRange.layerCount = 1;
      viewCI.image = offscreen.image;
      VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &offscreen.view));

      // Framebuffer
      VkFramebufferCreateInfo framebufferCI{};
      framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      framebufferCI.renderPass = renderpass;
      framebufferCI.attachmentCount = 1;
      framebufferCI.pAttachments = &offscreen.view;
      framebufferCI.width = dim;
      framebufferCI.height = dim;
      framebufferCI.layers = 1;
      VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCI, nullptr, &offscreen.framebuffer));

      VkCommandBuffer layoutCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
      VkImageMemoryBarrier imageMemoryBarrier{};
      imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      imageMemoryBarrier.image = offscreen.image;
      imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      imageMemoryBarrier.srcAccessMask = 0;
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      imageMemoryBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdPipelineBarrier(layoutCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
      vulkanDevice->flushCommandBuffer(layoutCmd, queue, true);
    }

    // Descriptors
    VkDescriptorSetLayout descriptorsetlayout;
    VkDescriptorSetLayoutBinding setLayoutBinding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
    descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCI.pBindings = &setLayoutBinding;
    descriptorSetLayoutCI.bindingCount = 1;
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout));

    // Descriptor Pool
    VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo descriptorPoolCI{};
    descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCI.poolSizeCount = 1;
    descriptorPoolCI.pPoolSizes = &poolSize;
    descriptorPoolCI.maxSets = 2;
    VkDescriptorPool descriptorpool;
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorpool));

    // Descriptor sets
    VkDescriptorSet descriptorset;
    VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
    descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocInfo.descriptorPool = descriptorpool;
    descriptorSetAllocInfo.pSetLayouts = &descriptorsetlayout;
    descriptorSetAllocInfo.descriptorSetCount = 1;
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorset));
    VkWriteDescriptorSet writeDescriptorSet{};
    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.dstSet = descriptorset;
    writeDescriptorSet.dstBinding = 0;
    writeDescriptorSet.pImageInfo = &textures.environmentCube.descriptor;
    vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

    struct PushBlockIrradiance {
      glm::mat4 mvp;
      float deltaPhi = (2.0f * float(M_PI)) / 180.0f;
      float deltaTheta = (0.5f * float(M_PI)) / 64.0f;
    } pushBlockIrradiance;

    struct PushBlockPrefilterEnv {
      glm::mat4 mvp;
      float roughness;
      uint32_t numSamples = 32u;
    } pushBlockPrefilterEnv;

    // Pipeline layout
    VkPipelineLayout pipelinelayout;
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    switch (target) {
      case IRRADIANCE:
        pushConstantRange.size = sizeof(PushBlockIrradiance);
        break;
      case PREFILTEREDENV:
        pushConstantRange.size = sizeof(PushBlockPrefilterEnv);
        break;
    };

    VkPipelineLayoutCreateInfo pipelineLayoutCI{};
    pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCI.setLayoutCount = 1;
    pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
    pipelineLayoutCI.pushConstantRangeCount = 1;
    pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

    // Pipeline
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
    blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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
    depthStencilStateCI.front = depthStencilStateCI.back;
    depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineViewportStateCreateInfo viewportStateCI{};
    viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportStateCI.viewportCount = 1;
    viewportStateCI.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
    multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicStateCI{};
    dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
    dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

    // Vertex input state
    VkVertexInputBindingDescription vertexInputBinding = {0, sizeof(vkglTF::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription vertexInputAttribute = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};

    VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
    vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputStateCI.vertexBindingDescriptionCount = 1;
    vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
    vertexInputStateCI.vertexAttributeDescriptionCount = 1;
    vertexInputStateCI.pVertexAttributeDescriptions = &vertexInputAttribute;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

    VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.layout = pipelinelayout;
    pipelineCI.renderPass = renderpass;
    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    pipelineCI.pVertexInputState = &vertexInputStateCI;
    pipelineCI.pRasterizationState = &rasterizationStateCI;
    pipelineCI.pColorBlendState = &colorBlendStateCI;
    pipelineCI.pMultisampleState = &multisampleStateCI;
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pDepthStencilState = &depthStencilStateCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.renderPass = renderpass;

    shaderStages[0] = loadShader(device, "filtercube.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    switch (target) {
      case IRRADIANCE:
        shaderStages[1] = loadShader(device, "irradiancecube.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        break;
      case PREFILTEREDENV:
        shaderStages[1] = loadShader(device, "prefilterenvmap.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        break;
    };
    VkPipeline pipeline;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
    for (auto shaderStage : shaderStages) {
      vkDestroyShaderModule(device, shaderStage.module, nullptr);
    }

    // Render cubemap
    VkClearValue clearValues[1];
    clearValues[0].color = {{0.0f, 0.0f, 0.2f, 0.0f}};

    VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderpass;
    renderPassBeginInfo.framebuffer = offscreen.framebuffer;
    renderPassBeginInfo.renderArea.extent.width = dim;
    renderPassBeginInfo.renderArea.extent.height = dim;
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = clearValues;

    std::vector<glm::mat4> matrices = {
        glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
        glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
        glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
        glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
        glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
    };

    VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);

    VkViewport viewport{};
    viewport.width = (float)dim;
    viewport.height = (float)dim;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent.width = dim;
    scissor.extent.height = dim;

    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = numMips;
    subresourceRange.layerCount = 6;

    // Change image layout for all cubemap faces to transfer destination
    {
      vulkanDevice->beginCommandBuffer(cmdBuf);
      VkImageMemoryBarrier imageMemoryBarrier{};
      imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      imageMemoryBarrier.image = cubemap.image;
      imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      imageMemoryBarrier.srcAccessMask = 0;
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      imageMemoryBarrier.subresourceRange = subresourceRange;
      vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
      vulkanDevice->flushCommandBuffer(cmdBuf, queue, false);
    }

    for (uint32_t m = 0; m < numMips; m++) {
      for (uint32_t f = 0; f < 6; f++) {
        vulkanDevice->beginCommandBuffer(cmdBuf);

        viewport.width = static_cast<float>(dim * std::pow(0.5f, m));
        viewport.height = static_cast<float>(dim * std::pow(0.5f, m));
        vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

        // Render scene from cube face's point of view
        vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Pass parameters for current pass using a push constant block
        switch (target) {
          case IRRADIANCE:
            pushBlockIrradiance.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
            vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockIrradiance), &pushBlockIrradiance);
            break;
          case PREFILTEREDENV:
            pushBlockPrefilterEnv.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
            pushBlockPrefilterEnv.roughness = (float)m / (float)(numMips - 1);
            vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockPrefilterEnv), &pushBlockPrefilterEnv);
            break;
        };

        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, NULL);

        VkDeviceSize offsets[1] = {0};

        models.skybox.draw(cmdBuf);

        vkCmdEndRenderPass(cmdBuf);

        VkImageSubresourceRange subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = numMips;
        subresourceRange.layerCount = 6;

        {
          VkImageMemoryBarrier imageMemoryBarrier{};
          imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          imageMemoryBarrier.image = offscreen.image;
          imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
          imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
          imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
          imageMemoryBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
          vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
        }

        // Copy region for transfer from framebuffer to cube face
        VkImageCopy copyRegion{};

        copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.srcSubresource.baseArrayLayer = 0;
        copyRegion.srcSubresource.mipLevel = 0;
        copyRegion.srcSubresource.layerCount = 1;
        copyRegion.srcOffset = {0, 0, 0};

        copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.dstSubresource.baseArrayLayer = f;
        copyRegion.dstSubresource.mipLevel = m;
        copyRegion.dstSubresource.layerCount = 1;
        copyRegion.dstOffset = {0, 0, 0};

        copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
        copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
        copyRegion.extent.depth = 1;

        vkCmdCopyImage(cmdBuf, offscreen.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cubemap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        {
          VkImageMemoryBarrier imageMemoryBarrier{};
          imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          imageMemoryBarrier.image = offscreen.image;
          imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
          imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
          imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
          imageMemoryBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
          vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
        }

        vulkanDevice->flushCommandBuffer(cmdBuf, queue, false);
      }
    }

    {
      vulkanDevice->beginCommandBuffer(cmdBuf);
      VkImageMemoryBarrier imageMemoryBarrier{};
      imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      imageMemoryBarrier.image = cubemap.image;
      imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
      imageMemoryBarrier.subresourceRange = subresourceRange;
      vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
      vulkanDevice->flushCommandBuffer(cmdBuf, queue, false);
    }

    vkDestroyRenderPass(device, renderpass, nullptr);
    vkDestroyFramebuffer(device, offscreen.framebuffer, nullptr);
    vkFreeMemory(device, offscreen.memory, nullptr);
    vkDestroyImageView(device, offscreen.view, nullptr);
    vkDestroyImage(device, offscreen.image, nullptr);
    vkDestroyDescriptorPool(device, descriptorpool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorsetlayout, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelinelayout, nullptr);

    cubemap.descriptor.imageView = cubemap.view;
    cubemap.descriptor.sampler = cubemap.sampler;
    cubemap.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    cubemap.device = vulkanDevice;

    switch (target) {
      case IRRADIANCE:
        textures.irradianceCube = cubemap;
        break;
      case PREFILTEREDENV:
        textures.prefilteredCube = cubemap;
        shaderValuesParams.prefilteredCubeMipLevels = static_cast<float>(numMips);
        break;
    };

    auto tEnd = std::chrono::high_resolution_clock::now();
    auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    std::cout << "Generating cube map with " << numMips << " mip levels took " << tDiff << " ms" << std::endl;
  }
}