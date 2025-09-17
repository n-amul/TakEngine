// ModelManager.cpp
#include "renderer/ModelManager.hpp"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <stdexcept>
#include <unordered_map>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE

ModelManager::Model ModelManager::loadGLTF(const std::string& filename) {
  spdlog::info("Loading glTF model: {}", filename);

  // Init gltf context
  tinygltf::TinyGLTF gltfContext;

  auto loadImageDataFunc = [](tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, int req_width, int req_height,
                              const unsigned char* bytes, int size, void* userData) -> bool {
    // KTX files will be handled by our own code
    if (image->uri.find_last_of(".") != std::string::npos) {
      if (image->uri.substr(image->uri.find_last_of(".") + 1) == "ktx") {
        return true;
      }
    }
    return tinygltf::LoadImageData(image, imageIndex, error, warning, req_width, req_height, bytes, size, userData);
  };

  gltfContext.SetImageLoader(loadImageDataFunc, nullptr);

  Model model;
  tinygltf::Model gltfModel;
  std::string err, warn;

  // Load file - check if binary or ASCII
  bool fileLoaded = false;
  std::string ext = filename.substr(filename.find_last_of(".") + 1);
  if (ext == "glb") {
    fileLoaded = gltfContext.LoadBinaryFromFile(&gltfModel, &err, &warn, filename);
  } else {
    // std::filesystem::path gltfPath(filename);
    // std::filesystem::current_path(gltfPath.parent_path());
    // spdlog::info("Set working directory to: {}", std::filesystem::current_path().string());
    fileLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &err, &warn, filename);
  }
  fileLoaded = false;
  if (!warn.empty()) {
    spdlog::warn("glTF loader warning: {}", warn);
  }
  if (!err.empty()) {
    spdlog::error("glTF loader error: {}", err);
  }
  if (!fileLoaded) {
    throw std::runtime_error("Failed to load glTF file: " + filename);
  }

  // Extract model name
  size_t lastSlash = filename.find_last_of("/\\");
  model.name = (lastSlash != std::string::npos) ? filename.substr(lastSlash + 1) : filename;

  // TOOD: add vulkan extenstions
  //  Load all components
  loadTextures(gltfModel, model, filename);  //<--
  loadMaterials(gltfModel, model);

  // Prepare vertex and index data
  std::vector<Vertex> vertexBuffer;
  std::vector<uint32_t> indexBuffer;
  loadMeshes(gltfModel, model, vertexBuffer, indexBuffer);

  // Create GPU buffers
  if (!vertexBuffer.empty()) {
    VkDeviceSize vertexBufferSize = sizeof(Vertex) * vertexBuffer.size();
    model.vertexBuffer = bufferManager->createGPULocalBuffer(vertexBuffer.data(), vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    spdlog::info("Created vertex buffer with {} vertices", vertexBuffer.size());
  }

  if (!indexBuffer.empty()) {
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * indexBuffer.size();
    model.indexBuffer = bufferManager->createGPULocalBuffer(indexBuffer.data(), indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    spdlog::info("Created index buffer with {} indices", indexBuffer.size());
  }

  // Load scene hierarchy
  if (!gltfModel.scenes.empty()) {
    int sceneIndex = gltfModel.defaultScene;
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(gltfModel.scenes.size())) {
      sceneIndex = 0;
    }

    const tinygltf::Scene& scene = gltfModel.scenes[sceneIndex];
    for (int nodeIndex : scene.nodes) {
      loadNode(gltfModel, gltfModel.nodes[nodeIndex], nullptr, nodeIndex, model);
    }
  } else {
    spdlog::warn("No scenes found in glTF model");
  }

  spdlog::info("Model loaded successfully: {} meshes, {} materials, {} textures", model.meshes.size(), model.materials.size(), model.textures.size());

  return model;
}

