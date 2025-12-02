#define GLFW_INCLUDE_VULKAN
#include "VulkanDeferredBase.hpp"

#include <glfw/glfw3.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>
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
  // Wait until the previous frame(same frame #) has finished, we are in host-cpu side
  vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

  // Acquire image that are done displaying from swap chain
  uint32_t imageIndex;
  VkResult res = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

  if (res == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapChain();
    return;
  } else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  // reset current frame: cpu didn't recieve image from gpu
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
  geometryPassInfo.framebuffer = gBuffer.geometryFramebuffers[currentFrame];
  geometryPassInfo.renderArea.offset = {0, 0};
  geometryPassInfo.renderArea.extent = swapChainExtent;

  std::array<VkClearValue, 4> geometryClearValues{};
  geometryClearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Normal
  geometryClearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Albedo
  geometryClearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Material
  geometryClearValues[3].depthStencil = {1.0f, 0};            // Depth

  geometryPassInfo.clearValueCount = static_cast<uint32_t>(geometryClearValues.size());
  geometryPassInfo.pClearValues = geometryClearValues.data();

  vkCmdBeginRenderPass(commandBuffer, &geometryPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  // Record geometry pass commands from derived class
  recordGeometryCommands(commandBuffer);
  vkCmdEndRenderPass(commandBuffer);
  // ==== SSAO PASS ====
  VkRenderPassBeginInfo ssaoPassInfo{};
  ssaoPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  ssaoPassInfo.renderPass = ssaoElements.ssaoRenderPass;
  ssaoPassInfo.framebuffer = ssaoElements.ssaoFramebuffers[currentFrame];
  ssaoPassInfo.renderArea.offset = {0, 0};
  ssaoPassInfo.renderArea.extent = swapChainExtent;

  VkClearValue ssaoClearValue{};
  ssaoClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // Clear to 0 occlusion

  ssaoPassInfo.clearValueCount = 1;
  ssaoPassInfo.pClearValues = &ssaoClearValue;

  vkCmdBeginRenderPass(commandBuffer, &ssaoPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  recordSSAOCommands(commandBuffer, imageIndex);
  vkCmdEndRenderPass(commandBuffer);

  // ==== SSAO BLUR PASS ====
  VkRenderPassBeginInfo ssaoBlurPassInfo{};
  ssaoBlurPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  ssaoBlurPassInfo.renderPass = ssaoElements.ssaoBlurRenderPass;
  ssaoBlurPassInfo.framebuffer = ssaoElements.ssaoBlurFramebuffers[imageIndex];
  ssaoBlurPassInfo.renderArea.offset = {0, 0};
  ssaoBlurPassInfo.renderArea.extent = swapChainExtent;

  VkClearValue ssaoBlurClearValue{};
  ssaoBlurClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // Clear to 0 occlusion

  ssaoBlurPassInfo.clearValueCount = 1;
  ssaoBlurPassInfo.pClearValues = &ssaoBlurClearValue;

  vkCmdBeginRenderPass(commandBuffer, &ssaoBlurPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  recordSSAOBlurCommands(commandBuffer, imageIndex);
  vkCmdEndRenderPass(commandBuffer);

  // ==== LIGHTING PASS ====

  VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
}

void VulkanDeferredBase::createDescriptorPool() {
  //  Create descriptor pool
  const u32 frameCount = static_cast<u32>(swapChainImages.size());
  std::vector<VkDescriptorPoolSize> poolSizes = {
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frameCount * 4},  // Gbuffer
      // SSAO pass: depth + normal + noise + 2 UBOs per frame
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frameCount * 3},  // ssao depth, normal, noise
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frameCount * 2},          // ssao kernel, params
      // SSAO Blur pass: 1 sampler per frame
      VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frameCount * 1},  // ssao blur
                                                                                        // Add more for lighting pass later...
  };
  // retrive sizes
  uint32_t maxSets = frameCount * 4;
  getDescriptorPoolSizes(poolSizes, maxSets);

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = maxSets;

  VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));
}
// G-Buffer Creation
void VulkanDeferredBase::createGBuffer() {
  // Formats for G-Buffer
  // normal: RGB=normal, A=metallic
  // albedo: RGB=albedo, A=AO
  // material: R=roughness, GBA=emissive (LDR emissive)
  gBuffer.normal.resize(MAX_FRAMES_IN_FLIGHT);
  gBuffer.albedo.resize(MAX_FRAMES_IN_FLIGHT);
  gBuffer.material.resize(MAX_FRAMES_IN_FLIGHT);
  static constexpr VkFormat GBUFFER_NORMAL_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
  static constexpr VkFormat GBUFFER_ALBEDO_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
  static constexpr VkFormat GBUFFER_MATERIAL_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
  VkFormat DEPTH_FORMAT = gBuffer.depthBuffer[0].format;

  // Create G-Buffer textures
  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    textureManager->InitTexture(gBuffer.normal[i], swapChainExtent.width, swapChainExtent.height, GBUFFER_NORMAL_FORMAT, VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    gBuffer.normal[i].imageView = textureManager->createImageView(gBuffer.normal[i].image, GBUFFER_NORMAL_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
    gBuffer.normal[i].sampler = textureManager->createGBufferSampler();

    textureManager->InitTexture(gBuffer.albedo[i], swapChainExtent.width, swapChainExtent.height, GBUFFER_ALBEDO_FORMAT, VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    gBuffer.albedo[i].imageView = textureManager->createImageView(gBuffer.albedo[i].image, GBUFFER_ALBEDO_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
    gBuffer.albedo[i].sampler = textureManager->createGBufferSampler();

    textureManager->InitTexture(gBuffer.material[i], swapChainExtent.width, swapChainExtent.height, GBUFFER_MATERIAL_FORMAT, VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    gBuffer.material[i].imageView = textureManager->createImageView(gBuffer.material[i].image, GBUFFER_MATERIAL_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
    gBuffer.material[i].sampler = textureManager->createGBufferSampler();
  }

  // NOTE: No manual layout transitions needed here
  // The render pass will handle transitions
  // Create descriptor set layout for G-Buffer "(used in lighting pass)"
  // Bindings: normal, albedo, material, depth
  std::array<VkDescriptorSetLayoutBinding, 4> bindings{};

  for (uint32_t i = 0; i < 4; i++) {
    bindings[i].binding = i;
    bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[i].pImmutableSamplers = nullptr;
  }

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &gBuffer.descriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create G-Buffer descriptor set layout!");
  }

  // Allocate descriptor sets
  std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, gBuffer.descriptorSetLayout);
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
  allocInfo.pSetLayouts = layouts.data();

  gBuffer.descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
  if (vkAllocateDescriptorSets(device, &allocInfo, gBuffer.descriptorSets.data()) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate G-Buffer descriptor sets!");
  }

  // Update descriptor sets
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    std::array<VkDescriptorImageInfo, 4> imageInfos{};

    // Binding 0: Normal + Metallic
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].imageView = gBuffer.normal[i].imageView;
    imageInfos[0].sampler = gBuffer.normal[i].sampler;

    // Binding 1: Albedo + AO
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].imageView = gBuffer.albedo[i].imageView;
    imageInfos[1].sampler = gBuffer.albedo[i].sampler;

    // Binding 2: Material (Roughness + Emissive)
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[2].imageView = gBuffer.material[i].imageView;
    imageInfos[2].sampler = gBuffer.material[i].sampler;

    // Binding 3: Depth (for position reconstruction)
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    imageInfos[3].imageView = gBuffer.depthBuffer[i].imageView;
    imageInfos[3].sampler = gBuffer.depthBuffer[i].sampler;

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
  vkDestroyDescriptorSetLayout(device, gBuffer.descriptorSetLayout, nullptr);
  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    textureManager->destroyTexture(gBuffer.normal[i]);
    textureManager->destroyTexture(gBuffer.albedo[i]);
    textureManager->destroyTexture(gBuffer.material[i]);
    textureManager->destroyTexture(gBuffer.depthBuffer[i]);
  }
}
void VulkanDeferredBase::createSsaoElements() {
  generateSSAOKernel();
  createSSAONoiseTexture();
  const uint32_t frameCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

  // createSSAOTextures:
  ssaoElements.ssaoOutput.resize(frameCount);
  ssaoElements.ssaoBlurred.resize(frameCount);
  for (size_t i = 0; i < frameCount; i++) {
    // Raw SSAO output
    textureManager->InitTexture(ssaoElements.ssaoOutput[i], swapChainExtent.width, swapChainExtent.height, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ssaoElements.ssaoOutput[i].imageView = textureManager->createImageView(ssaoElements.ssaoOutput[i].image, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    ssaoElements.ssaoOutput[i].sampler = textureManager->createGBufferSampler();

    // Blurred SSAO output
    textureManager->InitTexture(ssaoElements.ssaoBlurred[i], swapChainExtent.width, swapChainExtent.height, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ssaoElements.ssaoBlurred[i].imageView = textureManager->createImageView(ssaoElements.ssaoBlurred[i].image, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    ssaoElements.ssaoBlurred[i].sampler = textureManager->createGBufferSampler();
  }
  // ssao layout
  {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    // Binding 0: G-Buffer depth (for position reconstruction)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 1: G-Buffer normals
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 2: Noise texture
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 3: SSAO kernel UBO
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 4: SSAO params UBO (projection matrix, etc.)
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &ssaoElements.ssaoDescriptorSetLayout));
  }
  // blur layout
  {  // SSAO Blur only needs the raw SSAO output to blur it
    std::array<VkDescriptorSetLayoutBinding, 1> bindings{};

    // Binding 0: Raw SSAO texture (from SSAO pass)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &ssaoElements.ssaoBlurDescriptorSetLayout));
  }
  // allocate descriptor sets
  {
    std::vector<VkDescriptorSetLayout> layouts(frameCount, ssaoElements.ssaoDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = frameCount;
    allocInfo.pSetLayouts = layouts.data();

    ssaoElements.ssaoDescriptorSets.resize(frameCount);
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, ssaoElements.ssaoDescriptorSets.data()));
  }
  {
    std::vector<VkDescriptorSetLayout> layouts(frameCount, ssaoElements.ssaoBlurDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = frameCount;
    allocInfo.pSetLayouts = layouts.data();

    ssaoElements.ssaoBlurDescriptorSets.resize(frameCount);
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, ssaoElements.ssaoBlurDescriptorSets.data()));
  }
  for (size_t i = 0; i < frameCount; i++) {
    {
      std::array<VkDescriptorImageInfo, 3> imageInfos{};

      // Binding 0: Depth buffer (for position reconstruction)
      imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
      imageInfos[0].imageView = gBuffer.depthBuffer[i].imageView;
      imageInfos[0].sampler = gBuffer.depthBuffer[i].sampler;

      // Binding 1: Normal buffer
      imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imageInfos[1].imageView = gBuffer.normal[i].imageView;
      imageInfos[1].sampler = gBuffer.normal[i].sampler;

      // Binding 2: Noise texture (same for all frames)
      imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imageInfos[2].imageView = ssaoElements.noiseTexture.imageView;
      imageInfos[2].sampler = ssaoElements.noiseTexture.sampler;

      std::array<VkDescriptorBufferInfo, 2> bufferInfos{};

      // Binding 3: SSAO Kernel UBO
      bufferInfos[0].buffer = ssaoElements.ssaoKernelUBO[i].buffer;
      bufferInfos[0].offset = 0;
      bufferInfos[0].range = VK_WHOLE_SIZE;

      // Binding 4: SSAO Params UBO
      bufferInfos[1].buffer = ssaoElements.ssaoParamsUBO[i].buffer;
      bufferInfos[1].offset = 0;
      bufferInfos[1].range = VK_WHOLE_SIZE;

      std::array<VkWriteDescriptorSet, 5> writes{};

      // Image descriptors
      for (uint32_t j = 0; j < 3; j++) {
        writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[j].dstSet = ssaoElements.ssaoDescriptorSets[i];
        writes[j].dstBinding = j;
        writes[j].dstArrayElement = 0;
        writes[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[j].descriptorCount = 1;
        writes[j].pImageInfo = &imageInfos[j];
      }

      // Buffer descriptors
      for (uint32_t j = 0; j < 2; j++) {
        writes[3 + j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3 + j].dstSet = ssaoElements.ssaoDescriptorSets[i];
        writes[3 + j].dstBinding = 3 + j;
        writes[3 + j].dstArrayElement = 0;
        writes[3 + j].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3 + j].descriptorCount = 1;
        writes[3 + j].pBufferInfo = &bufferInfos[j];
      }

      vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    {
      VkDescriptorImageInfo imageInfo{};
      imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imageInfo.imageView = ssaoElements.ssaoOutput[i].imageView;
      imageInfo.sampler = ssaoElements.ssaoOutput[i].sampler;

      VkWriteDescriptorSet write{};
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = ssaoElements.ssaoBlurDescriptorSets[i];
      write.dstBinding = 0;
      write.dstArrayElement = 0;
      write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.descriptorCount = 1;
      write.pImageInfo = &imageInfo;

      vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
  }
  // createSSAOParamsUBO
  ssaoElements.ssaoParamsUBO.resize(frameCount);
  for (size_t i = 0; i < frameCount; i++) {
    ssaoElements.ssaoParamsUBO[i] = bufferManager->createBuffer(sizeof(SsaoElements::SsaoParamsUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
  }
}
// Render Passes: how attachments are used: just description
void VulkanDeferredBase::createRenderPasses() {
  // ==== GEOMETRY RENDER PASS ====
  {
    // Attachment 0: Normal (world-space normals)
    VkAttachmentDescription normalAttachment{};
    normalAttachment.format = gBuffer.normal[0].format;  // GBUFFER_NORMAL_FORMAT
    normalAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    normalAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    normalAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // Need to read in lighting pass
    normalAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    normalAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    normalAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    normalAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Attachment 1: Albedo (base color)
    VkAttachmentDescription albedoAttachment{};
    albedoAttachment.format = gBuffer.albedo[0].format;
    albedoAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    albedoAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    albedoAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    albedoAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    albedoAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    albedoAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    albedoAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Attachment 2: Material (roughness, metallic, ao, etc.)
    VkAttachmentDescription materialAttachment{};
    materialAttachment.format = gBuffer.material[0].format;
    materialAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    materialAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    materialAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    materialAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    materialAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    materialAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    materialAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Attachment 3: Depth
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = gBuffer.depthBuffer[0].format;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // May need depth in lighting pass
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    std::array<VkAttachmentDescription, 4> attachments = {normalAttachment, albedoAttachment, materialAttachment, depthAttachment};

    // ==== ATTACHMENT REFERENCES ====

    std::array<VkAttachmentReference, 3> colorAttachmentRefs{};
    colorAttachmentRefs[0] = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};  // Normal
    colorAttachmentRefs[1] = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};  // Albedo
    colorAttachmentRefs[2] = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};  // Material

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 3;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // ==== SUBPASS ====

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentRefs.size());
    subpass.pColorAttachments = colorAttachmentRefs.data();
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // ==== SUBPASS DEPENDENCIES ====

    std::array<VkSubpassDependency, 2> dependencies{};

    // Dependency 0: Before geometry pass
    // Wait for any previous reads (from lighting pass) to finish
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Dependency 1: After geometry pass
    // Transition attachments for shader reads in lighting pass
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // ==== CREATE RENDER PASS ====

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &geometryRenderPass) != VK_SUCCESS) {
      throw std::runtime_error("failed to create geometry render pass!");
    }
  }
  // ==== SSAO RENDER PASS ====
  {
    // Single color attachment (R8 is enough for occlusion factor)
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_R8_UNORM;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // Dependencies to ensure G-Buffer is ready
    std::array<VkSubpassDependency, 2> dependencies{};

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &ssaoElements.ssaoRenderPass));
    // blur renderpass
    VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &ssaoElements.ssaoBlurRenderPass));
  }
  // ==== LIGHTING RENDER PASS ====
  {
  }
}

