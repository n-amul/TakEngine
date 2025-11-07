#pragma once
#include <ktx.h>
#include <tiny_gltf.h>

#include <array>
#include <vector>

#include "renderer/VulkanBase.hpp"

class TAK_API TriangleScene : public VulkanBase {
 public:
  TriangleScene();
  ~TriangleScene() override = default;

 protected:
  // Pure virtual methods
  void createPipeline() override;
  void loadResources() override;
  void recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
  void cleanupResources() override;

  // Optional virtual methods from VulkanBase
  void updateScene(float deltaTime) override;
  void onResize(int width, int height) override;

 private:
  // meet alignment
  struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
  };
  struct UniformBufferSkybox {
    glm::mat4 proj;
    glm::mat4 model;
  };
  struct UBOParamsSkybox {
    glm::vec4 _pad0;
    float exposure = 4.5f;
    float gamma = 2.2f;
  } uboParamsSkybox;
  // Scene-specific vertex structure
  struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
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
      // position attribute
      attributeDescriptions[0].binding = 0;
      attributeDescriptions[0].location = 0;
      attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
      attributeDescriptions[0].offset = offsetof(Vertex, pos);
      // color
      attributeDescriptions[1].binding = 0;
      attributeDescriptions[1].location = 1;
      attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
      attributeDescriptions[1].offset = offsetof(Vertex, color);
      // tex coord uv
      attributeDescriptions[2].binding = 0;
      attributeDescriptions[2].location = 2;
      attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
      attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

      return attributeDescriptions;
    }
  };
  // main scene descriptor sets
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorPool descriptorPool;
  std::vector<VkDescriptorSet> descriptorSets;

  // Triangle-specific pipeline objects
  VkPipeline graphicsPipeline = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

  // Triangle-specific buffer objects
  BufferManager::Buffer vertexBuffer;
  BufferManager::Buffer indexBuffer;
  std::vector<BufferManager::Buffer> uniformBuffers;
  std::vector<void*> uniformBuffersMapped;
  TextureManager::Texture rectTexture;
  // skybox
  ModelManager::Model skybox;
  VkPipeline skyboxPipeline = VK_NULL_HANDLE;
  VkPipelineLayout skyboxPipelineLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout skyboxDescriptorSetLayout = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> skyboxDescriptorSets;
  std::vector<BufferManager::Buffer> skyboxUniformBuffers;
  BufferManager::Buffer skyBoxParamBuffer;
  void createSkyboxPipeline();

  //+Z = up, +X = right, +Y = forward
  const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4};
  const std::vector<Vertex> vertices = {
      {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},  {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
      {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},    {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},

      {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}}, {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
      {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},   {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}};

  f32 rotationAngle = 0.0f;
  f32 totalTime = 0.0f;

  // resource
  void createVertexBuffer();
  void createIndexBuffer();
  void createDescriptorPool();
  void createDescriptorSetLayout();
  void createDescriptorSets();
  void createUniformBuffers();
  void updateUniformBuffer(f32 deltatime);
  void createTextures();
};