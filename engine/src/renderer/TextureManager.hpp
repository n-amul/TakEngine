#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>

#include "BufferManager.hpp"
#include "defines.hpp"

TAK_API class TextureManager {
 public:
  struct Texture {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkSampler sampler;
  };

 private:
  VkDevice device;
  VkPhysicalDevice physicalDevice;
  VkCommandPool commandPool;
  VkQueue graphicsQueue;

  std::unique_ptr<BufferManager> bufferManager;
  std::unordered_map<std::string, Texture> loadedTextures;

 public:
  TextureManager(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue)
      : device(device), physicalDevice(physicalDevice), commandPool(commandPool), graphicsQueue(graphicsQueue) {
    bufferManager = std::make_unique<BufferManager>(device, physicalDevice, commandPool, graphicsQueue);
  }
  ~TextureManager() {
    // addcleanup function
    bufferManager->cleanup();
    bufferManager.reset();
  }

  // Simple interface
  VkImageView loadTexture(const std::string& filepath);
  void cleanup();

 private:
  void createTextureImage(const std::string& filepath, Texture& texture);
  void createTextureImageView(Texture& texture);
  void createTextureSampler(Texture& texture);
};