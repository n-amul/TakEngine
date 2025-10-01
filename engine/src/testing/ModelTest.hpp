// ModelTest.hpp
#pragma once

#include "renderer/ModelManager.hpp"
#include "renderer/VulkanBase.hpp"

class TAK_API ModelTest : public VulkanBase {
 public:
  ModelTest();

 protected:
  // Implement pure virtual methods from VulkanBase
  void createPipeline() override;
  void loadResources() override;
  void recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
  void cleanupResources() override;

  // Optional overrides
  void updateScene(float deltaTime) override;

 private:
  std::unique_ptr<ModelManager> modelManager;
  ModelManager::Model testModel;
  std::string modelFilePath;
};