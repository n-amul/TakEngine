#include "PBRIBLScene.hpp"
void PBRIBLScene::loadResources() {
  // load skybox

  emptyTexture = textureManager->createDefault();
  // load scene
  models.scene = modelManager->createModelFromFile(
      std::string(MODEL_DIR) + "/buster_drone/scene.gltf");
  createMaterialBuffer();
  createMeshDataBuffer();

  prepareUniformBuffers();
  setupDescriptors();
  // createSkyboxDescriptorSets();
}
void PBRIBLScene::cleanupResources() {
  for (auto& pipeline : pipelines) {
    vkDestroyPipeline(device, pipeline.second, nullptr);
  }

  vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene,
                               nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.material,
                               nullptr);
  vkDestroyDescriptorSetLayout(
      device, descriptorSetLayouts.materialBuffer, nullptr);
  vkDestroyDescriptorSetLayout(
      device, descriptorSetLayouts.meshDataBuffer, nullptr);

  modelManager->destroyModel(models.skybox);
  modelManager->destroyModel(models.scene);

  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    bufferManager->destroyBuffer(uniformBuffers[i].params);
    bufferManager->destroyBuffer(uniformBuffers[i].skybox);
    bufferManager->destroyBuffer(uniformBuffers[i].scene);
  }

  textureManager->destroyTexture(textures.environmentCube);
  textureManager->destroyTexture(textures.empty);
  textureManager->destroyTexture(textures.irradianceCube);
  textureManager->destroyTexture(textures.lutBrdf);
  textureManager->destroyTexture(textures.prefilteredCube);
}