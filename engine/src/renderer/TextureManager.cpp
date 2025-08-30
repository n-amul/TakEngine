// #include "TextureManager.hpp"
// #define STB_IMAGE_IMPLEMENTATION
// #include <stb_image.h>

// #include <stdexcept>

// void TextureManager::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
//                                  Texture texture) {
//   VkImageCreateInfo imageInfo{};
//   imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
//   imageInfo.imageType = VK_IMAGE_TYPE_2D;
//   imageInfo.extent.width = width;
//   imageInfo.extent.height = height;
//   imageInfo.extent.depth = 1;
//   imageInfo.mipLevels = 1;
//   imageInfo.arrayLayers = 1;
//   imageInfo.format = format;
//   imageInfo.tiling = tiling;
//   imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
//   imageInfo.usage = usage;
//   imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
//   imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

//   if (vkCreateImage(device, &imageInfo, nullptr, &texture.image) != VK_SUCCESS) {
//     throw std::runtime_error("failed to create image!");
//   }

//   VkMemoryRequirements memRequirements;
//   vkGetImageMemoryRequirements(device, texture.image, &memRequirements);

//   VkMemoryAllocateInfo allocInfo{};
//   allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
//   allocInfo.allocationSize = memRequirements.size;
//   allocInfo.memoryTypeIndex = this->bufferManager->findMemoryType(memRequirements.memoryTypeBits, properties);

//   if (vkAllocateMemory(device, &allocInfo, nullptr, &texture.memory) != VK_SUCCESS) {
//     throw std::runtime_error("failed to allocate image memory!");
//   }

//   vkBindImageMemory(device, texture.image, texture.memory, 0);
// }

// void TextureManager::createTextureImage(const std::string& filepath, Texture& texture) {
//   int texWidth, texHeight, texChannels;
//   stbi_uc* pixels = stbi_load("textures/texture.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
//   VkDeviceSize imageSize = texWidth * texHeight * 4;

//   if (!pixels) {
//     throw std::runtime_error("failed to load texture image!");
//   }
//   BufferManager::Buffer stagingBuffer =
//       bufferManager->createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

//   bufferManager->updateBuffer(stagingBuffer, pixels, imageSize, 0);
//   stbi_image_free(pixels);
//   // Texture texture();
//   // createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
//   //             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture);
//   // load in map?
// }