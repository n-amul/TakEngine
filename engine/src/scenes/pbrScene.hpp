#pragma once

#include <array>
#include <memory>
#include <vector>

#include "../renderer/ModelManager.hpp"
#include "../renderer/VulkanBase.hpp"

class TAK_API PBRScene : public VulkanBase {
 public:
  PBRScene();
  ~PBRScene() override = default;

 protected:
  // Pure virtual methods from VulkanBase
  void createPipeline() override;
  void loadResources() override;
  void recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
  void cleanupResources() override;

  // Optional virtual methods
  void updateScene(float deltaTime) override;
  void onResize(int width, int height) override;

 private:
  // Uniform buffer objects
  struct GlobalUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 cameraPos;
    float time;
  };

  struct ModelUBO {
    glm::mat4 model;
    glm::mat4 normalMatrix;
  };

  struct LightUBO {
    glm::vec4 position[4];  // w = 0 for directional, 1 for point
    glm::vec4 color[4];     // w = intensity
    glm::vec4 params;       // x = light count, yzw = reserved
  };

  struct MaterialUBO {
    glm::vec4 baseColorFactor;
    glm::vec3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    glm::ivec4 textureFlags;  // x=hasBaseColor, y=hasMetallicRoughness, z=hasNormal, w=hasOcclusion
  };

  // Pipeline objects
  VkPipeline graphicsPipeline = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

  // Descriptor set layouts
  VkDescriptorSetLayout globalDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout modelDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout materialDescriptorSetLayout = VK_NULL_HANDLE;

  // Descriptor pools and sets
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> globalDescriptorSets;
  std::vector<VkDescriptorSet> modelDescriptorSets;
  std::vector<std::vector<VkDescriptorSet>> materialDescriptorSets;  // Per model, per material

  // Uniform buffers
  std::vector<BufferManager::Buffer> globalUniformBuffers;
  std::vector<void*> globalUniformBuffersMapped;

  std::vector<BufferManager::Buffer> modelUniformBuffers;
  std::vector<void*> modelUniformBuffersMapped;

  std::vector<BufferManager::Buffer> lightUniformBuffers;
  std::vector<void*> lightUniformBuffersMapped;

  std::vector<std::vector<BufferManager::Buffer>> materialUniformBuffers;  // Per model
  std::vector<std::vector<void*>> materialUniformBuffersMapped;

  // Model management
  std::shared_ptr<ModelManager> modelManager;
  std::vector<ModelManager::Model> models;

  // Default textures for missing PBR textures
  TextureManager::Texture defaultWhiteTexture;
  TextureManager::Texture defaultNormalTexture;
  TextureManager::Texture defaultBlackTexture;

  // Camera and scene parameters
  struct Camera {
    glm::vec3 position = glm::vec3(5.0f, 5.0f, 5.0f);
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float rotationSpeed = 1.0f;
    float zoomSpeed = 1.0f;
  } camera;

  // Scene animation
  float totalTime = 0.0f;
  float modelRotation = 0.0f;
  bool autoRotate = true;

  // Lights
  struct Light {
    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    bool isDirectional;
  };
  std::vector<Light> lights;

  // Private methods
  void createDescriptorSetLayouts();
  void createDescriptorPool();
  void createDescriptorSets();
  void createUniformBuffers();
  void createDefaultTextures();
  void updateGlobalUniformBuffer(uint32_t currentImage);
  void updateModelUniformBuffer(uint32_t currentImage, const glm::mat4& modelMatrix);
  void updateLightUniformBuffer(uint32_t currentImage);
  void updateMaterialUniformBuffer(uint32_t currentImage, uint32_t modelIndex, uint32_t materialIndex);
  void loadModels();
  void setupLights();
  void drawNode(VkCommandBuffer commandBuffer, const ModelManager::Node* node, uint32_t modelIndex, const glm::mat4& parentMatrix);
  VkShaderModule createShaderModule(const std::vector<char>& code);
};