void ModelManager::loadTextures(const tinygltf::Model& gltfModel, Model& model, const std::string& filename) {
  model.textures.reserve(gltfModel.textures.size());

  for (const auto& gltfTexture : gltfModel.textures) {
    const tinygltf::Image& gltfImage = gltfModel.images[gltfTexture.source];

    TextureManager::Texture texture;

    // Check if embedded or external
    if (gltfImage.image.size() > 0) {
      // Embedded texture
      VkDeviceSize imageSize = gltfImage.image.size();

      // Create staging buffer
      BufferManager::Buffer stagingBuffer =
          bufferManager->createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

      bufferManager->updateBuffer(stagingBuffer, gltfImage.image.data(), imageSize, 0);

      // Determine format
      VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
      if (gltfImage.component == 3) {
        format = VK_FORMAT_R8G8B8_UNORM;  // RGB: many gpu dont support this so need to convert to rgba
      }

      // Create texture
      VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
      textureManager->InitTexture(texture, gltfImage.width, gltfImage.height, format, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

      // Transfer data
      VkCommandBuffer cmd = cmdUtils->beginSingleTimeCommands();
      textureManager->transitionImageLayout(texture, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd);
      textureManager->copyBufferToImage(texture, stagingBuffer.buffer, cmd);
      textureManager->transitionImageLayout(texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd);
      cmdUtils->endSingleTimeCommands(cmd);

      texture.imageView = textureManager->createImageView(texture.image, format);

      // Check sampler settings
      if (gltfTexture.sampler >= 0) {
        const tinygltf::Sampler& sampler = gltfModel.samplers[gltfTexture.sampler];

        VkFilter filter = VK_FILTER_LINEAR;
        if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST || sampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST) {
          filter = VK_FILTER_NEAREST;
        }

        VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        if (sampler.wrapS == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE) {
          addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        } else if (sampler.wrapS == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT) {
          addressMode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        }

        texture.sampler = textureManager->createTextureSampler(filter, addressMode);
      } else {
        texture.sampler = textureManager->createTextureSampler();
      }
    } else if (!gltfImage.uri.empty()) {
      // External texture file
      std::string texturePath = gltfImage.uri;
      // Handle relative paths
      size_t lastSlash = filename.find_last_of("/\\");
      if (lastSlash != std::string::npos) {
        texturePath = filename.substr(0, lastSlash + 1) + gltfImage.uri;
      }

      texture = textureManager->createTextureFromFile(texturePath);
    }

    model.textures.push_back(std::move(texture));
  }

  spdlog::info("Loaded {} textures", model.textures.size());
}

void ModelManager::loadMaterials(const tinygltf::Model& gltfModel, Model& model) {
  model.materials.reserve(gltfModel.materials.size());

  for (const auto& gltfMaterial : gltfModel.materials) {
    Material material{};

    // Set default values (glTF 2.0 spec defaults)
    material.baseColorFactor = glm::vec4(1.0f);
    material.metallicFactor = 1.0f;
    material.roughnessFactor = 1.0f;
    material.normalScale = 1.0f;
    material.occlusionStrength = 1.0f;
    material.emissiveFactor = glm::vec3(0.0f);
    material.alphaCutoff = 0.5f;

    // Initialize texture indices to -1 (no texture)
    material.baseColorTextureIndex = -1;
    material.metallicRoughnessTextureIndex = -1;
    material.normalTextureIndex = -1;
    material.occlusionTextureIndex = -1;
    material.emissiveTextureIndex = -1;

    // PBR Metallic Roughness
    const auto& pbr = gltfMaterial.pbrMetallicRoughness;

    if (pbr.baseColorFactor.size() == 4) {
      material.baseColorFactor = glm::make_vec4(pbr.baseColorFactor.data());
    }
    material.metallicFactor = static_cast<float>(pbr.metallicFactor);
    material.roughnessFactor = static_cast<float>(pbr.roughnessFactor);

    // PBR Textures
    if (pbr.baseColorTexture.index >= 0) {
      material.baseColorTextureIndex = pbr.baseColorTexture.index;
    }

    if (pbr.metallicRoughnessTexture.index >= 0) {
      material.metallicRoughnessTextureIndex = pbr.metallicRoughnessTexture.index;
    }
    // Normal map
    if (gltfMaterial.normalTexture.index >= 0) {
      material.normalTextureIndex = gltfMaterial.normalTexture.index;
      material.normalScale = static_cast<float>(gltfMaterial.normalTexture.scale);
    }
    // Occlusion
    if (gltfMaterial.occlusionTexture.index >= 0) {
      material.occlusionTextureIndex = gltfMaterial.occlusionTexture.index;
      material.occlusionStrength = static_cast<float>(gltfMaterial.occlusionTexture.strength);
    }
    // Emissive
    if (gltfMaterial.emissiveTexture.index >= 0) {
      material.emissiveTextureIndex = gltfMaterial.emissiveTexture.index;
    }

    if (gltfMaterial.emissiveFactor.size() == 3) {
      material.emissiveFactor = glm::make_vec3(gltfMaterial.emissiveFactor.data());
    }
    // Alpha mode
    if (gltfMaterial.alphaMode == "MASK") {
      material.alphaMode = Material::ALPHA_MASK;
      material.alphaCutoff = static_cast<float>(gltfMaterial.alphaCutoff);
    } else if (gltfMaterial.alphaMode == "BLEND") {
      material.alphaMode = Material::ALPHA_BLEND;
    } else {
      material.alphaMode = Material::ALPHA_OPAQUE;  // Default
    }

    material.doubleSided = gltfMaterial.doubleSided;

    // Optional: Store material name for debugging
    // material.name = gltfMaterial.name;

    model.materials.push_back(material);
  }

  // Add default material if none exist
  if (model.materials.empty()) {
    Material defaultMaterial{};
    defaultMaterial.baseColorFactor = glm::vec4(1.0f);
    defaultMaterial.metallicFactor = 1.0f;
    defaultMaterial.roughnessFactor = 1.0f;
    model.materials.push_back(defaultMaterial);
  }

  spdlog::info("Loaded {} materials", model.materials.size());
}

