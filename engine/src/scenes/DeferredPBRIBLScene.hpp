#pragma once
#include <memory>
#include <unordered_set>
#include <vector>

#include "renderer/VulkanDeferredBase.hpp"
// First goal: draw g-buffer
class TAK_API DeferredPBRIBLScene : public VulkanDeferredBase {
 public:
  DeferredPBRIBLScene() = default;
  ~DeferredPBRIBLScene() = default;

 protected:
  void createGeometryPipeline();
  void createLightingPipeline();

  void loadResources();
  void recordGeometryCommands(VkCommandBuffer cmdBuffer);
  void recordLightingCommands(VkCommandBuffer cmdBuffer);
  void cleanupResource();

  void updateScene(float deltaTime);
};