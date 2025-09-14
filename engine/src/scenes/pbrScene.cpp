#include "pbrScene.hpp"

#include <spdlog/spdlog.h>

#include <cstring>
#include <stdexcept>

#include "../core/utils.hpp"

PBRScene::PBRScene() {
  window_width = 1920;
  window_height = 1080;
  title = "Vulkan PBR Scene";
  name = "PBRScene";
}

void PBRScene::createPipeline() {
  spdlog::info("Creating PBR pipeline");

  // Load shaders
  std::string vertPath = std::string(SHADER_DIR) + "/pbr_vert.spv";
  std::string fragPath = std::string(SHADER_DIR) + "/pbr_frag.spv";
  auto vertShaderCode = readFile(vertPath);
  auto fragShaderCode = readFile(fragPath);

  spdlog::info("Loading vertex shader from: {}", vertPath);
  spdlog::info("Loading fragment shader from: {}", fragPath);

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

  // Vertex input
  auto bindingDescription = ModelManager::Vertex::getBindingDescription();
  auto attributeDescriptions = ModelManager::Vertex::getAttributeDescriptions();

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

  // Pipeline layout
  std::array<VkDescriptorSetLayout, 3> setLayouts = {globalDescriptorSetLayout, modelDescriptorSetLayout, materialDescriptorSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create pipeline layout!");
  }

  // Graphics pipeline
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

  // Cleanup
  vkDestroyShaderModule(device, fragShaderModule, nullptr);
  vkDestroyShaderModule(device, vertShaderModule, nullptr);

  spdlog::info("PBR pipeline created successfully");
}

void PBRScene::loadResources() {
  spdlog::info("Loading PBR resources");

  // Initialize model manager
  modelManager = std::make_shared<ModelManager>(context, bufferManager, textureManager, commandBufferUtils);

  // Create descriptor set layouts first
  createDescriptorSetLayouts();

  // Create default textures
  createDefaultTextures();

  // Load models
  loadModels();

  // Setup lights
  setupLights();

  // Create uniform buffers
  createUniformBuffers();

  // Create descriptor pool and sets
  createDescriptorPool();
  createDescriptorSets();
}

void PBRScene::createDescriptorSetLayouts() {
  // Global descriptor set layout (set = 0)
  {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    // Global UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Light UBO
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &globalDescriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create global descriptor set layout!");
    }
  }

  // Model descriptor set layout (set = 1)
  {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &modelDescriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create model descriptor set layout!");
    }
  }

  // Material descriptor set layout (set = 2)
  {
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};

    // Material UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Base color texture
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Metallic-roughness texture
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Normal texture
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Occlusion texture
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Emissive texture
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &materialDescriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create material descriptor set layout!");
    }
  }
}

void PBRScene::createDescriptorPool() {
  // Calculate pool sizes based on models and materials
  uint32_t totalMaterials = 0;
  for (const auto& model : models) {
    totalMaterials += static_cast<uint32_t>(model.materials.size());
  }

  std::array<VkDescriptorPoolSize, 2> poolSizes{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * (3 + totalMaterials));  // Global, Model, Light, Materials

  poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * totalMaterials * 5);  // 5 textures per material

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * (2 + totalMaterials));  // Global, Model, Materials

  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create descriptor pool!");
  }
}

