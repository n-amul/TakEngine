#define GLFW_INCLUDE_VULKAN
#include "VulkanDeferredBase.hpp"

#include <glfw/glfw3.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

#include "VulkanDeferredBase.hpp"
#include "core/utils.hpp"

// Public Methods

void VulkanDeferredBase::run() {
  spdlog::info("VulkanDeferredBase::run entered");
  initWindow();
  initVulkan();
  mainLoop();
  cleanup();
}

// Main Loop and Drawing

void VulkanDeferredBase::mainLoop() {
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    // Update scene with delta time
    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
    lastTime = currentTime;

    // Process input and update camera
    processInput(deltaTime);
    camera.update(deltaTime);

    updateScene(deltaTime);  // Virtual method - default does nothing
    drawFrame();
  }

  vkDeviceWaitIdle(device);
}

void VulkanDeferredBase::drawFrame() {
  // Wait until the previous frame has finished
  vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

  // Acquire image from swap chain
  uint32_t imageIndex;
  VkResult res = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE,
                                       &imageIndex);

  if (res == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapChain();
    return;
  } else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  // Only reset fence after successful acquire
  vkResetFences(device, 1, &inFlightFences[currentFrame]);

  vkResetCommandBuffer(commandBuffers[currentFrame], 0);
  recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

  // Submit command buffer
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

  VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[imageIndex]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
    throw std::runtime_error("failed to submit draw command buffer!");
  }

  // Present
  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;

  VkSwapchainKHR swapChains[] = {swapChain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;
  presentInfo.pImageIndices = &imageIndex;

  res = vkQueuePresentKHR(presentQueue, &presentInfo);

  if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || framebufferResized) {
    framebufferResized = false;
    recreateSwapChain();
  } else if (res != VK_SUCCESS) {
    throw std::runtime_error("failed to present swap chain image!");
  }

  currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanDeferredBase::recordCommandBuffer(VkCommandBuffer commandBuffer, u32 imageIndex) {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("failed to begin recording command buffer!");
  }

  // ==== GEOMETRY PASS ====
  VkRenderPassBeginInfo geometryPassInfo{};
  geometryPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  geometryPassInfo.renderPass = geometryRenderPass;
  geometryPassInfo.framebuffer = geometryFramebuffers[imageIndex];
  geometryPassInfo.renderArea.offset = {0, 0};
  geometryPassInfo.renderArea.extent = swapChainExtent;

  std::array<VkClearValue, 5> geometryClearValues{};
  geometryClearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Position
  geometryClearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Normal
  geometryClearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Albedo
  geometryClearValues[3].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Material
  geometryClearValues[4].depthStencil = {1.0f, 0};            // Depth

  geometryPassInfo.clearValueCount = static_cast<uint32_t>(geometryClearValues.size());
  geometryPassInfo.pClearValues = geometryClearValues.data();

  vkCmdBeginRenderPass(commandBuffer, &geometryPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  // Record geometry pass commands from derived class
  recordGeometryCommands(commandBuffer);

  vkCmdEndRenderPass(commandBuffer);

  // ==== LIGHTING PASS ====
  VkRenderPassBeginInfo lightingPassInfo{};
  lightingPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  lightingPassInfo.renderPass = lightingRenderPass;
  lightingPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
  lightingPassInfo.renderArea.offset = {0, 0};
  lightingPassInfo.renderArea.extent = swapChainExtent;

  std::array<VkClearValue, 1> lightingClearValues{};
  lightingClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};

  lightingPassInfo.clearValueCount = static_cast<uint32_t>(lightingClearValues.size());
  lightingPassInfo.pClearValues = lightingClearValues.data();

  vkCmdBeginRenderPass(commandBuffer, &lightingPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  // Record lighting pass commands from derived class
  recordLightingCommands(commandBuffer);

  vkCmdEndRenderPass(commandBuffer);

  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to record command buffer!");
  }
}

// G-Buffer Creation

