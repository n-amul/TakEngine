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

  VkPhysicalDeviceProperties properties;
  VkPhysicalDeviceFeatures features;
  VkPhysicalDeviceFeatures enabledFeatures;
  u32 queueFamilyIndex;  // supports graphics and presentation queue
};