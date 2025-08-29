#include "TextureManager.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <stdexcept>

void TextureManager::createTextureImage(const std::string& filepath, Texture& texture) {
  int texWidth, texHeight, texChannels;
  stbi_uc* pixels = stbi_load("textures/texture.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
  VkDeviceSize imageSize = texWidth * texHeight * 4;

  if (!pixels) {
    throw std::runtime_error("failed to load texture image!");
  }
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  // createbuffer
}