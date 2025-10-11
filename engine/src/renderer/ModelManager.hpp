#pragma once
#include <memory>
#include <string>
#include <vector>

#include "ModelStructs.hpp"
#include "defines.hpp"

class ModelManager {
 public:
  ModelManager(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<BufferManager> bufferMgr, std::shared_ptr<TextureManager> textureMgr,
               std::shared_ptr<CommandBufferUtils> cmdUtil)
      : context(ctx), bufferManager(bufferMgr), textureManager(textureMgr), cmdUtils(cmdUtil) {}
  ~ModelManager() {}

  //  Complete model
  struct Model {
    BufferManager::Buffer vertices;
    BufferManager::Buffer indices;
    glm::mat4 aabb;

    std::vector<tak::Node*> nodes;
    std::vector<tak::Node*> linearNodes;
    std::vector<tak::Skin*> skins;

    std::vector<TextureManager::Texture> textures;
    std::vector<TextureManager::TextureSampler> textureSamplers;
    std::vector<tak::Material> materials;

    std::vector<tak::Animation> animations;
    std::vector<std::string> extensions;

    struct Dimensions {
      glm::vec3 min = glm::vec3(FLT_MAX);
      glm::vec3 max = glm::vec3(-FLT_MAX);
    } dimensions;

    std::string filePath;
  };

  // Model management
  Model createModelFromFile(const std::string& filename, float scale = 1.0f);
  void updateAnimation(ModelManager::Model& model, int index, float time);
  void destroyModel(Model& model);

 private:
  void loadTextures(Model& model, tinygltf::Model& gltfModel);
  void loadMaterials(Model& model, tinygltf::Model& gltfModel);
  void loadNode(tak::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, Model& model, const tinygltf::Model& gltfModel, tak::LoaderInfo& loaderInfo,
                float globalscale);
  void loadSkins(Model& model, tinygltf::Model& gltfModel);
  void loadAnimations(Model& model, tinygltf::Model& gltfModel);
  void getNodeVertexCounts(const tinygltf::Node& node, const tinygltf::Model& model, size_t& vertexCount, size_t& indexCount);
  tak::Node* findNode(tak::Node* parent, uint32_t index);
  tak::Node* nodeFromIndex(uint32_t index, const Model& model);
  void getSceneDimensions(Model& model);
  void calculateBoundingBox(tak::Node* node, tak::Node* parent, Model& model);

  std::shared_ptr<VulkanContext> context;
  std::shared_ptr<BufferManager> bufferManager;
  std::shared_ptr<TextureManager> textureManager;
  std::shared_ptr<CommandBufferUtils> cmdUtils;
};