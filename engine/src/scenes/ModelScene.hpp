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

  struct Textures {
    TextureManager::Texture environmentCube;  // HDR environment map
    TextureManager::Texture empty;            // Fallback texture
    TextureManager::Texture lutBrdf;          // BRDF lookup table
    TextureManager::Texture irradianceCube;   // Diffuse irradiance
    TextureManager::Texture prefilteredCube;  // Specular prefiltered map
  };

  // Helper methods
  void createDescriptorSetLayout();
  void createDescriptorPool();
  void createDescriptorSets();
  void createUniformBuffers();
  void updateUniformBuffer(uint32_t frameIndex);
  void loadModel(const std::string& path, float scale = 1.0f);
  void drawNode(VkCommandBuffer commandBuffer, tak::Node* node, uint32_t frameIndex, const ModelManager::Model& model);
};