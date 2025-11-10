#include "PBRIBLScene.hpp"

#include <assert.h>

#include "core/utils.hpp"

void PBRIBLScene::loadResources() {
  uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
  shaderMeshDataBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  descriptorSetsMeshData.resize(MAX_FRAMES_IN_FLIGHT);
  // Initialize PBR environment resources in base class
  initializePBREnvironment();
  emptyTexture = textureManager->createDefault();

  loadAssets();  // Scene and environment loading entry point
  prepareUniformBuffers();
  setupDescriptors();
}

void PBRIBLScene::createPipeline() {
  // Create the pipeline layout ONCE here
  spdlog::info("Creating pipelines - current count: {}", pipelines.size());
  const std::vector<VkDescriptorSetLayout> setLayouts = {descriptorSetLayouts.scene, descriptorSetLayouts.material,
                                                         descriptorSetLayouts.meshDataBuffer,
                                                         descriptorSetLayouts.materialBuffer};
  VkPipelineLayoutCreateInfo pipelineLayoutCI{};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutCI.pSetLayouts = setLayouts.data();

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.size = sizeof(MeshPushConstantBlock);
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pipelineLayoutCI.pushConstantRangeCount = 1;
  pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

  VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));
  createSkyboxPipeline();
  // PBR pipelines
  addPipelineSet("pbr", std::string(SHADER_DIR) + "/pbribl.vert.spv", std::string(SHADER_DIR) + "/material_pbr.frag.spv");
  // KHR_materials_unlit
  addPipelineSet("unlit", std::string(SHADER_DIR) + "/pbribl.vert.spv",
                 std::string(SHADER_DIR) + "/material_unlit.frag.spv");
}

void PBRIBLScene::recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
  boundPipeline = VK_NULL_HANDLE;
  // Render pass is already begun in base class recordCommandBuffer()
  // Just need to bind pipeline and draw
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
  // skybox render
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipelineLayout, 0, 1,
                          &skyboxDescriptorSets[currentFrame], 0, nullptr);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
  const VkDeviceSize offsetSkybox[1] = {0};
  for (tak::Node* node : models.skybox.nodes) {
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &models.skybox.vertices.buffer, offsetSkybox);
    vkCmdBindIndexBuffer(commandBuffer, models.skybox.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    modelManager->drawNode(node, commandBuffer);
  }
  // scene render
  boundPipeline = VK_NULL_HANDLE;
  VkDeviceSize offsets_scene[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &models.scene.vertices.buffer, offsets_scene);
  if (models.scene.indices.buffer != VK_NULL_HANDLE) {
    vkCmdBindIndexBuffer(commandBuffer, models.scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
  }

  // Opaque primitives first
  for (auto node : models.scene.nodes) {
    renderNode(commandBuffer, node, imageIndex, tak::Material::ALPHAMODE_OPAQUE);
  }
  // Alpha masked primitives
  for (auto node : models.scene.nodes) {
    renderNode(commandBuffer, node, imageIndex, tak::Material::ALPHAMODE_MASK);
  }
  // Transparent primitives
  // TODO: Correct depth sorting
  for (auto node : models.scene.nodes) {
    renderNode(commandBuffer, node, imageIndex, tak::Material::ALPHAMODE_BLEND);
  }
}

