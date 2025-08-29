#include "BufferManager.hpp"

#include <cstring>
#include <stdexcept>

BufferManager::Buffer::Buffer(VkDevice dev) : device(dev) {}

BufferManager::Buffer::Buffer(Buffer&& other) noexcept
    : buffer(other.buffer), memory(other.memory), size(other.size), device(other.device) {
  other.buffer = VK_NULL_HANDLE;
  other.memory = VK_NULL_HANDLE;
  other.size = 0;
}

BufferManager::Buffer& BufferManager::Buffer::operator=(Buffer&& other) noexcept {
  // steal the other's resources and other will lose ownership using move() implicitly
  if (this != &other) {
    cleanup();

    buffer = other.buffer;
    memory = other.memory;
    size = other.size;
    device = other.device;

    // Clear the source
    other.buffer = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
    other.size = 0;
  }
  return *this;
}

BufferManager::Buffer::~Buffer() { cleanup(); }

void BufferManager::Buffer::cleanup() {
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

// BufferManager implementation
BufferManager::BufferManager(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue)
    : device(device), physicalDevice(physicalDevice), commandPool(commandPool), queue(queue) {}

BufferManager::Buffer BufferManager::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                                  VkMemoryPropertyFlags properties) {
  Buffer buffer(device);  // Initialize with device handle
  buffer.size = size;

  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer.buffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to create buffer!");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, buffer.buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &buffer.memory) != VK_SUCCESS) {
    vkDestroyBuffer(device, buffer.buffer, nullptr);
    throw std::runtime_error("failed to allocate buffer memory!");
  }

  vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0);

  return buffer;
}

BufferManager::Buffer BufferManager::createVertexBuffer(const void* vertexData, VkDeviceSize size) {
  // Create staging buffer
  Buffer stagingBuffer = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  // Copy data to staging buffer
  void* data;
  vkMapMemory(device, stagingBuffer.memory, 0, size, 0, &data);
  memcpy(data, vertexData, size);
  vkUnmapMemory(device, stagingBuffer.memory);

  // Create device local buffer
  Buffer vertexBuffer = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // Copy from staging to device
  copyBuffer(stagingBuffer.buffer, vertexBuffer.buffer, size);

  // Clean up staging buffer
  destroyBuffer(stagingBuffer);

  return vertexBuffer;
}

BufferManager::Buffer BufferManager::createIndexBuffer(const void* indexData, VkDeviceSize size) {
  // Create staging buffer
  Buffer stagingBuffer = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  // Copy data to staging buffer
  void* data;
  vkMapMemory(device, stagingBuffer.memory, 0, size, 0, &data);
  memcpy(data, indexData, size);
  vkUnmapMemory(device, stagingBuffer.memory);

  // Create device local buffer
  Buffer indexBuffer = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // Copy from staging to device
  copyBuffer(stagingBuffer.buffer, indexBuffer.buffer, size);

  // Clean up staging buffer
  destroyBuffer(stagingBuffer);

  return indexBuffer;
}

BufferManager::Buffer BufferManager::createUniformBuffer(VkDeviceSize size) {
  return createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

void* BufferManager::mapBuffer(const Buffer& buffer) {
  void* data;
  vkMapMemory(device, buffer.memory, 0, buffer.size, 0, &data);
  return data;
}

void BufferManager::unmapBuffer(const Buffer& buffer) { vkUnmapMemory(device, buffer.memory); }

void BufferManager::updateBuffer(const Buffer& buffer, const void* data, VkDeviceSize size, VkDeviceSize offset) {
  void* mappedData;
  vkMapMemory(device, buffer.memory, offset, size, 0, &mappedData);
  memcpy(mappedData, data, size);
  vkUnmapMemory(device, buffer.memory);
}

void BufferManager::destroyBuffer(Buffer& buffer) {
  buffer = Buffer();  // Move-assign an empty buffer, triggering cleanup
}

void BufferManager::cleanup() {
  // BufferManager doesn't own the device/queue/etc, so nothing to clean here
  // Individual buffers should be destroyed by their owners
}

u32 BufferManager::findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

  for (u32 i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}

void BufferManager::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  VkBufferCopy copyRegion{};
  copyRegion.size = size;
  vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue);

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}