#pragma once
#include <memory>
#include <unordered_set>
#include <vector>

#include "renderer/VulkanBase.hpp"

class TAK_API PBRIBLScene : VulkanBase {
 public:
  PBRIBLScene() {};
  ~PBRIBLScene() = default;

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
  struct Textures {
    TextureManager::Texture environmentCube;
    TextureManager::Texture empty;
    TextureManager::Texture lutBrdf;
    TextureManager::Texture irradianceCube;
    TextureManager::Texture prefilteredCube;
  } textures;
  ModelManager::Model scene;
  ModelManager::Model skybox;

  // ============= Lighting =============
  struct LightSource {
    glm::vec3 color = glm::vec3(1.0f);
    glm::vec3 rotation = glm::vec3(75.0f, -40.0f, 0.0f);  // For directional light
  } lightSource;

  // ============= Uniform Data Structures =============
  struct ShaderValuesParams {
    glm::vec4 lightDir;
    float exposure = 4.5f;
    float gamma = 2.2f;
    float prefilteredCubeMipLevels;
    float scaleIBLAmbient = 1.0f;
    float debugViewInputs = 0;
    float debugViewEquation = 0;
  } shaderValuesParams;

  // Scene matrices (per-frame UBO)
  struct alignas(16) UBOMatrices {
    glm::mat4 projection;
    glm::mat4 model;
    glm::mat4 view;
    glm::vec3 camPos;
    float _padding;
  } SceneUboMatrices, skyboxUnoMatrices;

  // ============= Buffers =============
  struct UniformBufferSet {
    BufferManager::Buffer scene;   // UBOMatrices
    BufferManager::Buffer params;  // ShaderValuesParams
    BufferManager::Buffer skybox;  // skybox
  };
  std::vector<UniformBufferSet> uniformBuffers;  // One per frame

  // Material SSBO (GPU buffer with all material properties)
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
    uint32_t jointcount{0};
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
  struct DescriptorSets {
    VkDescriptorSet scene;
    VkDescriptorSet skybox;
  };
  std::vector<DescriptorSets> descriptorSets;           // One per frame
  std::vector<VkDescriptorSet> descriptorSetsMeshData;  // One per frame
  VkDescriptorSet descriptorSetMaterials{VK_NULL_HANDLE};

  VkDescriptorPool descriptorPool;

  // ============= Pipeline =============
  // Push constants (per-draw call)
  struct MeshPushConstantBlock {
    int32_t meshIndex;
    int32_t materialIndex;
  };
  // Pipeline
  VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
  std::unordered_map<std::string, VkPipeline> pipelines;
  VkPipeline boundPipeline{VK_NULL_HANDLE};  // Track current bound pipeline
  TextureManager::Texture emptyTexture;      // White 1x1 texture
  bool displayBackground = true;

  // ============= Animation =============
  int32_t animationIndex = 0;
  float animationTimer = 0.0f;
  bool animate = false;

  // ============= defines =============
  enum PBRWorkflows { PBR_WORKFLOW_METALLIC_ROUGHNESS = 0, PBR_WORKFLOW_SPECULAR_GLOSSINESS = 1 };
  // List of glTF extensions supported by this application
  // Models with un-supported extensions may not work/look as expected
  const std::vector<std::string> supportedExtensions = {"KHR_texture_basisu", "KHR_materials_pbrSpecularGlossiness", "KHR_materials_unlit", "KHR_materials_emissive_strength"};

  // ============= Private Methods =============
  void setupDescriptors();
  void createMaterialBuffer();
  void createMeshDataBuffer();
  void createModelPipeline();
  void updateMeshDataBuffer(uint32_t index);
  void prepareUniformBuffers();
  void updateUniformData();
  void updateParams();

  void renderNode(VkCommandBuffer cmdBuffer, tak::Node* node, uint32_t cbIndex, tak::Material::AlphaMode alphaMode);

  // skybox
};