void PBRIBLScene::renderNode(VkCommandBuffer cmdBuffer, tak::Node* node, uint32_t ImageIndex,
                             tak::Material::AlphaMode alphaMode) {
  if (node->mesh) {
    // Render mesh primitives
    for (tak::Primitive* primitive : node->mesh->primitives) {
      if (models.scene.materials[primitive->materialIndex].alphaMode == alphaMode) {
        std::string pipelineName = "pbr";
        std::string pipelineVariant = "";

        if (models.scene.materials[primitive->materialIndex].unlit) {
          pipelineName = "unlit";
        }

        // Material properties define if we e.g. need to bind a pipeline variant with culling disabled (double sided)
        if (alphaMode == tak::Material::ALPHAMODE_BLEND) {
          pipelineVariant = "_alpha_blending";
        } else {
          if (models.scene.materials[primitive->materialIndex].doubleSided) {
            pipelineVariant = "_double_sided";
          }
        }
        const VkPipeline pipeline = pipelines[pipelineName + pipelineVariant];

        if (boundPipeline != pipeline) {
          vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
          boundPipeline = pipeline;
        }

        const std::vector<VkDescriptorSet> descriptorsets = {
            descriptorSets[currentFrame].scene,                              // set 0
            models.scene.materials[primitive->materialIndex].descriptorSet,  // set 1
            descriptorSetsMeshData[currentFrame],                            // set 2
            descriptorSetMaterials                                           // set 3
        };
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0,
                                static_cast<uint32_t>(descriptorsets.size()), descriptorsets.data(), 0, NULL);

        // Pass material index for this primitive using a push constant, the shader uses this to index into the material
        // buffer
        MeshPushConstantBlock pushConstantBlock{};
        // @todo: index
        pushConstantBlock.meshIndex = node->mesh->index;
        pushConstantBlock.materialIndex = models.scene.materials[primitive->materialIndex].materialIndex;

        vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(MeshPushConstantBlock), &pushConstantBlock);

        if (primitive->hasIndices) {
          vkCmdDrawIndexed(cmdBuffer, primitive->indexCount, 1, primitive->firstIndex, 0, 0);
        } else {
          vkCmdDraw(cmdBuffer, primitive->vertexCount, 1, 0, 0);
        }
      }
    }
  };
  for (auto child : node->children) {
    renderNode(cmdBuffer, child, ImageIndex, alphaMode);
  }
}

void PBRIBLScene::updateScene(float deltaTime) {
  //  Update UBOs
  updateUniformData();
  updateParams();
  //  update shader buffer
  bufferManager->updateBuffer(uniformBuffers[currentFrame].scene, &sceneUboMatrices, sizeof(sceneUboMatrices), 0);
  bufferManager->updateBuffer(uniformBuffers[currentFrame].params, &shaderValuesParams, sizeof(shaderValuesParams), 0);
  bufferManager->updateBuffer(uniformBuffers[currentFrame].skybox, &uboSkybox, sizeof(uboSkybox), 0);

  // update animation and params
  if ((animate) && (models.scene.animations.size() > 0)) {
    animationTimer += deltaTime;
    if (animationTimer > models.scene.animations[animationIndex].end) {
      animationTimer -= models.scene.animations[animationIndex].end;
    }
    modelManager->updateAnimation(models.scene, animationIndex, animationTimer);
    updateMeshDataBuffer(currentFrame);
  }
}

void PBRIBLScene::updateMeshDataBuffer(uint32_t index) {  // @todo: optimize (no push, use fixed size)
  std::vector<ShaderMeshData> shaderMeshData;
  std::map<uint32_t, ShaderMeshData> meshDataMap;
  for (auto& node : models.scene.linearNodes) {
    if (node->mesh) {
      ShaderMeshData meshData{};
      memcpy(meshData.jointMatrix, node->mesh->jointMatrix.data(), sizeof(glm::mat4) * MAX_NUM_JOINTS);
      meshData.jointcount = node->mesh->jointcount;
      meshData.matrix = node->mesh->matrix;
      meshDataMap[node->mesh->index] = meshData;
    }
  }
  // Convert map to vector in correct order
  for (const auto& [idx, data] : meshDataMap) {
    shaderMeshData.push_back(data);
  }
  VkDeviceSize bufferSize = shaderMeshData.size() * sizeof(ShaderMeshData);
  bufferManager->updateBuffer(shaderMeshDataBuffers[index], shaderMeshData.data(), bufferSize, 0);
}

