// DeferredTriangleScene.cpp
#include "DeferredTriangleScene.hpp"

#include <spdlog/spdlog.h>

#include "core/utils.hpp"

DeferredTriangleScene::DeferredTriangleScene() {
  window_width = 1920;
  window_height = 1080;
  title = "Vulkan Deferred Rendering Scene";
  name = "DeferredTriangleScene";
}

void DeferredTriangleScene::loadResources() {
  spdlog::info("Loading deferred scene resources");

  // Create scene resources
  createVertexBuffer();
  createIndexBuffer();
  createUniformBuffers();
  createDescriptorSetLayout();
  createDescriptorPool();
  createDescriptorSets();

  // Load textures
  albedoTexture = textureManager->createTextureFromFile(std::string(TEXTURE_DIR) + "/cuteCat.jpg");

  // Initialize UI
  ui = new UI(textureManager, lightingRenderPass, VK_SAMPLE_COUNT_1_BIT, std::string(SHADER_DIR), window);

  // Register G-Buffer textures with ImGui (using first frame's textures)
  normalTexId = ui->addTexture(gBuffer.normal[0].sampler, gBuffer.normal[0].imageView);
  albedoTexId = ui->addTexture(gBuffer.albedo[0].sampler, gBuffer.albedo[0].imageView);
  materialTexId = ui->addTexture(gBuffer.material[0].sampler, gBuffer.material[0].imageView);
  depthTexId = ui->addTexture(gBuffer.depthBuffer[0].sampler, gBuffer.depthBuffer[0].imageView);
  ssaoTexId = ui->addTexture(ssaoElements.ssaoOutput[0].sampler, ssaoElements.ssaoOutput[0].imageView);
  ssaoBlurredTexId = ui->addTexture(ssaoElements.ssaoBlurred[0].sampler, ssaoElements.ssaoBlurred[0].imageView);
}

void DeferredTriangleScene::createGeometryPipeline() {
  spdlog::info("Creating geometry pipeline");

  // Shader stages
  VkPipelineShaderStageCreateInfo shaderStages[] = {
      loadShader(std::string(SHADER_DIR) + "/deferred_geometry.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
      loadShader(std::string(SHADER_DIR) + "/deferred_geometry.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)};

  // Vertex input
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

  // Viewport & scissor (dynamic)
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

  // Multisampling (disabled for deferred)
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.sampleShadingEnable = VK_FALSE;

  // Depth testing
  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  // Color blending - MRT setup for G-Buffer
  std::array<VkPipelineColorBlendAttachmentState, 3> colorBlendAttachments{};
  for (auto& attachment : colorBlendAttachments) {
    attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    attachment.blendEnable = VK_FALSE;
  }

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
  colorBlending.pAttachments = colorBlendAttachments.data();

  // Dynamic state
  std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  // Pipeline layout
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &geometryDescriptorSetLayout;

  VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &geometryPipelineLayout));

  // Create pipeline
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
  pipelineInfo.layout = geometryPipelineLayout;
  pipelineInfo.renderPass = geometryRenderPass;
  pipelineInfo.subpass = 0;

  VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &gBuffer.gBufferPipeline));

  // Cleanup shader modules
  vkDestroyShaderModule(device, shaderStages[0].module, nullptr);
  vkDestroyShaderModule(device, shaderStages[1].module, nullptr);

  spdlog::info("Geometry pipeline created successfully");
}

