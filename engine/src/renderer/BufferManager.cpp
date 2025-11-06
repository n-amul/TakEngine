#include "renderer/BufferManager.hpp"

#include <cstring>
#include <stdexcept>

#include "BufferManager.hpp"

BufferManager::Buffer BufferManager::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                                  VkMemoryPropertyFlags properties, bool keepMapped) {
  Buffer buffer(context->device);  // Initialize with device handle
  buffer.size = size;

  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(context->device, &bufferInfo, nullptr, &buffer.buffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to create buffer!");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(context->device, buffer.buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(context->device, &allocInfo, nullptr, &buffer.memory) != VK_SUCCESS) {
    vkDestroyBuffer(context->device, buffer.buffer, nullptr);
    throw std::runtime_error("failed to allocate buffer memory!");
  }
  // link buffer to memory somewhere in gpu
  vkBindBufferMemory(context->device, buffer.buffer, buffer.memory, 0);
  // Map the buffer if requested and if it's host-visible
  if (keepMapped && (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
    if (vkMapMemory(context->device, buffer.memory, 0, size, 0, &buffer.mapped) != VK_SUCCESS) {
      vkDestroyBuffer(context->device, buffer.buffer, nullptr);
      vkFreeMemory(context->device, buffer.memory, nullptr);
      throw std::runtime_error("failed to map buffer memory!");
    }
    buffer.isMapped = true;
  }
  buffer.descriptor.buffer = buffer.buffer;
  buffer.descriptor.offset = 0;
  buffer.descriptor.range = size;

  return buffer;
}

BufferManager::Buffer BufferManager::createGPULocalBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
  if (!data || size == 0) {
    throw std::runtime_error("Invalid data or size for GPU buffer creation");
  }
  // Create staging buffer
  Buffer stagingBuffer = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  // Copy data to staging buffer
  updateBuffer(stagingBuffer, data, size, 0);
  // Create device local buffer with the specified usage
  Buffer deviceBuffer = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  // Copy from staging to device
  copyBuffer(stagingBuffer.buffer, deviceBuffer.buffer, size);
  // Clean up staging buffer
  destroyBuffer(stagingBuffer);
  return deviceBuffer;
}

void BufferManager::updateBuffer(const Buffer& buffer, const void* data, VkDeviceSize size, VkDeviceSize offset) {
  if (buffer.isMapped && buffer.mapped) {
    // Calculate the destination pointer with offset
    void* destPtr = static_cast<char*>(buffer.mapped) + offset;
    memcpy(destPtr, data, size);
    // If the memory is not coherent, we need to flush
    // check for VK_MEMORY_PROPERTY_HOST_COHERENT_BIT and flush if not set
    // VkMappedMemoryRange memoryRange{};
    // memoryRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    // memoryRange.memory = buffer.memory;
    // memoryRange.offset = offset;
    // memoryRange.size = size;
    // vkFlushMappedMemoryRanges(context->device, 1, &memoryRange);
  } else {
    void* mappedData;
    vkMapMemory(context->device, buffer.memory, offset, size, 0, &mappedData);
    memcpy(mappedData, data, size);
    vkUnmapMemory(context->device, buffer.memory);
  }
}

void BufferManager::destroyBuffer(Buffer& buffer) {
  buffer = Buffer();  // Move-assign an empty buffer, triggering Buffer::cleanup
}

u32 BufferManager::findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(context->physicalDevice, &memProperties);

  for (u32 i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}

void BufferManager::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
  VkCommandBuffer commandBuffer = cmdUtils->beginSingleTimeCommands();
  VkBufferCopy copyRegion{};
  copyRegion.size = size;
  vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

  cmdUtils->endSingleTimeCommands(commandBuffer);
  // A fence would allow to schedule multiple transfers simultaneously and wait for all of them complete, instead of
  // executing one at a time. That may give the driver more opportunities to optimize.
}

BufferManager::Buffer BufferManager::createStagingBuffer(VkDeviceSize size) {
  return createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false);
}