void VulkanDeferredBase::createGBuffer() {
  VkFormat positionFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
  VkFormat normalFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
  VkFormat albedoFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkFormat materialFormat = VK_FORMAT_R8G8B8A8_UNORM;

  // Create G-Buffer textures
  textureManager->InitTexture(gBuffer.position, swapChainExtent.width, swapChainExtent.height, positionFormat,
                              VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  gBuffer.position.imageView =
      textureManager->createImageView(gBuffer.position.image, positionFormat, VK_IMAGE_ASPECT_COLOR_BIT);
  gBuffer.position.sampler = textureManager->createTextureSampler();

  textureManager->InitTexture(gBuffer.normal, swapChainExtent.width, swapChainExtent.height, normalFormat,
                              VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  gBuffer.normal.imageView = textureManager->createImageView(gBuffer.normal.image, normalFormat, VK_IMAGE_ASPECT_COLOR_BIT);
  gBuffer.normal.sampler = textureManager->createTextureSampler();

  textureManager->InitTexture(gBuffer.albedo, swapChainExtent.width, swapChainExtent.height, albedoFormat,
                              VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  gBuffer.albedo.imageView = textureManager->createImageView(gBuffer.albedo.image, albedoFormat, VK_IMAGE_ASPECT_COLOR_BIT);
  gBuffer.albedo.sampler = textureManager->createTextureSampler();

  textureManager->InitTexture(gBuffer.material, swapChainExtent.width, swapChainExtent.height, materialFormat,
                              VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  gBuffer.material.imageView =
      textureManager->createImageView(gBuffer.material.image, materialFormat, VK_IMAGE_ASPECT_COLOR_BIT);
  gBuffer.material.sampler = textureManager->createTextureSampler();

  // Transition layouts for shader usage
  VkCommandBuffer cmdBuf = cmdUtils->beginSingleTimeCommands();
  textureManager->transitionImageLayout(gBuffer.position, VK_IMAGE_LAYOUT_UNDEFINED,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmdBuf);
  textureManager->transitionImageLayout(gBuffer.normal, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                        cmdBuf);
  textureManager->transitionImageLayout(gBuffer.albedo, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                        cmdBuf);
  textureManager->transitionImageLayout(gBuffer.material, VK_IMAGE_LAYOUT_UNDEFINED,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmdBuf);
  cmdUtils->endSingleTimeCommands(cmdBuf);

  // Create descriptor set layout for G-Buffer
  std::array<VkDescriptorSetLayoutBinding, 4> bindings{};

  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  bindings[1].binding = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[1].descriptorCount = 1;
  bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  bindings[2].binding = 2;
  bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[2].descriptorCount = 1;
  bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  bindings[3].binding = 3;
  bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[3].descriptorCount = 1;
  bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &gBuffer.descriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create G-Buffer descriptor set layout!");
  }

  // Create descriptor pool
  std::array<VkDescriptorPoolSize, 1> poolSizes{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[0].descriptorCount = static_cast<uint32_t>(swapChainImages.size() * 4);

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = static_cast<uint32_t>(swapChainImages.size());

  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &gBuffer.descriptorPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create G-Buffer descriptor pool!");
  }

  // Create descriptor sets
  std::vector<VkDescriptorSetLayout> layouts(swapChainImages.size(), gBuffer.descriptorSetLayout);
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = gBuffer.descriptorPool;
  allocInfo.descriptorSetCount = static_cast<uint32_t>(swapChainImages.size());
  allocInfo.pSetLayouts = layouts.data();

  gBuffer.descriptorSets.resize(swapChainImages.size());
  if (vkAllocateDescriptorSets(device, &allocInfo, gBuffer.descriptorSets.data()) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate G-Buffer descriptor sets!");
  }

  // Update descriptor sets
  for (size_t i = 0; i < swapChainImages.size(); i++) {
    std::array<VkDescriptorImageInfo, 4> imageInfos{};

    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].imageView = gBuffer.position.imageView;
    imageInfos[0].sampler = gBuffer.position.sampler;

    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].imageView = gBuffer.normal.imageView;
    imageInfos[1].sampler = gBuffer.normal.sampler;

    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[2].imageView = gBuffer.albedo.imageView;
    imageInfos[2].sampler = gBuffer.albedo.sampler;

    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[3].imageView = gBuffer.material.imageView;
    imageInfos[3].sampler = gBuffer.material.sampler;

    std::array<VkWriteDescriptorSet, 4> descriptorWrites{};

    for (uint32_t j = 0; j < 4; j++) {
      descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[j].dstSet = gBuffer.descriptorSets[i];
      descriptorWrites[j].dstBinding = j;
      descriptorWrites[j].dstArrayElement = 0;
      descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      descriptorWrites[j].descriptorCount = 1;
      descriptorWrites[j].pImageInfo = &imageInfos[j];
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
  }
}

void VulkanDeferredBase::cleanupGBuffer() {
  vkDestroyDescriptorPool(device, gBuffer.descriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(device, gBuffer.descriptorSetLayout, nullptr);

  textureManager->destroyTexture(gBuffer.position);
  textureManager->destroyTexture(gBuffer.normal);
  textureManager->destroyTexture(gBuffer.albedo);
  textureManager->destroyTexture(gBuffer.material);
}

// Full-screen Quad Creation

void VulkanDeferredBase::createFullscreenQuad() {
  // Vertices for full-screen quad
  struct QuadVertex {
    glm::vec2 pos;
    glm::vec2 uv;
  };

  std::vector<QuadVertex> vertices = {{{-1.0f, -1.0f}, {0.0f, 0.0f}},
                                      {{1.0f, -1.0f}, {1.0f, 0.0f}},
                                      {{1.0f, 1.0f}, {1.0f, 1.0f}},
                                      {{-1.0f, 1.0f}, {0.0f, 1.0f}}};

  std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

  // Create vertex buffer
  VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
  fullscreenQuad.vertexBuffer =
      bufferManager->createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
  bufferManager->updateBuffer(fullscreenQuad.vertexBuffer, vertices.data(), bufferSize, 0);
  // Create index buffer
  VkDeviceSize IndexBufferSize = sizeof(indices[0]) * indices.size();
  fullscreenQuad.indexBuffer =
      bufferManager->createBuffer(IndexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);

  bufferManager->updateBuffer(fullscreenQuad.indexBuffer, indices.data(), IndexBufferSize, 0);

  fullscreenQuad.indexCount = static_cast<uint32_t>(indices.size());
}

// Render Passes

void VulkanDeferredBase::createRenderPasses() {
  // ==== GEOMETRY RENDER PASS ====
  {
    std::array<VkAttachmentDescription, 5> attachments{};

    // Position attachment
    attachments[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Normal attachment
    attachments[1].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Albedo attachment
    attachments[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Material attachment
    attachments[3].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[3].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Depth attachment
    attachments[4].format = depthBuffer.format;
    attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[4].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentReference, 4> colorReferences{};
    colorReferences[0] = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    colorReferences[1] = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    colorReferences[2] = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    colorReferences[3] = {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkAttachmentReference depthReference = {4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
    subpass.pColorAttachments = colorReferences.data();
    subpass.pDepthStencilAttachment = &depthReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &geometryRenderPass) != VK_SUCCESS) {
      throw std::runtime_error("failed to create geometry render pass!");
    }
  }

  // ==== LIGHTING RENDER PASS ====
  {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &lightingRenderPass) != VK_SUCCESS) {
      throw std::runtime_error("failed to create lighting render pass!");
    }
  }
}

// Framebuffers

void VulkanDeferredBase::createFramebuffers() {
  // Create geometry framebuffers
  geometryFramebuffers.resize(swapChainImageViews.size());

  for (size_t i = 0; i < swapChainImageViews.size(); i++) {
    std::array<VkImageView, 5> attachments = {gBuffer.position.imageView, gBuffer.normal.imageView, gBuffer.albedo.imageView,
                                              gBuffer.material.imageView, depthBuffer.imageView};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = geometryRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &geometryFramebuffers[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create geometry framebuffer!");
    }
  }

  // Create lighting framebuffers (for swap chain)
  swapChainFramebuffers.resize(swapChainImageViews.size());

  for (size_t i = 0; i < swapChainImageViews.size(); i++) {
    VkImageView attachments[] = {swapChainImageViews[i]};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = lightingRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create swap chain framebuffer!");
    }
  }
}

// Initialization

void VulkanDeferredBase::initWindow() {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  window = glfwCreateWindow(window_width, window_height, title.c_str(), nullptr, nullptr);
  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

  // Set input callbacks
  glfwSetKeyCallback(window, keyCallback);
  glfwSetCursorPosCallback(window, mouseCallback);
  glfwSetScrollCallback(window, scrollCallback);
  glfwSetMouseButtonCallback(window, mouseButtonCallback);

  // Capture mouse by default
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  // Initialize camera
  camera.initialize(glm::vec3(1.5f, 0.0f, 1.5f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

  // Get initial mouse position
  glfwGetCursorPos(window, &lastX, &lastY);
}

void VulkanDeferredBase::initVulkan() {
  // 1. Core Vulkan setup
  spdlog::info("Creating instance...");
  createInstance();
  spdlog::info("Setting up debug messenger...");
  setupDebugMessenger();
  spdlog::info("Creating surface...");
  createSurface();
  spdlog::info("Picking physical device...");
  pickPhysicalDevice();
  spdlog::info("Creating logical device...");
  createLogicalDevice();

  // 2. Context initialization
  spdlog::info("Creating context...");
  context = std::make_shared<VulkanContext>();
  context->instance = instance;
  context->device = device;
  context->physicalDevice = physicalDevice;
  context->graphicsQueue = graphicsQueue;
  context->presentQueue = presentQueue;
  vkGetPhysicalDeviceProperties(physicalDevice, &context->properties);
  vkGetPhysicalDeviceFeatures(physicalDevice, &context->features);
  context->enabledFeatures = deviceFeatures;
  context->queueFamilyIndex = queueFamilyIndex;

  spdlog::info("Creating command pool...");
  // 3. Command pools (needed before resource loading)
  createCommandPool();
  context->commandPool = commandPool;
  context->transientCommandPool = transientCommandPool;

  spdlog::info("Creating utils...");
  // 4. Initialize shared utilities
  cmdUtils = std::make_shared<CommandBufferUtils>(context);
  bufferManager = std::make_shared<BufferManager>(context, cmdUtils);
  textureManager = std::make_shared<TextureManager>(context, cmdUtils, bufferManager);
  modelManager = std::make_shared<ModelManager>(context, bufferManager, textureManager, cmdUtils);

  spdlog::info("Creating swapchain & render passes...");
  // 5. Rendering setup
  createSwapChain();
  createImageViews();
  createDepthResources();
  createGBuffer();
  createRenderPasses();
  createFullscreenQuad();

  // 6. Load resources
  spdlog::info("Loading resources...");
  loadResources();

  // 7. Create pipelines
  spdlog::info("Creating pipelines...");
  createGeometryPipeline();
  createLightingPipeline();

  // 8. Final setup
  spdlog::info("Creating framebuffers, command buffers, sync objects");
  createFramebuffers();
  createCommandBuffers();
  createSyncObjects();
}

void VulkanDeferredBase::recreateSwapChain() {
  SPDLOG_INFO("swapchain recreate called");

  int width = 0, height = 0;
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(window, &width, &height);
    glfwWaitEvents();
  }

  vkDeviceWaitIdle(device);

  // Notify derived class about resize
  onResize(width, height);

  cleanupSwapChain();
  cleanupGBuffer();

  depthBuffer = TextureManager::Texture();

  createSwapChain();
  createImageViews();
  createDepthResources();
  createGBuffer();
  createFramebuffers();
}

void VulkanDeferredBase::cleanupSwapChain() {
  for (auto framebuffer : geometryFramebuffers) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
  }

  for (auto framebuffer : swapChainFramebuffers) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
  }

  for (auto imageView : swapChainImageViews) {
    vkDestroyImageView(device, imageView, nullptr);
  }

  vkDestroySwapchainKHR(device, swapChain, nullptr);
}

// Cleanup

void VulkanDeferredBase::cleanup() {
  vkDeviceWaitIdle(device);

  // Clean up derived class resources FIRST
  cleanupResources();

  // Clean up fullscreen quad
  bufferManager->destroyBuffer(fullscreenQuad.vertexBuffer);
  bufferManager->destroyBuffer(fullscreenQuad.indexBuffer);

  // Clean up G-Buffer
  cleanupGBuffer();

  // Clean up depth buffer
  textureManager->destroyTexture(depthBuffer);

  // Clean up swap chain
  cleanupSwapChain();

  // Clean up render passes
  vkDestroyRenderPass(device, geometryRenderPass, nullptr);
  vkDestroyRenderPass(device, lightingRenderPass, nullptr);

  // Clean up synchronization objects
  for (u32 i = 0; i < swapChainImages.size(); i++) {
    vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
  }

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
    vkDestroyFence(device, inFlightFences[i], nullptr);
  }

  vkDestroyCommandPool(device, transientCommandPool, nullptr);
  vkDestroyCommandPool(device, commandPool, nullptr);
  vkDestroyDevice(device, nullptr);

  if (enableValidationLayers) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
      func(instance, debugMessenger, nullptr);
    }
  }

  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);

  glfwDestroyWindow(window);
  glfwTerminate();
}
// Core Vulkan Setup Methods
void VulkanDeferredBase::createInstance() {
  if (enableValidationLayers && !checkValidationLayerSupport()) {
    throw std::runtime_error("validation layers requested, but not available!");
  }

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = name.c_str();
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "Tak Engine Deferred";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  auto extensions = getRequiredExtensions();
  createInfo.enabledExtensionCount = static_cast<u32>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  if (enableValidationLayers) {
    createInfo.enabledLayerCount = static_cast<u32>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    populateDebugMessengerCreateInfo(debugCreateInfo);
    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
  } else {
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = nullptr;
  }

  if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
    throw std::runtime_error("failed to create vkinstance");
  }
}

