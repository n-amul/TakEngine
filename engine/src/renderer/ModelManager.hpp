#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <tiny_gltf.h>
#include <vulkan/vulkan.h>

#include <memory>
#include <string>
#include <vector>

#include "BufferManager.hpp"
#include "CommandBufferUtils.hpp"
#include "TextureManager.hpp"
#include "VulkanContext.hpp"
#include "defines.hpp"

// Forward declaration
namespace tinygltf {
class TinyGLTF;
class Model;
class Node;
struct Primitive;
}  // namespace tinygltf

class ModelManager {
 public:
  ModelManager(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<BufferManager> bufferMgr, std::shared_ptr<TextureManager> textureMgr,
               std::shared_ptr<CommandBufferUtils> cmdUtil)
      : context(ctx), bufferManager(bufferMgr), textureManager(textureMgr), cmdUtils(cmdUtil) {
    // gltfLoader = std::make_unique<tinygltf::TinyGLTF>();
  }
  ~ModelManager() {
    // Cleanup handled by RAII
  }
  // PBR Vertex structure for glTF models
  struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;  // xyz = tangent direction, w = handedness
    glm::vec4 color;

    // Optional: additional UV sets
    // glm::vec2 uv1;
    static VkVertexInputBindingDescription getBindingDescription() {
      VkVertexInputBindingDescription bindingDescription{};
      bindingDescription.binding = 0;
      bindingDescription.stride = sizeof(Vertex);
      bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
      return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 5> getAttributeDescriptions() {
      std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{};

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

      // UV
      attributeDescriptions[2].binding = 0;
      attributeDescriptions[2].location = 2;
      attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
      attributeDescriptions[2].offset = offsetof(Vertex, uv);

      // Tangent
      attributeDescriptions[3].binding = 0;
      attributeDescriptions[3].location = 3;
      attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
      attributeDescriptions[3].offset = offsetof(Vertex, tangent);

      // Color
      attributeDescriptions[4].binding = 0;
      attributeDescriptions[4].location = 4;
      attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
      attributeDescriptions[4].offset = offsetof(Vertex, color);

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
    enum AlphaMode { ALPHA_OPAQUE, ALPHA_MASK, ALPHA_BLEND };
    AlphaMode alphaMode = ALPHA_OPAQUE;
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
    // Hierarchy
    Node* parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;

    // Transform data
    glm::mat4 localMatrix = glm::mat4(1.0f);

    // References
    int32_t mesh = -1;  // Mesh index (-1 = no mesh)
    std::string name;   // Node name for debugging

    // Get world transformation matrix
    glm::mat4 getWorldMatrix() const {
      if (parent) {
        return parent->getWorldMatrix() * localMatrix;
      }
      return localMatrix;
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

  // Helper functions for loading
  void loadNode(const tinygltf::Model& gltfModel, const tinygltf::Node& inputNode, Node* parent, uint32_t nodeIndex, Model& model);
  void loadMaterials(const tinygltf::Model& gltfModel, Model& model);
  void loadTextures(const tinygltf::Model& gltfModel, Model& model, const std::string& filename);
  void loadMeshes(const tinygltf::Model& gltfModel, Model& model, std::vector<Vertex>& vertexBuffer, std::vector<uint32_t>& indexBuffer);

  // Attribute extraction helpers
  void extractPrimitiveData(const tinygltf::Model& gltfModel, const tinygltf::Primitive& gltfPrimitive, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

 public:
  // Main loading function
  Model loadGLTF(const std::string& filepath);

  // Create descriptor sets for materials
  void createMaterialDescriptorSets(Model& model, VkDescriptorPool pool, VkDescriptorSetLayout layout);

  // Update material UBO
};