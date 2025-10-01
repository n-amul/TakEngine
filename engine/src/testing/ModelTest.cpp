// ModelTest.cpp
#include "ModelTest.hpp"

ModelTest::ModelTest() {
  spdlog::info("ModelTest constructor called");
  modelFilePath = std::string(MODEL_DIR) + "/buster_drone/scene.gltf";
  spdlog::info("Model path: {}", modelFilePath);
  title = "Model Loader Test";
  name = "ModelTest";

  // Set window size if needed
  window_width = 1920;
  window_height = 1080;
}

void ModelTest::loadResources() {
  spdlog::info("=== Starting Model Loading Test ===");
  spdlog::info("loadResources called");

  // Create ModelManager with the shared utilities from VulkanBase
  modelManager = std::make_unique<ModelManager>(context, bufferManager, textureManager, cmdUtils);
  // Load the model
  spdlog::info("Loading model from: {}", modelFilePath);
  testModel = modelManager->createModelFromFile(modelFilePath, 1.0f);

  // Log model info
  spdlog::info("=== Model Loading Complete ===");
  spdlog::info("Model file path: {}", testModel.filePath);
  spdlog::info("Number of nodes: {}", testModel.nodes.size());
  spdlog::info("Number of linear nodes: {}", testModel.linearNodes.size());
  spdlog::info("Number of materials: {}", testModel.materials.size());
  spdlog::info("Number of textures: {}", testModel.textures.size());
}

void ModelTest::createPipeline() { spdlog::info("Creating pipeline for model test"); }

void ModelTest::recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) {}

void ModelTest::updateScene(float deltaTime) {}

void ModelTest::cleanupResources() {
  spdlog::info("Cleaning up ModelTest resources");
  modelManager->destroyModel(testModel);
}