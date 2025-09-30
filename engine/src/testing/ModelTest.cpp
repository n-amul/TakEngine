#include "testing/ModelTest.hpp"

#include "modeltest.hpp"

ModelTest::ModelTest() {
  spdlog::info("ModelTest constructor called");
  modelFilePath = std::string(MODEL_DIR) + "/buster_drone/scene.gltf";
  spdlog::info("Model path: {}", modelFilePath);
  title = "Model Loader Test";
  name = "ModelTest";
}

void ModelTest::createPipeline() {}

void ModelTest::loadResources() { testModelLoad(); }

void ModelTest::recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) {}
void ModelTest::cleanupResources() { modelManager.reset(); }

void ModelTest::testModelLoad() {
  spdlog::info("=== Starting Model Loading Test ===");
  spdlog::info("loadResources called");

  // Create ModelManager with the shared utilities from VulkanBase
  modelManager = std::make_unique<ModelManager>(context, bufferManager, textureManager, cmdUtils);

  // Load the model
  spdlog::info("Loading model from: {}", modelFilePath);
  testModel = modelManager->createModelFromFile(modelFilePath, 1.0f);

  // Log basic model info (additional to what ModelManager logs internally)
  spdlog::info("=== Model Loading Complete ===");
  spdlog::info("Model file path: {}", testModel.filePath);
  spdlog::info("Number of nodes: {}", testModel.nodes.size());
  spdlog::info("Number of linear nodes: {}", testModel.linearNodes.size());
  spdlog::info("Number of materials: {}", testModel.materials.size());
  spdlog::info("Number of textures: {}", testModel.textures.size());
  spdlog::info("Number of animations: {}", testModel.animations.size());
  spdlog::info("Number of skins: {}", testModel.skins.size());

  // Log extensions used
  if (!testModel.extensions.empty()) {
    spdlog::info("Extensions used:");
    for (const auto& ext : testModel.extensions) {
      spdlog::info("  - {}", ext);
    }
  }
}