void PBRIBLScene::createSkyboxPipeline() {
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

void PBRIBLScene::loadAssets() {
  // load scene
  models.scene = modelManager->createModelFromFile(std::string(MODEL_DIR) + "/buster_drone/scene.gltf");
  createMaterialBuffer();
  createMeshDataBuffer();
  // Check and list unsupported extensions
  for (auto& ext : models.scene.extensions) {
    if (std::find(supportedExtensions.begin(), supportedExtensions.end(), ext) == supportedExtensions.end()) {
      spdlog::warn("Unsupported extension {}detected. Scene may not work or display as intended", ext);
    }
  }
  models.skybox = modelManager->createModelFromFile(std::string(MODEL_DIR) + "/box/box.gltf");

  // Load environment using base class method
  loadSceneEnvironment(std::string(TEXTURE_DIR) + "/skybox/workshop.hdr");
}

void PBRIBLScene::loadSceneEnvironment(std::string& filename) {
  // Use base class to load environment and generate IBL maps
  loadEnvironment(filename);
}

void PBRIBLScene::setupDescriptors() {
  /*
                        Descriptor Pool
  */
  uint32_t imageSamplerCount = 0;
  uint32_t materialCount = 0;
  uint32_t meshCount = 0;

  // Environment samplers (radiance, irradiance, brdf lut)
  imageSamplerCount += 3;

  std::vector<ModelManager::Model*> modellist = {&models.skybox, &models.scene};
  for (auto& model : modellist) {
    for (auto& material : model->materials) {
      imageSamplerCount += 5;
      materialCount++;
    }
    for (auto node : model->linearNodes) {
      if (node->mesh) {
        meshCount++;
      }
    }
  }
  u32 imageCnt = swapChainImages.size();
  std::vector<VkDescriptorPoolSize> poolSizes = {
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (4 + meshCount) * imageCnt},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageSamplerCount * imageCnt},
      // One SSBO for the shader material buffer and one SSBO for the mesh data buffer
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 + static_cast<uint32_t>(shaderMeshDataBuffers.size())}};
  VkDescriptorPoolCreateInfo descriptorPoolCI{};
  descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  descriptorPoolCI.pPoolSizes = poolSizes.data();
  descriptorPoolCI.maxSets = (2 + materialCount + meshCount) * imageCnt;
  vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool);
  // Scene (matrices and environment maps)
  {
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
    descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
    descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
    vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.scene);

    for (auto i = 0; i < descriptorSets.size(); i++) {
      VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
      descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      descriptorSetAllocInfo.descriptorPool = descriptorPool;
      descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.scene;
      descriptorSetAllocInfo.descriptorSetCount = 1;
      vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets[i].scene);
      VkDescriptorImageInfo irradianceInfo{};
      irradianceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      irradianceInfo.imageView = pbrEnvironment.irradianceCube.imageView;
      irradianceInfo.sampler = pbrEnvironment.irradianceCube.sampler;

      VkDescriptorImageInfo prefilteredInfo{};
      prefilteredInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      prefilteredInfo.imageView = pbrEnvironment.prefilteredCube.imageView;
      prefilteredInfo.sampler = pbrEnvironment.prefilteredCube.sampler;

      VkDescriptorImageInfo lutBrdfInfo{};
      lutBrdfInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      lutBrdfInfo.imageView = pbrEnvironment.lutBrdf.imageView;
      lutBrdfInfo.sampler = pbrEnvironment.lutBrdf.sampler;
      std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};

      writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      writeDescriptorSets[0].descriptorCount = 1;
      writeDescriptorSets[0].dstSet = descriptorSets[i].scene;
      writeDescriptorSets[0].dstBinding = 0;
      writeDescriptorSets[0].pBufferInfo = &uniformBuffers[i].scene.descriptor;

      writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      writeDescriptorSets[1].descriptorCount = 1;
      writeDescriptorSets[1].dstSet = descriptorSets[i].scene;
      writeDescriptorSets[1].dstBinding = 1;
      writeDescriptorSets[1].pBufferInfo = &uniformBuffers[i].params.descriptor;
      // Use base class PBR environment textures
      writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writeDescriptorSets[2].descriptorCount = 1;
      writeDescriptorSets[2].dstSet = descriptorSets[i].scene;
      writeDescriptorSets[2].dstBinding = 2;
      writeDescriptorSets[2].pImageInfo = &irradianceInfo;

      writeDescriptorSets[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDescriptorSets[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writeDescriptorSets[3].descriptorCount = 1;
      writeDescriptorSets[3].dstSet = descriptorSets[i].scene;
      writeDescriptorSets[3].dstBinding = 3;
      writeDescriptorSets[3].pImageInfo = &prefilteredInfo;

      writeDescriptorSets[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDescriptorSets[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writeDescriptorSets[4].descriptorCount = 1;
      writeDescriptorSets[4].dstSet = descriptorSets[i].scene;
      writeDescriptorSets[4].dstBinding = 4;
      writeDescriptorSets[4].pImageInfo = &lutBrdfInfo;

      vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
    }
  }

  // Material (samplers)
  {
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
    descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
    descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
    vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.material);

    // Per-Material descriptor sets
    for (auto& material : models.scene.materials) {
      VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
      descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      descriptorSetAllocInfo.descriptorPool = descriptorPool;
      descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.material;
      descriptorSetAllocInfo.descriptorSetCount = 1;
      vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &material.descriptorSet);

      auto normalDescriptor = material.normalTextureIndex != UINT32_MAX
                                  ? models.scene.textures[material.normalTextureIndex].descriptor
                                  : emptyTexture.descriptor;
      auto occlusionDescriptor = material.occlusionTextureIndex != UINT32_MAX
                                     ? models.scene.textures[material.occlusionTextureIndex].descriptor
                                     : emptyTexture.descriptor;
      auto emissiveDescriptor = material.emissiveTextureIndex != UINT32_MAX
                                    ? models.scene.textures[material.emissiveTextureIndex].descriptor
                                    : emptyTexture.descriptor;
      std::vector<VkDescriptorImageInfo> imageDescriptors = {emptyTexture.descriptor, emptyTexture.descriptor,
                                                             normalDescriptor, occlusionDescriptor, emissiveDescriptor};

      if (material.pbrWorkflows.metallicRoughness) {
        if (material.baseColorTextureIndex != UINT32_MAX) {
          imageDescriptors[0] = models.scene.textures[material.baseColorTextureIndex].descriptor;
        }
        if (material.metallicRoughnessTextureIndex != UINT32_MAX) {
          imageDescriptors[1] = models.scene.textures[material.metallicRoughnessTextureIndex].descriptor;
        }
      } else {
        if (material.pbrWorkflows.specularGlossiness) {
          if (material.extension.diffuseTextureIndex != UINT32_MAX) {
            imageDescriptors[0] = models.scene.textures[material.extension.diffuseTextureIndex].descriptor;
          }
          if (material.extension.specularGlossinessTextureIndex != UINT32_MAX) {
            imageDescriptors[1] = models.scene.textures[material.extension.specularGlossinessTextureIndex].descriptor;
          }
        }
      }

      std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};
      for (size_t i = 0; i < imageDescriptors.size(); i++) {
        writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSets[i].descriptorCount = 1;
        writeDescriptorSets[i].dstSet = material.descriptorSet;
        writeDescriptorSets[i].dstBinding = static_cast<uint32_t>(i);
        writeDescriptorSets[i].pImageInfo = &imageDescriptors[i];
      }

      vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
    }

    // Material buffer
    {
      std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
          {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
      };
      VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
      descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
      descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
      vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.materialBuffer);

      VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
      descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      descriptorSetAllocInfo.descriptorPool = descriptorPool;
      descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.materialBuffer;
      descriptorSetAllocInfo.descriptorSetCount = 1;
      vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSetMaterials);

      VkWriteDescriptorSet writeDescriptorSet{};
      writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writeDescriptorSet.descriptorCount = 1;
      writeDescriptorSet.dstSet = descriptorSetMaterials;
      writeDescriptorSet.dstBinding = 0;
      writeDescriptorSet.pBufferInfo = &shaderMaterialBuffer.descriptor;
      vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
    }

    // Mesh data buffer
    {
      std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
          {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
      };
      VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
      descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
      descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
      vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.meshDataBuffer);

      for (auto i = 0; i < descriptorSetsMeshData.size(); i++) {
        VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
        descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocInfo.descriptorPool = descriptorPool;
        descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.meshDataBuffer;
        descriptorSetAllocInfo.descriptorSetCount = 1;
        vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSetsMeshData[i]);

        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.dstSet = descriptorSetsMeshData[i];
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.pBufferInfo = &shaderMeshDataBuffers[i].descriptor;
        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
      }
    }
  }

  // Skybox (fixed set)
  {
    std::array<VkDescriptorSetLayoutBinding, 3> bindingsSkybox = {
        VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                                     nullptr}};

    VkDescriptorSetLayoutCreateInfo layoutInfoSkybox{};
    layoutInfoSkybox.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfoSkybox.bindingCount = static_cast<uint32_t>(bindingsSkybox.size());
    layoutInfoSkybox.pBindings = bindingsSkybox.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfoSkybox, nullptr, &skyboxDescriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create descriptor set layout!");
    }

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
      skyboxUBOBufferInfo.buffer = uniformBuffers[i].skybox.buffer;  // per-frame skybox UBO
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