// Framebuffers: which actual images the render pass will write to.
void VulkanDeferredBase::createFramebuffers() {
  const uint32_t frameCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
  gBuffer.geometryFramebuffers.resize(frameCount);
  swapChainFramebuffers.resize(frameCount);
  ssaoElements.ssaoFramebuffers.resize(frameCount);
  ssaoElements.ssaoBlurFramebuffers.resize(frameCount);

  // ==== GEOMETRY FRAMEBUFFERS ====
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    // Order must match attachment order in geometry render pass
    std::array<VkImageView, 4> attachments = {gBuffer.normal[i].imageView, gBuffer.albedo[i].imageView, gBuffer.material[i].imageView, gBuffer.depthBuffer[i].imageView};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = geometryRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;

    VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &gBuffer.geometryFramebuffers[i]));
  }
  // ==== SSAO FRAMEBUFFERS ====

  for (size_t i = 0; i < frameCount; i++) {
    // SSAO framebuffer
    {
      VkImageView attachment = ssaoElements.ssaoOutput[i].imageView;

      VkFramebufferCreateInfo fbInfo{};
      fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fbInfo.renderPass = ssaoElements.ssaoRenderPass;
      fbInfo.attachmentCount = 1;
      fbInfo.pAttachments = &attachment;
      fbInfo.width = swapChainExtent.width;
      fbInfo.height = swapChainExtent.height;
      fbInfo.layers = 1;

      VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbInfo, nullptr, &ssaoElements.ssaoFramebuffers[i]));
    }

    // SSAO Blur framebuffer
    {
      VkImageView attachment = ssaoElements.ssaoBlurred[i].imageView;

      VkFramebufferCreateInfo fbInfo{};
      fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fbInfo.renderPass = ssaoElements.ssaoBlurRenderPass;
      fbInfo.attachmentCount = 1;
      fbInfo.pAttachments = &attachment;
      fbInfo.width = swapChainExtent.width;
      fbInfo.height = swapChainExtent.height;
      fbInfo.layers = 1;

      VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbInfo, nullptr, &ssaoElements.ssaoBlurFramebuffers[i]));
    }
  }

  // ==== SWAPCHAIN FRAMEBUFFERS (for lighting pass - skip for now) ====
  // Uncomment when you implement lighting pass
  /*
  for (size_t i = 0; i < swapChainImages.size(); i++) {
    std::array<VkImageView, 1> attachments = {
        swapChainImageViews[i]
    };

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = lightingRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create swapchain framebuffer!");
    }
  }
  */
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
  createDescriptorPool();
  createDepthResources();
  createGBuffer();
  createSsaoElements();
  createRenderPasses();
  createFullscreenQuad();

  // 6. Load resources
  spdlog::info("Loading resources...");
  loadResources();

  // 7. Create pipelines
  spdlog::info("Creating pipelines...");
  createGeometryPipeline();
  // createLightingPipeline();

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

  createSwapChain();
  createImageViews();
  createDepthResources();
  createGBuffer();
  createFramebuffers();
}

