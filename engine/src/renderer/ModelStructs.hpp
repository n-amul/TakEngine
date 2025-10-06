#pragma once
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

#define MAX_NUM_JOINTS 64u

namespace tak {
struct Vertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 uv0;
  glm::vec2 uv1;
  glm::uvec4 joint0;
  glm::vec4 weight0;
  glm::vec4 color;
  static VkVertexInputBindingDescription getBindingDescription() {
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
  }

  static std::array<VkVertexInputAttributeDescription, 7> getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 7> attributeDescriptions{};

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normal);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, uv0);

    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[3].offset = offsetof(Vertex, uv1);

    attributeDescriptions[4].binding = 0;
    attributeDescriptions[4].location = 4;
    attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_UINT;
    attributeDescriptions[4].offset = offsetof(Vertex, joint0);

    attributeDescriptions[5].binding = 0;
    attributeDescriptions[5].location = 5;
    attributeDescriptions[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[5].offset = offsetof(Vertex, weight0);

    attributeDescriptions[6].binding = 0;
    attributeDescriptions[6].location = 6;
    attributeDescriptions[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[6].offset = offsetof(Vertex, color);

    return attributeDescriptions;
  }
};
struct LoaderInfo {
  std::vector<uint32_t> indexBuffer;
  std::vector<Vertex> vertexBuffer;
  size_t indexPos = 0;
  size_t vertexPos = 0;
};

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

  // (UINT32_MAX = no texture)
  uint32_t baseColorTextureIndex = UINT32_MAX;
  uint32_t metallicRoughnessTextureIndex = UINT32_MAX;
  uint32_t normalTextureIndex = UINT32_MAX;
  uint32_t occlusionTextureIndex = UINT32_MAX;
  uint32_t emissiveTextureIndex = UINT32_MAX;

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
    uint32_t specularGlossinessTextureIndex = UINT32_MAX;
    uint32_t diffuseTextureIndex = UINT32_MAX;
    glm::vec4 diffuseFactor = glm::vec4(1.0f);
    glm::vec3 specularFactor = glm::vec3(0.0f);
  } extension;
  struct PbrWorkflows {
    bool metallicRoughness = true;
    bool specularGlossiness = false;
  } pbrWorkflows;
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  uint32_t materialIndex = 0;
  bool unlit = false;
  float emissiveStrength = 1.0f;
};