void PBRIBLScene::addPipelineSet(const std::string prefix, const std::string vertexShader,
                                 const std::string fragmentShader) {
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
  inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
  rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
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
  depthStencilStateCI.depthTestEnable = VK_TRUE;
  depthStencilStateCI.depthWriteEnable = VK_TRUE;
  depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  depthStencilStateCI.front = depthStencilStateCI.back;
  depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

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
  shaderStages[0] = loadShader(vertexShader, VK_SHADER_STAGE_VERTEX_BIT);
  shaderStages[1] = loadShader(fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT);

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

  VkPipeline pipeline{};
  // Default pipeline with back-face culling
  VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineCI, nullptr, &pipeline));
  pipelines[prefix] = pipeline;
  // Double sided
  rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
  VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineCI, nullptr, &pipeline));
  pipelines[prefix + "_double_sided"] = pipeline;
  // Alpha blending
  rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
  blendAttachmentState.blendEnable = VK_TRUE;
  blendAttachmentState.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
  blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
  VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineCI, nullptr, &pipeline));
  pipelines[prefix + "_alpha_blending"] = pipeline;

  for (auto shaderStage : shaderStages) {
    vkDestroyShaderModule(device, shaderStage.module, nullptr);
  }
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
      shaderMaterial.physicalDescriptorTextureSet =
          material.metallicRoughnessTextureIndex != UINT32_MAX ? material.texCoordSets.metallicRoughness : -1;
      shaderMaterial.colorTextureSet = material.baseColorTextureIndex != UINT32_MAX ? material.texCoordSets.baseColor : -1;
    } else {
      if (material.pbrWorkflows.specularGlossiness) {
        // Specular glossiness workflow
        shaderMaterial.workflow = static_cast<float>(PBR_WORKFLOW_SPECULAR_GLOSSINESS);
        shaderMaterial.physicalDescriptorTextureSet =
            material.extension.specularGlossinessTextureIndex != UINT32_MAX ? material.texCoordSets.specularGlossiness : -1;
        shaderMaterial.colorTextureSet =
            material.extension.diffuseTextureIndex != UINT32_MAX ? material.texCoordSets.baseColor : -1;
        shaderMaterial.diffuseFactor = material.extension.diffuseFactor;
        shaderMaterial.specularFactor = glm::vec4(material.extension.specularFactor, 1.0f);
      }
    }
    shaderMaterials.push_back(shaderMaterial);
  }
  // init shaderMaterialBuffer
  VkDeviceSize bufferSize = shaderMaterials.size() * sizeof(ShaderMaterial);
  shaderMaterialBuffer = bufferManager->createGPULocalBuffer(
      shaderMaterials.data(), bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

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
        bufferManager->createBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    bufferManager->updateBuffer(shaderMeshDataBuffer, shaderMeshData.data(), bufferSize, 0);
    // Update descriptor
    shaderMeshDataBuffer.descriptor.buffer = shaderMeshDataBuffer.buffer;
    shaderMeshDataBuffer.descriptor.offset = 0;
    shaderMeshDataBuffer.descriptor.range = bufferSize;
    shaderMeshDataBuffer.device = device;
  }
}