void VulkanDeferredBase::createSurface() {
  if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
    throw std::runtime_error("failed to create window surface!");
  }
}

void VulkanDeferredBase::pickPhysicalDevice() {
  u32 deviceCount = 0;
  vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

  if (deviceCount == 0) {
    throw std::runtime_error("failed to find gpu that supports vulkan");
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

  for (const auto& device : devices) {
    if (isDeviceSuitable(device)) {
      physicalDevice = device;
      break;
    }
  }

  if (physicalDevice == VK_NULL_HANDLE) {
    throw std::runtime_error("failed to find a suitable GPU!");
  }
}

void VulkanDeferredBase::createLogicalDevice() {
  std::optional<u32> queueFamily_index = findQueueFamilies(physicalDevice);

  VkDeviceQueueCreateInfo queueCreateInfo{};
  queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfo.queueFamilyIndex = queueFamily_index.value();
  queueCreateInfo.queueCount = 1;

  float queuePriority = 1.0f;
  queueCreateInfo.pQueuePriorities = &queuePriority;

  VkPhysicalDeviceFeatures deviceFeatures{};
  deviceFeatures.samplerAnisotropy = VK_TRUE;

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.pQueueCreateInfos = &queueCreateInfo;
  createInfo.queueCreateInfoCount = 1;
  createInfo.pEnabledFeatures = &deviceFeatures;

  if (enableValidationLayers) {
    createInfo.enabledLayerCount = static_cast<u32>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
  } else {
    createInfo.enabledLayerCount = 0;
  }

  createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();

  if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
    throw std::runtime_error("failed to create logical device!");
  }

  vkGetDeviceQueue(device, queueFamily_index.value(), 0, &graphicsQueue);
  vkGetDeviceQueue(device, queueFamily_index.value(), 0, &presentQueue);
  this->queueFamilyIndex = queueFamily_index.value();
  this->deviceFeatures = deviceFeatures;
}