void PBRScene::createDescriptorSets() {
  // Global descriptor sets
  {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, globalDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    globalDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, globalDescriptorSets.data()) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate global descriptor sets!");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      std::array<VkDescriptorBufferInfo, 2> bufferInfos{};

      bufferInfos[0].buffer = globalUniformBuffers[i].buffer;
      bufferInfos[0].offset = 0;
      bufferInfos[0].range = sizeof(GlobalUBO);

      bufferInfos[1].buffer = lightUniformBuffers[i].buffer;
      bufferInfos[1].offset = 0;
      bufferInfos[1].range = sizeof(LightUBO);

      std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

      descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[0].dstSet = globalDescriptorSets[i];
      descriptorWrites[0].dstBinding = 0;
      descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptorWrites[0].descriptorCount = 1;
      descriptorWrites[0].pBufferInfo = &bufferInfos[0];

      descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[1].dstSet = globalDescriptorSets[i];
      descriptorWrites[1].dstBinding = 1;
      descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptorWrites[1].descriptorCount = 1;
      descriptorWrites[1].pBufferInfo = &bufferInfos[1];

      vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
  }

  // Model descriptor sets
  {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, modelDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    modelDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, modelDescriptorSets.data()) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate model descriptor sets!");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      VkDescriptorBufferInfo bufferInfo{};
      bufferInfo.buffer = modelUniformBuffers[i].buffer;
      bufferInfo.offset = 0;
      bufferInfo.range = sizeof(ModelUBO);

      VkWriteDescriptorSet descriptorWrite{};
      descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrite.dstSet = modelDescriptorSets[i];
      descriptorWrite.dstBinding = 0;
      descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptorWrite.descriptorCount = 1;
      descriptorWrite.pBufferInfo = &bufferInfo;

      vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }
  }

  // Material descriptor sets (per model, per material)
  materialDescriptorSets.resize(models.size());
  for (size_t modelIdx = 0; modelIdx < models.size(); ++modelIdx) {
    const auto& model = models[modelIdx];
    materialDescriptorSets[modelIdx].resize(model.materials.size() * MAX_FRAMES_IN_FLIGHT);

    for (size_t matIdx = 0; matIdx < model.materials.size(); ++matIdx) {
      for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
        VkDescriptorSetLayout layout = materialDescriptorSetLayout;

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        size_t descIdx = matIdx * MAX_FRAMES_IN_FLIGHT + frame;
        if (vkAllocateDescriptorSets(device, &allocInfo, &materialDescriptorSets[modelIdx][descIdx]) != VK_SUCCESS) {
          throw std::runtime_error("Failed to allocate material descriptor set!");
        }

        // Update descriptor set
        std::vector<VkWriteDescriptorSet> descriptorWrites;
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkDescriptorImageInfo> imageInfos;

        // Material UBO
        VkDescriptorBufferInfo materialBufferInfo{};
        materialBufferInfo.buffer = materialUniformBuffers[modelIdx][matIdx * MAX_FRAMES_IN_FLIGHT + frame].buffer;
        materialBufferInfo.offset = 0;
        materialBufferInfo.range = sizeof(MaterialUBO);
        bufferInfos.push_back(materialBufferInfo);

        VkWriteDescriptorSet uboWrite{};
        uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboWrite.dstSet = materialDescriptorSets[modelIdx][descIdx];
        uboWrite.dstBinding = 0;
        uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboWrite.descriptorCount = 1;
        uboWrite.pBufferInfo = &bufferInfos.back();
        descriptorWrites.push_back(uboWrite);

        // Textures
        const auto& material = model.materials[matIdx];

        // Helper lambda to add texture
        auto addTexture = [&](int textureIndex, const TextureManager::Texture& defaultTex, uint32_t binding) {
          VkDescriptorImageInfo imageInfo{};
          imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

          if (textureIndex >= 0 && textureIndex < model.textures.size()) {
            imageInfo.imageView = model.textures[textureIndex].imageView;
            imageInfo.sampler = model.textures[textureIndex].sampler;
          } else {
            imageInfo.imageView = defaultTex.imageView;
            imageInfo.sampler = defaultTex.sampler;
          }

          imageInfos.push_back(imageInfo);

          VkWriteDescriptorSet write{};
          write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
          write.dstSet = materialDescriptorSets[modelIdx][descIdx];
          write.dstBinding = binding;
          write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
          write.descriptorCount = 1;
          write.pImageInfo = &imageInfos.back();
          descriptorWrites.push_back(write);
        };

        addTexture(material.baseColorTextureIndex, defaultWhiteTexture, 1);
        addTexture(material.metallicRoughnessTextureIndex, defaultWhiteTexture, 2);
        addTexture(material.normalTextureIndex, defaultNormalTexture, 3);
        addTexture(material.occlusionTextureIndex, defaultWhiteTexture, 4);
        addTexture(material.emissiveTextureIndex, defaultBlackTexture, 5);

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
      }
    }
  }
}

