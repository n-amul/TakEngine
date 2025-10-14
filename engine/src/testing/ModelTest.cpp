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

void ModelTest::createPipeline() { spdlog::info("Creating pipeline for model test"); }

void ModelTest::recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) {}

void ModelTest::updateScene(float deltaTime) {}
void ModelTest::loadResources() {
  spdlog::info("=== Starting Model Loading Test ===");

  // Load the model
  spdlog::info("Loading model from: {}", modelFilePath);
  testModel = modelManager->createModelFromFile(modelFilePath, 1.0f);

  // Comprehensive validation
  spdlog::info("=== Model Validation ===");

  // 1. Check vertex and index buffers
  if (testModel.vertices.buffer == VK_NULL_HANDLE) {
    spdlog::error("ERROR: Vertex buffer is NULL!");
  } else {
    spdlog::info("✓ Vertex buffer created");
  }

  if (testModel.indices.buffer == VK_NULL_HANDLE) {
    spdlog::error("ERROR: Index buffer is NULL!");
  } else {
    spdlog::info("✓ Index buffer created");
  }

  // 2. Validate materials
  spdlog::info("\n=== Materials Validation ===");
  spdlog::info("Total materials: {}", testModel.materials.size());
  for (size_t i = 0; i < testModel.materials.size(); i++) {
    auto& mat = testModel.materials[i];
    spdlog::info("Material {}: index={}, baseColor=({:.2f},{:.2f},{:.2f},{:.2f})", i, mat.materialIndex, mat.baseColorFactor.r, mat.baseColorFactor.g, mat.baseColorFactor.b,
                 mat.baseColorFactor.a);

    // Check texture indices
    if (mat.baseColorTextureIndex != UINT32_MAX) {
      if (mat.baseColorTextureIndex >= testModel.textures.size()) {
        spdlog::error("  ERROR: Invalid baseColorTextureIndex: {} (max: {})", mat.baseColorTextureIndex, testModel.textures.size() - 1);
      }
    }
  }

  // 3. Validate node meshes
  spdlog::info("\n=== Node Mesh Validation ===");
  int totalMeshes = 0;
  int totalPrimitives = 0;
  int nodesWithMeshes = 0;

  for (auto node : testModel.linearNodes) {
    if (node->mesh) {
      nodesWithMeshes++;
      totalMeshes++;

      // Check mesh index
      if (node->mesh->index >= testModel.linearNodes.size()) {
        spdlog::error("ERROR: Node '{}' has invalid mesh index: {}", node->name, node->mesh->index);
      }

      // Check primitives
      for (auto primitive : node->mesh->primitives) {
        totalPrimitives++;

        // Validate material index
        if (primitive->materialIndex >= testModel.materials.size()) {
          spdlog::error("ERROR: Primitive has invalid material index: {} (max: {})", primitive->materialIndex, testModel.materials.size() - 1);
        }

        // Check if indices are valid
        if (primitive->indexCount == 0) {
          spdlog::warn("WARNING: Primitive has 0 indices!");
        }
        if (primitive->vertexCount == 0) {
          spdlog::warn("WARNING: Primitive has 0 vertices!");
        }
      }

      // Check mesh matrix
      glm::mat4& m = node->mesh->matrix;
      float det = glm::determinant(m);
      if (std::abs(det) < 0.0001f) {
        spdlog::error("ERROR: Node '{}' has degenerate matrix (determinant ~0)", node->name);
      }
    }
  }

  spdlog::info("Nodes with meshes: {}/{}", nodesWithMeshes, testModel.linearNodes.size());
  spdlog::info("Total primitives: {}", totalPrimitives);

  glm::vec3 scale, translation, skew;
  glm::vec4 perspective;
  glm::quat rotation;

  // 4. Validate textures
  spdlog::info("\n=== Texture Validation ===");
  spdlog::info("Total textures: {}", testModel.textures.size());
  for (size_t i = 0; i < testModel.textures.size(); i++) {
    auto& tex = testModel.textures[i];
    if (tex.image == VK_NULL_HANDLE) {
      spdlog::error("ERROR: Texture {} has NULL image handle", i);
    }
    if (tex.imageView == VK_NULL_HANDLE) {
      spdlog::error("ERROR: Texture {} has NULL view handle", i);
    }
    if (tex.sampler == VK_NULL_HANDLE) {
      spdlog::error("ERROR: Texture {} has NULL sampler handle", i);
    }
  }

  // 5. Check hierarchy
  spdlog::info("\n=== Node Hierarchy ===");
  spdlog::info("Root nodes: {}", testModel.nodes.size());
  for (auto node : testModel.nodes) {
    printNodeHierarchy(node, 0);
  }

  // 6. Summary
  spdlog::info("\n=== Loading Summary ===");
  if (nodesWithMeshes == 0) {
    spdlog::error("CRITICAL: No nodes have meshes!");
  } else if (nodesWithMeshes < 39) {
    spdlog::warn("WARNING: Expected 39 nodes with meshes, found {}", nodesWithMeshes);
  } else {
    spdlog::info("✓ All expected nodes have meshes");
  }
}

void ModelTest::printNodeHierarchy(tak::Node* node, int depth) {
  std::string indent(depth * 2, ' ');

  if (node->mesh) {
    spdlog::info("{}[{}] {} (mesh idx: {}, {} primitives)", indent, depth, node->name, node->mesh->index, node->mesh->primitives.size());

    for (auto primitive : node->mesh->primitives) {
      spdlog::info("{}  └─ Prim: mat={}, idx={}, vtx={}", indent, primitive->materialIndex, primitive->indexCount, primitive->vertexCount);
    }
  } else {
    spdlog::info("{}[{}] {} (no mesh)", indent, depth, node->name);
  }

  for (auto child : node->children) {
    printNodeHierarchy(child, depth + 1);
  }
}
void ModelTest::cleanupResources() {
  spdlog::info("Cleaning up ModelTest resources");
  modelManager->destroyModel(testModel);
}