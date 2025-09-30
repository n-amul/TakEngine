#pragma once

#include <memory>
#include <string>

#include "defines.hpp"
#include "renderer/ModelManager.hpp"
#include "renderer/VulkanBase.hpp"

class TAK_API ModelTest : public VulkanBase {
 public:
  ModelTest();
  ~ModelTest() override = default;

 protected:
  // Required virtual method implementations
  void createPipeline() override;
  void loadResources() override;
  void recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
  void cleanupResources() override;

 private:
  void testModelLoad();
  std::unique_ptr<ModelManager> modelManager;
  ModelManager::Model testModel;
  std::string modelFilePath;
  bool modelLoaded = false;
};