std::optional<u32> VulkanDeferredBase::findQueueFamilies(VkPhysicalDevice device) {
  u32 queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
  spdlog::info("Found {} queue families", queueFamilyCount);

  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

  for (u32 i = 0; i < queueFamilies.size(); i++) {
    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

    bool hasGraphics = queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT;
    spdlog::info("Queue family {}: Graphics={}, Present={}", i, hasGraphics, presentSupport);

    if (hasGraphics && presentSupport) {
      spdlog::info("Selected queue family index: {}", i);
      return i;
    }
  }
  throw std::runtime_error("no suitable queue family found");
  return std::nullopt;
}

bool VulkanDeferredBase::isDeviceSuitable(VkPhysicalDevice device) {
  std::optional<u32> index = findQueueFamilies(device);

  bool extensionsSupported = checkDeviceExtensionSupport(device);

  bool swapChainAdequate = false;
  if (extensionsSupported) {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
    swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
  }
  VkPhysicalDeviceFeatures supportedFeatures;
  vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

  return index.has_value() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
}

bool VulkanDeferredBase::checkDeviceExtensionSupport(VkPhysicalDevice device) {
  u32 extensionCount;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

  std::vector<VkExtensionProperties> availableExtensions(extensionCount);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

  std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

  for (const auto& extension : availableExtensions) {
    requiredExtensions.erase(extension.extensionName);
  }

  return requiredExtensions.empty();
}

