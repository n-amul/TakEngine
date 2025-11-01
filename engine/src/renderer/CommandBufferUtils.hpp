#pragma once
#include <vulkan/vulkan.h>

#include <functional>
#include <stdexcept>

#include "defines.hpp"
#include "renderer/VulkanContext.hpp"

class CommandBufferUtils {
 private:
  std::shared_ptr<VulkanContext> context;

  void checkVkResult(VkResult result, const char* errorMsg) {
    if (result != VK_SUCCESS) {
      throw std::runtime_error(errorMsg);
    }
  }

 public:
  // TODO: seperate creating cb and begin
  CommandBufferUtils(std::shared_ptr<VulkanContext> ctx) : context(ctx) {}

  VkCommandBuffer beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = context->transientCommandPool;  // Use transient pool
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VkResult result = vkAllocateCommandBuffers(context->device, &allocInfo, &commandBuffer);
    checkVkResult(result, "Failed to allocate command buffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    checkVkResult(result, "Failed to begin command buffer");

    return commandBuffer;
  }

  void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    VkResult result = vkEndCommandBuffer(commandBuffer);
    checkVkResult(result, "Failed to end command buffer");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    result = vkQueueSubmit(context->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    checkVkResult(result, "Failed to submit command buffer");

    result = vkQueueWaitIdle(context->graphicsQueue);
    checkVkResult(result, "Failed to wait for queue idle");

    vkFreeCommandBuffers(context->device, context->transientCommandPool, 1, &commandBuffer);
  }

  // Convenience method
  void executeCommands(std::function<void(VkCommandBuffer)> recordCommands) {
    VkCommandBuffer cmd = beginSingleTimeCommands();
    recordCommands(cmd);
    endSingleTimeCommands(cmd);
  }
};