#include "renderer/TextureManager.hpp"

#include <basisu_transcoder.h>
#include <spdlog/spdlog.h>
#include <stb_image.h>

#include <cmath>
#include <fstream>
#include <stdexcept>

#include "TextureManager.hpp"

void TextureManager::InitTexture(Texture& texture, uint32_t width,
                                 uint32_t height, VkFormat format,
                                 VkImageTiling tiling,
                                 VkImageUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 uint32_t mipLevels) {
  // Initialize texture properties
  texture.device = context->device;
  texture.extent = {width, height, 1};
  texture.format = format;
  texture.mipLevels = mipLevels;
  texture.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  texture.usage = usage;
  texture.imageType = VK_IMAGE_TYPE_2D;

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = texture.imageType;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = texture.mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = texture.currentLayout;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(context->device, &imageInfo, nullptr,
                    &texture.image) != VK_SUCCESS) {
    throw std::runtime_error("failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(context->device, texture.image,
                               &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = bufferManager->findMemoryType(
      memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(context->device, &allocInfo, nullptr,
                       &texture.memory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  vkBindImageMemory(context->device, texture.image, texture.memory,
                    0);
}

void TextureManager::transitionImageLayout(
    Texture& texture, VkImageLayout oldLayout,
    VkImageLayout newLayout, VkCommandBuffer commandBuffer,
    uint32_t mipLevels) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = texture.image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = 0;

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;
  if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    // if has stencil bit
    if (texture.format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        texture.format == VK_FORMAT_D24_UNORM_S8_UINT) {
      barrier.subresourceRange.aspectMask |=
          VK_IMAGE_ASPECT_STENCIL_BIT;
    }
  } else {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  }

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;  // Ensures all
                                       // vkCmdCopyBufferToImage
                                       // operations complete
    barrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;  // Tells Vulkan that shaders will
                                    // read this texture

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;  // transfer must be
                                                // done before
                                                // fragment shader
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout ==
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  } else {
    throw std::invalid_argument("unsupported layout transition!");
  }
  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage,
                       0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkSampler TextureManager::createTextureSampler(
    TextureSampler textureSampler, float maxLod) {
  VkSampler sampler;
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = textureSampler.magFilter;
  samplerInfo.minFilter = textureSampler.minFilter;

  samplerInfo.addressModeU = textureSampler.addressModeU;
  samplerInfo.addressModeV = textureSampler.addressModeV;
  samplerInfo.addressModeW = textureSampler.addressModeW;

  samplerInfo.anisotropyEnable = VK_TRUE;

  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(context->physicalDevice, &properties);

  samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;

  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = maxLod;

  if (vkCreateSampler(context->device, &samplerInfo, nullptr,
                      &sampler) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }

  return sampler;
}

VkImageView TextureManager::createImageView(
    VkImage image, VkFormat format, VkImageAspectFlags aspectFlags,
    uint32_t levelCount) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspectFlags;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = levelCount;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkImageView imageView;
  if (vkCreateImageView(context->device, &viewInfo, nullptr,
                        &imageView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create image view!");
  }

  return imageView;
}

void TextureManager::copyBufferToImage(
    Texture& texture, VkBuffer buffer, VkCommandBuffer commandBuffer,
    VkDeviceSize bufferOffset, uint32_t miplevel, VkExtent3D extent) {
  VkBufferImageCopy region{};
  region.bufferOffset = bufferOffset;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;

  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = miplevel;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;

  region.imageOffset = {0, 0, 0};
  region.imageExtent = extent.width == 0 ? texture.extent : extent;
  vkCmdCopyBufferToImage(commandBuffer, buffer, texture.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &region);
}

TextureManager::Texture TextureManager::createTextureFromFile(
    const std::string& filepath, VkFormat format) {
  // Add logging
  spdlog::info("Loading texture from: {}", filepath);

  int texWidth, texHeight, texChannels;
  stbi_uc* pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight,
                              &texChannels, STBI_rgb_alpha);

  if (!pixels) {
    throw std::runtime_error(
        "failed to load texture image: " + filepath +
        " (stbi error: " + stbi_failure_reason() + ")");
  }

  spdlog::info("Texture loaded: {}x{}, {} channels", texWidth,
               texHeight, texChannels);

  VkDeviceSize imageSize = texWidth * texHeight * 4;

  // Create staging buffer
  BufferManager::Buffer stagingBuffer =
      bufferManager->createStagingBuffer(imageSize);

  bufferManager->updateBuffer(stagingBuffer, pixels, imageSize, 0);
  stbi_image_free(pixels);

  Texture texture;
  VkImageUsageFlags usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

  InitTexture(texture, texWidth, texHeight, format,
              VK_IMAGE_TILING_OPTIMAL, usage,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // Begin batch command buffer
  VkCommandBuffer commandBuffer = cmdUtils->beginSingleTimeCommands();
  // undefined --> ready to recieve data
  transitionImageLayout(texture, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        commandBuffer);
  // Copy staging buffer to image
  copyBufferToImage(texture, stagingBuffer.buffer, commandBuffer);
  // From: "Optimized for receiving data" → To: "Optimized for shader
  // sampling"
  transitionImageLayout(texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        commandBuffer);
  cmdUtils->endSingleTimeCommands(commandBuffer);

  // Create imageview and sampler
  texture.imageView = createImageView(texture.image, format,
                                      VK_IMAGE_ASPECT_COLOR_BIT);
  texture.sampler = createTextureSampler();
  // Staging buffer will be cleaned up by its destructor
  return texture;
}

TextureManager::Texture TextureManager::createTextureFromGLTFImage(
    const tinygltf::Image& gltfImage, std::string path,
    TextureSampler textureSampler, VkQueue copyQueue) {
  Texture texture;
  spdlog::info("Creating texture from glTF image: {}",
               gltfImage.name);
  // Get image dimensions and data

  // KTX2 files need to be handled explicitly
  bool isKtx2 = false;
  if (gltfImage.uri.find_last_of(".") != std::string::npos) {
    if (gltfImage.uri.substr(gltfImage.uri.find_last_of(".") + 1) ==
        "ktx2") {
      isKtx2 = true;
    }
  }
  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
  if (isKtx2) {
    // Image is KTX2 using basis universal compression. Those images
    // need to be loaded from disk and will be transcoded to a native
    // GPU format
    basist::ktx2_transcoder ktxTranscoder;
    const std::string filename = path + "/" + gltfImage.uri;
    std::ifstream ifs(
        filename, std::ios::binary | std::ios::in | std::ios::ate);
    if (!ifs.is_open()) {
      throw std::runtime_error(
          "Could not load the requested image file " + filename);
    }
    uint32_t inputDataSize = static_cast<uint32_t>(ifs.tellg());
    std::vector<char> inputData(inputDataSize);

    ifs.seekg(0, std::ios::beg);
    ifs.read(inputData.data(), inputDataSize);
    bool success =
        ktxTranscoder.init(inputData.data(), inputDataSize);
    if (!success) {
      throw std::runtime_error(
          "Could not initialize ktx2 transcoder for image file " +
          filename);
    }

    // Select target format based on device features (use uncompressed
    // if none supported)
    auto targetFormat = basist::transcoder_texture_format::cTFRGBA32;
    auto formatSupported = [&](VkFormat format) {
      VkFormatProperties formatProperties;
      vkGetPhysicalDeviceFormatProperties(context->physicalDevice,
                                          format, &formatProperties);
      return ((formatProperties.optimalTilingFeatures &
               VK_FORMAT_FEATURE_TRANSFER_DST_BIT) &&
              (formatProperties.optimalTilingFeatures &
               VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT));
    };
    if (context->features.textureCompressionBC) {
      if (formatSupported(VK_FORMAT_BC7_UNORM_BLOCK)) {
        targetFormat = basist::transcoder_texture_format::cTFBC7_RGBA;
        format = VK_FORMAT_BC7_UNORM_BLOCK;
      } else if (formatSupported(VK_FORMAT_BC3_UNORM_BLOCK)) {
        targetFormat = basist::transcoder_texture_format::cTFBC3_RGBA;
        format = VK_FORMAT_BC3_UNORM_BLOCK;
      }
    }

    const bool targetFormatIsUncompressed =
        basist::basis_transcoder_format_is_uncompressed(targetFormat);
    std::vector<basist::ktx2_image_level_info> levelInfos(
        ktxTranscoder.get_levels());
    texture.mipLevels = ktxTranscoder.get_levels();

    // Retrieves information about each mip level in the texture for
    // later transcoding. only support 2D images (no cube maps or
    // layered images)
    for (uint32_t i = 0; i < texture.mipLevels; i++) {
      ktxTranscoder.get_image_level_info(levelInfos[i], i, 0, 0);
    }
    // Create one staging buffer large enough to hold all uncompressed
    // image levels
    const uint32_t bytesPerBlockOrPixel =
        basist::basis_get_bytes_per_block_or_pixel(targetFormat);
    uint32_t numBlocksOrPixels = 0;
    VkDeviceSize totalBufferSize = 0;
    for (uint32_t i = 0; i < texture.mipLevels; i++) {
      // Size calculations differ for compressed/uncompressed formats
      numBlocksOrPixels = targetFormatIsUncompressed
                              ? levelInfos[i].m_orig_width *
                                    levelInfos[i].m_orig_height
                              : levelInfos[i].m_total_blocks;
      totalBufferSize += numBlocksOrPixels * bytesPerBlockOrPixel;
    }
    // Create staging buffer using BufferManager
    BufferManager::Buffer stagingBuffer =
        bufferManager->createStagingBuffer(totalBufferSize);

    // Map the staging buffer memory
    void* data;
    vkMapMemory(context->device, stagingBuffer.memory, 0,
                totalBufferSize, 0, &data);
    unsigned char* bufferPtr = static_cast<unsigned char*>(data);
    // Start transcoding
    success = ktxTranscoder.start_transcoding();
    if (!success) {
      vkUnmapMemory(context->device, stagingBuffer.memory);
      throw std::runtime_error(
          "Could not start transcoding for image file " + filename);
    }
    // Transcode all mip levels into the temporary buffer
    for (uint32_t i = 0; i < texture.mipLevels; i++) {
      numBlocksOrPixels = targetFormatIsUncompressed
                              ? levelInfos[i].m_orig_width *
                                    levelInfos[i].m_orig_height
                              : levelInfos[i].m_total_blocks;
      uint32_t outputSize = numBlocksOrPixels * bytesPerBlockOrPixel;
      if (!ktxTranscoder.transcode_image_level(i, 0, 0, bufferPtr,
                                               numBlocksOrPixels,
                                               targetFormat, 0)) {
        vkUnmapMemory(context->device, stagingBuffer.memory);
        throw std::runtime_error(
            "Could not transcode the requested image file " +
            filename);
      }
      bufferPtr += outputSize;
    }
    vkUnmapMemory(
        context->device,
        stagingBuffer.memory);  // need copy instead of map??

    // create image
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT;
    InitTexture(texture, levelInfos[0].m_orig_width,
                levelInfos[0].m_orig_height, format,
                VK_IMAGE_TILING_OPTIMAL, usage,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                texture.mipLevels);

    VkCommandBuffer copyCmd = cmdUtils->beginSingleTimeCommands();
    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.levelCount = texture.mipLevels;
    subresourceRange.layerCount = 1;
    transitionImageLayout(texture, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          copyCmd, texture.mipLevels);

    VkDeviceSize bufferOffset = 0;
    for (uint32_t i = 0; i < texture.mipLevels; i++) {
      numBlocksOrPixels = targetFormatIsUncompressed
                              ? levelInfos[i].m_orig_width *
                                    levelInfos[i].m_orig_height
                              : levelInfos[i].m_total_blocks;
      uint32_t outputSize = numBlocksOrPixels * bytesPerBlockOrPixel;
      copyBufferToImage(texture, stagingBuffer.buffer, copyCmd,
                        bufferOffset, i,
                        {levelInfos[i].m_orig_width,
                         levelInfos[i].m_orig_height, 1});
      bufferOffset += outputSize;
    }
    transitionImageLayout(texture,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          copyCmd, texture.mipLevels);

    // Update the texture's current layout
    texture.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    cmdUtils->endSingleTimeCommands(copyCmd);
    stagingBuffer = BufferManager::Buffer();
  } else {  // Image is a basic glTF format like png or jpg and can be
            // loaded directly via tinyglTF
    std::vector<unsigned char> buffer;
    VkDeviceSize bufferSize = 0;
    if (gltfImage.component == 3) {
      // convert to rgba
      u32 resolution = gltfImage.width * gltfImage.height;
      bufferSize = resolution * 4;
      buffer.resize(bufferSize);
      unsigned char* rgba = buffer.data();
      const unsigned char* rgb = gltfImage.image.data();
      for (uint32_t i = 0; i < resolution; ++i) {
        rgba[0] = rgb[0];
        rgba[1] = rgb[1];
        rgba[2] = rgb[2];
        rgba[3] = 255;  // opaque

        rgba += 4;
        rgb += 3;
      }
    } else {
      buffer = gltfImage.image;
      bufferSize = gltfImage.image.size();
    }
    // PNG supports up to 64 bits
    if (gltfImage.pixel_type ==
        TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
      format = VK_FORMAT_R16G16B16A16_UNORM;
    }
    texture.mipLevels = static_cast<uint32_t>(
        floor(log2(std::max(gltfImage.width, gltfImage.height))) +
        1.0);
    // texture.mipLevels =
    // static_cast<uint32_t>(floor(log2(std::max(texture.extent.width,
    // texture.extent.height))) + 1.0);

    // upload image data to staging buffer
    BufferManager::Buffer stagingBuffer =
        bufferManager->createStagingBuffer(bufferSize);
    void* data;
    vkMapMemory(context->device, stagingBuffer.memory, 0, bufferSize,
                0, &data);
    memcpy(data, buffer.data(), bufferSize);
    vkUnmapMemory(context->device, stagingBuffer.memory);
    // Create the texture image
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT;
    InitTexture(texture, static_cast<uint32_t>(gltfImage.width),
                static_cast<uint32_t>(gltfImage.height), format,
                VK_IMAGE_TILING_OPTIMAL, usage,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                texture.mipLevels);

    VkCommandBuffer copyCmd = cmdUtils->beginSingleTimeCommands();
    // undefined --> ready to recieve data
    transitionImageLayout(texture, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          copyCmd, texture.mipLevels);
    // Copy buffer to mip level = 0
    copyBufferToImage(texture, stagingBuffer.buffer, copyCmd, 0, 0,
                      texture.extent);

    // Transition first mip level to transfer source for mipmap
    // generation
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = texture.image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = texture.extent.width;
    int32_t mipHeight = texture.extent.height;

    for (uint32_t i = 1; i < texture.mipLevels; i++) {
      barrier.subresourceRange.baseMipLevel = i - 1;
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

      vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                           nullptr, 0, nullptr, 1, &barrier);

      VkImageBlit blit{};
      blit.srcOffsets[0] = {0, 0, 0};
      blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
      blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.srcSubresource.mipLevel = i - 1;
      blit.srcSubresource.baseArrayLayer = 0;
      blit.srcSubresource.layerCount = 1;
      blit.dstOffsets[0] = {0, 0, 0};
      blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1,
                            mipHeight > 1 ? mipHeight / 2 : 1, 1};
      blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.dstSubresource.mipLevel = i;
      blit.dstSubresource.baseArrayLayer = 0;
      blit.dstSubresource.layerCount = 1;

      vkCmdBlitImage(copyCmd, texture.image,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     texture.image,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                     VK_FILTER_LINEAR);

      // Transition previous mip level to shader read
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                           0, nullptr, 0, nullptr, 1, &barrier);

      if (mipWidth > 1) mipWidth /= 2;
      if (mipHeight > 1) mipHeight /= 2;
    }

    // Transition last mip level to shader read
    barrier.subresourceRange.baseMipLevel = texture.mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    texture.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    cmdUtils->endSingleTimeCommands(copyCmd);
  }

  // Create image view
  texture.imageView =
      createImageView(texture.image, format,
                      VK_IMAGE_ASPECT_COLOR_BIT, texture.mipLevels);

  // Create sampler based on textureSampler parameter
  texture.sampler = createTextureSampler(
      textureSampler, static_cast<float>(texture.mipLevels));
  // descriptor
  texture.descriptor.imageLayout = texture.currentLayout;
  texture.descriptor.imageView = texture.imageView;
  texture.descriptor.sampler = texture.sampler;

  return texture;
}

