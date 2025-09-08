#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>

#include "BufferManager.hpp"
#include "CommandBufferUtils.hpp"
#include "VulkanContext.hpp"
#include "defines.hpp"

class TextureManager {
  public:
    struct Texture {
        // Core Vulkan handles
        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;

        // Image properties
        VkExtent3D extent = {0, 0, 0};
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t mipLevels = 1;

        // Runtime state (very useful!)
        VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        // Optional but helpful
        VkImageUsageFlags usage = 0;              // How it was created
        VkImageType imageType = VK_IMAGE_TYPE_2D; // 1D, 2D, 3D

        // Only if you need device for cleanup
        VkDevice device = VK_NULL_HANDLE;

        // Constructors
        Texture() = default;
        Texture(VkDevice dev) : device(dev) {}

        // Move constructor
        Texture(Texture&& other) noexcept
            : image(other.image), imageView(other.imageView), memory(other.memory), sampler(other.sampler),
              extent(other.extent), format(other.format), mipLevels(other.mipLevels),
              currentLayout(other.currentLayout), usage(other.usage), imageType(other.imageType), device(other.device) {

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
            }
            return *this;
        }

        ~Texture() {
            cleanup();
        }

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

  private:
    std::shared_ptr<VulkanContext> context;
    std::shared_ptr<CommandBufferUtils> cmdUtils;
    std::shared_ptr<BufferManager> bufferManager;
    // consider to cache textures here for switching scenes
    // std::unordered_map<std::string, Texture> loadedTextures;

  public:
    TextureManager(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<CommandBufferUtils> cmdUtils,
                   std::shared_ptr<BufferManager> bufferManager)
        : context(ctx), cmdUtils(cmdUtils), bufferManager(bufferManager) {}
    ~TextureManager() {}

    void cleanup();
    TextureManager::Texture createTextureFromFile(const std::string& filepath,
                                                  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
    VkImageView createImageView(VkImage image, VkFormat format,
                                VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT);
    VkSampler createTextureSampler(VkFilter filter = VK_FILTER_LINEAR,
                                   VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);
    void InitTexture(Texture& texture, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                     VkImageUsageFlags usage, VkMemoryPropertyFlags properties);
    void transitionImageLayout(Texture& texture, VkImageLayout oldLayout, VkImageLayout newLayout,
                               VkCommandBuffer commandBuffer);
    void copyBufferToImage(Texture& texture, VkBuffer buffer, VkCommandBuffer commandBuffer);
};
