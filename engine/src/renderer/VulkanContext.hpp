#pragma once
#include <vulkan/vulkan.h>

#include <memory>

struct VulkanContext {
  VkInstance instance;
  VkDevice device;
  VkPhysicalDevice physicalDevice;
  VkCommandPool commandPool;
  VkCommandPool transientCommandPool;
  VkQueue graphicsQueue;
  VkQueue presentQueue;
};