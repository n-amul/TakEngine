#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <vulkan/vulkan.h>

#include <memory>
#include <string>
#include <tiny_gltf.h>
#include <vector>

#include "BufferManager.hpp"
#include "CommandBufferUtils.hpp"
#include "TextureManager.hpp"
#include "VulkanContext.hpp"
#include "defines.hpp"
#define TINYGLTF_NO_STB_IMAGE_WRITE

// Forward declaration
namespace tinygltf {
class TinyGLTF;
class Model;
class Node;
struct Primitive;
} // namespace tinygltf

class ModelManager {
  public:
    ModelManager(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<BufferManager> bufferMgr,
                 std::shared_ptr<TextureManager> textureMgr, std::shared_ptr<CommandBufferUtils> cmdUtil)
        : context(ctx), bufferManager(bufferMgr), textureManager(textureMgr), cmdUtils(cmdUtil) {
        gltfLoader = std::make_unique<tinygltf::TinyGLTF>();
    }
    ~ModelManager() {
        // Cleanup handled by smart pointers and RAII
    }
    // PBR Vertex structure for glTF models
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec2 texCoord0;
        glm::vec2 texCoord1;
        glm::vec4 tangent;
        glm::vec4 color;

        static VkVertexInputBindingDescription getBindingDescription() {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }
        static std::array<VkVertexInputAttributeDescription, 6> getAttributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 6> attributeDescriptions{};

            // Position
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            // Normal
            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, normal);

            // TexCoord0
            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, texCoord0);

            // TexCoord1
            attributeDescriptions[3].binding = 0;
            attributeDescriptions[3].location = 3;
            attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[3].offset = offsetof(Vertex, texCoord1);

            // Tangent
            attributeDescriptions[4].binding = 0;
            attributeDescriptions[4].location = 4;
            attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[4].offset = offsetof(Vertex, tangent);

            // Color
            attributeDescriptions[5].binding = 0;
            attributeDescriptions[5].location = 5;
            attributeDescriptions[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[5].offset = offsetof(Vertex, color);

            return attributeDescriptions;
        }
    };

    // Material data matching glTF PBR spec
    struct Material {
        // PBR parameters
        glm::vec4 baseColorFactor = glm::vec4(1.0f);
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        float normalScale = 1.0f;
        float occlusionStrength = 1.0f;
        glm::vec3 emissiveFactor = glm::vec3(0.0f);

        // Alpha mode
        enum AlphaMode { OPAQUE, MASK, BLEND };
        AlphaMode alphaMode = OPAQUE;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;

        // Texture indices (-1 means no texture)
        int baseColorTextureIndex = -1;
        int metallicRoughnessTextureIndex = -1;
        int normalTextureIndex = -1;
        int occlusionTextureIndex = -1;
        int emissiveTextureIndex = -1;

        // Vulkan descriptor set for this material
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    // Mesh primitive (submesh)
    struct Primitive {
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t firstVertex;
        uint32_t vertexCount;
        int materialIndex;

        // Bounding box for frustum culling
        glm::vec3 minBounds;
        glm::vec3 maxBounds;
    };

    // Mesh containing multiple primitives
    struct Mesh {
        std::vector<Primitive> primitives;
        std::string name;
    };

    // Node for scene graph
    struct Node {
        Node* parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;

        glm::mat4 matrix = glm::mat4(1.0f);
        glm::vec3 translation = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale = glm::vec3(1.0f);

        int meshIndex = -1; // -1 means no mesh

        glm::mat4 getLocalMatrix() const {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), translation);
            m = m * glm::mat4_cast(rotation);
            m = glm::scale(m, scale);
            return matrix * m; // Combine with stored matrix if any
        }
        glm::mat4 getWorldMatrix() const {
            glm::mat4 m = getLocalMatrix();
            if (parent) {
                m = parent->getWorldMatrix() * m;
            }
            return m;
        }
    };

    // Complete model
    struct Model {
        std::vector<Mesh> meshes;
        std::vector<Material> materials;
        std::vector<TextureManager::Texture> textures;
        std::vector<std::unique_ptr<Node>> nodes;

        // Single vertex/index buffer for entire model
        BufferManager::Buffer vertexBuffer;
        BufferManager::Buffer indexBuffer;

        // Model info
        std::string name;
        glm::vec3 minBounds = glm::vec3(FLT_MAX);
        glm::vec3 maxBounds = glm::vec3(-FLT_MAX);

        void cleanup(VkDevice device) {
            // Buffers will clean themselves up through RAII
            vertexBuffer = BufferManager::Buffer();
            indexBuffer = BufferManager::Buffer();

            // Textures also use RAII
            textures.clear();

            // Clear other data
            meshes.clear();
            materials.clear();
            nodes.clear();
        }
    };

  private:
    std::shared_ptr<VulkanContext> context;
    std::shared_ptr<BufferManager> bufferManager;
    std::shared_ptr<TextureManager> textureManager;
    std::shared_ptr<CommandBufferUtils> cmdUtils;

    // tinygltf loader
    std::unique_ptr<tinygltf::TinyGLTF> gltfLoader;
    std::string filepath; // Store for relative path resolution

    // Helper functions for loading
    void loadNode(const tinygltf::Model& gltfModel, const tinygltf::Node& inputNode, Node* parent, uint32_t nodeIndex,
                  Model& model);
    void loadMaterials(const tinygltf::Model& gltfModel, Model& model);
    void loadTextures(const tinygltf::Model& gltfModel, Model& model);
    void loadMeshes(const tinygltf::Model& gltfModel, Model& model, std::vector<Vertex>& vertexBuffer,
                    std::vector<uint32_t>& indexBuffer);

    // Attribute extraction helpers
    void extractVertexData(const tinygltf::Model& gltfModel, const tinygltf::Primitive& primitive,
                           std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, uint32_t vertexStart,
                           uint32_t indexStart);

  public:
    ModelManager(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<BufferManager> bufferMgr,
                 std::shared_ptr<TextureManager> textureMgr, std::shared_ptr<CommandBufferUtils> cmdUtil);
    ~ModelManager();

    // Main loading function
    Model loadGLTF(const std::string& filepath);

    // Create descriptor sets for materials
    void createMaterialDescriptorSets(Model& model, VkDescriptorPool pool, VkDescriptorSetLayout layout);

    // Update material UBO
    void updateMaterialBuffer(const Material& material, VkBuffer buffer);
};