void PBRScene::createUniformBuffers() {
  VkDeviceSize globalBufferSize = sizeof(GlobalUBO);
  VkDeviceSize modelBufferSize = sizeof(ModelUBO);
  VkDeviceSize lightBufferSize = sizeof(LightUBO);
  VkDeviceSize materialBufferSize = sizeof(MaterialUBO);

  globalUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  globalUniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
  modelUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  modelUniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
  lightUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  lightUniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    // Global UBO
    globalUniformBuffers[i] =
        bufferManager->createBuffer(globalBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkMapMemory(device, globalUniformBuffers[i].memory, 0, globalBufferSize, 0, &globalUniformBuffersMapped[i]);

    // Model UBO
    modelUniformBuffers[i] =
        bufferManager->createBuffer(modelBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkMapMemory(device, modelUniformBuffers[i].memory, 0, modelBufferSize, 0, &modelUniformBuffersMapped[i]);

    // Light UBO
    lightUniformBuffers[i] =
        bufferManager->createBuffer(lightBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkMapMemory(device, lightUniformBuffers[i].memory, 0, lightBufferSize, 0, &lightUniformBuffersMapped[i]);
  }

  // Material UBOs (per model, per material)
  materialUniformBuffers.resize(models.size());
  materialUniformBuffersMapped.resize(models.size());

  for (size_t modelIdx = 0; modelIdx < models.size(); ++modelIdx) {
    const auto& model = models[modelIdx];
    size_t materialCount = model.materials.size();

    materialUniformBuffers[modelIdx].resize(materialCount * MAX_FRAMES_IN_FLIGHT);
    materialUniformBuffersMapped[modelIdx].resize(materialCount * MAX_FRAMES_IN_FLIGHT);

    for (size_t matIdx = 0; matIdx < materialCount; ++matIdx) {
      for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
        size_t bufferIdx = matIdx * MAX_FRAMES_IN_FLIGHT + frame;

        materialUniformBuffers[modelIdx][bufferIdx] =
            bufferManager->createBuffer(materialBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        vkMapMemory(device, materialUniformBuffers[modelIdx][bufferIdx].memory, 0, materialBufferSize, 0, &materialUniformBuffersMapped[modelIdx][bufferIdx]);
      }
    }
  }
}

void PBRScene::createDefaultTextures() {
  // Create 1x1 white texture
  uint32_t white = 0xFFFFFFFF;
  defaultWhiteTexture = textureManager->createTextureFromData(&white, 1, 1, 4, VK_FORMAT_R8G8B8A8_UNORM);

  // Create 1x1 normal map (0.5, 0.5, 1.0, 1.0) = (128, 128, 255, 255)
  uint32_t normal = 0xFFFF8080;
  defaultNormalTexture = textureManager->createTextureFromData(&normal, 1, 1, 4, VK_FORMAT_R8G8B8A8_UNORM);

  // Create 1x1 black texture
  uint32_t black = 0xFF000000;
  defaultBlackTexture = textureManager->createTextureFromData(&black, 1, 1, 4, VK_FORMAT_R8G8B8A8_UNORM);
}

void PBRScene::loadModels() {
  // Load a sample glTF model
  std::string modelPath = "../resources/models/ftm/scene.gltf";

  try {
    ModelManager::Model model = modelManager->loadGLTF(modelPath);
    models.push_back(std::move(model));
    spdlog::info("Loaded model: {}", modelPath);
  } catch (const std::exception& e) {
    spdlog::error("Failed to load model: {}", e.what());
    // Load a fallback cube or sphere if model loading fails
  }

  // Add more models if needed
  // models.push_back(modelManager->loadGLTF(anotherPath));
}

void PBRScene::setupLights() {
  lights.clear();

  // Main directional light (sun)
  Light sun;
  sun.position = glm::vec3(-1.0f, -1.0f, -1.0f);
  sun.color = glm::vec3(1.0f, 0.95f, 0.8f);
  sun.intensity = 3.0f;
  sun.isDirectional = true;
  lights.push_back(sun);

  // Fill light
  Light fill;
  fill.position = glm::vec3(1.0f, 1.0f, 1.0f);
  fill.color = glm::vec3(0.8f, 0.9f, 1.0f);
  fill.intensity = 0.5f;
  fill.isDirectional = true;
  lights.push_back(fill);

  // Point lights (optional)
  Light point1;
  point1.position = glm::vec3(2.0f, 2.0f, 2.0f);
  point1.color = glm::vec3(1.0f, 0.8f, 0.6f);
  point1.intensity = 5.0f;
  point1.isDirectional = false;
  lights.push_back(point1);
}

void PBRScene::updateGlobalUniformBuffer(uint32_t currentImage) {
  GlobalUBO ubo{};

  ubo.view = glm::lookAt(camera.position, camera.target, camera.up);
  ubo.proj = glm::perspective(glm::radians(camera.fov), swapChainExtent.width / (float)swapChainExtent.height, camera.nearPlane, camera.farPlane);
  ubo.proj[1][1] *= -1;  // Flip Y for Vulkan
  ubo.cameraPos = camera.position;
  ubo.time = totalTime;

  memcpy(globalUniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

void PBRScene::updateModelUniformBuffer(uint32_t currentImage, const glm::mat4& modelMatrix) {
  ModelUBO ubo{};

  ubo.model = modelMatrix;
  ubo.normalMatrix = glm::transpose(glm::inverse(glm::mat3(modelMatrix)));

  memcpy(modelUniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

void PBRScene::updateLightUniformBuffer(uint32_t currentImage) {
  LightUBO ubo{};

  size_t lightCount = std::min(lights.size(), size_t(4));
  for (size_t i = 0; i < lightCount; ++i) {
    ubo.position[i] = glm::vec4(lights[i].position, lights[i].isDirectional ? 0.0f : 1.0f);
    ubo.color[i] = glm::vec4(lights[i].color * lights[i].intensity, 1.0f);
  }
  ubo.params.x = static_cast<float>(lightCount);

  memcpy(lightUniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

void PBRScene::updateMaterialUniformBuffer(uint32_t currentImage, uint32_t modelIndex, uint32_t materialIndex) {
  const auto& material = models[modelIndex].materials[materialIndex];

  MaterialUBO ubo{};
  ubo.baseColorFactor = material.baseColorFactor;
  ubo.emissiveFactor = material.emissiveFactor;
  ubo.metallicFactor = material.metallicFactor;
  ubo.roughnessFactor = material.roughnessFactor;
  ubo.normalScale = material.normalScale;
  ubo.occlusionStrength = material.occlusionStrength;
  ubo.alphaCutoff = material.alphaCutoff;

  // Set texture flags
  ubo.textureFlags.x = (material.baseColorTextureIndex >= 0) ? 1 : 0;
  ubo.textureFlags.y = (material.metallicRoughnessTextureIndex >= 0) ? 1 : 0;
  ubo.textureFlags.z = (material.normalTextureIndex >= 0) ? 1 : 0;
  ubo.textureFlags.w = (material.occlusionTextureIndex >= 0) ? 1 : 0;

  size_t bufferIndex = materialIndex * MAX_FRAMES_IN_FLIGHT + currentImage;
  memcpy(materialUniformBuffersMapped[modelIndex][bufferIndex], &ubo, sizeof(ubo));
}

void PBRScene::recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
  // Bind pipeline
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

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

  // Bind global descriptor set
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &globalDescriptorSets[currentFrame], 0, nullptr);

  // Draw all models
  for (size_t modelIdx = 0; modelIdx < models.size(); ++modelIdx) {
    const auto& model = models[modelIdx];

    if (model.vertexBuffer.buffer == VK_NULL_HANDLE || model.indexBuffer.buffer == VK_NULL_HANDLE) {
      continue;
    }

    // Bind vertex and index buffers
    VkBuffer vertexBuffers[] = {model.vertexBuffer.buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, model.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    // Calculate model matrix
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    if (autoRotate) {
      modelMatrix = glm::rotate(modelMatrix, modelRotation, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // Update and bind model descriptor set
    updateModelUniformBuffer(currentFrame, modelMatrix);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &modelDescriptorSets[currentFrame], 0, nullptr);

    // Draw nodes
    for (const auto& node : model.nodes) {
      drawNode(commandBuffer, node.get(), static_cast<uint32_t>(modelIdx), modelMatrix);
    }
  }
}

void PBRScene::drawNode(VkCommandBuffer commandBuffer, const ModelManager::Node* node, uint32_t modelIndex, const glm::mat4& parentMatrix) {
  if (!node) return;

  glm::mat4 nodeMatrix = parentMatrix * node->localMatrix;

  // Draw mesh if present
  if (node->mesh >= 0 && node->mesh < models[modelIndex].meshes.size()) {
    const auto& mesh = models[modelIndex].meshes[node->mesh];

    for (const auto& primitive : mesh.primitives) {
      // Update and bind material descriptor set
      uint32_t materialIndex = (primitive.materialIndex >= 0) ? primitive.materialIndex : 0;

      if (materialIndex < models[modelIndex].materials.size()) {
        updateMaterialUniformBuffer(currentFrame, modelIndex, materialIndex);

        size_t descIdx = materialIndex * MAX_FRAMES_IN_FLIGHT + currentFrame;
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 2, 1, &materialDescriptorSets[modelIndex][descIdx], 0, nullptr);
      }

      // Draw indexed
      vkCmdDrawIndexed(commandBuffer, primitive.indexCount, 1, primitive.firstIndex, primitive.firstVertex, 0);
    }
  }

  // Draw children
  for (const auto& child : node->children) {
    drawNode(commandBuffer, child.get(), modelIndex, nodeMatrix);
  }
}

void PBRScene::updateScene(float deltaTime) {
  totalTime += deltaTime;

  if (autoRotate) {
    modelRotation += deltaTime * 0.5f;
  }

  // Update camera orbit
  float orbitSpeed = 0.2f;
  float radius = glm::length(camera.position - camera.target);
  camera.position.x = camera.target.x + radius * cos(totalTime * orbitSpeed);
  camera.position.z = camera.target.z + radius * sin(totalTime * orbitSpeed);

  // Update uniform buffers
  updateGlobalUniformBuffer(currentFrame);
  updateLightUniformBuffer(currentFrame);
}

void PBRScene::onResize(int width, int height) { spdlog::info("PBR scene resized to {}x{}", width, height); }

void PBRScene::cleanupResources() {
  spdlog::info("Cleaning up PBR resources");

  // Clean up models
  for (auto& model : models) {
    model.cleanup(device);
  }
  models.clear();

  // Clean up default textures
  defaultWhiteTexture = TextureManager::Texture();
  defaultNormalTexture = TextureManager::Texture();
  defaultBlackTexture = TextureManager::Texture();

  // Unmap uniform buffers
  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    if (globalUniformBuffers[i].memory != VK_NULL_HANDLE) {
      vkUnmapMemory(device, globalUniformBuffers[i].memory);
    }
    if (modelUniformBuffers[i].memory != VK_NULL_HANDLE) {
      vkUnmapMemory(device, modelUniformBuffers[i].memory);
    }
    if (lightUniformBuffers[i].memory != VK_NULL_HANDLE) {
      vkUnmapMemory(device, lightUniformBuffers[i].memory);
    }
  }

  for (auto& modelBuffers : materialUniformBuffersMapped) {
    for (size_t i = 0; i < modelBuffers.size(); ++i) {
      if (i < materialUniformBuffers.size() && i < materialUniformBuffers[0].size() && materialUniformBuffers[0][i].memory != VK_NULL_HANDLE) {
        vkUnmapMemory(device, materialUniformBuffers[0][i].memory);
      }
    }
  }

  // Clean up buffers
  globalUniformBuffers.clear();
  modelUniformBuffers.clear();
  lightUniformBuffers.clear();
  materialUniformBuffers.clear();

  // Clean up descriptor pool and layouts
  vkDestroyDescriptorPool(device, descriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(device, globalDescriptorSetLayout, nullptr);
  vkDestroyDescriptorSetLayout(device, modelDescriptorSetLayout, nullptr);
  vkDestroyDescriptorSetLayout(device, materialDescriptorSetLayout, nullptr);

  // Clean up pipeline
  vkDestroyPipeline(device, graphicsPipeline, nullptr);
  vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

  spdlog::info("PBR resources cleaned up");
}

VkShaderModule PBRScene::createShaderModule(const std::vector<char>& code) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create shader module!");
  }

  return shaderModule;
}