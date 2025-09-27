#include <basisu_transcoder.h>
#include <tiny_gltf.h>
#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "renderer/BufferManager.hpp"
#include "renderer/CommandBufferUtils.hpp"
#include "renderer/TextureManager.hpp"
#include "renderer/VulkanContext.hpp"

#define MAX_NUM_JOINTS 128u

struct BoundingBox {
  glm::vec3 min;
  glm::vec3 max;
  bool valid = false;
  BoundingBox() {};
  BoundingBox(glm::vec3 min, glm::vec3 max) : min(min), max(max) {};
  // determines where the new min/max extents will be
  BoundingBox getAABB(glm::mat4 m) {
    glm::vec3 min = glm::vec3(m[3]);
    glm::vec3 max = min;
    glm::vec3 v0, v1;

    glm::vec3 right = glm::vec3(m[0]);
    v0 = right * this->min.x;
    v1 = right * this->max.x;
    min += glm::min(v0, v1);
    max += glm::max(v0, v1);

    glm::vec3 up = glm::vec3(m[1]);
    v0 = up * this->min.y;
    v1 = up * this->max.y;
    min += glm::min(v0, v1);
    max += glm::max(v0, v1);

    glm::vec3 back = glm::vec3(m[2]);
    v0 = back * this->min.z;
    v1 = back * this->max.z;
    min += glm::min(v0, v1);
    max += glm::max(v0, v1);

    return BoundingBox(min, max);
  }
};

// PBR Vertex structure for glTF models
struct Material {
  enum AlphaMode { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
  AlphaMode alphaMode = ALPHAMODE_OPAQUE;
  float alphaCutoff = 1.0f;
  float metallicFactor = 1.0f;
  float roughnessFactor = 1.0f;
  glm::vec4 baseColorFactor = glm::vec4(1.0f);
  glm::vec4 emissiveFactor = glm::vec4(0.0f);
  TextureManager::Texture baseColorTexture;
  TextureManager::Texture metallicRoughnessTexture;
  TextureManager::Texture normalTexture;
  TextureManager::Texture occlusionTexture;
  TextureManager::Texture emissiveTexture;
  bool doubleSided = false;  // cull front: counter clockwise
  struct TexCoordSets {
    uint8_t baseColor = 0;
    uint8_t metallicRoughness = 0;
    uint8_t specularGlossiness = 0;
    uint8_t normal = 0;
    uint8_t occlusion = 0;
    uint8_t emissive = 0;
  } texCoordSets;
  // alternative PBR workflow
  struct Extension {
    TextureManager::Texture specularGlossinessTexture;
    TextureManager::Texture diffuseTexture;
    glm::vec4 diffuseFactor = glm::vec4(1.0f);
    glm::vec3 specularFactor = glm::vec3(0.0f);
  } extension;
  struct PbrWorkflows {
    bool metallicRoughness = true;
    bool specularGlossiness = false;
  } pbrWorkflows;
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  int index = 0;
  bool unlit = false;
  float emissiveStrength = 1.0f;
};

struct Primitive {
  uint32_t firstIndex;
  uint32_t indexCount;
  uint32_t vertexCount;  // no first vertex?
  Material& material;
  bool hasIndices;
  BoundingBox bb;
  Primitive(uint32_t firstIndex, uint32_t indexCount, uint32_t vertexCount, Material& material)
      : firstIndex(firstIndex), indexCount(indexCount), vertexCount(vertexCount), material(material) {
    hasIndices = indexCount > 0;
  }
  void setBoundingBox(glm::vec3 min, glm::vec3 max) {
    bb.min = min;
    bb.max = max;
    bb.valid = true;
  }
};
struct Mesh {
  std::vector<Primitive*> primitives;
  BoundingBox bb;
  BoundingBox aabb;
  glm::mat4 matrix;
  std::vector<glm::mat4> jointMatrix = std::vector<glm::mat4>(MAX_NUM_JOINTS);  // consider not setting the size here
  uint32_t jointcount{0};
  uint32_t index;
  Mesh(glm::mat4 matrix) { this->matrix = matrix; }  // @todo: create large SSBO instead of many small uniform buffers
  ~Mesh() {
    for (Primitive* p : primitives) delete p;
  }
  void setBoundingBox(glm::vec3 min, glm::vec3 max) {
    bb.min = min;
    bb.max = max;
    bb.valid = true;
  }
};
struct Node;

struct Skin {
  std::string name;
  Node* skeletonRoot = nullptr;
  std::vector<glm::mat4> inverseBindMatrices;
  std::vector<Node*> joints;
};

// node for scene graph
struct Node {
  Node* parent;
  uint32_t index;
  std::vector<Node*> children;
  glm::mat4 matrix;
  std::string name;
  Mesh* mesh;
  Skin* skin;
  int32_t skinIndex = -1;
  glm::vec3 translation{};
  glm::vec3 scale{1.0f};
  glm::quat rotation{};
  BoundingBox bvh;
  BoundingBox aabb;
  bool useCachedMatrix{false};
  glm::mat4 cachedLocalMatrix{glm::mat4(1.0f)};
  glm::mat4 cachedMatrix{glm::mat4(1.0f)};
  glm::mat4 localMatrix();
  glm::mat4 getMatrix();
  void update();
  ~Node() = default;
};

struct AnimationChannel {
  enum PathType { TRANSLATION, ROTATION, SCALE };
  PathType path;
  Node* node;
  uint32_t samplerIndex;
};

struct AnimationSampler {
  enum InterpolationType { LINEAR, STEP, CUBICSPLINE };
  InterpolationType interpolation;
  std::vector<float> inputs;
  std::vector<glm::vec4> outputsVec4;
  std::vector<float> outputs;
  glm::vec4 cubicSplineInterpolation(size_t index, float time, uint32_t stride);
  void translate(size_t index, float time, Node* node);
  void scale(size_t index, float time, Node* node);
  void rotate(size_t index, float time, Node* node);
};

struct Animation {
  std::string name;
  std::vector<AnimationSampler> samplers;
  std::vector<AnimationChannel> channels;
  float start = std::numeric_limits<float>::max();
  float end = std::numeric_limits<float>::min();
};