struct Primitive {
  uint32_t firstIndex;
  uint32_t indexCount;
  uint32_t vertexCount;
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
  Mesh(glm::mat4 matrix) { this->matrix = matrix; }
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
  glm::mat4 localMatrix() {
    if (!useCachedMatrix) {
      cachedLocalMatrix = glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) * matrix;
    };
    return cachedLocalMatrix;
  }
  glm::mat4 getMatrix() {
    // Use a simple caching algorithm to avoid having to recalculate matrices to often while traversing the node hierarchy
    if (!useCachedMatrix) {
      glm::mat4 m = localMatrix();
      Node* p = parent;
      while (p) {
        m = p->localMatrix() * m;
        p = p->parent;
      }
      cachedMatrix = m;
      useCachedMatrix = true;
      return m;
    } else {
      return cachedMatrix;
    }
  }
  void update() {
    useCachedMatrix = false;
    if (mesh) {
      glm::mat4 m = getMatrix();
      if (skin) {
        mesh->matrix = m;
        // Update join matrices
        glm::mat4 inverseTransform = glm::inverse(m);
        size_t numJoints = std::min((uint32_t)skin->joints.size(), MAX_NUM_JOINTS);
        for (size_t i = 0; i < numJoints; i++) {
          Node* jointNode = skin->joints[i];
          glm::mat4 jointMat = jointNode->getMatrix() * skin->inverseBindMatrices[i];
          jointMat = inverseTransform * jointMat;
          mesh->jointMatrix[i] = jointMat;
        }
        mesh->jointcount = static_cast<uint32_t>(numJoints);
      } else {
        mesh->matrix = m;
      }
    }
    for (auto& child : children) {
      child->update();
    }
  }
  ~Node() {
    if (mesh) {
      delete mesh;
    }
    for (auto& child : children) {
      delete child;
    }
  }
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
  glm::vec4 cubicSplineInterpolation(size_t index, float time, uint32_t stride) {
    float delta = inputs[index + 1] - inputs[index];
    float t = (time - inputs[index]) / delta;
    const size_t current = index * stride * 3;
    const size_t next = (index + 1) * stride * 3;
    const size_t A = 0;
    const size_t V = stride * 1;
    const size_t B = stride * 2;

    float t2 = powf(t, 2);
    float t3 = powf(t, 3);
    glm::vec4 pt{0.0f};
    for (uint32_t i = 0; i < stride; i++) {
      float p0 = outputs[current + i + V];          // starting point at t = 0
      float m0 = delta * outputs[current + i + A];  // scaled starting tangent at t = 0
      float p1 = outputs[next + i + V];             // ending point at t = 1
      float m1 = delta * outputs[next + i + B];     // scaled ending tangent at t = 1
      pt[i] = ((2.f * t3 - 3.f * t2 + 1.f) * p0) + ((t3 - 2.f * t2 + t) * m0) + ((-2.f * t3 + 3.f * t2) * p1) + ((t3 - t2) * m0);
    }
    return pt;
  }
  void translate(size_t index, float time, Node* node) {
    switch (interpolation) {
      case AnimationSampler::InterpolationType::LINEAR: {
        float u = std::max(0.0f, time - inputs[index]) / (inputs[index + 1] - inputs[index]);
        node->translation = glm::mix(outputsVec4[index], outputsVec4[index + 1], u);
        break;
      }
      case AnimationSampler::InterpolationType::STEP: {
        node->translation = outputsVec4[index];
        break;
      }
      case AnimationSampler::InterpolationType::CUBICSPLINE: {
        node->translation = cubicSplineInterpolation(index, time, 3);
        break;
      }
    }
  }
  void scale(size_t index, float time, Node* node) {
    switch (interpolation) {
      case AnimationSampler::InterpolationType::LINEAR: {
        float u = std::max(0.0f, time - inputs[index]) / (inputs[index + 1] - inputs[index]);
        node->scale = glm::mix(outputsVec4[index], outputsVec4[index + 1], u);
        break;
      }
      case AnimationSampler::InterpolationType::STEP: {
        node->scale = outputsVec4[index];
        break;
      }
      case AnimationSampler::InterpolationType::CUBICSPLINE: {
        node->scale = cubicSplineInterpolation(index, time, 3);
        break;
      }
    }
  }
  void rotate(size_t index, float time, Node* node) {
    switch (interpolation) {
      case AnimationSampler::InterpolationType::LINEAR: {
        float u = std::max(0.0f, time - inputs[index]) / (inputs[index + 1] - inputs[index]);
        glm::quat q1;
        q1.x = outputsVec4[index].x;
        q1.y = outputsVec4[index].y;
        q1.z = outputsVec4[index].z;
        q1.w = outputsVec4[index].w;
        glm::quat q2;
        q2.x = outputsVec4[index + 1].x;
        q2.y = outputsVec4[index + 1].y;
        q2.z = outputsVec4[index + 1].z;
        q2.w = outputsVec4[index + 1].w;
        node->rotation = glm::normalize(glm::slerp(q1, q2, u));
        break;
      }
      case AnimationSampler::InterpolationType::STEP: {
        glm::quat q1;
        q1.x = outputsVec4[index].x;
        q1.y = outputsVec4[index].y;
        q1.z = outputsVec4[index].z;
        q1.w = outputsVec4[index].w;
        node->rotation = q1;
        break;
      }
      case AnimationSampler::InterpolationType::CUBICSPLINE: {
        glm::vec4 rot = cubicSplineInterpolation(index, time, 4);
        glm::quat q;
        q.x = rot.x;
        q.y = rot.y;
        q.z = rot.z;
        q.w = rot.w;
        node->rotation = glm::normalize(q);
        break;
      }
    }
  }
};

struct Animation {
  std::string name;
  std::vector<AnimationSampler> samplers;
  std::vector<AnimationChannel> channels;
  float start = std::numeric_limits<float>::max();
  float end = std::numeric_limits<float>::min();
};
}  // namespace tak