void VulkanDeferredBase::createSwapChain() {
  SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);
  VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
  VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
  VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

  uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
  if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
    imageCount = swapChainSupport.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
    throw std::runtime_error("failed to create swap chain!");
  }

  vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
  swapChainImages.resize(imageCount);
  vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

  swapChainImageFormat = surfaceFormat.format;
  swapChainExtent = extent;
}

void VulkanDeferredBase::createImageViews() {
  swapChainImageViews.resize(swapChainImages.size());

  for (size_t i = 0; i < swapChainImages.size(); i++) {
    swapChainImageViews[i] =
        textureManager->createImageView(swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
  }
}

void VulkanDeferredBase::createDepthResources() {
  auto findDepthFormat = [this]() -> VkFormat {
    auto findSupportedFormat = [this](const std::vector<VkFormat>& candidates, VkImageTiling tiling,
                                      VkFormatFeatureFlags features) -> VkFormat {
      for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
          return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
          return format;
        }
      }
      throw std::runtime_error("Failed to find supported format");
    };

    return findSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                               VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  };

  if (depthBuffer.format == VK_FORMAT_UNDEFINED) {
    depthBuffer.format = findDepthFormat();
  }

  textureManager->InitTexture(depthBuffer, swapChainExtent.width, swapChainExtent.height, depthBuffer.format,
                              VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, VK_SAMPLE_COUNT_1_BIT);
  depthBuffer.imageView = textureManager->createImageView(depthBuffer.image, depthBuffer.format, VK_IMAGE_ASPECT_DEPTH_BIT);
}