void PBRIBLScene::cleanupResources() {
  spdlog::info("Cleanup called - destroying {} pipelines", pipelines.size());
  spdlog::info("pipelineLayout: {}", (void*)pipelineLayout);
  spdlog::info("skyboxPipelineLayout: {}", (void*)skyboxPipelineLayout);
  // Destroy all pipelines
  for (auto& [name, pipeline] : pipelines) {
    if (pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, pipeline, nullptr);
    }
  }
  pipelines.clear();

  // Clean up skybox pipeline resources
  if (skyboxPipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, skyboxPipeline, nullptr);
    skyboxPipeline = VK_NULL_HANDLE;
  }

  if (skyboxPipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, skyboxPipelineLayout, nullptr);
    skyboxPipelineLayout = VK_NULL_HANDLE;
  }

  if (skyboxDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, skyboxDescriptorSetLayout, nullptr);
    skyboxDescriptorSetLayout = VK_NULL_HANDLE;
  }

  // Destroy pipeline layout
  if (pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    pipelineLayout = VK_NULL_HANDLE;
  }

  // Destroy descriptor pool (this also frees all descriptor sets including skyboxDescriptorSets)
  if (descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    descriptorPool = VK_NULL_HANDLE;
  }

  // Destroy descriptor set layouts
  if (descriptorSetLayouts.scene != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
    descriptorSetLayouts.scene = VK_NULL_HANDLE;
  }
  if (descriptorSetLayouts.material != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.material, nullptr);
    descriptorSetLayouts.material = VK_NULL_HANDLE;
  }
  if (descriptorSetLayouts.materialBuffer != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.materialBuffer, nullptr);
    descriptorSetLayouts.materialBuffer = VK_NULL_HANDLE;
  }
  if (descriptorSetLayouts.meshDataBuffer != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.meshDataBuffer, nullptr);
    descriptorSetLayouts.meshDataBuffer = VK_NULL_HANDLE;
  }

  // Clean up models, buffers, textures...
  modelManager->destroyModel(models.scene);
  modelManager->destroyModel(models.skybox);

  // Clean up uniform buffers and skybox param buffer
  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    bufferManager->destroyBuffer(uniformBuffers[i].params);
    bufferManager->destroyBuffer(uniformBuffers[i].skybox);
    bufferManager->destroyBuffer(uniformBuffers[i].scene);
    bufferManager->destroyBuffer(shaderMeshDataBuffers[i]);
  }

  // Clean up skybox param buffer
  bufferManager->destroyBuffer(skyBoxParamBuffer);

  // Clean up other buffers
  bufferManager->destroyBuffer(shaderMaterialBuffer);
  textureManager->destroyTexture(emptyTexture);
  cleanupPBREnvironment();
}

