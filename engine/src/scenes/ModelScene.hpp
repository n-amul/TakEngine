#pragma once
#include <memory>
#include <unordered_set>
#include <vector>

#include "renderer/VulkanBase.hpp"
#include "renderer/ui.hpp"

// simple pbr

class TAK_API ModelScene : public VulkanBase {
 public:
  ModelScene() {};
  ~ModelScene() = default;

 protected:
  // Required VulkanBase overrides
  void loadResources() override;
  void createPipeline() override;
  void recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
  void updateScene(float deltaTime) override;
  void cleanupResources() override;
  void onResize(int width, int height) override {};

 private:
  // ============= Scene Data =============
  ModelManager::Model scene;
  // ============= Lighting =============
  struct LightSource {
    glm::vec3 color = glm::vec3(1.0f);
    glm::vec3 rotation = glm::vec3(75.0f, -40.0f, 0.0f);  // For directional light
  } lightSource;

  // ============= Uniform Data Structures =============
  struct alignas(16) ShaderValuesParams {
    glm::vec3 lightPos = glm::vec3(0.0f, -1.0f, 1.0f);
    float exposure = 1.0f;
    float gamma = 2.2f;
    glm::vec3 ambientLight = glm::vec3(0.01);
  } shaderValuesParams;

  // Scene matrices (per-frame UBO)
  struct alignas(16) UBOMatrices {
    glm::mat4 projection;
    glm::mat4 model;
    glm::mat4 view;
    glm::vec3 camPos;
  } uboMatrices;

  // ============= Buffers =============
  struct UniformBufferSet {
    BufferManager::Buffer scene;   // UBOMatrices
    BufferManager::Buffer params;  // ShaderValuesParams
  };
  std::vector<UniformBufferSet> uniformBuffers;  // One per frame

  // Material SSBO (GPU buffer with all material properties)
  // std430 align: multiple of biggest data
  struct alignas(16) ShaderMaterial {
    glm::vec4 baseColorFactor;
    glm::vec4 emissiveFactor;
    glm::vec4 diffuseFactor;
    glm::vec4 specularFactor;
    float workflow;
    int colorTextureSet;  // which uv coord to use
    int physicalDescriptorTextureSet;
    int normalTextureSet;
    int occlusionTextureSet;
    int emissiveTextureSet;
    float metallicFactor;
    float roughnessFactor;
    float alphaMask;
    float alphaMaskCutoff;
    float emissiveStrength;
  };
  BufferManager::Buffer shaderMaterialBuffer;
  // Mesh data SSBO (per-mesh transforms, skinning)
  struct alignas(16) ShaderMeshData {
    glm::mat4 matrix;
    glm::mat4 jointMatrix[MAX_NUM_JOINTS];
    uint32_t jointCount;
  };
  std::vector<BufferManager::Buffer> shaderMeshDataBuffers;  // One per frame
  // ============= Descriptors =============
  // Descriptor layouts
  struct DescriptorSetLayouts {
    VkDescriptorSetLayout scene{VK_NULL_HANDLE};           // matrices + params
    VkDescriptorSetLayout material{VK_NULL_HANDLE};        // per-material textures
    VkDescriptorSetLayout materialBuffer{VK_NULL_HANDLE};  // SSBO with all material data
    VkDescriptorSetLayout meshDataBuffer{VK_NULL_HANDLE};  // SSBO with all mesh data
  } descriptorSetLayouts;
  // Descriptor sets
  std::vector<VkDescriptorSet> descriptorSetsScene;     // One per frame
  VkDescriptorSet descriptorSetMaterials;               // Single, shared (SSBO)
  std::vector<VkDescriptorSet> descriptorSetsMeshData;  // One per frame

  VkDescriptorPool descriptorPool;

  // ============= Pipeline =============
  // Push constants (per-draw call)
  struct MeshPushConstantBlock {
    int32_t meshIndex;
    int32_t materialIndex;
  };
  // Pipeline
  VkPipelineCache pipelineCache;
  VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
  std::unordered_map<std::string, VkPipeline> pipelines;
  VkPipeline boundPipeline{VK_NULL_HANDLE};  // Track current bound pipeline

  TextureManager::Texture emptyTexture;  // White 1x1 texture

  std::unordered_set<tak::Node*> visitedNodes;

  // ============= Animation =============
  int32_t animationIndex = 0;
  float animationTimer = 0.0f;
  bool animate = true;

  // ============= defines =============
  enum PBRWorkflows { PBR_WORKFLOW_METALLIC_ROUGHNESS = 0, PBR_WORKFLOW_SPECULAR_GLOSSINESS = 1 };

  // ============= Private Methods =============
  void setupDescriptors();
  void createMaterialBuffer();
  void createMeshDataBuffer();
  void createModelPipeline(const std::string& prefix);
  void updateMeshDataBuffer(uint32_t index);
  void prepareUniformBuffers();
  void updateUniformData();
  void updateParams();

  void renderNode(VkCommandBuffer cmdBuffer, tak::Node* node, uint32_t cbIndex, tak::Material::AlphaMode alphaMode);
};