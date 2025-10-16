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

  // CRITICAL: Update the scene graph to compute all transforms
  spdlog::info("Computing initial node transforms...");
  for (auto& node : testModel.nodes) {
    node->update();
  }

  // Comprehensive validation
  spdlog::info("\n=== Model Validation ===");

  // 1. Check vertex and index buffers
  if (testModel.vertices.buffer == VK_NULL_HANDLE) {
    spdlog::error("ERROR: Vertex buffer is NULL!");
  } else {
    spdlog::info("✓ Vertex buffer created: {}", (void*)testModel.vertices.buffer);
  }

  if (testModel.indices.buffer == VK_NULL_HANDLE) {
    spdlog::error("ERROR: Index buffer is NULL!");
  } else {
    spdlog::info("✓ Index buffer created: {}", (void*)testModel.indices.buffer);
  }

  // 2. Validate materials with detailed texture checks
  spdlog::info("\n=== Materials Validation ===");
  spdlog::info("Total materials: {}", testModel.materials.size());

  int invalidTextureReferences = 0;
  for (size_t i = 0; i < testModel.materials.size(); i++) {
    auto& mat = testModel.materials[i];
    spdlog::info("\nMaterial {}: index={}", i, mat.materialIndex);
    spdlog::info("  Base Color: ({:.2f},{:.2f},{:.2f},{:.2f})", mat.baseColorFactor.r, mat.baseColorFactor.g, mat.baseColorFactor.b, mat.baseColorFactor.a);
    spdlog::info("  Metallic: {:.2f}, Roughness: {:.2f}", mat.metallicFactor, mat.roughnessFactor);
    spdlog::info("  Alpha Mode: {}, Cutoff: {:.2f}", (int)mat.alphaMode, mat.alphaCutoff);
    spdlog::info("  Double Sided: {}, Unlit: {}", mat.doubleSided, mat.unlit);

    // Validate all texture indices
    auto validateTextureIndex = [&](uint32_t texIdx, const char* name) {
      if (texIdx != UINT32_MAX) {
        if (texIdx >= testModel.textures.size()) {
          spdlog::error("  ERROR: Invalid {} texture index: {} (max: {})", name, texIdx, testModel.textures.size() - 1);
          invalidTextureReferences++;
          return false;
        } else {
          spdlog::info("  ✓ {} texture: index {}", name, texIdx);
          return true;
        }
      }
      return true;
    };

    validateTextureIndex(mat.baseColorTextureIndex, "Base Color");
    validateTextureIndex(mat.metallicRoughnessTextureIndex, "Metallic-Roughness");
    validateTextureIndex(mat.normalTextureIndex, "Normal");
    validateTextureIndex(mat.occlusionTextureIndex, "Occlusion");
    validateTextureIndex(mat.emissiveTextureIndex, "Emissive");

    // Check extension textures (specular-glossiness workflow)
    if (mat.pbrWorkflows.specularGlossiness) {
      spdlog::info("  Using Specular-Glossiness workflow");
      validateTextureIndex(mat.extension.specularGlossinessTextureIndex, "Specular-Glossiness");
      validateTextureIndex(mat.extension.diffuseTextureIndex, "Diffuse");
    }
  }

  if (invalidTextureReferences > 0) {
    spdlog::error("CRITICAL: Found {} invalid texture references!", invalidTextureReferences);
  } else {
    spdlog::info("✓ All material texture references are valid");
  }

  // 3. Validate node meshes and primitives with transform checks
  spdlog::info("\n=== Node Mesh & Primitive Validation ===");
  int totalMeshes = 0;
  int totalPrimitives = 0;
  int nodesWithMeshes = 0;
  int invalidMaterialReferences = 0;
  int invalidMeshIndices = 0;
  int degenerateMatrices = 0;

  for (auto node : testModel.linearNodes) {
    if (node->mesh) {
      nodesWithMeshes++;
      totalMeshes++;

      // Check mesh index
      if (node->mesh->index >= testModel.linearNodes.size()) {
        spdlog::error("ERROR: Node '{}' has invalid mesh index: {} (max: {})", node->name, node->mesh->index, testModel.linearNodes.size() - 1);
        invalidMeshIndices++;
      }

      // Check mesh matrix AFTER update()
      glm::mat4& m = node->mesh->matrix;
      float det = glm::determinant(m);

      if (nodesWithMeshes <= 5) {
        spdlog::info("Node '{}' mesh matrix:", node->name);
        spdlog::info("  Determinant: {:.6f}", det);
        spdlog::info("  Translation: [{:.3f}, {:.3f}, {:.3f}]", m[3][0], m[3][1], m[3][2]);
        spdlog::info("  Scale: [{:.3f}, {:.3f}, {:.3f}]", glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])));
      }

      // Check primitives
      for (size_t primIdx = 0; primIdx < node->mesh->primitives.size(); primIdx++) {
        auto primitive = node->mesh->primitives[primIdx];
        totalPrimitives++;

        // Validate material index
        if (primitive->materialIndex >= testModel.materials.size()) {
          spdlog::error("ERROR: Node '{}' primitive {} has invalid material index: {} (max: {})", node->name, primIdx, primitive->materialIndex,
                        testModel.materials.size() - 1);
          invalidMaterialReferences++;
        } else {
          auto& primMat = testModel.materials[primitive->materialIndex];
          // Verify texture indices
          bool hasInvalidTexture = false;
          if (primMat.baseColorTextureIndex != UINT32_MAX && primMat.baseColorTextureIndex >= testModel.textures.size()) {
            hasInvalidTexture = true;
          }
          if (primMat.metallicRoughnessTextureIndex != UINT32_MAX && primMat.metallicRoughnessTextureIndex >= testModel.textures.size()) {
            hasInvalidTexture = true;
          }
          if (primMat.normalTextureIndex != UINT32_MAX && primMat.normalTextureIndex >= testModel.textures.size()) {
            hasInvalidTexture = true;
          }
          if (primMat.occlusionTextureIndex != UINT32_MAX && primMat.occlusionTextureIndex >= testModel.textures.size()) {
            hasInvalidTexture = true;
          }
          if (primMat.emissiveTextureIndex != UINT32_MAX && primMat.emissiveTextureIndex >= testModel.textures.size()) {
            hasInvalidTexture = true;
          }

          if (hasInvalidTexture) {
            spdlog::error("ERROR: Node '{}' primitive {} uses material {} which has invalid texture indices!", node->name, primIdx, primitive->materialIndex);
          }
        }

        // Check if indices/vertices are valid
        if (primitive->indexCount == 0 && primitive->hasIndices) {
          spdlog::warn("WARNING: Node '{}' primitive {} has 0 indices but hasIndices=true!", node->name, primIdx);
        }
        if (primitive->vertexCount == 0) {
          spdlog::error("ERROR: Node '{}' primitive {} has 0 vertices!", node->name, primIdx);
        }

        // Log first few primitives for debugging
        if (totalPrimitives <= 5) {
          spdlog::info("Primitive {}: firstIndex={}, indexCount={}, vertexCount={}, materialIndex={}", totalPrimitives, primitive->firstIndex, primitive->indexCount,
                       primitive->vertexCount, primitive->materialIndex);
        }

        // Validate bounding box
        if (!primitive->bb.valid) {
          spdlog::warn("WARNING: Node '{}' primitive {} has invalid bounding box", node->name, primIdx);
        }
      }

      // Check joint data if skinned
      if (node->mesh->jointcount > 0) {
        if (node->mesh->jointcount > MAX_NUM_JOINTS) {
          spdlog::error("ERROR: Node '{}' has too many joints: {} (max: {})", node->name, node->mesh->jointcount, MAX_NUM_JOINTS);
        } else {
          spdlog::info("  Node '{}' has {} joints (skinned mesh)", node->name, node->mesh->jointcount);
        }
      }
    }
  }

  spdlog::info("Nodes with meshes: {}/{}", nodesWithMeshes, testModel.linearNodes.size());
  spdlog::info("Total primitives: {}", totalPrimitives);

  if (invalidMaterialReferences > 0) {
    spdlog::error("CRITICAL: Found {} invalid material index references!", invalidMaterialReferences);
  }
  if (invalidMeshIndices > 0) {
    spdlog::error("CRITICAL: Found {} invalid mesh indices!", invalidMeshIndices);
  }

  // 4. Validate textures
  spdlog::info("\n=== Texture Validation ===");
  spdlog::info("Total textures: {}", testModel.textures.size());

  int invalidTextures = 0;
  for (size_t i = 0; i < testModel.textures.size(); i++) {
    auto& tex = testModel.textures[i];
    bool isValid = true;

    if (tex.image == VK_NULL_HANDLE) {
      spdlog::error("ERROR: Texture {} has NULL image handle", i);
      isValid = false;
    }
    if (tex.imageView == VK_NULL_HANDLE) {
      spdlog::error("ERROR: Texture {} has NULL view handle", i);
      isValid = false;
    }
    if (tex.sampler == VK_NULL_HANDLE) {
      spdlog::error("ERROR: Texture {} has NULL sampler handle", i);
      isValid = false;
    }

    if (!isValid) {
      invalidTextures++;
    } else {
      spdlog::info("✓ Texture {}: {}x{} format={}", i, tex.extent.width, tex.extent.height, tex.format);
    }
  }

  if (invalidTextures > 0) {
    spdlog::error("CRITICAL: {} textures have invalid Vulkan handles!", invalidTextures);
  }

  // 5. Alpha mode distribution
  spdlog::info("\n=== Alpha Mode Distribution ===");
  int opaqueCount = 0, maskCount = 0, blendCount = 0;
  for (auto& mat : testModel.materials) {
    switch (mat.alphaMode) {
      case tak::Material::ALPHAMODE_OPAQUE:
        opaqueCount++;
        break;
      case tak::Material::ALPHAMODE_MASK:
        maskCount++;
        break;
      case tak::Material::ALPHAMODE_BLEND:
        blendCount++;
        break;
    }
  }
  spdlog::info("Materials by alpha mode:");
  spdlog::info("  OPAQUE: {}", opaqueCount);
  spdlog::info("  MASK: {}", maskCount);
  spdlog::info("  BLEND: {}", blendCount);

  if (maskCount > 0 || blendCount > 0) {
    spdlog::warn("WARNING: Model has MASK or BLEND materials - make sure you render all passes!");
  }

  // 6. Check hierarchy
  spdlog::info("\n=== Node Hierarchy (first 10 nodes) ===");
  spdlog::info("Root nodes: {}", testModel.nodes.size());
  int printCount = 0;
  for (auto node : testModel.nodes) {
    printNodeHierarchy(node, 0, printCount);
    if (printCount >= 10) {
      spdlog::info("... ({} more nodes not shown)", testModel.linearNodes.size() - printCount);
      break;
    }
  }

  // 7. Animation validation
  if (testModel.animations.size() > 0) {
    spdlog::info("\n=== Animation Validation ===");
    spdlog::info("Total animations: {}", testModel.animations.size());
    for (size_t i = 0; i < testModel.animations.size(); i++) {
      auto& anim = testModel.animations[i];
      spdlog::info("Animation {}: '{}' ({}s - {}s)", i, anim.name, anim.start, anim.end);
      spdlog::info("  Samplers: {}, Channels: {}", anim.samplers.size(), anim.channels.size());
    }
  }

  // 8. Final Summary
  spdlog::info("\n=== Loading Summary ===");

  int totalErrors = invalidTextureReferences + invalidMaterialReferences + invalidMeshIndices + invalidTextures + degenerateMatrices;

  if (totalErrors > 0) {
    spdlog::error("FAILED: Found {} total errors!", totalErrors);
    spdlog::error("  - Invalid texture references in materials: {}", invalidTextureReferences);
    spdlog::error("  - Invalid material indices in primitives: {}", invalidMaterialReferences);
    spdlog::error("  - Invalid mesh indices: {}", invalidMeshIndices);
    spdlog::error("  - Invalid texture Vulkan handles: {}", invalidTextures);
    spdlog::error("  - Degenerate matrices after update: {}", degenerateMatrices);
  } else {
    spdlog::info("✓✓✓ ALL VALIDATIONS PASSED ✓✓✓");
  }

  if (nodesWithMeshes == 0) {
    spdlog::error("CRITICAL: No nodes have meshes!");
  } else {
    spdlog::info("✓ Found {} nodes with meshes", nodesWithMeshes);
  }

  spdlog::info("\n=== Model Statistics ===");
  spdlog::info("Nodes: {} ({} with meshes)", testModel.linearNodes.size(), nodesWithMeshes);
  spdlog::info("Primitives: {}", totalPrimitives);
  spdlog::info("Materials: {}", testModel.materials.size());
  spdlog::info("Textures: {}", testModel.textures.size());
  spdlog::info("Animations: {}", testModel.animations.size());
  spdlog::info("Skins: {}", testModel.skins.size());
}