void PBRIBLScene::prepareUniformBuffers() {
  for (auto& uniformBuffer : uniformBuffers) {
    uniformBuffer.scene =
        bufferManager->createBuffer(sizeof(sceneUboMatrices), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    uniformBuffer.params =
        bufferManager->createBuffer(sizeof(shaderValuesParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    // skybox
    uniformBuffer.skybox =
        bufferManager->createBuffer(sizeof(uboSkybox), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    skyBoxParamBuffer = bufferManager->createGPULocalBuffer(
        &uboParamsSkybox, sizeof(uboParamsSkybox), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  }
  updateUniformData();
}

void PBRIBLScene::updateUniformData() {
  // Scene
  float aspectRatio = swapChainExtent.width / static_cast<float>(swapChainExtent.height);
  sceneUboMatrices.projection = camera.getProjectionMatrix(aspectRatio);
  sceneUboMatrices.view = camera.getViewMatrix();
  // Center and scale model
  float scale =
      (1.0f / std::max(models.scene.aabb[0][0], std::max(models.scene.aabb[1][1], models.scene.aabb[2][2]))) * 0.5f;
  glm::vec3 translate = -glm::vec3(models.scene.aabb[3][0], models.scene.aabb[3][1], models.scene.aabb[3][2]);
  translate += -0.5f * glm::vec3(models.scene.aabb[0][0], models.scene.aabb[1][1], models.scene.aabb[2][2]);

  sceneUboMatrices.model = glm::mat4(1.0f);
  sceneUboMatrices.model[0][0] = scale;
  sceneUboMatrices.model[1][1] = scale;
  sceneUboMatrices.model[2][2] = scale;
  sceneUboMatrices.model = glm::translate(sceneUboMatrices.model, translate);

  // Rotate 90 degrees around X-axis to convert Y-up to Z-up
  sceneUboMatrices.model = glm::rotate(sceneUboMatrices.model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

  // Shader requires camera position in world space
  glm::mat4 cv = glm::inverse(camera.getViewMatrix());
  sceneUboMatrices.camPos = glm::vec3(cv[3]);

  // Skybox
  uboSkybox.proj = sceneUboMatrices.projection;  // Same projection as main scene
  // For skybox, we want the view matrix without translation
  // This keeps the skybox centered around the camera
  glm::mat4 viewWithoutTranslation = glm::mat4(glm::mat3(camera.getViewMatrix()));
  uboSkybox.model = viewWithoutTranslation;
  uboSkybox.model = glm::rotate(uboSkybox.model, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
}

void PBRIBLScene::updateParams() {
  shaderValuesParams.lightDir =
      glm::vec4(sin(glm::radians(lightSource.rotation.x)) * cos(glm::radians(lightSource.rotation.y)),
                sin(glm::radians(lightSource.rotation.y)),
                cos(glm::radians(lightSource.rotation.x)) * cos(glm::radians(lightSource.rotation.y)), 0.0f);
}