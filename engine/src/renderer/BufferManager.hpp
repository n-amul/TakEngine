#pragma once

#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

#include "defines.hpp"
#include "renderer/CommandBufferUtils.hpp"
#include "renderer/VulkanContext.hpp"
class BufferManager {
 public:
  struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkDevice device = VK_NULL_HANDLE;

    Buffer() = default;
    Buffer(VkDevice dev) : device(dev) {}
    // steal the other's resources and other will lose ownership using move() implicitly
    Buffer(Buffer&& other) noexcept : buffer(other.buffer), memory(other.memory), size(other.size), device(other.device) {
      other.buffer = VK_NULL_HANDLE;
      other.memory = VK_NULL_HANDLE;
      other.size = 0;
      other.device = VK_NULL_HANDLE;
    }
    Buffer& operator=(Buffer&& other) noexcept {
      if (this != &other) {
        cleanup();

        buffer = other.buffer;
        memory = other.memory;
        size = other.size;
        device = other.device;

        other.buffer = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        other.size = 0;
        other.device = VK_NULL_HANDLE;
      }
      return *this;
    }
    ~Buffer() { cleanup(); }

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

   private:
    void cleanup() {
      if (device != VK_NULL_HANDLE) {
        if (buffer != VK_NULL_HANDLE) {
          vkDestroyBuffer(device, buffer, nullptr);
          buffer = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE) {
          vkFreeMemory(device, memory, nullptr);
          memory = VK_NULL_HANDLE;
        }
      }
      size = 0;
    }
  };

 private:
  std::shared_ptr<VulkanContext> context;
  std::shared_ptr<CommandBufferUtils> cmdUtils;

 public:
  BufferManager(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<CommandBufferUtils> cmdUtils) : context(ctx), cmdUtils(cmdUtils) {};

  // Create a buffer with specified properties
  Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);

  Buffer createGPULocalBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);

  // Update buffer data (for host-visible buffers)
  void updateBuffer(const Buffer& buffer, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

  // Destroy a buffer (explicitly calls destructor's cleanup)
  void destroyBuffer(Buffer& buffer);

  //  void cleanup();
  //  Find suitable memory type
  u32 findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties);

  // Copy buffer using command buffer
  void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
};