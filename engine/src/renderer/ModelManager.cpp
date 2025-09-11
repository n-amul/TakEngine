// ModelManager.cpp
#include "ModelManager.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <unordered_map>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION // We already have stb_image in TextureManager
#define TINYGLTF_NO_STB_IMAGE_WRITE

ModelManager::Model ModelManager::loadGLTF(const std::string& filepath) {
    spdlog::info("Loading glTF model: {}", filepath);

    Model model;
    tinygltf::Model gltfModel;
    std::string err, warn;

    bool ret = false;
    if (filepath.find(".gltf") != std::string::npos) {
        ret = gltfLoader.LoadASCIIFromFile(&gltfModel, &err, &warn, filepath);
    } else if (filepath.find(".glb") != std::string::npos) {
        ret = gltfLoader.LoadBinaryFromFile(&gltfModel, &err, &warn, filepath);
    } else {
        throw std::runtime_error("Unknown file format: " + filepath);
    }

    if (!warn.empty()) {
        spdlog::warn("glTF loader warning: {}", warn);
    }

    if (!err.empty()) {
        spdlog::error("glTF loader error: {}", err);
    }

    if (!ret) {
        throw std::runtime_error("Failed to load glTF file: " + filepath);
    }

    // Extract model name
    size_t lastSlash = filepath.find_last_of("/\\");
    model.name = filepath.substr(lastSlash + 1);

    // Load all components
    loadTextures(gltfModel, model);
    loadMaterials(gltfModel, model);

    // Prepare vertex and index data
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    loadMeshes(gltfModel, model, vertices, indices);

    // Create GPU buffers
    if (!vertices.empty()) {
        VkDeviceSize vertexBufferSize = sizeof(Vertex) * vertices.size();
        model.vertexBuffer =
            bufferManager->createGPULocalBuffer(vertices.data(), vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

        spdlog::info("Created vertex buffer with {} vertices", vertices.size());
    }

    if (!indices.empty()) {
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        model.indexBuffer =
            bufferManager->createGPULocalBuffer(indices.data(), indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

        spdlog::info("Created index buffer with {} indices", indices.size());
    }

    // Load scene hierarchy
    const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
    for (int nodeIndex : scene.nodes) {
        const tinygltf::Node& node = gltfModel.nodes[nodeIndex];
        auto newNode = std::make_unique<Node>();
        loadNode(gltfModel, node, nullptr, nodeIndex, model);
        model.nodes.push_back(std::move(newNode));
    }

    spdlog::info("Model loaded successfully: {} meshes, {} materials, {} textures", model.meshes.size(),
                 model.materials.size(), model.textures.size());

    return model;
}

void ModelManager::loadNode(const tinygltf::Model& gltfModel, const tinygltf::Node& inputNode, Node* parent,
                            uint32_t nodeIndex, Model& model) {
    auto node = std::make_unique<Node>();
    node->parent = parent;
    node->meshIndex = inputNode.mesh;

    // Load transformation
    if (inputNode.matrix.size() == 16) {
        // Use provided matrix
        node->matrix = glm::make_mat4x4(inputNode.matrix.data());
    } else {
        // Build from TRS
        if (inputNode.translation.size() == 3) {
            node->translation = glm::make_vec3(inputNode.translation.data());
        }
        if (inputNode.rotation.size() == 4) {
            // glTF quaternion is XYZW
            node->rotation =
                glm::quat(static_cast<float>(inputNode.rotation[3]), static_cast<float>(inputNode.rotation[0]),
                          static_cast<float>(inputNode.rotation[1]), static_cast<float>(inputNode.rotation[2]));
        }
        if (inputNode.scale.size() == 3) {
            node->scale = glm::make_vec3(inputNode.scale.data());
        }
    }

    // Process children
    for (int childIndex : inputNode.children) {
        loadNode(gltfModel, gltfModel.nodes[childIndex], node.get(), childIndex, model);
    }

    if (parent) {
        parent->children.push_back(std::move(node));
    } else {
        model.nodes.push_back(std::move(node));
    }
}

void ModelManager::loadTextures(const tinygltf::Model& gltfModel, Model& model) {
    model.textures.reserve(gltfModel.textures.size());

    for (const auto& gltfTexture : gltfModel.textures) {
        const tinygltf::Image& gltfImage = gltfModel.images[gltfTexture.source];

        TextureManager::Texture texture(context->device);

        // Check if embedded or external
        if (gltfImage.image.size() > 0) {
            // Embedded texture
            VkDeviceSize imageSize = gltfImage.image.size();

            // Create staging buffer
            BufferManager::Buffer stagingBuffer =
                bufferManager->createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            bufferManager->updateBuffer(stagingBuffer, gltfImage.image.data(), imageSize, 0);

            // Determine format
            VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
            if (gltfImage.component == 3) {
                format = VK_FORMAT_R8G8B8_UNORM; // RGB
            }

            // Create texture
            VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            textureManager->InitTexture(texture, gltfImage.width, gltfImage.height, format, VK_IMAGE_TILING_OPTIMAL,
                                        usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            // Transfer data
            VkCommandBuffer cmd = cmdUtils->beginSingleTimeCommands();
            textureManager->transitionImageLayout(texture, VK_IMAGE_LAYOUT_UNDEFINED,
                                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd);
            textureManager->copyBufferToImage(texture, stagingBuffer.buffer, cmd);
            textureManager->transitionImageLayout(texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd);
            cmdUtils->endSingleTimeCommands(cmd);

            texture.imageView = textureManager->createImageView(texture.image, format);

            // Check sampler settings
            if (gltfTexture.sampler >= 0) {
                const tinygltf::Sampler& sampler = gltfModel.samplers[gltfTexture.sampler];

                VkFilter filter = VK_FILTER_LINEAR;
                if (sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST ||
                    sampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST) {
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
            size_t lastSlash = filepath.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                texturePath = filepath.substr(0, lastSlash + 1) + gltfImage.uri;
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

        // PBR Metallic Roughness
        if (gltfMaterial.values.find("baseColorFactor") != gltfMaterial.values.end()) {
            const auto& factor = gltfMaterial.values.at("baseColorFactor").ColorFactor();
            material.baseColorFactor = glm::make_vec4(factor.data());
        }

        if (gltfMaterial.values.find("metallicFactor") != gltfMaterial.values.end()) {
            material.metallicFactor = static_cast<float>(gltfMaterial.values.at("metallicFactor").Factor());
        }

        if (gltfMaterial.values.find("roughnessFactor") != gltfMaterial.values.end()) {
            material.roughnessFactor = static_cast<float>(gltfMaterial.values.at("roughnessFactor").Factor());
        }

        // Textures
        if (gltfMaterial.values.find("baseColorTexture") != gltfMaterial.values.end()) {
            material.baseColorTextureIndex = gltfMaterial.values.at("baseColorTexture").TextureIndex();
        }

        if (gltfMaterial.values.find("metallicRoughnessTexture") != gltfMaterial.values.end()) {
            material.metallicRoughnessTextureIndex = gltfMaterial.values.at("metallicRoughnessTexture").TextureIndex();
        }

        // Normal map
        if (gltfMaterial.additionalValues.find("normalTexture") != gltfMaterial.additionalValues.end()) {
            material.normalTextureIndex = gltfMaterial.additionalValues.at("normalTexture").TextureIndex();
            if (gltfMaterial.additionalValues.at("normalTexture").has_number_value) {
                material.normalScale =
                    static_cast<float>(gltfMaterial.additionalValues.at("normalTexture").number_value);
            }
        }

        // Occlusion
        if (gltfMaterial.additionalValues.find("occlusionTexture") != gltfMaterial.additionalValues.end()) {
            material.occlusionTextureIndex = gltfMaterial.additionalValues.at("occlusionTexture").TextureIndex();
            if (gltfMaterial.additionalValues.at("occlusionTexture").has_number_value) {
                material.occlusionStrength =
                    static_cast<float>(gltfMaterial.additionalValues.at("occlusionTexture").number_value);
            }
        }

        // Emissive
        if (gltfMaterial.additionalValues.find("emissiveTexture") != gltfMaterial.additionalValues.end()) {
            material.emissiveTextureIndex = gltfMaterial.additionalValues.at("emissiveTexture").TextureIndex();
        }

        if (gltfMaterial.additionalValues.find("emissiveFactor") != gltfMaterial.additionalValues.end()) {
            const auto& factor = gltfMaterial.additionalValues.at("emissiveFactor").ColorFactor();
            material.emissiveFactor = glm::vec3(factor[0], factor[1], factor[2]);
        }

        // Alpha mode
        if (gltfMaterial.alphaMode == "MASK") {
            material.alphaMode = Material::MASK;
            material.alphaCutoff = static_cast<float>(gltfMaterial.alphaCutoff);
        } else if (gltfMaterial.alphaMode == "BLEND") {
            material.alphaMode = Material::BLEND;
        }

        material.doubleSided = gltfMaterial.doubleSided;

        model.materials.push_back(material);
    }

    // Add default material if none exist
    if (model.materials.empty()) {
        model.materials.push_back(Material{});
    }

    spdlog::info("Loaded {} materials", model.materials.size());
}

void ModelManager::loadMeshes(const tinygltf::Model& gltfModel, Model& model, std::vector<Vertex>& vertexBuffer,
                              std::vector<uint32_t>& indexBuffer) {
    uint32_t vertexStart = 0;
    uint32_t indexStart = 0;

    for (const auto& gltfMesh : gltfModel.meshes) {
        Mesh mesh;
        mesh.name = gltfMesh.name;

        for (const auto& gltfPrimitive : gltfMesh.primitives) {
            Primitive primitive{};
            primitive.firstVertex = vertexStart;
            primitive.firstIndex = indexStart;

            // Extract vertex data
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;

            extractVertexData(gltfModel, gltfPrimitive, vertexBuffer, indexBuffer, vertexStart, indexStart);

            // Calculate counts
            vertexCount = static_cast<uint32_t>(vertexBuffer.size()) - vertexStart;
            indexCount = static_cast<uint32_t>(indexBuffer.size()) - indexStart;

            primitive.vertexCount = vertexCount;
            primitive.indexCount = indexCount;
            primitive.materialIndex = gltfPrimitive.material;

            // Calculate bounds
            primitive.minBounds = glm::vec3(FLT_MAX);
            primitive.maxBounds = glm::vec3(-FLT_MAX);

            for (uint32_t i = vertexStart; i < vertexStart + vertexCount; i++) {
                primitive.minBounds = glm::min(primitive.minBounds, vertexBuffer[i].pos);
                primitive.maxBounds = glm::max(primitive.maxBounds, vertexBuffer[i].pos);
            }

            mesh.primitives.push_back(primitive);

            vertexStart += vertexCount;
            indexStart += indexCount;
        }

        model.meshes.push_back(mesh);
    }
}

void ModelManager::extractVertexData(const tinygltf::Model& gltfModel, const tinygltf::Primitive& primitive,
                                     std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                                     uint32_t vertexStart, uint32_t indexStart) {
    // Get accessor helpers
    auto getAccessorData = [&](int accessorIndex) -> const float* {
        const tinygltf::Accessor& accessor = gltfModel.accessors[accessorIndex];
        const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];
        return reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);
    };

    // Positions (required)
    const float* positionBuffer = nullptr;
    size_t vertexCount = 0;

    if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
        const tinygltf::Accessor& accessor = gltfModel.accessors[primitive.attributes.at("POSITION")];
        positionBuffer = getAccessorData(primitive.attributes.at("POSITION"));
        vertexCount = accessor.count;
    }

    // Other attributes
    const float* normalBuffer = nullptr;
    const float* texCoord0Buffer = nullptr;
    const float* texCoord1Buffer = nullptr;
    const float* tangentBuffer = nullptr;
    const float* colorBuffer = nullptr;

    if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
        normalBuffer = getAccessorData(primitive.attributes.at("NORMAL"));
    }

    if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
        texCoord0Buffer = getAccessorData(primitive.attributes.at("TEXCOORD_0"));
    }

    if (primitive.attributes.find("TEXCOORD_1") != primitive.attributes.end()) {
        texCoord1Buffer = getAccessorData(primitive.attributes.at("TEXCOORD_1"));
    }

    if (primitive.attributes.find("TANGENT") != primitive.attributes.end()) {
        tangentBuffer = getAccessorData(primitive.attributes.at("TANGENT"));
    }

    if (primitive.attributes.find("COLOR_0") != primitive.attributes.end()) {
        colorBuffer = getAccessorData(primitive.attributes.at("COLOR_0"));
    }

    // Build vertices
    for (size_t i = 0; i < vertexCount; i++) {
        Vertex vertex{};

        // Position
        vertex.pos = glm::vec3(positionBuffer[i * 3], positionBuffer[i * 3 + 1], positionBuffer[i * 3 + 2]);

        // Normal
        if (normalBuffer) {
            vertex.normal = glm::vec3(normalBuffer[i * 3], normalBuffer[i * 3 + 1], normalBuffer[i * 3 + 2]);
        } else {
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        // TexCoords
        if (texCoord0Buffer) {
            vertex.texCoord0 = glm::vec2(texCoord0Buffer[i * 2], texCoord0Buffer[i * 2 + 1]);
        }

        if (texCoord1Buffer) {
            vertex.texCoord1 = glm::vec2(texCoord1Buffer[i * 2], texCoord1Buffer[i * 2 + 1]);
        }

        // Tangent
        if (tangentBuffer) {
            vertex.tangent = glm::vec4(tangentBuffer[i * 4], tangentBuffer[i * 4 + 1], tangentBuffer[i * 4 + 2],
                                       tangentBuffer[i * 4 + 3]);
        } else {
            vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        }

        // Color
        if (colorBuffer) {
            vertex.color =
                glm::vec4(colorBuffer[i * 4], colorBuffer[i * 4 + 1], colorBuffer[i * 4 + 2], colorBuffer[i * 4 + 3]);
        } else {
            vertex.color = glm::vec4(1.0f);
        }

        vertices.push_back(vertex);
    }

    // Indices
    if (primitive.indices >= 0) {
        const tinygltf::Accessor& accessor = gltfModel.accessors[primitive.indices];
        const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

        const void* dataPtr = &buffer.data[bufferView.byteOffset + accessor.byteOffset];

        if (accessor.componentType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT) {
            const uint32_t* buf = static_cast<const uint32_t*>(dataPtr);
            for (size_t i = 0; i < accessor.count; i++) {
                indices.push_back(buf[i] + vertexStart);
            }
        } else if (accessor.componentType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT) {
            const uint16_t* buf = static_cast<const uint16_t*>(dataPtr);
            for (size_t i = 0; i < accessor.count; i++) {
                indices.push_back(buf[i] + vertexStart);
            }
        } else if (accessor.componentType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE) {
            const uint8_t* buf = static_cast<const uint8_t*>(dataPtr);
            for (size_t i = 0; i < accessor.count; i++) {
                indices.push_back(buf[i] + vertexStart);
            }
        }
    } else {
        // Generate indices for non-indexed geometry
        for (uint32_t i = 0; i < vertexCount; i++) {
            indices.push_back(vertexStart + i);
        }
    }
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

void ModelManager::updateMaterialBuffer(const Material& material, VkBuffer buffer) {
    // Structure matching shader expectations
    struct MaterialUBO {
        glm::vec4 baseColorFactor;
        glm::vec4 emissiveFactor;                   // xyz = factor, w = unused
        glm::vec4 metallicRoughnessNormalOcclusion; // x = metallic, y = roughness, z = normal scale, w = occlusion
        glm::ivec4 textureFlags;  // x = hasBaseColor, y = hasMetallicRoughness, z = hasNormal, w = hasOcclusion
        glm::ivec4 textureFlags2; // x = hasEmissive, y = alphaMode, z = doubleSided, w = alphaCutoff (as int)
    } ubo;

    ubo.baseColorFactor = material.baseColorFactor;
    ubo.emissiveFactor = glm::vec4(material.emissiveFactor, 0.0f);
    ubo.metallicRoughnessNormalOcclusion =
        glm::vec4(material.metallicFactor, material.roughnessFactor, material.normalScale, material.occlusionStrength);

    ubo.textureFlags =
        glm::ivec4(material.baseColorTextureIndex >= 0 ? 1 : 0, material.metallicRoughnessTextureIndex >= 0 ? 1 : 0,
                   material.normalTextureIndex >= 0 ? 1 : 0, material.occlusionTextureIndex >= 0 ? 1 : 0);

    // Continuation of ModelManager::updateMaterialBuffer
    ubo.textureFlags2 = glm::ivec4(material.emissiveTextureIndex >= 0 ? 1 : 0, static_cast<int>(material.alphaMode),
                                   material.doubleSided ? 1 : 0,
                                   *reinterpret_cast<const int*>(&material.alphaCutoff) // Float as int for shader
    );

    // Update buffer
    void* data;
    vkMapMemory(context->device, bufferManager->getBufferMemory(buffer), 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(context->device, bufferManager->getBufferMemory(buffer));
}