SwapChainSupportDetails VulkanDeferredBase::querySwapChainSupport(VkPhysicalDevice device) {
  SwapChainSupportDetails details;

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

  uint32_t formatCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

  if (formatCount != 0) {
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
  }

  uint32_t presentModeCount;
  vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

  if (presentModeCount != 0) {
    details.presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
  }

  return details;
}

VkSurfaceFormatKHR VulkanDeferredBase::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
  for (const auto& availableFormat : availableFormats) {
    if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
        availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return availableFormat;
    }
  }

  return availableFormats[0];
}

VkPresentModeKHR VulkanDeferredBase::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
  for (const auto& availablePresentMode : availablePresentModes) {
    if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return availablePresentMode;
    }
  }

  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanDeferredBase::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  } else {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

    TCLAMP(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    TCLAMP(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
  }
}

void VulkanDeferredBase::createCommandPool() {
  std::optional<u32> queueFamilyIndices = findQueueFamilies(physicalDevice);

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = queueFamilyIndices.value();

  if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create command pool!");
  }

  // Transient command pool (for copies, transitions, etc.)
  VkCommandPoolCreateInfo transientPoolInfo{};
  transientPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  transientPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  transientPoolInfo.queueFamilyIndex = queueFamilyIndices.value();

  if (vkCreateCommandPool(device, &transientPoolInfo, nullptr, &transientCommandPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create transient command pool!");
  }
}

void VulkanDeferredBase::createCommandBuffers() {
  commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = (u32)commandBuffers.size();

  if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate command buffers!");
  }
}

void VulkanDeferredBase::createSyncObjects() {
  imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  renderFinishedSemaphores.resize(swapChainImages.size());
  inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
        vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create frame synchronization objects!");
    }
  }

  for (size_t i = 0; i < swapChainImages.size(); i++) {
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create render finished semaphores!");
    }
  }
}

VkShaderModule VulkanDeferredBase::createShaderModule(const std::vector<char>& code) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }

  return shaderModule;
}

VkPipelineShaderStageCreateInfo VulkanDeferredBase::loadShader(std::string filename, VkShaderStageFlagBits shaderStage) {
  auto code = readFile(filename);
  VkPipelineShaderStageCreateInfo ShaderStageInfo{};
  ShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  ShaderStageInfo.module = createShaderModule(code);
  ShaderStageInfo.pName = "main";
  ShaderStageInfo.stage = shaderStage;

  return ShaderStageInfo;
}

