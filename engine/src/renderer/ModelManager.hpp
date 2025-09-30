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

    std::vector<Node> nodes;
    std::vector<Node*> linearNodes;
    std::vector<Skin> skins;
    std::vector<TextureManager::Texture> textures;
    std::vector<TextureManager::TextureSampler> textureSamplers;

    std::vector<Material> materials;
    std::vector<Animation> animations;
    std::vector<std::string> extensions;

    struct Dimensions {
      glm::vec3 min = glm::vec3(FLT_MAX);
      glm::vec3 max = glm::vec3(-FLT_MAX);
    } dimensions;

    std::string filePath;
  };

  // Model management
  Model createModelFromFile(const std::string& filename, float scale = 1.0f);

  void destroyModel(Model* model);

  // Rendering
  void drawModel(Model* model, VkCommandBuffer commandBuffer);
  void drawNode(Model* model, Node* node, VkCommandBuffer commandBuffer);

  // Animation
  void updateAnimation(Model* model, uint32_t index, float time);

  // Utilities
  Node* findNode(Model* model, Node* parent, uint32_t index);
  Node* nodeFromIndex(Model* model, uint32_t index);
  void calculateBoundingBox(Model* model, Node* node, Node* parent);
  void getSceneDimensions(Model* model);

 private:
  void loadTextures(Model& model, tinygltf::Model& gltfModel);
  void loadMaterials(Model& model, tinygltf::Model& gltfModel);
  void loadNode(Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model, LoaderInfo& loaderInfo, float globalscale);
  void loadSkins(Model& model, tinygltf::Model& gltfModel);
  void loadAnimations(Model* model, tinygltf::Model& gltfModel);
  void getNodeVertexCounts(const tinygltf::Node& node, const tinygltf::Model& model, size_t& vertexCount, size_t& indexCount);
  VkSamplerAddressMode getVkWrapMode(int32_t wrapMode);
  VkFilter getVkFilterMode(int32_t filterMode);

  std::shared_ptr<VulkanContext> context;
  std::shared_ptr<BufferManager> bufferManager;
  std::shared_ptr<TextureManager> textureManager;
  std::shared_ptr<CommandBufferUtils> cmdUtils;
};