void ModelManager::loadMeshes(const tinygltf::Model& gltfModel, Model& model, std::vector<Vertex>& vertexBuffer, std::vector<uint32_t>& indexBuffer) {
  uint32_t vertexOffset = 0;  // Running total of vertices
  uint32_t indexOffset = 0;   // Running total of indices

  for (const auto& gltfMesh : gltfModel.meshes) {
    Mesh mesh;
    mesh.name = gltfMesh.name;

    for (const auto& gltfPrimitive : gltfMesh.primitives) {
      Primitive primitive{};

      // Store where this primitive starts in the buffers
      primitive.firstIndex = indexOffset;
      primitive.firstVertex = vertexOffset;

      // Get the vertex and index data for this primitive
      std::vector<Vertex> primitiveVertices;
      std::vector<uint32_t> primitiveIndices;

      extractPrimitiveData(gltfModel, gltfPrimitive, primitiveVertices, primitiveIndices);

      primitive.vertexCount = static_cast<uint32_t>(primitiveVertices.size());
      primitive.indexCount = static_cast<uint32_t>(primitiveIndices.size());
      primitive.materialIndex = gltfPrimitive.material;

      // Calculate bounds
      primitive.minBounds = glm::vec3(FLT_MAX);
      primitive.maxBounds = glm::vec3(-FLT_MAX);
      for (const auto& vertex : primitiveVertices) {
        primitive.minBounds = glm::min(primitive.minBounds, vertex.pos);
        primitive.maxBounds = glm::max(primitive.maxBounds, vertex.pos);
      }

      // Adjust indices to account for vertex offset
      for (auto& index : primitiveIndices) {
        index += vertexOffset;
      }

      // Append to main buffers
      vertexBuffer.insert(vertexBuffer.end(), primitiveVertices.begin(), primitiveVertices.end());
      indexBuffer.insert(indexBuffer.end(), primitiveIndices.begin(), primitiveIndices.end());

      mesh.primitives.push_back(primitive);

      // Update offsets for next primitive
      vertexOffset += primitive.vertexCount;
      indexOffset += primitive.indexCount;
    }
    model.meshes.push_back(mesh);
  }
}
void ModelManager::loadNode(const tinygltf::Model& gltfModel, const tinygltf::Node& inputNode, Node* parent, uint32_t nodeIndex, Model& model) {
  // Create new node
  auto node = std::make_unique<Node>();
  node->parent = parent;
  node->name = inputNode.name;
  node->mesh = inputNode.mesh;  // Store mesh index directly

  // Process node transformation
  // glTF supports either a matrix or TRS (translation, rotation, scale) components
  if (inputNode.matrix.size() == 16) {
    // Use the provided matrix directly
    node->localMatrix = glm::make_mat4(inputNode.matrix.data());
  } else {
    // Build matrix from TRS components
    glm::mat4 translation = glm::mat4(1.0f);
    glm::mat4 rotation = glm::mat4(1.0f);
    glm::mat4 scale = glm::mat4(1.0f);

    // Translation
    if (inputNode.translation.size() == 3) {
      translation = glm::translate(glm::mat4(1.0f), glm::vec3(inputNode.translation[0], inputNode.translation[1], inputNode.translation[2]));
    }

    // Rotation (quaternion)
    if (inputNode.rotation.size() == 4) {
      // glTF uses XYZW quaternion format
      glm::quat q(static_cast<float>(inputNode.rotation[3]),   // w
                  static_cast<float>(inputNode.rotation[0]),   // x
                  static_cast<float>(inputNode.rotation[1]),   // y
                  static_cast<float>(inputNode.rotation[2]));  // z
      rotation = glm::mat4_cast(q);
    }

    // Scale
    if (inputNode.scale.size() == 3) {
      scale = glm::scale(glm::mat4(1.0f), glm::vec3(inputNode.scale[0], inputNode.scale[1], inputNode.scale[2]));
    }

    // Combine transformations: T * R * S
    node->localMatrix = translation * rotation * scale;
  }

  // Update model bounds based on mesh if present
  if (node->mesh >= 0 && node->mesh < static_cast<int>(model.meshes.size())) {
    const Mesh& mesh = model.meshes[node->mesh];
    glm::mat4 worldMatrix = node->getWorldMatrix();

    // Transform mesh bounds by world matrix and update model bounds
    for (const auto& primitive : mesh.primitives) {
      // Transform the 8 corners of the bounding box
      glm::vec3 corners[8] = {
          glm::vec3(primitive.minBounds.x, primitive.minBounds.y, primitive.minBounds.z), glm::vec3(primitive.maxBounds.x, primitive.minBounds.y, primitive.minBounds.z),
          glm::vec3(primitive.minBounds.x, primitive.maxBounds.y, primitive.minBounds.z), glm::vec3(primitive.maxBounds.x, primitive.maxBounds.y, primitive.minBounds.z),
          glm::vec3(primitive.minBounds.x, primitive.minBounds.y, primitive.maxBounds.z), glm::vec3(primitive.maxBounds.x, primitive.minBounds.y, primitive.maxBounds.z),
          glm::vec3(primitive.minBounds.x, primitive.maxBounds.y, primitive.maxBounds.z), glm::vec3(primitive.maxBounds.x, primitive.maxBounds.y, primitive.maxBounds.z)};

      for (const auto& corner : corners) {
        glm::vec4 transformedCorner = worldMatrix * glm::vec4(corner, 1.0f);
        glm::vec3 transformedPos = glm::vec3(transformedCorner) / transformedCorner.w;

        model.minBounds = glm::min(model.minBounds, transformedPos);
        model.maxBounds = glm::max(model.maxBounds, transformedPos);
      }
    }
  }

  // Process children recursively
  for (int childIndex : inputNode.children) {
    if (childIndex >= 0 && childIndex < static_cast<int>(gltfModel.nodes.size())) {
      loadNode(gltfModel, gltfModel.nodes[childIndex], node.get(), childIndex, model);
    } else {
      spdlog::warn("Invalid child node index: {} in node: {}", childIndex, inputNode.name);
    }
  }

  // Store node pointer before moving
  Node* nodePtr = node.get();

  // Add node to appropriate parent
  if (parent) {
    parent->children.push_back(std::move(node));
  } else {
    // Root node - add to model's nodes vector
    model.nodes.push_back(std::move(node));
  }

  // Log node information for debugging
  spdlog::debug("Loaded node: '{}' with {} children, mesh index: {}", inputNode.name.empty() ? "unnamed" : inputNode.name, inputNode.children.size(), inputNode.mesh);
}