void ModelTest::printNodeHierarchy(tak::Node* node, int depth, int& printCount) {
  if (printCount >= 10) return;

  std::string indent(depth * 2, ' ');

  if (node->mesh) {
    printCount++;
    spdlog::info("{}[{}] {} (mesh idx: {}, {} primitives)", indent, depth, node->name, node->mesh->index, node->mesh->primitives.size());

    for (size_t primIdx = 0; primIdx < node->mesh->primitives.size(); primIdx++) {
      auto primitive = node->mesh->primitives[primIdx];

      std::string matInfo;
      if (primitive->materialIndex < testModel.materials.size()) {
        auto& mat = testModel.materials[primitive->materialIndex];
        matInfo = fmt::format("mat={}", primitive->materialIndex);

        // Show texture usage
        std::vector<std::string> textures;
        if (mat.baseColorTextureIndex != UINT32_MAX) textures.push_back("BC");
        if (mat.metallicRoughnessTextureIndex != UINT32_MAX) textures.push_back("MR");
        if (mat.normalTextureIndex != UINT32_MAX) textures.push_back("N");
        if (mat.occlusionTextureIndex != UINT32_MAX) textures.push_back("AO");
        if (mat.emissiveTextureIndex != UINT32_MAX) textures.push_back("E");

        if (!textures.empty()) {
          matInfo += " [";
          for (size_t i = 0; i < textures.size(); i++) {
            matInfo += textures[i];
            if (i < textures.size() - 1) matInfo += ",";
          }
          matInfo += "]";
        }
      } else {
        matInfo = fmt::format("mat=INVALID({})", primitive->materialIndex);
      }

      spdlog::info("{}  └─ Prim {}: {}, idx={}, vtx={}", indent, primIdx, matInfo, primitive->indexCount, primitive->vertexCount);
    }
  } else if (printCount < 10) {
    printCount++;
    spdlog::info("{}[{}] {} (no mesh)", indent, depth, node->name);
  }

  for (auto child : node->children) {
    if (printCount >= 10) break;
    printNodeHierarchy(child, depth + 1, printCount);
  }
}

void ModelTest::cleanupResources() {
  spdlog::info("Cleaning up ModelTest resources");
  modelManager->destroyModel(testModel);
}