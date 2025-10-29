#pragma once
#include <tiny_gltf.h>
#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>

#include "defines.hpp"
#include "renderer/BufferManager.hpp"
#include "renderer/CommandBufferUtils.hpp"
#include "renderer/VulkanContext.hpp"

class TextureManager {
 public:
  struct Texture {
    // Core Vulkan handles
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorImageInfo descriptor;

    // Image properties
    VkExtent3D extent = {0, 0, 1};
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t mipLevels = 1;
    uint32_t layerCount = 1;  // 1 for 2D, 6 for cubemap
    bool isCubemap() const { return layerCount == 6; }

    // Runtime state
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Optional
    VkImageUsageFlags usage = 0;
    VkImageType imageType = VK_IMAGE_TYPE_2D;
    VkDevice device = VK_NULL_HANDLE;

    // Constructors
    Texture() = default;
    Texture(VkDevice dev) : device(dev) {}

    // Move constructor
    Texture(Texture&& other) noexcept
        : image(other.image),
          imageView(other.imageView),
          memory(other.memory),
          sampler(other.sampler),
          descriptor(other.descriptor),
          extent(other.extent),
          format(other.format),
          mipLevels(other.mipLevels),
          layerCount(other.layerCount),
          currentLayout(other.currentLayout),
          usage(other.usage),
          imageType(other.imageType),
          device(other.device) {
      other.image = VK_NULL_HANDLE;
      other.imageView = VK_NULL_HANDLE;
      other.memory = VK_NULL_HANDLE;
      other.sampler = VK_NULL_HANDLE;
      other.extent = {0, 0, 0};
      other.format = VK_FORMAT_UNDEFINED;
      other.mipLevels = 1;
      other.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      other.usage = 0;
      other.imageType = VK_IMAGE_TYPE_2D;
      other.device = VK_NULL_HANDLE;
      other.descriptor = {};
      other.layerCount = 1;
    }

    // Move assignment operator
    Texture& operator=(Texture&& other) noexcept {
      if (this != &other) {
        cleanup();

        image = other.image;
        imageView = other.imageView;
        memory = other.memory;
        sampler = other.sampler;
        extent = other.extent;
        format = other.format;
        mipLevels = other.mipLevels;
        currentLayout = other.currentLayout;
        usage = other.usage;
        imageType = other.imageType;
        device = other.device;
        descriptor = other.descriptor;
        layerCount = other.layerCount;

        other.image = VK_NULL_HANDLE;
        other.imageView = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        other.sampler = VK_NULL_HANDLE;
        other.extent = {0, 0, 0};
        other.format = VK_FORMAT_UNDEFINED;
        other.mipLevels = 1;
        other.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        other.usage = 0;
        other.imageType = VK_IMAGE_TYPE_2D;
        other.device = VK_NULL_HANDLE;
        other.descriptor = {};
        other.layerCount = 1;
      }
      return *this;
    }

    ~Texture() { cleanup(); }

    // Delete copy operations
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

   private:
    void cleanup() {
      if (device != VK_NULL_HANDLE) {
        if (sampler != VK_NULL_HANDLE) {
          vkDestroySampler(device, sampler, nullptr);
          sampler = VK_NULL_HANDLE;
        }
        if (imageView != VK_NULL_HANDLE) {
          vkDestroyImageView(device, imageView, nullptr);
          imageView = VK_NULL_HANDLE;
        }
        if (image != VK_NULL_HANDLE) {
          vkDestroyImage(device, image, nullptr);
          image = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE) {
          vkFreeMemory(device, memory, nullptr);
          memory = VK_NULL_HANDLE;
        }
      }
      extent = {0, 0, 0};
      format = VK_FORMAT_UNDEFINED;
      mipLevels = 1;
      currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      usage = 0;
      imageType = VK_IMAGE_TYPE_2D;
    }
  };
  struct IBLTextures {
    Texture enviromentCubemap;
    Texture irradianceCubemap;
    Texture prefilteredCubemap;
  };
  struct IBLSettings {
    u32 enviromentSize = 2048;
    u32 irradianceSize = 32;
    u32 prefilteredSize = 512;
    u32 prefilteredMipLevel = 5;
    VkFormat hdrFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkFormat ldrFormat = VK_FORMAT_R8G8B8A8_UNORM;
  };

  struct TextureSampler {
    VkFilter magFilter;
    VkFilter minFilter;
    VkSamplerAddressMode addressModeU;
    VkSamplerAddressMode addressModeV;
    VkSamplerAddressMode addressModeW;
  };
  Texture createDefault();
  std::shared_ptr<VulkanContext> context;
  std::shared_ptr<CommandBufferUtils> cmdUtils;
  std::shared_ptr<BufferManager> bufferManager;

 private:
  // consider to cache textures here for switching scenes
  // std::unordered_map<std::string, Texture> loadedTextures;

 public:
  TextureManager(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<CommandBufferUtils> cmdUtils, std::shared_ptr<BufferManager> bufferManager)
      : context(ctx), cmdUtils(cmdUtils), bufferManager(bufferManager) {}
  ~TextureManager() {}

  void destroyTexture(Texture& texture);
  TextureManager::Texture createTextureFromFile(const std::string& filepath, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
  VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t levelCount = 1);
  VkSampler createTextureSampler(TextureSampler textureSampler = {VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                                                  VK_SAMPLER_ADDRESS_MODE_REPEAT},
                                 float maxLod = 0.0f, float maxAnisotropy = 0.0f);
  void InitTexture(Texture& texture, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                   uint32_t mipLevels = 1, VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT);
  void transitionImageLayout(Texture& texture, VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer commandBuffer, uint32_t mipLevels = 1);
  void copyBufferToImage(Texture& texture, VkBuffer buffer, VkCommandBuffer commandBuffer, VkDeviceSize bufferOffset = 0, uint32_t miplevel = 0,
                         VkExtent3D extent = {0, 0, 1});

  Texture createTextureFromGLTFImage(const tinygltf::Image& gltfImage, std::string path, TextureSampler textureSampler, VkQueue copyQueue);
  std::vector<TextureSampler> loadTextureSamplers(tinygltf::Model& gltfModel);
  // std::vector<Texture> loadTextures(tinygltf::Model& gltfModel, std::vector<TextureSampler>& samplers);
  Texture createTextureFromBuffer(void* data, uint32_t size, VkFormat format, uint32_t width, uint32_t height);

  // cube map
  Texture createCubemapFromSingleFile(const std::string& filepath, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
  VkImageView createCubemapImageView(VkImage image, VkFormat format, uint32_t levelCount = 1);
  void transitionCubemapLayout(Texture& texture, VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer commandBuffer);
  void copyBufferToCubemapFace(Texture& texture, VkBuffer buffer, VkCommandBuffer commandBuffer, uint32_t faceIndex, VkDeviceSize bufferOffset = 0, uint32_t miplevel = 0);
  void InitCubemapTexture(Texture& texture, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                          uint32_t mipLevels = 1);
  Texture loadHDRCubemapTexture(std::string& filename, VkFormat format, VkImageUsageFlags usage);
};