std::vector<TextureManager::TextureSampler>
TextureManager::loadTextureSamplers(tinygltf::Model& gltfModel) {
  auto getVkFilterMode = [](int32_t filterMode) -> VkFilter {
    switch (filterMode) {
      case -1:
      case 9728:
        return VK_FILTER_NEAREST;
      case 9729:
        return VK_FILTER_LINEAR;
      case 9984:
        return VK_FILTER_NEAREST;
      case 9985:
        return VK_FILTER_NEAREST;
      case 9986:
        return VK_FILTER_LINEAR;
      case 9987:
        return VK_FILTER_LINEAR;
    }
    spdlog::info("Unknown filter mode for getVkFilterMode: {}",
                 filterMode);
    return VK_FILTER_NEAREST;
  };
  auto getVkWrapMode = [](int32_t wrapMode) -> VkSamplerAddressMode {
    switch (wrapMode) {
      case -1:
      case 10497:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
      case 33071:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      case 33648:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
    spdlog::info("Unknown wrap mode for getVkWrapMode: {}", wrapMode);
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  };
  std::vector<TextureSampler> samplers;
  for (tinygltf::Sampler smpl : gltfModel.samplers) {
    TextureSampler sampler{};
    sampler.minFilter = getVkFilterMode(smpl.minFilter);
    sampler.magFilter = getVkFilterMode(smpl.magFilter);
    sampler.addressModeU = getVkWrapMode(smpl.wrapS);
    sampler.addressModeV = getVkWrapMode(smpl.wrapT);
    sampler.addressModeW = sampler.addressModeV;
    samplers.push_back(sampler);
  }
  return samplers;
}
TextureManager::Texture TextureManager::createTextureFromBuffer(
    void* data, uint32_t size, VkFormat format, uint32_t width,
    uint32_t height) {
  Texture texture;
  // Create staging buffer
  BufferManager::Buffer stagingBuffer =
      bufferManager->createStagingBuffer(size);
  // Copy pixel data to staging buffer
  void* dst;
  vkMapMemory(context->device, stagingBuffer.memory, 0, size, 0,
              &dst);
  memcpy(dst, data, static_cast<size_t>(size));
  vkUnmapMemory(context->device, stagingBuffer.memory);

  // Initialize texture
  texture.device = context->device;
  InitTexture(
      texture, width, height, format, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // Transition image layout and copy buffer to image
  VkCommandBuffer commandBuffer = cmdUtils->beginSingleTimeCommands();
  // Transition to transfer destination
  transitionImageLayout(texture, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        commandBuffer);
  // Copy buffer to image
  VkExtent3D copyExtent = {width, height, 1};
  copyBufferToImage(texture, stagingBuffer.buffer, commandBuffer, 0,
                    0, copyExtent);
  // Transition to shader read
  transitionImageLayout(texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        commandBuffer);
  // Update current layout after transition
  texture.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  cmdUtils->endSingleTimeCommands(commandBuffer);

  // Create image view
  texture.imageView = createImageView(texture.image, format);

  // Create default sampler
  TextureSampler samplerInfo{};
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU =
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;  // for fonts
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

  texture.sampler = createTextureSampler(samplerInfo);
  // descriptor
  texture.descriptor.imageLayout = texture.currentLayout;
  texture.descriptor.imageView = texture.imageView;
  texture.descriptor.sampler = texture.sampler;

  return texture;
}
// cube map
void TextureManager::InitCubemapTexture(
    Texture& texture, uint32_t width, uint32_t height,
    VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties, uint32_t mipLevels) {
  // Initialize texture properties
  texture.device = context->device;
  texture.extent = {width, height, 1};
  texture.format = format;
  texture.mipLevels = mipLevels;
  texture.layerCount = 6;  // 6 faces for cubemap
  texture.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  texture.usage = usage;
  texture.imageType = VK_IMAGE_TYPE_2D;

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 6;  // 6 layers for cubemap
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.flags =
      VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;  // Important for cubemap

  if (vkCreateImage(context->device, &imageInfo, nullptr,
                    &texture.image) != VK_SUCCESS) {
    throw std::runtime_error("failed to create cubemap image!");
  }

  // Allocate memory (same as regular texture)
  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(context->device, texture.image,
                               &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = bufferManager->findMemoryType(
      memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(context->device, &allocInfo, nullptr,
                       &texture.memory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate cubemap memory!");
  }

  vkBindImageMemory(context->device, texture.image, texture.memory,
                    0);
}

VkImageView TextureManager::createCubemapImageView(
    VkImage image, VkFormat format, uint32_t levelCount) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;  // Cube view type
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = levelCount;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 6;  // All 6 faces

  VkImageView imageView;
  if (vkCreateImageView(context->device, &viewInfo, nullptr,
                        &imageView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create cubemap image view!");
  }

  return imageView;
}
void TextureManager::transitionCubemapLayout(
    Texture& texture, VkImageLayout oldLayout,
    VkImageLayout newLayout, VkCommandBuffer commandBuffer) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = texture.image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = texture.mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 6;  // All 6 faces

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    throw std::invalid_argument(
        "unsupported cubemap layout transition!");
  }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage,
                       0, 0, nullptr, 0, nullptr, 1, &barrier);

  texture.currentLayout = newLayout;
}
void TextureManager::copyBufferToCubemapFace(
    Texture& texture, VkBuffer buffer, VkCommandBuffer commandBuffer,
    uint32_t faceIndex, VkDeviceSize bufferOffset,
    uint32_t miplevel) {
  VkBufferImageCopy region{};
  region.bufferOffset = bufferOffset;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = miplevel;
  region.imageSubresource.baseArrayLayer = faceIndex;  // Face index
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = texture.extent;

  vkCmdCopyBufferToImage(commandBuffer, buffer, texture.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &region);
}

TextureManager::Texture TextureManager::createCubemapFromFiles(
    const std::array<std::string, 6>& faceFilepaths,
    VkFormat format) {
  spdlog::info("Loading cubemap texture");
  // Face order: +X, -X, +Y, -Y, +Z, -Z
  // (right, left, top, bottom, front, back)

  int texWidth = 0, texHeight = 0;
  VkDeviceSize layerSize = 0;
  VkDeviceSize totalSize = 0;
  // Load all faces and verify they have the same dimensions
  std::array<stbi_uc*, 6> faceData;
  for (int i = 0; i < 6; ++i) {
    int width, height, channels;
    faceData[i] = stbi_load(faceFilepaths[i].c_str(), &width, &height,
                            &channels, STBI_rgb_alpha);

    if (!faceData[i]) {
      // Clean up previously loaded faces
      for (int j = 0; j < i; ++j) {
        stbi_image_free(faceData[j]);
      }
      throw std::runtime_error("Failed to load cubemap face: " +
                               faceFilepaths[i]);
    }

    if (i == 0) {
      texWidth = width;
      texHeight = height;
      layerSize = width * height * 4;
      totalSize = layerSize * 6;
    } else if (width != texWidth || height != texHeight) {
      // Clean up all loaded faces
      for (int j = 0; j <= i; ++j) {
        stbi_image_free(faceData[j]);
      }
      throw std::runtime_error(
          "Cubemap faces must have the same dimensions");
    }
    spdlog::info("Loaded cubemap face {}: {}x{}", i, width, height);
  }

  // Create staging buffer for all faces
  BufferManager::Buffer stagingBuffer =
      bufferManager->createStagingBuffer(totalSize);

  // Copy all face data to staging buffer
  void* data;
  vkMapMemory(context->device, stagingBuffer.memory, 0, totalSize, 0,
              &data);
  for (int i = 0; i < 6; ++i) {
    memcpy(static_cast<char*>(data) + (i * layerSize), faceData[i],
           layerSize);
    stbi_image_free(faceData[i]);
  }
  vkUnmapMemory(context->device, stagingBuffer.memory);

  // Create cubemap texture
  Texture texture;
  VkImageUsageFlags usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

  InitCubemapTexture(texture, texWidth, texHeight, format,
                     VK_IMAGE_TILING_OPTIMAL, usage,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // Transfer data to GPU
  VkCommandBuffer commandBuffer = cmdUtils->beginSingleTimeCommands();

  // Transition entire cubemap to transfer destination
  transitionCubemapLayout(texture, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          commandBuffer);

  // Copy each face from staging buffer
  for (uint32_t face = 0; face < 6; ++face) {
    copyBufferToCubemapFace(texture, stagingBuffer.buffer,
                            commandBuffer, face, face * layerSize);
  }

  // Transition to shader read
  transitionCubemapLayout(
      texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandBuffer);

  cmdUtils->endSingleTimeCommands(commandBuffer);

  // Create image view and sampler
  texture.imageView = createCubemapImageView(texture.image, format);

  // Cubemaps typically use clamp to edge addressing
  TextureSampler cubemapSampler = {
      VK_FILTER_LINEAR, VK_FILTER_LINEAR,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
  texture.sampler = createTextureSampler(cubemapSampler);

  return texture;
}

TextureManager::Texture TextureManager::createCubemapFromSingleFile(
    const std::string& filepath, VkFormat format) {
  spdlog::info("Loading cubemap from single file: {}", filepath);

  // bool isHDR = filepath.find(".hdr") != -1;

  int imgWidth, imgHeight, imgChannels;
  stbi_uc* pixels = stbi_load(filepath.c_str(), &imgWidth, &imgHeight,
                              &imgChannels, STBI_rgb_alpha);

  if (!pixels) {
    throw std::runtime_error("Failed to load cubemap image: " +
                             filepath);
  }

  // Detect layout: horizontal cross (4:3) or vertical strip (1:6)
  int faceSize = 0;
  bool isHorizontalCross = false;
  bool isVerticalStrip = false;
  bool isHorizontalStrip = false;

  if (imgWidth == imgHeight * 6) {
    // Horizontal strip layout (6:1 aspect ratio)
    isHorizontalStrip = true;
    faceSize = imgHeight;
  } else if (imgWidth * 3 == imgHeight * 4) {
    // Horizontal cross layout (4:3 aspect ratio)
    isHorizontalCross = true;
    faceSize = imgWidth / 4;
  } else if (imgHeight == imgWidth * 6) {
    // Vertical strip layout (1:6 aspect ratio)
    isVerticalStrip = true;
    faceSize = imgWidth;
  } else {
    stbi_image_free(pixels);
    throw std::runtime_error(
        "Unsupported cubemap layout. Expected 4:3 (cross) or 1:6 "
        "(strip) aspect ratio");
  }

  VkDeviceSize faceDataSize = faceSize * faceSize * 4;
  VkDeviceSize totalSize = faceDataSize * 6;

  // Create staging buffer
  BufferManager::Buffer stagingBuffer =
      bufferManager->createStagingBuffer(totalSize);

  // Extract faces based on layout
  void* data;
  vkMapMemory(context->device, stagingBuffer.memory, 0, totalSize, 0,
              &data);
  unsigned char* destData = static_cast<unsigned char*>(data);
  auto copyFace = [&](int faceIndex, int srcX, int srcY) {
    int testIdx = (srcY * imgWidth + srcX) * 4;
    spdlog::info(
        "Face {}: Reading from ({}, {}), first pixel RGBA: ({}, {}, "
        "{}, {})",
        faceIndex, srcX, srcY, pixels[testIdx], pixels[testIdx + 1],
        pixels[testIdx + 2], pixels[testIdx + 3]);
    for (int y = 0; y < faceSize; ++y) {
      for (int x = 0; x < faceSize; ++x) {
        int srcIdx = ((srcY + y) * imgWidth + (srcX + x)) * 4;
        int destIdx = (y * faceSize + x) * 4;
        memcpy(destData + faceIndex * faceDataSize + destIdx,
               pixels + srcIdx, 4);
      }
    }
  };

  if (isHorizontalCross) {
    // Horizontal cross layout:
    //     [+Y]
    // [-X][+Z][+X][-Z]
    //     [-Y]
    spdlog::info("isHorizontalCross");
    copyFace(0, faceSize * 2, faceSize);  // +X (right)
    copyFace(1, 0, faceSize);             // -X (left)
    copyFace(2, faceSize, 0);             // +Y (check if valid)
    copyFace(3, faceSize, faceSize * 2);  // -Y (bottom)
    copyFace(4, faceSize, faceSize);  // +Z (front - center of cross)
    copyFace(5, faceSize * 3, faceSize);  // -Z (back)

  } else if (isVerticalStrip) {
    // Vertical strip layout (top to bottom: +X, -X, +Y, -Y, +Z, -Z)
    for (int i = 0; i < 6; ++i) {
      copyFace(i, 0, i * faceSize);
    }
  } else if (isHorizontalStrip) {
    // Horizontal strip: +X, -X, +Y, -Y, +Z, -Z (left to right)
    for (int i = 0; i < 6; ++i) {
      copyFace(i, i * faceSize, 0);
    }
  }

  vkUnmapMemory(context->device, stagingBuffer.memory);
  stbi_image_free(pixels);

  // Create cubemap texture
  Texture texture;
  VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT;

  uint32_t mipLevels =
      static_cast<uint32_t>(floor(log2(faceSize)) + 1.0);
  InitCubemapTexture(texture, faceSize, faceSize, format,
                     VK_IMAGE_TILING_OPTIMAL, usage,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mipLevels);

  // Transfer and generate mipmaps
  VkCommandBuffer commandBuffer = cmdUtils->beginSingleTimeCommands();

  transitionCubemapLayout(texture, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          commandBuffer);

  // Copy base mip level for all faces
  for (uint32_t face = 0; face < 6; ++face) {
    copyBufferToCubemapFace(texture, stagingBuffer.buffer,
                            commandBuffer, face, face * faceDataSize);
  }

  // Generate mipmaps for cubemap
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.image = texture.image;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 6;
  barrier.subresourceRange.levelCount = 1;

  int32_t mipWidth = faceSize;
  int32_t mipHeight = faceSize;

  for (uint32_t i = 1; i < mipLevels; i++) {
    barrier.subresourceRange.baseMipLevel = i - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    // Blit for all 6 faces
    for (uint32_t face = 0; face < 6; ++face) {
      VkImageBlit blit{};
      blit.srcOffsets[0] = {0, 0, 0};
      blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
      blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.srcSubresource.mipLevel = i - 1;
      blit.srcSubresource.baseArrayLayer = face;
      blit.srcSubresource.layerCount = 1;
      blit.dstOffsets[0] = {0, 0, 0};
      blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1,
                            mipHeight > 1 ? mipHeight / 2 : 1, 1};
      blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.dstSubresource.mipLevel = i;
      blit.dstSubresource.baseArrayLayer = face;
      blit.dstSubresource.layerCount = 1;

      vkCmdBlitImage(commandBuffer, texture.image,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     texture.image,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                     VK_FILTER_LINEAR);
    }

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    if (mipWidth > 1) mipWidth /= 2;
    if (mipHeight > 1) mipHeight /= 2;
  }

  // Transition last mip level
  barrier.subresourceRange.baseMipLevel = mipLevels - 1;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);

  texture.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  cmdUtils->endSingleTimeCommands(commandBuffer);

  // Create image view and sampler
  texture.imageView =
      createCubemapImageView(texture.image, format, mipLevels);

  TextureSampler cubemapSampler = {
      VK_FILTER_LINEAR, VK_FILTER_LINEAR,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
  texture.sampler = createTextureSampler(
      cubemapSampler, static_cast<float>(mipLevels));

  return texture;
}

TextureManager::Texture TextureManager::createDefault() {
  Texture texture;
  // Create a small white texture to use as default
  const uint32_t width = 4;
  const uint32_t height = 4;
  VkDeviceSize imageSize = width * height * 4;

  // Create red pixel data
  std::vector<uint8_t> pixels(imageSize);
  for (uint32_t i = 0; i < width * height; ++i) {
    pixels[i * 4 + 0] = 255;  // R
    pixels[i * 4 + 1] = 0;    // G
    pixels[i * 4 + 2] = 0;    // B
    pixels[i * 4 + 3] = 255;  // A
  }

  // Create staging buffer
  BufferManager::Buffer stagingBuffer =
      bufferManager->createStagingBuffer(imageSize);

  // Copy pixel data to staging buffer
  void* data;
  vkMapMemory(context->device, stagingBuffer.memory, 0, imageSize, 0,
              &data);
  memcpy(data, pixels.data(), static_cast<size_t>(imageSize));
  vkUnmapMemory(context->device, stagingBuffer.memory);

  // Initialize texture
  texture.device = context->device;
  InitTexture(
      texture, width, height, VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // Transition image layout and copy buffer to image
  VkCommandBuffer commandBuffer = cmdUtils->beginSingleTimeCommands();

  // Transition to transfer destination
  transitionImageLayout(texture, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        commandBuffer);

  // Copy buffer to image
  VkExtent3D copyExtent = {width, height, 1};
  copyBufferToImage(texture, stagingBuffer.buffer, commandBuffer, 0,
                    0, copyExtent);

  // Transition to shader read
  transitionImageLayout(texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        commandBuffer);
  // Update current layout after transition
  texture.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  cmdUtils->endSingleTimeCommands(commandBuffer);

  // Create image view
  texture.imageView =
      createImageView(texture.image, VK_FORMAT_R8G8B8A8_UNORM);

  // Create default sampler
  TextureSampler samplerInfo{};
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

  texture.sampler = createTextureSampler(samplerInfo);
  // descriptor
  texture.descriptor.imageLayout = texture.currentLayout;
  texture.descriptor.imageView = texture.imageView;
  texture.descriptor.sampler = texture.sampler;

  return texture;
}
void TextureManager::destroyTexture(Texture& texture) {
  texture = Texture();
}
