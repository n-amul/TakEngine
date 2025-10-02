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
  void onResize(int width, int height) override;

 private:
  // Pipeline resources
  VkPipeline graphicsPipeline = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

  // Models
  std::vector<ModelManager::Model> models;

  // Uniform buffers
  struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 lightPos;
    glm::vec3 viewPos;
  };
  std::vector<BufferManager::Buffer> uniformBuffers;
  std::vector<void*> uniformBuffersMapped;

  std::vector<std::vector<VkDescriptorSet>> descriptorSets;

  // Push constants for per-object transforms
  struct PushConstantData {
    glm::mat4 model;
    // glm::mat4 mvp;
    uint32_t materialIndex;
  } pushConstantData;

  // Scene state
  float totalTime = 0.0f;
  bool animationEnabled = true;
  uint32_t currentAnimationIndex = 0;

  // Helper methods
  void createDescriptorSetLayout();
  void createDescriptorPool();
  void createDescriptorSets();
  void createUniformBuffers();
  void updateUniformBuffer(uint32_t frameIndex);
  void loadModel(const std::string& path, float scale = 1.0f);
  void drawNode(VkCommandBuffer commandBuffer, tak::Node* node, uint32_t frameIndex, const ModelManager::Model& model);
};