void DeferredTriangleScene::createssaoPipeline() {
  spdlog::info("Creating SSAO pipelines");

  // Common pipeline state
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  // Use fullscreen quad vertex input (from base class)
  struct QuadVertex {
    glm::vec2 pos;
    glm::vec2 uv;
  };

  VkVertexInputBindingDescription bindingDesc{};
  bindingDesc.binding = 0;
  bindingDesc.stride = sizeof(QuadVertex);
  bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 2> attrDesc{};
  attrDesc[0].binding = 0;
  attrDesc[0].location = 0;
  attrDesc[0].format = VK_FORMAT_R32G32_SFLOAT;
  attrDesc[0].offset = offsetof(QuadVertex, pos);

  attrDesc[1].binding = 0;
  attrDesc[1].location = 1;
  attrDesc[1].format = VK_FORMAT_R32G32_SFLOAT;
  attrDesc[1].offset = offsetof(QuadVertex, uv);

  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &bindingDesc;
  vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDesc.size());
  vertexInput.pVertexAttributeDescriptions = attrDesc.data();

  // Viewport & scissor
  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  // Rasterizer
  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.lineWidth = 1.0f;

  // No depth testing for SSAO
  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_FALSE;
  depthStencil.depthWriteEnable = VK_FALSE;

  // Multisampling
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Color blending
  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;  // Only R channel for SSAO
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  // Dynamic state
  std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  // === SSAO Pipeline ===
  VkPipelineLayoutCreateInfo ssaoLayoutInfo{};
  ssaoLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  ssaoLayoutInfo.setLayoutCount = 1;
  ssaoLayoutInfo.pSetLayouts = &ssaoElements.ssaoDescriptorSetLayout;

  VK_CHECK_RESULT(vkCreatePipelineLayout(device, &ssaoLayoutInfo, nullptr, &ssaoElements.ssaoPipelineLayout));

  VkPipelineShaderStageCreateInfo ssaoShaders[] = {
      loadShader(std::string(SHADER_DIR) + "/ssao.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
      loadShader(std::string(SHADER_DIR) + "/ssao.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)};

  VkGraphicsPipelineCreateInfo ssaoPipelineInfo{};
  ssaoPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  ssaoPipelineInfo.stageCount = 2;
  ssaoPipelineInfo.pStages = ssaoShaders;
  ssaoPipelineInfo.pVertexInputState = &vertexInput;
  ssaoPipelineInfo.pInputAssemblyState = &inputAssembly;
  ssaoPipelineInfo.pViewportState = &viewportState;
  ssaoPipelineInfo.pRasterizationState = &rasterizer;
  ssaoPipelineInfo.pMultisampleState = &multisampling;
  ssaoPipelineInfo.pDepthStencilState = &depthStencil;
  ssaoPipelineInfo.pColorBlendState = &colorBlending;
  ssaoPipelineInfo.pDynamicState = &dynamicState;
  ssaoPipelineInfo.layout = ssaoElements.ssaoPipelineLayout;
  ssaoPipelineInfo.renderPass = ssaoElements.ssaoRenderPass;

  VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ssaoPipelineInfo, nullptr, &ssaoPipeline));

  vkDestroyShaderModule(device, ssaoShaders[0].module, nullptr);
  vkDestroyShaderModule(device, ssaoShaders[1].module, nullptr);

  // === SSAO Blur Pipeline ===
  VkPipelineLayoutCreateInfo blurLayoutInfo{};
  blurLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  blurLayoutInfo.setLayoutCount = 1;
  blurLayoutInfo.pSetLayouts = &ssaoElements.ssaoBlurDescriptorSetLayout;

  VK_CHECK_RESULT(vkCreatePipelineLayout(device, &blurLayoutInfo, nullptr, &ssaoElements.ssaoBlurPipelineLayout));

  VkPipelineShaderStageCreateInfo blurShaders[] = {
      loadShader(std::string(SHADER_DIR) + "/ssao_blur.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
      loadShader(std::string(SHADER_DIR) + "/ssao_blur.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)};

  VkGraphicsPipelineCreateInfo blurPipelineInfo = ssaoPipelineInfo;
  blurPipelineInfo.pStages = blurShaders;
  blurPipelineInfo.layout = ssaoElements.ssaoBlurPipelineLayout;
  blurPipelineInfo.renderPass = ssaoElements.ssaoBlurRenderPass;

  VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &blurPipelineInfo, nullptr, &ssaoBlurPipeline));

  vkDestroyShaderModule(device, blurShaders[0].module, nullptr);
  vkDestroyShaderModule(device, blurShaders[1].module, nullptr);

  spdlog::info("SSAO pipelines created successfully");
}

void DeferredTriangleScene::recordGeometryCommands(VkCommandBuffer commandBuffer) {
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

  // Bind geometry pipeline
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gBuffer.gBufferPipeline);

  // Bind vertex and index buffers
  VkBuffer vertexBuffers[] = {vertexBuffer.buffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
  vkCmdBindIndexBuffer(commandBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

  // Bind descriptor sets
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipelineLayout, 0, 1,
                          &geometryDescriptorSets[currentFrame], 0, nullptr);

  // Draw
  vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

  // After geometry pass, render SSAO
  // TODO: Add SSAO pass recording here
}

void DeferredTriangleScene::createVertexBuffer() {
  VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
  vertexBuffer = bufferManager->createGPULocalBuffer(vertices.data(), bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void DeferredTriangleScene::createIndexBuffer() {
  VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
  indexBuffer = bufferManager->createGPULocalBuffer(indices.data(), bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void DeferredTriangleScene::createUniformBuffers() {
  VkDeviceSize bufferSize = sizeof(GeometryUBO);
  uniformBuffers.resize(swapChainImages.size());

  for (size_t i = 0; i < swapChainImages.size(); i++) {
    uniformBuffers[i] =
        bufferManager->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
  }
}

void DeferredTriangleScene::createDescriptorSetLayout() {
  // UBO binding
  VkDescriptorSetLayoutBinding uboBinding{};
  uboBinding.binding = 0;
  uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboBinding.descriptorCount = 1;
  uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  // Albedo texture binding
  VkDescriptorSetLayoutBinding samplerBinding{};
  samplerBinding.binding = 1;
  samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  samplerBinding.descriptorCount = 1;
  samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboBinding, samplerBinding};

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &geometryDescriptorSetLayout));
}

void DeferredTriangleScene::createDescriptorPool() {
  std::array<VkDescriptorPoolSize, 2> poolSizes = {
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(swapChainImages.size())},
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(swapChainImages.size())}};

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = static_cast<uint32_t>(swapChainImages.size());

  VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPoolLocal));
}

