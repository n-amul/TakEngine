// #pragma once
// #include <vulkan/vulkan.h>

// #include <array>
// #include <memory>
// #include <string>
// #include <unordered_map>

// #include "BufferManager.hpp"
// #include "CommandBufferUtils.hpp"
// #include "defines.hpp"

// TAK_API class TextureManager {
//  public:
//   struct Texture {
//     VkImage image;
//     VkDeviceMemory memory;
//     VkImageView view;
//     VkSampler sampler;
//   };

//  private:
//   VkDevice device;
//   VkPhysicalDevice physicalDevice;
//   VkCommandPool commandPool;
//   VkQueue graphicsQueue;

//   std::shared_ptr<CommandBufferUtils> cmdUtils;
//   std::unique_ptr<BufferManager> bufferManager;
//   std::unordered_map<std::string, Texture> loadedTextures;

//  public:
//   TextureManager(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue, std::shared_ptr<CommandBufferUtils> cmdUtils)
//       : device(device), physicalDevice(physicalDevice), commandPool(commandPool), graphicsQueue(graphicsQueue), cmdUtils(cmdUtils) {
//     bufferManager = std::make_unique<BufferManager>(device, physicalDevice, commandPool, graphicsQueue);
//   }
//   ~TextureManager() {}

//   // Simple interface
//   VkImageView loadTexture(const std::string& filepath);
//   void cleanup();

//  private:
//   void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, Texture texture);
//   void createTextureImage(const std::string& filepath, Texture& texture);
//   void createTextureImageView(Texture& texture);
//   void createTextureSampler(Texture& texture);
// };