void ModelManager::createMaterialDescriptorSets(Model& model, VkDescriptorPool pool, VkDescriptorSetLayout layout) {
  std::vector<VkDescriptorSetLayout> layouts(model.materials.size(), layout);
  std::vector<VkDescriptorSet> sets(model.materials.size());

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = pool;
  allocInfo.descriptorSetCount = static_cast<uint32_t>(model.materials.size());
  allocInfo.pSetLayouts = layouts.data();

  if (vkAllocateDescriptorSets(context->device, &allocInfo, sets.data()) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate material descriptor sets");
  }

  for (size_t i = 0; i < model.materials.size(); i++) {
    model.materials[i].descriptorSet = sets[i];

    // Update descriptor set with material textures
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorImageInfo> imageInfos;

    // Reserve space to prevent reallocation
    imageInfos.reserve(5);

    // Helper to add texture binding
    auto addTextureBinding = [&](int textureIndex, uint32_t binding) {
      if (textureIndex >= 0 && textureIndex < model.textures.size()) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = model.textures[textureIndex].imageView;
        imageInfo.sampler = model.textures[textureIndex].sampler;
        imageInfos.push_back(imageInfo);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = sets[i];
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfos.back();
        writes.push_back(write);
      }
    };

    // Add texture bindings (adjust binding numbers as needed)
    addTextureBinding(model.materials[i].baseColorTextureIndex, 0);
    addTextureBinding(model.materials[i].metallicRoughnessTextureIndex, 1);
    addTextureBinding(model.materials[i].normalTextureIndex, 2);
    addTextureBinding(model.materials[i].occlusionTextureIndex, 3);
    addTextureBinding(model.materials[i].emissiveTextureIndex, 4);

    if (!writes.empty()) {
      vkUpdateDescriptorSets(context->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
  }
}

void ModelManager::extractPrimitiveData(const tinygltf::Model& gltfModel, const tinygltf::Primitive& gltfPrimitive, std::vector<Vertex>& vertices,
                                        std::vector<uint32_t>& indices) {
  // Clear output vectors
  vertices.clear();
  indices.clear();

  // Helper lambda to get buffer data pointer
  auto getBufferData = [&](int accessorIdx) -> const uint8_t* {
    if (accessorIdx < 0) return nullptr;

    const tinygltf::Accessor& accessor = gltfModel.accessors[accessorIdx];
    const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

    return buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
  };

  // Extract indices
  if (gltfPrimitive.indices >= 0) {
    const tinygltf::Accessor& accessor = gltfModel.accessors[gltfPrimitive.indices];
    const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

    indices.reserve(accessor.count);

    const void* dataPtr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;

    switch (accessor.componentType) {
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        const uint16_t* buf = static_cast<const uint16_t*>(dataPtr);
        for (size_t i = 0; i < accessor.count; ++i) {
          indices.push_back(buf[i]);
        }
        break;
      }
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
        const uint32_t* buf = static_cast<const uint32_t*>(dataPtr);
        for (size_t i = 0; i < accessor.count; ++i) {
          indices.push_back(buf[i]);
        }
        break;
      }
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
        const uint8_t* buf = static_cast<const uint8_t*>(dataPtr);
        for (size_t i = 0; i < accessor.count; ++i) {
          indices.push_back(buf[i]);
        }
        break;
      }
      default:
        spdlog::warn("Unsupported index component type: {}", accessor.componentType);
        break;
    }
  }

  // Get vertex count from POSITION attribute
  size_t vertexCount = 0;
  auto posIt = gltfPrimitive.attributes.find("POSITION");
  if (posIt != gltfPrimitive.attributes.end()) {
    const tinygltf::Accessor& posAccessor = gltfModel.accessors[posIt->second];
    vertexCount = posAccessor.count;
    vertices.resize(vertexCount);
  } else {
    spdlog::error("Primitive missing POSITION attribute");
    return;
  }

  // Extract POSITION
  if (posIt != gltfPrimitive.attributes.end()) {
    const tinygltf::Accessor& accessor = gltfModel.accessors[posIt->second];
    const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

    const float* positions = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);

    size_t stride = accessor.ByteStride(bufferView) / sizeof(float);
    if (stride == 0) stride = 3;  // Default for VEC3

    for (size_t i = 0; i < vertexCount; ++i) {
      vertices[i].pos = glm::vec3(positions[i * stride], positions[i * stride + 1], positions[i * stride + 2]);
    }
  }

  // Extract NORMAL
  auto normalIt = gltfPrimitive.attributes.find("NORMAL");
  if (normalIt != gltfPrimitive.attributes.end()) {
    const tinygltf::Accessor& accessor = gltfModel.accessors[normalIt->second];
    const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

    const float* normals = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);

    size_t stride = accessor.ByteStride(bufferView) / sizeof(float);
    if (stride == 0) stride = 3;

    for (size_t i = 0; i < vertexCount; ++i) {
      vertices[i].normal = glm::vec3(normals[i * stride], normals[i * stride + 1], normals[i * stride + 2]);
    }
  } else {
    // Generate default normals if not present
    for (size_t i = 0; i < vertexCount; ++i) {
      vertices[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
    }
  }

  // Extract TEXCOORD_0
  auto uvIt = gltfPrimitive.attributes.find("TEXCOORD_0");
  if (uvIt != gltfPrimitive.attributes.end()) {
    const tinygltf::Accessor& accessor = gltfModel.accessors[uvIt->second];
    const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

    const float* uvs = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);

    size_t stride = accessor.ByteStride(bufferView) / sizeof(float);
    if (stride == 0) stride = 2;

    for (size_t i = 0; i < vertexCount; ++i) {
      vertices[i].uv = glm::vec2(uvs[i * stride], uvs[i * stride + 1]);
    }
  } else {
    // Default UVs
    for (size_t i = 0; i < vertexCount; ++i) {
      vertices[i].uv = glm::vec2(0.0f, 0.0f);
    }
  }

  // Extract TANGENT (if present)
  auto tangentIt = gltfPrimitive.attributes.find("TANGENT");
  if (tangentIt != gltfPrimitive.attributes.end()) {
    const tinygltf::Accessor& accessor = gltfModel.accessors[tangentIt->second];
    const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

    const float* tangents = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);

    size_t stride = accessor.ByteStride(bufferView) / sizeof(float);
    if (stride == 0) stride = 4;  // VEC4 for tangents

    for (size_t i = 0; i < vertexCount; ++i) {
      vertices[i].tangent = glm::vec4(tangents[i * stride], tangents[i * stride + 1], tangents[i * stride + 2],
                                      tangents[i * stride + 3]  // W component for handedness
      );
    }
  } else {
    // Generate tangents if needed (simplified - you might want to use a proper tangent generation algorithm)
    for (size_t i = 0; i < vertexCount; ++i) {
      vertices[i].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    }
  }

  // Extract COLOR_0 (if present)
  auto colorIt = gltfPrimitive.attributes.find("COLOR_0");
  if (colorIt != gltfPrimitive.attributes.end()) {
    const tinygltf::Accessor& accessor = gltfModel.accessors[colorIt->second];
    const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
      const float* colors = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);

      size_t stride = accessor.ByteStride(bufferView) / sizeof(float);
      if (stride == 0) {
        stride = (accessor.type == TINYGLTF_TYPE_VEC3) ? 3 : 4;
      }

      for (size_t i = 0; i < vertexCount; ++i) {
        if (accessor.type == TINYGLTF_TYPE_VEC3) {
          vertices[i].color = glm::vec4(colors[i * stride], colors[i * stride + 1], colors[i * stride + 2], 1.0f);
        } else {
          vertices[i].color = glm::vec4(colors[i * stride], colors[i * stride + 1], colors[i * stride + 2], colors[i * stride + 3]);
        }
      }
    } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
      const uint8_t* colors = reinterpret_cast<const uint8_t*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);

      size_t stride = accessor.ByteStride(bufferView);
      if (stride == 0) {
        stride = (accessor.type == TINYGLTF_TYPE_VEC3) ? 3 : 4;
      }

      for (size_t i = 0; i < vertexCount; ++i) {
        if (accessor.type == TINYGLTF_TYPE_VEC3) {
          vertices[i].color = glm::vec4(colors[i * stride] / 255.0f, colors[i * stride + 1] / 255.0f, colors[i * stride + 2] / 255.0f, 1.0f);
        } else {
          vertices[i].color = glm::vec4(colors[i * stride] / 255.0f, colors[i * stride + 1] / 255.0f, colors[i * stride + 2] / 255.0f, colors[i * stride + 3] / 255.0f);
        }
      }
    }
  } else {
    // Default white color
    for (size_t i = 0; i < vertexCount; ++i) {
      vertices[i].color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
  }

  // TODO: Handle TEXCOORD_1 if need multiple UV sets
  auto uv1It = gltfPrimitive.attributes.find("TEXCOORD_1");
  if (uv1It != gltfPrimitive.attributes.end()) {
    // Similar to TEXCOORD_0 but store in vertices[i].uv1
  }

  // Generate indices for non-indexed geometry
  if (gltfPrimitive.indices < 0) {
    indices.reserve(vertexCount);
    for (uint32_t i = 0; i < vertexCount; ++i) {
      indices.push_back(i);
    }
  }

  spdlog::debug("Extracted primitive: {} vertices, {} indices", vertices.size(), indices.size());
}