//-----------------------------------------------------------
// Validation Layer Methods
//-----------------------------------------------------------
void VulkanDeferredBase::setupDebugMessenger() {
  if (!enableValidationLayers) return;

  VkDebugUtilsMessengerCreateInfoEXT createInfo;
  populateDebugMessengerCreateInfo(createInfo);

  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

  if (func != nullptr) {
    if (func(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
      spdlog::error("failed to set up debug messenger!");
    }
  } else {
    spdlog::error("debug utils extension not available!");
  }
}

bool VulkanDeferredBase::checkValidationLayerSupport() {
  u32 layerCount;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

  for (const char* layerName : validationLayers) {
    bool layerFound = false;

    for (const auto& layerProperties : availableLayers) {
      if (strcmp(layerName, layerProperties.layerName) == 0) {
        layerFound = true;
        break;
      }
    }

    if (!layerFound) {
      return false;
    }
  }

  return true;
}

std::vector<const char*> VulkanDeferredBase::getRequiredExtensions() {
  u32 glfwExtensionCount = 0;
  const char** glfwExtensions;
  glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  return extensions;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDeferredBase::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                                 VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                                 const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                                 void* pUserData) {
  switch (messageSeverity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
      spdlog::error("validation layer: {}", pCallbackData->pMessage);
      break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
      spdlog::warn("validation layer: {}", pCallbackData->pMessage);
      break;
    default:
      spdlog::info("validation layer: {}", pCallbackData->pMessage);
  }

  return VK_FALSE;
}

void VulkanDeferredBase::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
  createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;
}

//-----------------------------------------------------------
// Input Callbacks
//-----------------------------------------------------------
void VulkanDeferredBase::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
  SPDLOG_INFO("window new size {}x{}", width, height);
  auto app = reinterpret_cast<VulkanDeferredBase*>(glfwGetWindowUserPointer(window));
  app->framebufferResized = true;
}

void VulkanDeferredBase::processInput(float deltaTime) {
  // Movement keys
  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.moveForward();
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.moveBackward();
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.moveLeft();
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.moveRight();
  if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) camera.moveUp();
  if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera.moveDown();

  // Roll keys
  if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) camera.roll(-deltaTime);
  if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) camera.roll(deltaTime);

  // Speed modifiers
  if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
    camera.setSpeed(10.0f);  // Fast movement
  else
    camera.setSpeed(0.5f);  // Normal movement
}

void VulkanDeferredBase::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
  auto app = reinterpret_cast<VulkanDeferredBase*>(glfwGetWindowUserPointer(window));

  if (action == GLFW_PRESS) {
    switch (key) {
      case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, true);
        break;

      case GLFW_KEY_TAB:
        // Toggle mouse capture
        app->mouseCaptured = !app->mouseCaptured;
        if (app->mouseCaptured) {
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
          app->firstMouse = true;  // Reset mouse state
        } else {
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        break;

      case GLFW_KEY_R:
        app->camera.initialize(glm::vec3(0.0f, 1.5f, 1.5f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        break;
    }
  }

  // Call derived class handler
  app->onKeyEvent(key, scancode, action, mods);
}

void VulkanDeferredBase::mouseCallback(GLFWwindow* window, double xpos, double ypos) {
  auto app = reinterpret_cast<VulkanDeferredBase*>(glfwGetWindowUserPointer(window));

  if (!app->mouseCaptured) {
    app->onMouseMove(xpos, ypos);
    return;
  }

  if (app->firstMouse) {
    app->lastX = xpos;
    app->lastY = ypos;
    app->firstMouse = false;
    return;
  }

  double xoffset = xpos - app->lastX;
  double yoffset = ypos - app->lastY;
  app->lastX = xpos;
  app->lastY = ypos;

  // Apply camera rotation
  app->camera.rotate(static_cast<float>(xoffset), static_cast<float>(yoffset));

  // Call derived class handler
  app->onMouseMove(xpos, ypos);
}

void VulkanDeferredBase::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
  auto app = reinterpret_cast<VulkanDeferredBase*>(glfwGetWindowUserPointer(window));

  // Adjust camera FOV with scroll
  float currentFov = app->camera.getFov();
  currentFov -= static_cast<float>(yoffset) * 2.0f;
  currentFov = std::clamp(currentFov, 10.0f, 120.0f);
  app->camera.setFov(currentFov);
}

void VulkanDeferredBase::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
  auto app = reinterpret_cast<VulkanDeferredBase*>(glfwGetWindowUserPointer(window));

  // Middle mouse button to reset FOV
  if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
    app->camera.setFov(45.0f);
  }
  app->onMouseButton(button, action, mods);
}