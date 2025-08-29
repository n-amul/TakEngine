#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include "defines.hpp"

class BufferManager {
 public:
  struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkDevice device = VK_NULL_HANDLE;

    Buffer() = default;
    Buffer(VkDevice dev);
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

   private:
    void cleanup();
  };

 private:
  VkDevice device;
  VkPhysicalDevice physicalDevice;
  VkCommandPool commandPool;
  VkQueue queue;

 public:
  BufferManager(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue);

  // Create a buffer with specified properties
  Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);

  // Create vertex buffer from data
  Buffer createVertexBuffer(const void* vertexData, VkDeviceSize size);

  // Create index buffer from data
  Buffer createIndexBuffer(const void* indexData, VkDeviceSize size);

  // Create uniform buffer (host visible for frequent updates)
  Buffer createUniformBuffer(VkDeviceSize size);

  // Map buffer memory for writing
  void* mapBuffer(const Buffer& buffer);

  // Unmap buffer memory
  void unmapBuffer(const Buffer& buffer);

  // Update buffer data (for host-visible buffers)
  void updateBuffer(const Buffer& buffer, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

  // Destroy a buffer (explicitly calls destructor's cleanup)
  void destroyBuffer(Buffer& buffer);

  // Cleanup resources
  void cleanup();

 private:
  // Find suitable memory type
  u32 findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties);

  // Copy buffer using command buffer
  void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
};