void DeferredTriangleScene::createDescriptorSets() {
  std::vector<VkDescriptorSetLayout> layouts(swapChainImages.size(), geometryDescriptorSetLayout);

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPoolLocal;
  allocInfo.descriptorSetCount = static_cast<uint32_t>(swapChainImages.size());
  allocInfo.pSetLayouts = layouts.data();

  geometryDescriptorSets.resize(swapChainImages.size());
  VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, geometryDescriptorSets.data()));

  // Update descriptor sets
  for (size_t i = 0; i < swapChainImages.size(); i++) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffers[i].buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(GeometryUBO);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = albedoTexture.imageView;
    imageInfo.sampler = albedoTexture.sampler;

    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = geometryDescriptorSets[i];
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = geometryDescriptorSets[i];
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
  }
}

void DeferredTriangleScene::updateUniformBuffer(uint32_t currentImage) {
  GeometryUBO ubo{};
  ubo.model = glm::mat4(1.0f);
  ubo.view = camera.getViewMatrix();
  float aspectRatio = swapChainExtent.width / static_cast<float>(swapChainExtent.height);
  ubo.proj = camera.getProjectionMatrix(aspectRatio);
  ubo.normalMatrix = glm::transpose(glm::inverse(ubo.view * ubo.model));

  bufferManager->updateBuffer(uniformBuffers[currentImage], &ubo, sizeof(ubo), 0);
}

void DeferredTriangleScene::updateSSAOParams(uint32_t currentImage) {
  SSAOParams params{};
  float aspectRatio = swapChainExtent.width / static_cast<float>(swapChainExtent.height);
  params.projection = camera.getProjectionMatrix(aspectRatio);
  params.nearPlane = 0.1f;
  params.farPlane = 100.0f;
  params.noiseScale = glm::vec2(swapChainExtent.width / static_cast<float>(SsaoElements::SSAO_NOISE_DIM),
                                swapChainExtent.height / static_cast<float>(SsaoElements::SSAO_NOISE_DIM));

  bufferManager->updateBuffer(ssaoElements.ssaoParamsUBO[currentImage], &params, sizeof(params), 0);
}

