#include "PBRIBLScene.hpp"
void PBRIBLScene::loadResources() {
  // load skybox
  //   createSkyboxDescriptorSetLayout();
  //   createSkyboxTexture();
  //   createSkyboxVertexBuffer();
  //   createSkyboxIndexBuffer();
  //   createSkyboxUniformBuffers();

  emptyTexture = textureManager->createDefault();
  // load scene
  scene = modelManager->createModelFromFile(std::string(MODEL_DIR) + "/buster_drone/scene.gltf");
  spdlog::info("Scene has {} root nodes and {} total linear nodes", scene.nodes.size(), scene.linearNodes.size());
  for (auto& node : scene.linearNodes) {
    if (node->mesh) {
      spdlog::info("  Node '{}' has mesh", node->name);
    }
  }
  createMaterialBuffer();
  createMeshDataBuffer();

  prepareUniformBuffers();
  setupDescriptors();
  // createSkyboxDescriptorSets();
}