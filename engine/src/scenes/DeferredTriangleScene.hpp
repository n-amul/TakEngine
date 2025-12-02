// DeferredTriangleScene.hpp
#pragma once

#include "renderer/VulkanDeferredBase.hpp"

class DeferredTriangleScene : public VulkanDeferredBase {
 public:
  DeferredTriangleScene();
  ~DeferredTriangleScene() = default;

 protected:
  // Required pure virtual implementations
  void getDescriptorPoolSizes(std::vector<VkDescriptorPoolSize>& poolSizes, uint32_t& maxSets);
  void createGeometryPipeline() override;
  void createssaoPipeline() override;
  void loadResources() override;
  void recordGeometryCommands(VkCommandBuffer commandBuffer) override;
  void recordSSAOCommands(VkCommandBuffer commandBuffer, u32 imageIndex) override;
  void recordSSAOBlurCommands(VkCommandBuffer commandBuffer, u32 imageIndex) override;
  void cleanupResources() override;

  // Optional overrides
  void updateScene(float deltaTime) override;
  void onResize(int width, int height) override;

 private:
  // Scene geometry
  struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;

    static VkVertexInputBindingDescription getBindingDescription() {
      VkVertexInputBindingDescription bindingDescription{};
      bindingDescription.binding = 0;
      bindingDescription.stride = sizeof(Vertex);
      bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
      return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
      std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

      attributeDescriptions[0].binding = 0;
      attributeDescriptions[0].location = 0;
      attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
      attributeDescriptions[0].offset = offsetof(Vertex, pos);

      attributeDescriptions[1].binding = 0;
      attributeDescriptions[1].location = 1;
      attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
      attributeDescriptions[1].offset = offsetof(Vertex, normal);

      attributeDescriptions[2].binding = 0;
      attributeDescriptions[2].location = 2;
      attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
      attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

      return attributeDescriptions;
    }
  };

  // Test triangle with normals
  const std::vector<Vertex> vertices = {{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
                                        {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                                        {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                                        {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}};

  const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

  // Buffers
  BufferManager::Buffer vertexBuffer;
  BufferManager::Buffer indexBuffer;
  std::vector<BufferManager::Buffer> uniformBuffers;

  // Uniform data
  struct GeometryUBO {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 normalMatrix;
  };

  // Pipeline resources
  VkPipelineLayout geometryPipelineLayout;
  VkDescriptorSetLayout geometryDescriptorSetLayout;
  std::vector<VkDescriptorSet> geometryDescriptorSets;

  // SSAO pipeline resources
  VkPipeline ssaoPipeline;
  VkPipeline ssaoBlurPipeline;

  // Textures
  TextureManager::Texture albedoTexture;

  // UI
  UI* ui = nullptr;

  // ImGui texture IDs for G-Buffer visualization
  ImTextureID normalTexId;
  ImTextureID albedoTexId;
  ImTextureID materialTexId;
  ImTextureID depthTexId;
  ImTextureID ssaoTexId;
  ImTextureID ssaoBlurredTexId;

  // Debug info
  float fps = 0.0f;
  float fpsTimer = 0.0f;
  uint32_t frameCounter = 0;
  bool showGBuffer = true;
  bool showSSAO = true;

  void createVertexBuffer();
  void createIndexBuffer();
  void createUniformBuffers();
  void createDescriptorSets();
  void updateUniformBuffer(uint32_t currentImage);
  void updateSSAOParams(uint32_t currentImage);
  void updateOverlay(float deltaTime);
};