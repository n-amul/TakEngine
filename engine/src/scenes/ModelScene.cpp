#include "ModelScene.hpp"

#include <iostream>

#include "core/utils.hpp"

void ModelScene::loadResources() {
  emptyTexture = textureManager->createDefault();
  // load scene
  scene = modelManager->createModelFromFile(std::string(MODEL_DIR) + "/buster_drone/scene.gltf");
  createMaterialBuffer();
  createMeshDataBuffer();

  prepareUniformBuffers();
  setupDescriptors();
  updateParams();
}

void ModelScene::prepareUniformBuffers() {
  uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  for (auto& uniformBuffer : uniformBuffers) {
    int size = 0;
    uniformBuffer.scene =
        bufferManager->createBuffer(sizeof(uboMatrices), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    uniformBuffer.params = bufferManager->createBuffer(sizeof(shaderValuesParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
  }
  updateUniformData();
}

void ModelScene::updateUniformData() {
  float aspectRatio = swapChainExtent.width / static_cast<float>(swapChainExtent.height);
  uboMatrices.projection = camera.getProjectionMatrix(aspectRatio);
  uboMatrices.view = camera.getViewMatrix();

  // move to z-up coord, Center and scale model
  float scale = (1.0f / std::max(scene.aabb[0][0], std::max(scene.aabb[1][1], scene.aabb[2][2]))) * 0.5f;
  glm::vec3 translate = -glm::vec3(scene.aabb[3][0], scene.aabb[3][1], scene.aabb[3][2]);
  translate += -0.5f * glm::vec3(scene.aabb[0][0], scene.aabb[1][1], scene.aabb[2][2]);

  uboMatrices.model = glm::mat4(1.0f);
  // Apply scaling
  uboMatrices.model[0][0] = scale;
  uboMatrices.model[1][1] = scale;
  uboMatrices.model[2][2] = scale;

  // Rotate 90 degrees around X-axis to convert Y-up to Z-up
  uboMatrices.model = glm::rotate(uboMatrices.model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

  // Apply translation
  uboMatrices.model = glm::translate(uboMatrices.model, translate);

  // Shader requires camera position in world space
  glm::mat4 cv = glm::inverse(camera.getViewMatrix());
  uboMatrices.camPos = glm::vec3(cv[3]);
}

void ModelScene::updateParams() {
  shaderValuesParams.lightDir = glm::vec4(sin(glm::radians(lightSource.rotation.x)) * cos(glm::radians(lightSource.rotation.y)), sin(glm::radians(lightSource.rotation.y)),
                                          cos(glm::radians(lightSource.rotation.x)) * cos(glm::radians(lightSource.rotation.y)), 0.0f);
}

void ModelScene::renderNode(VkCommandBuffer cmdBuffer, tak::Node* node, uint32_t ImageIndex, tak::Material::AlphaMode alphaMode) {
  if (node->mesh) {
    // Render mesh primitives
    for (tak::Primitive* primitive : node->mesh->primitives) {
      if (scene.materials[primitive->materialIndex].alphaMode == alphaMode) {
        std::string pipelineName = "pbr";
        std::string pipelineVariant = "";

        if (scene.materials[primitive->materialIndex].unlit) {
          // TODO: add unlit shader and pipeline
          pipelineName = "unlit";
        };

        // Material properties define if we e.g. need to bind a pipeline variant with culling disabled (double sided)
        if (alphaMode == tak::Material::ALPHAMODE_BLEND) {
          pipelineVariant = "_alpha_blending";
        } else {
          if (scene.materials[primitive->materialIndex].doubleSided) {
            pipelineVariant = "_double_sided";
          }
        }
        const VkPipeline pipeline = pipelines[pipelineName + pipelineVariant];

        if (boundPipeline != pipeline) {
          vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
          boundPipeline = pipeline;
        }

        const std::vector<VkDescriptorSet> descriptorsets = {
            descriptorSetsScene[currentFrame],                        // set 0
            scene.materials[primitive->materialIndex].descriptorSet,  // set 1
            descriptorSetsMeshData[currentFrame],                     // set 2
            descriptorSetMaterials                                    // set 3
        };
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, static_cast<uint32_t>(descriptorsets.size()), descriptorsets.data(), 0, NULL);

        // Pass material index for this primitive using a push constant, the shader uses this to index into the material buffer
        MeshPushConstantBlock pushConstantBlock{};
        // @todo: index
        pushConstantBlock.meshIndex = node->mesh->index;
        pushConstantBlock.materialIndex = scene.materials[primitive->materialIndex].materialIndex;

        vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPushConstantBlock), &pushConstantBlock);

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

void ModelScene::createMaterialBuffer() {
  std::vector<ShaderMaterial> shaderMaterials{};

  for (size_t i = 0; i < scene.materials.size(); i++) {
    tak::Material& material = scene.materials[i];
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
// We place all the shader data blocks for all meshes (node) into a single buffer
// This allows us to use one singular allocation instead of having to do lots of small allocations per mesh
// The vertex shader then get's the index into this buffer from a push constant set per mesh
void ModelScene::createMeshDataBuffer() {
  shaderMeshDataBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  std::vector<ShaderMeshData> shaderMeshData{};
  for (auto& node : scene.linearNodes) {
    ShaderMeshData meshData{};
    if (node->mesh) {
      memcpy(meshData.jointMatrix, node->mesh->jointMatrix.data(), sizeof(glm::mat4) * MAX_NUM_JOINTS);
      meshData.jointCount = node->mesh->jointcount;
      meshData.matrix = node->mesh->matrix;
      shaderMeshData.push_back(meshData);
    }
  }
  for (auto& shaderMeshDataBuffer : shaderMeshDataBuffers) {
    if (shaderMeshDataBuffer.buffer != VK_NULL_HANDLE) {
      bufferManager->destroyBuffer(shaderMeshDataBuffer);
    }
    // create buffer
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

void ModelScene::setupDescriptors() {
  descriptorSetsScene.resize(MAX_FRAMES_IN_FLIGHT);
  descriptorSetMaterials;  // single shared
  descriptorSetsMeshData.resize(MAX_FRAMES_IN_FLIGHT);

  // Descriptor Pool - Fixed calculations
  uint32_t imageSamplerCount = 0;
  uint32_t materialCount = static_cast<uint32_t>(scene.materials.size());
  uint32_t meshCount = 0;

  // Count meshes
  for (auto node : scene.linearNodes) {
    if (node->mesh) {
      meshCount++;
    }
  }

  uint32_t imageCount = static_cast<uint32_t>(swapChainImages.size());
  // Scene descriptors
  uint32_t sceneUBOCount = 1 * MAX_FRAMES_IN_FLIGHT;
  // Material descriptors: 5 image samplers per material
  uint32_t materialImageSamplerCount = 5 * materialCount;
  // Mesh data descriptors: 1 storage buffer per frame
  uint32_t meshDataStorageBufferCount = 1 * MAX_FRAMES_IN_FLIGHT;
  // Material buffer: 1 storage buffer (shared)
  uint32_t materialStorageBufferCount = 1;

  std::vector<VkDescriptorPoolSize> poolSizes = {// UBOs
                                                 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sceneUBOCount + (meshCount * imageCount)},
                                                 // Image samplers
                                                 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, materialImageSamplerCount},
                                                 // Storage buffers: mesh data + material buffer
                                                 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, meshDataStorageBufferCount + materialStorageBufferCount}};

  // to make sure we have enough size
  for (auto& poolSize : poolSizes) {
    poolSize.descriptorCount = static_cast<uint32_t>(poolSize.descriptorCount * 1.2f);
  }

  VkDescriptorPoolCreateInfo descriptorPoolCI{};
  descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  descriptorPoolCI.pPoolSizes = poolSizes.data();

  descriptorPoolCI.maxSets = static_cast<uint32_t>((5 + materialCount) * 1.2f);

  VkResult result = vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool);
  if (result != VK_SUCCESS) {
    throw std::runtime_error("Failed to create descriptor pool!");
  }

  {
    // set layout
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
    descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
    descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
    vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.scene);

    // Descriptor sets
    for (auto i = 0; i < descriptorSetsScene.size(); i++) {
      VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
      descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      descriptorSetAllocInfo.descriptorPool = descriptorPool;
      descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.scene;
      descriptorSetAllocInfo.descriptorSetCount = 1;
      vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSetsScene[i]);

      std::array<VkWriteDescriptorSet, 2> writeDescriptorSets{};

      writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      writeDescriptorSets[0].descriptorCount = 1;
      writeDescriptorSets[0].dstSet = descriptorSetsScene[i];
      writeDescriptorSets[0].dstBinding = 0;
      writeDescriptorSets[0].pBufferInfo = &uniformBuffers[i].scene.descriptor;

      writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      writeDescriptorSets[1].descriptorCount = 1;
      writeDescriptorSets[1].dstSet = descriptorSetsScene[i];
      writeDescriptorSets[1].dstBinding = 1;
      writeDescriptorSets[1].pBufferInfo = &uniformBuffers[i].params.descriptor;

      vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
    }
  }

  // Material (samplers)
  {
    // layout
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},  // baseColor
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},  // metallicRoughness
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},  // normal
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},  // occlusion
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},  // emissive
    };
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
    descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
    descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
    vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.material);

    // Per-Material descriptor sets
    for (auto& material : scene.materials) {
      VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
      descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      descriptorSetAllocInfo.descriptorPool = descriptorPool;
      descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.material;
      descriptorSetAllocInfo.descriptorSetCount = 1;
      vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &material.descriptorSet);
      auto normalDescriptor = material.normalTextureIndex != UINT32_MAX ? scene.textures[material.normalTextureIndex].descriptor : emptyTexture.descriptor;
      auto occlusionDescriptor = material.occlusionTextureIndex != UINT32_MAX ? scene.textures[material.occlusionTextureIndex].descriptor : emptyTexture.descriptor;
      auto emissiveDescriptor = material.emissiveTextureIndex != UINT32_MAX ? scene.textures[material.emissiveTextureIndex].descriptor : emptyTexture.descriptor;

      std::vector<VkDescriptorImageInfo> imageDescriptors = {emptyTexture.descriptor, emptyTexture.descriptor, normalDescriptor, occlusionDescriptor, emissiveDescriptor};

      if (material.pbrWorkflows.metallicRoughness) {
        if (material.baseColorTextureIndex != UINT32_MAX) {
          imageDescriptors[0] = scene.textures[material.baseColorTextureIndex].descriptor;
        }
        if (material.metallicRoughnessTextureIndex != UINT32_MAX) {
          imageDescriptors[1] = scene.textures[material.metallicRoughnessTextureIndex].descriptor;
        }
      } else {
        if (material.pbrWorkflows.specularGlossiness) {
          if (material.extension.diffuseTextureIndex != UINT32_MAX) {
            imageDescriptors[0] = scene.textures[material.extension.diffuseTextureIndex].descriptor;
          }
          if (material.extension.specularGlossinessTextureIndex != UINT32_MAX) {
            imageDescriptors[1] = scene.textures[material.extension.specularGlossinessTextureIndex].descriptor;
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
}

void ModelScene::createPipeline() {
  // spdlog::info("creating cubemap pipeline...");
  // createSkyboxPipeline();
  spdlog::info("creating Model pipeline...");
  createModelPipeline("pbr");
  // TODO: important // create KHR_materials_unlit pipeline
}

void ModelScene::recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
  boundPipeline = VK_NULL_HANDLE;
  // Note: Render pass is already begun in base class recordCommandBuffer()
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

  // scene render
  boundPipeline = VK_NULL_HANDLE;
  VkDeviceSize offsets_scene[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &scene.vertices.buffer, offsets_scene);
  if (scene.indices.buffer != VK_NULL_HANDLE) {
    vkCmdBindIndexBuffer(commandBuffer, scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
  }

  // Opaque primitives first
  for (auto node : scene.nodes) {
    renderNode(commandBuffer, node, imageIndex, tak::Material::ALPHAMODE_OPAQUE);
  }
  // Alpha masked primitives
  for (auto node : scene.nodes) {
    renderNode(commandBuffer, node, imageIndex, tak::Material::ALPHAMODE_MASK);
  }
  // Transparent primitives
  // TODO: Correct depth sorting
  for (auto node : scene.nodes) {
    renderNode(commandBuffer, node, imageIndex, tak::Material::ALPHAMODE_BLEND);
  }
}

void ModelScene::updateScene(float deltaTime) {
  // updateSkyboxUniformBuffer();
  //  Update UBOs
  updateUniformData();
  updateParams();
  //  update shader buffer (TODO: add mapped pointer inside the buffer for performance)
  bufferManager->updateBuffer(uniformBuffers[currentFrame].scene, &uboMatrices, sizeof(uboMatrices), 0);
  bufferManager->updateBuffer(uniformBuffers[currentFrame].params, &shaderValuesParams, sizeof(shaderValuesParams), 0);

  // update animation and params
  if ((animate) && (scene.animations.size() > 0)) {
    animationTimer += deltaTime;
    if (animationTimer > scene.animations[animationIndex].end) {
      animationTimer -= scene.animations[animationIndex].end;
    }
    modelManager->updateAnimation(scene, animationIndex, animationTimer);
    updateMeshDataBuffer(currentFrame);
  }
}

void ModelScene::createModelPipeline(const std::string& prefix) {
  // Load shaders
  std::string vertPath = std::string(SHADER_DIR) + "/pbr.vert.spv";
  std::string fragPath = std::string(SHADER_DIR) + "/pbr.frag.spv";
  auto vertShaderCode = readFile(vertPath);
  auto fragShaderCode = readFile(fragPath);
  spdlog::info("Loading vertex shader from: {}", vertPath);
  spdlog::info("Loading fragment shader from: {}", fragPath);

  VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
  VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

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

  VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
  //  input configuration
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

  // Viewport state (dynamic)
  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  // Rasterizer
  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

  // Multisampling (disabled)
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;  // TODO: add MSAA

  // Color blending
  VkPipelineColorBlendAttachmentState blendAttachmentState{};
  blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  blendAttachmentState.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &blendAttachmentState;

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
  const std::vector<VkDescriptorSetLayout> setLayouts = {
      descriptorSetLayouts.scene,           // 0
      descriptorSetLayouts.material,        // 1
      descriptorSetLayouts.meshDataBuffer,  // 2
      descriptorSetLayouts.materialBuffer   // 3
  };
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = setLayouts.size();
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.size = sizeof(MeshPushConstantBlock);
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
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
  pipelineInfo.renderPass = renderPass;  // Use base class render pass
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineInfo.basePipelineIndex = -1;

  VkPipeline pipeline{};
  // TODO: add cache later
  //  Default pipeline with back-face culling
  if (vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create graphics pipeline!");
  }
  pipelines[prefix] = pipeline;
  // Double sided
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  if (vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create graphics pipeline!");
  }
  pipelines[prefix + "_double_sided"] = pipeline;

  // Alpha blending
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  blendAttachmentState.blendEnable = VK_TRUE;
  blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
  blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
  if (vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineInfo, nullptr, &pipeline)) {
    throw std::runtime_error("Failed to create graphics pipeline!");
  }
  pipelines[prefix + "_alpha_blending"] = pipeline;

  // Cleanup shader modules
  for (auto shaderStage : shaderStages) {
    vkDestroyShaderModule(device, shaderStage.module, nullptr);
  }
  spdlog::info("Model pipeline created successfully");
}

void ModelScene::updateMeshDataBuffer(uint32_t index) {  // @todo: optimize (no push, use fixed size)
  std::vector<ShaderMeshData> shaderMeshData;
  std::map<uint32_t, ShaderMeshData> meshDataMap;
  for (auto& node : scene.linearNodes) {
    if (node->mesh) {
      ShaderMeshData meshData{};
      memcpy(meshData.jointMatrix, node->mesh->jointMatrix.data(), sizeof(glm::mat4) * MAX_NUM_JOINTS);
      meshData.jointCount = node->mesh->jointcount;
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

void ModelScene::cleanupResources() {
  spdlog::info("Cleaning up skybox resources");

  spdlog::info("Cleaning up model resources");
  vkDeviceWaitIdle(device);
  // Destroy buffers
  for (auto& ub : uniformBuffers) {
    bufferManager->destroyBuffer(ub.scene);
    bufferManager->destroyBuffer(ub.params);
  }
  for (auto& buf : shaderMeshDataBuffers) bufferManager->destroyBuffer(buf);
  bufferManager->destroyBuffer(shaderMaterialBuffer);

  // Destroy descriptor layouts and pool
  vkDestroyDescriptorPool(device, descriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.material, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.materialBuffer, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.meshDataBuffer, nullptr);

  // Destroy pipeline and layout
  for (auto p : pipelines) {
    vkDestroyPipeline(device, p.second, nullptr);
  }
  vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

  // Destroy textures and model
  textureManager->destroyTexture(emptyTexture);
  modelManager->destroyModel(scene);
}

// skybox init
void ModelScene::createSkyboxPipeline() {
  spdlog::info("Creating skybox pipeline");

  // Load shaders
  std::string vertPath = std::string(SHADER_DIR) + "/skybox.vert.spv";
  std::string fragPath = std::string(SHADER_DIR) + "/skybox.frag.spv";
  auto vertShaderCode = readFile(vertPath);
  auto fragShaderCode = readFile(fragPath);
  // Add file existence check
  spdlog::info("Skybox vertex shader size: {} bytes", vertShaderCode.size());
  spdlog::info("Skybox fragment shader size: {} bytes", fragShaderCode.size());

  VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
  VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

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

  VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

  // Vertex input configuration for skybox
  auto bindingDescription = SkyboxVertex::getBindingDescription();
  auto attributeDescriptions = SkyboxVertex::getAttributeDescriptions();

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

  // Rasterizer - Important: Front face culling for skybox
  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;  // Cull front faces since we're inside the cube (so front is discarded)
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  // Multisampling (disabled)
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

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

  // You want the skybox to always appear “behind” everything else,
  // By not writing its depth, the depth buffer still holds valid values from real geometry later.
  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_FALSE;                   // Don't write to depth buffer
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;  // Pass if depth is less or equal
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  // Pipeline layout
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &skyboxDescriptorSetLayout;

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &skyboxPipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create skybox pipeline layout!");
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
  pipelineInfo.layout = skyboxPipelineLayout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skyboxPipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create skybox graphics pipeline!");
  }

  vkDestroyShaderModule(device, fragShaderModule, nullptr);
  vkDestroyShaderModule(device, vertShaderModule, nullptr);

  spdlog::info("Skybox pipeline created successfully");
}

void ModelScene::createSkyboxVertexBuffer() {
  VkDeviceSize bufferSize = sizeof(skyboxVertices[0]) * skyboxVertices.size();
  skyboxVertexBuffer = bufferManager->createGPULocalBuffer(skyboxVertices.data(), bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void ModelScene::createSkyboxIndexBuffer() {
  VkDeviceSize bufferSize = sizeof(skyboxIndices[0]) * skyboxIndices.size();
  skyboxIndexBuffer = bufferManager->createGPULocalBuffer(skyboxIndices.data(), bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void ModelScene::createSkyboxDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding uboLayoutBinding{};
  uboLayoutBinding.binding = 0;
  uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboLayoutBinding.descriptorCount = 1;
  uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkDescriptorSetLayoutBinding samplerLayoutBinding{};
  samplerLayoutBinding.binding = 1;
  samplerLayoutBinding.descriptorCount = 1;
  samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &skyboxDescriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create skybox descriptor set layout!");
  }
}

void ModelScene::createSkyboxUniformBuffers() {
  VkDeviceSize bufferSize = sizeof(SkyboxUniformBufferObject);
  skyboxUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  skyboxUniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    // change to mapped = true
    skyboxUniformBuffers[i] =
        bufferManager->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkMapMemory(device, skyboxUniformBuffers[i].memory, 0, bufferSize, 0, &skyboxUniformBuffersMapped[i]);
  }
}

void ModelScene::updateSkyboxUniformBuffer() {
  SkyboxUniformBufferObject ubo{};
  // Remove translation from view matrix for skybox
  glm::mat4 view = camera.getViewMatrix();
  view[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);  // Zero out translation
  ubo.view = view;

  float aspectRatio = swapChainExtent.width / static_cast<float>(swapChainExtent.height);
  ubo.proj = camera.getProjectionMatrix(aspectRatio);

  memcpy(skyboxUniformBuffersMapped[currentFrame], &ubo, sizeof(ubo));
}

void ModelScene::createSkyboxTexture() {
  skyboxTexture = textureManager->createCubemapFromSingleFile(std::string(TEXTURE_DIR) + "/skybox/cubemap.png", VK_FORMAT_R8G8B8A8_SRGB);
}

void ModelScene::createSkyboxDescriptorSets() {
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
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = skyboxDescriptorSets[i];
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
  }
}