void VulkanDeferredBase::cleanupSwapChain() {
  for (auto framebuffer : gBuffer.geometryFramebuffers) {
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

  // destroy descriptor sets
  vkDestroyDescriptorSetLayout(device, ssaoElements.ssaoDescriptorSetLayout, nullptr);
  vkDestroyDescriptorSetLayout(device, ssaoElements.ssaoBlurDescriptorSetLayout, nullptr);
  vkDestroyDescriptorSetLayout(device, gBuffer.descriptorSetLayout, nullptr);
  vkDestroyDescriptorPool(device, descriptorPool, nullptr);

  // Clean up fullscreen quad
  bufferManager->destroyBuffer(fullscreenQuad.vertexBuffer);
  bufferManager->destroyBuffer(fullscreenQuad.indexBuffer);

  // Clean up G-Buffer
  cleanupGBuffer();
  // Clean up swap chain
  cleanupSwapChain();

  // Clean up render passes
  vkDestroyRenderPass(device, geometryRenderPass, nullptr);
  // vkDestroyRenderPass(device, lightingRenderPass, nullptr);

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
    swapChainImageViews[i] = textureManager->createImageView(swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
  }
}

void VulkanDeferredBase::createDepthResources() {
  auto findDepthFormat = [this]() -> VkFormat {
    auto findSupportedFormat = [this](const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) -> VkFormat {
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

    return findSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL,
                               VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  };
  static VkFormat depthFormat = VK_FORMAT_UNDEFINED;
  if (depthFormat == VK_FORMAT_UNDEFINED) {
    depthFormat = findDepthFormat();
  }
  gBuffer.depthBuffer.resize(MAX_FRAMES_IN_FLIGHT);
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    textureManager->InitTexture(gBuffer.depthBuffer[i], swapChainExtent.width, swapChainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, VK_SAMPLE_COUNT_1_BIT);
    gBuffer.depthBuffer[i].imageView = textureManager->createImageView(gBuffer.depthBuffer[i].image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    gBuffer.depthBuffer[i].sampler = textureManager->createGBufferSampler();
  }
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
    if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
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
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS || vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
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

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDeferredBase::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                                 const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
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
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
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

// calculations
// Full-screen Quad Creation
void VulkanDeferredBase::createFullscreenQuad() {
  // Vertices for full-screen quad
  struct QuadVertex {
    glm::vec2 pos;
    glm::vec2 uv;
  };

  std::vector<QuadVertex> vertices = {{{-1.0f, -1.0f}, {0.0f, 0.0f}}, {{1.0f, -1.0f}, {1.0f, 0.0f}}, {{1.0f, 1.0f}, {1.0f, 1.0f}}, {{-1.0f, 1.0f}, {0.0f, 1.0f}}};

  std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

  // Create vertex buffer
  VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
  fullscreenQuad.vertexBuffer =
      bufferManager->createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
  bufferManager->updateBuffer(fullscreenQuad.vertexBuffer, vertices.data(), bufferSize, 0);
  // Create index buffer
  VkDeviceSize IndexBufferSize = sizeof(indices[0]) * indices.size();
  fullscreenQuad.indexBuffer =
      bufferManager->createBuffer(IndexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);

  bufferManager->updateBuffer(fullscreenQuad.indexBuffer, indices.data(), IndexBufferSize, 0);

  fullscreenQuad.indexCount = static_cast<uint32_t>(indices.size());
}

void VulkanDeferredBase::generateSSAOKernel() {
  auto lerp = [](float a, float b, float f) -> float { return a + f * (b - a); };
  std::default_random_engine rndEngine(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);

  std::vector<glm::vec4> ssaoKernel(SsaoElements::SSAO_KERNEL_SIZE);

  for (uint32_t i = 0; i < SsaoElements::SSAO_KERNEL_SIZE; ++i) {
    // Random point in hemisphere (tangent space, z is up)
    glm::vec3 sample(rndDist(rndEngine) * 2.0f - 1.0f,  // x: [-1, 1]
                     rndDist(rndEngine) * 2.0f - 1.0f,  // y: [-1, 1]
                     rndDist(rndEngine)                 // z: [0, 1] - hemisphere
    );
    sample = glm::normalize(sample);
    sample *= rndDist(rndEngine);  // Random length

    // Scale samples to cluster near origin (more samples close to surface)
    float scale = static_cast<float>(i) / static_cast<float>(SsaoElements::SSAO_KERNEL_SIZE);
    scale = lerp(0.1f, 1.0f, scale * scale);  // Quadratic falloff

    ssaoKernel[i] = glm::vec4(sample * scale, 0.0f);
  }

  // Upload to UBO for each frame
  for (size_t i = 0; i < swapChainImages.size(); i++) {
    ssaoElements.ssaoKernelUBO[i] = bufferManager->createBuffer(ssaoKernel.size() * sizeof(glm::vec4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    bufferManager->updateBuffer(ssaoElements.ssaoKernelUBO[i], ssaoKernel.data(), ssaoKernel.size() * sizeof(glm::vec4), 0);
  }
}
void VulkanDeferredBase::createSSAONoiseTexture() {
  std::default_random_engine rndEngine(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);

  const int noiseDim = SsaoElements::SSAO_NOISE_DIM;
  std::vector<glm::vec4> noiseData(noiseDim * noiseDim);

  for (uint32_t i = 0; i < noiseData.size(); i++) {
    // Random rotation around Z axis (in tangent space)
    noiseData[i] = glm::vec4(rndDist(rndEngine) * 2.0f - 1.0f, rndDist(rndEngine) * 2.0f - 1.0f,
                             0.0f,  // Z = 0, rotation in XY plane
                             0.0f);
  }

  ssaoElements.noiseTexture =
      textureManager->createTextureFromBuffer(noiseData.data(), sizeof(glm::vec4) * noiseDim * noiseDim, VK_FORMAT_R32G32B32A32_SFLOAT, noiseDim, noiseDim, true);
}