void DeferredTriangleScene::updateScene(float deltaTime) {
  updateOverlay(deltaTime);
  updateUniformBuffer(currentFrame);
  updateSSAOParams(currentFrame);
}

void DeferredTriangleScene::updateOverlay(float deltaTime) {
  // FPS calculation
  fpsTimer += deltaTime;
  frameCounter++;
  if (fpsTimer >= 1.0f) {
    fps = static_cast<float>(frameCounter) / fpsTimer;
    fpsTimer = 0.0f;
    frameCounter = 0;
  }

  ui->newFrame();

  // Main debug window
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(350, 500), ImGuiCond_FirstUseEver);
  ImGui::Begin("Deferred Rendering Debug", nullptr);

  ui->text("FPS: %.1f", fps);
  ui->text("Frame time: %.3f ms", deltaTime * 1000.0f);

  ImGui::Separator();

  // G-Buffer visualization
  if (ImGui::CollapsingHeader("G-Buffer", ImGuiTreeNodeFlags_DefaultOpen)) {
    const float imageSize = 150.0f;

    ImGui::Text("Normal + Metallic:");
    ImGui::Image(normalTexId, ImVec2(imageSize, imageSize));

    ImGui::Text("Albedo + AO:");
    ImGui::Image(albedoTexId, ImVec2(imageSize, imageSize));

    ImGui::Text("Material (R/M/E):");
    ImGui::Image(materialTexId, ImVec2(imageSize, imageSize));

    ImGui::Text("Depth:");
    ImGui::Image(depthTexId, ImVec2(imageSize, imageSize));
  }

  if (ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen)) {
    const float imageSize = 150.0f;

    ImGui::Text("SSAO Raw:");
    ImGui::Image(ssaoTexId, ImVec2(imageSize, imageSize));

    ImGui::Text("SSAO Blurred:");
    ImGui::Image(ssaoBlurredTexId, ImVec2(imageSize, imageSize));
  }

  ImGui::End();
  ImGui::Render();

  // Update UI push constants
  ImGuiIO& io = ImGui::GetIO();
  ui->pushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
  ui->pushConstBlock.translate = glm::vec2(-1.0f);

  ui->updateBuffers();
}

void DeferredTriangleScene::onResize(int width, int height) {
  spdlog::info("Deferred scene resized to {}x{}", width, height);
}

void DeferredTriangleScene::cleanupResources() {
  spdlog::info("Cleaning up deferred scene resources");

  // Wait for device idle
  vkDeviceWaitIdle(device);

  // Clean up UI
  delete ui;

  // Clean up textures
  textureManager->destroyTexture(albedoTexture);

  // Clean up buffers
  bufferManager->destroyBuffer(vertexBuffer);
  bufferManager->destroyBuffer(indexBuffer);

  for (auto& buffer : uniformBuffers) {
    bufferManager->destroyBuffer(buffer);
  }

  // Clean up pipelines
  vkDestroyPipeline(device, gBuffer.gBufferPipeline, nullptr);
  vkDestroyPipelineLayout(device, geometryPipelineLayout, nullptr);
  vkDestroyPipeline(device, ssaoPipeline, nullptr);
  vkDestroyPipeline(device, ssaoBlurPipeline, nullptr);
  vkDestroyPipelineLayout(device, ssaoElements.ssaoPipelineLayout, nullptr);
  vkDestroyPipelineLayout(device, ssaoElements.ssaoBlurPipelineLayout, nullptr);

  // Clean up descriptor resources
  vkDestroyDescriptorPool(device, descriptorPoolLocal, nullptr);
  vkDestroyDescriptorSetLayout(device, geometryDescriptorSetLayout, nullptr);

  spdlog::info("Deferred scene resources cleaned up");
}