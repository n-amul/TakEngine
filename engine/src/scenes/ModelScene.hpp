#pragma once
#include <memory>
#include <vector>

#include "renderer/VulkanBase.hpp"

class TAK_API ModelScene : public VulkanBase {
 public:
  ModelScene();
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
  ModelManager::Model scene;

  struct ShaderValuesParams {
    glm::vec4 lightDir;
    float exposure = 4.5f;
    float gamma = 2.2f;
    // For now remove: prefilteredCubeMipLevels, scaleIBLAmbient (IBL-specific)
  } shaderValuesParams;
  // Scene matrices (per-frame UBO)
  struct UBOMatrices {
    glm::mat4 projection;
    glm::mat4 model;
    glm::mat4 view;
    glm::vec3 camPos;
  } uboMatrices;

  struct UniformBufferSet {
    BufferManager::Buffer scene;   // UBOMatrices
    BufferManager::Buffer params;  // ShaderValuesParams
  };
  std::vector<UniformBufferSet> uniformBuffers;

  // Descriptor layouts
  struct DescriptorSetLayouts {
    VkDescriptorSetLayout scene{VK_NULL_HANDLE};           // matrices + params
    VkDescriptorSetLayout material{VK_NULL_HANDLE};        // per-material textures
    VkDescriptorSetLayout materialBuffer{VK_NULL_HANDLE};  // SSBO with all material data
    VkDescriptorSetLayout meshDataBuffer{VK_NULL_HANDLE};  // SSBO with all mesh data
  } descriptorSetLayouts;

  // Descriptor sets - one per frame in flight
  std::vector<VkDescriptorSet> descriptorSetsScene;     // One per frame
  VkDescriptorSet descriptorSetMaterials;               // Single, shared (SSBO)
  std::vector<VkDescriptorSet> descriptorSetsMeshData;  // One per frame

  VkDescriptorPool descriptorPool;

  // Material SSBO (GPU buffer with all material properties)
  struct ShaderMaterial {
    glm::vec4 baseColorFactor;
    glm::vec4 emissiveFactor;
    float workflow;
    int colorTextureSet;
    int physicalDescriptorTextureSet;
    int normalTextureSet;
    int occlusionTextureSet;
    int emissiveTextureSet;
    float metallicFactor;
    float roughnessFactor;
    float alphaMask;
    float alphaMaskCutoff;
  };
  BufferManager::Buffer shaderMaterialBuffer;
  // Mesh data SSBO (per-mesh transforms, skinning)
  struct ShaderMeshData {
    glm::mat4 matrix;
    glm::mat4 jointMatrix[MAX_NUM_JOINTS];
    uint32_t jointcount;
  };
  std::vector<BufferManager::Buffer> shaderMeshDataBuffers;  // One per frame

  // Push constants (per-draw call)
  struct MeshPushConstantBlock {
    int32_t meshIndex;
    int32_t materialIndex;
  };

  // Pipeline
  VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
  std::unordered_map<std::string, VkPipeline> pipelines;  // pbr, pbr_double_sided, pbr_alpha_blending
  VkPipeline boundPipeline{VK_NULL_HANDLE};               // Track current bound pipeline

  // Set 0: Scene
  // binding 0: UBO matrices (projection, view, model, camPos)
  // binding 1: UBO params (lightDir, exposure, gamma)
  // No bindings 2,3,4 (those were irradiance, prefiltered, BRDF LUT)

  // Set 1: Material textures (per material instance)
  // binding 0-4: Combined image samplers

  // Set 2: Material buffer (single SSBO, all materials)
  // binding 0: Storage buffer

  // Set 3: Mesh data buffer (per frame)
  // binding 0: Storage buffer

  void createDescriptorSetLayouts();
  void createUniformBuffers();
  void createMaterialBuffer();
  void createMeshDataBuffers();
  void createDescriptorSets();
  void createSkyboxDescriptorSets();
  void createPipelineVariant(VkShaderModule vertModule, VkShaderModule fragModule, const std::string& name, VkCullModeFlags cullMode, bool alphaBlending);
  void drawNode(VkCommandBuffer commandBuffer, tak::Node* node);
};