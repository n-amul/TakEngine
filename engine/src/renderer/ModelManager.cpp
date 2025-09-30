#include "ModelManager.hpp"

#include <spdlog/spdlog.h>

#include <stdexcept>

void ModelManager::destroyModel(Model *model) {}

ModelManager::Model ModelManager::createModelFromFile(const std::string &filename, float scale) {
  ModelManager::Model model;
  tinygltf::Model gltfModel;
  tinygltf::TinyGLTF gltfContext;
  std::string error;
  std::string warning;

  bool binary = false;
  size_t extpos = filename.rfind('.', filename.length());
  if (extpos != std::string::npos) {
    binary = (filename.substr(extpos + 1, filename.length() - extpos) == "glb");
  }
  size_t pos = filename.find_last_of('/');
  if (pos == std::string::npos) {
    pos = filename.find_last_of('\\');
  }
  model.filePath = filename.substr(0, pos);

  auto loadImageDataFunc = [](tinygltf::Image *image, const int imageIndex, std::string *error, std::string *warning, int req_width, int req_height,
                              const unsigned char *bytes, int size, void *userData) -> bool {
    // KTX files will be handled by our own code
    if (image->uri.find_last_of(".") != std::string::npos) {
      if (image->uri.substr(image->uri.find_last_of(".") + 1) == "ktx2") {
        return true;
      }
    }
    return tinygltf::LoadImageData(image, imageIndex, error, warning, req_width, req_height, bytes, size, userData);
  };
  gltfContext.SetImageLoader(loadImageDataFunc, nullptr);

  bool fileLoaded =
      binary ? gltfContext.LoadBinaryFromFile(&gltfModel, &error, &warning, filename.c_str()) : gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename.c_str());

  spdlog::info("Meshes: {}, Nodes{}", gltfModel.meshes.size(), gltfModel.nodes.size());

  if (!fileLoaded) {
    throw std::runtime_error("Could not load gltf file: "+error);
  }

  model.extensions = gltfModel.extensionsUsed;
  for (auto &extension : model.extensions) {
    // If this model uses basis universal compressed textures, we need to transcode them
    if (extension == "KHR_texture_basisu") {
      spdlog::info("Model uses KHR_texture_basisu, initializing basisu transcoder");
      basist::basisu_transcoder_init();
    }
  }
  // load texture,sampler, and materials
  loadTextures(model, gltfModel);
  loadMaterials(model, gltfModel);

  LoaderInfo loaderInfo{};
  size_t vertexCount = 0;
  size_t indexCount = 0;

  const tinygltf::Scene &scene = gltfModel.scenes[gltfModel.defaultScene == -1 ? 0 : gltfModel.defaultScene];
  // Get vertex and index buffer sizes up-front
  for (size_t i = 0; i < scene.nodes.size(); i++) {
    getNodeVertexCounts(gltfModel.nodes[scene.nodes[i]], gltfModel, vertexCount, indexCount);
  }
  loaderInfo.vertexBuffer.resize(vertexCount);
  loaderInfo.indexBuffer.resize(indexCount);
  // no default scene handle
  for (size_t i = 0; i < scene.nodes.size(); i++) {
    const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
    loadNode(nullptr, node, scene.nodes[i], gltfModel, loaderInfo, scale);
  }

  return model;
}

void ModelManager::loadTextures(Model &model, tinygltf::Model &gltfModel) {
  // samplers
  model.textureSamplers = textureManager->loadTextureSamplers(gltfModel);
  // textures
  for (tinygltf::Texture &tex : gltfModel.textures) {
    int source = tex.source;
    // If this texture uses the KHR_texture_basisu, we need to get the source index from the extension structure
    if (tex.extensions.find("KHR_texture_basisu") != tex.extensions.end()) {
      auto ext = tex.extensions.find("KHR_texture_basisu");
      auto value = ext->second.Get("source");
      source = value.Get<int>();
    }
    tinygltf::Image image = gltfModel.images[source];
    spdlog::info("Image #{} validated: '{}' ({}), {}x{}, {} components, {} bytes", source, image.name.empty() ? "unnamed" : image.name,
                 image.uri.empty() ? "embedded" : image.uri, image.width, image.height, image.component, image.image.size());
    if (tex.sampler != -1) {
      model.textures.push_back(textureManager->createTextureFromGLTFImage(image, model.filePath, model.textureSamplers[tex.sampler], context->graphicsQueue));
    } else {
      TextureManager::TextureSampler texSamplerDefault = {VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                                          VK_SAMPLER_ADDRESS_MODE_REPEAT};
      model.textures.push_back(textureManager->createTextureFromGLTFImage(image, model.filePath, texSamplerDefault, context->graphicsQueue));
    }
  }
}

void ModelManager::loadMaterials(Model &model, tinygltf::Model &gltfModel) {
  // Create materials from glTF model
  for (tinygltf::Material &mat : gltfModel.materials) {
    Material material{};
    material.doubleSided = mat.doubleSided;
    if (mat.alphaMode == "OPAQUE") {
      material.alphaMode = Material::ALPHAMODE_OPAQUE;
    } else if (mat.alphaMode == "MASK") {
      material.alphaMode = Material::ALPHAMODE_MASK;
      material.alphaCutoff = 0.5f;
    } else if (mat.alphaMode == "BLEND") {
      material.alphaMode = Material::ALPHAMODE_BLEND;
    }
    // PBR Metallic Roughness workflow (default)
    if (mat.values.find("baseColorFactor") != mat.values.end()) {
      material.baseColorFactor = glm::make_vec4(mat.values.at("baseColorFactor").ColorFactor().data());
    }
    if (mat.values.find("metallicFactor") != mat.values.end()) {
      material.metallicFactor = static_cast<float>(mat.values.at("metallicFactor").Factor());
    }
    if (mat.values.find("roughnessFactor") != mat.values.end()) {
      material.roughnessFactor = static_cast<float>(mat.values.at("roughnessFactor").Factor());
    }
    if (mat.values.find("baseColorTexture") != mat.values.end()) {
      material.baseColorTextureIndex = mat.values.at("baseColorTexture").TextureIndex();
      material.texCoordSets.baseColor = mat.values.at("baseColorTexture").TextureTexCoord();
    }
    if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
      material.metallicRoughnessTextureIndex = mat.values.at("metallicRoughnessTexture").TextureIndex();
      material.texCoordSets.metallicRoughness = mat.values.at("metallicRoughnessTexture").TextureTexCoord();
    }

    // Normal map
    if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
      material.normalTextureIndex = mat.additionalValues.at("normalTexture").TextureIndex();
      material.texCoordSets.normal = mat.additionalValues.at("normalTexture").TextureTexCoord();
    }

    // Occlusion map
    if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
      material.occlusionTextureIndex = mat.additionalValues.at("occlusionTexture").TextureIndex();
      material.texCoordSets.occlusion = mat.additionalValues.at("occlusionTexture").TextureTexCoord();
    }

    // Emissive
    if (mat.additionalValues.find("emissiveFactor") != mat.additionalValues.end()) {
      material.emissiveFactor = glm::vec4(glm::make_vec3(mat.additionalValues.at("emissiveFactor").ColorFactor().data()), 1.0f);
    }
    if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
      material.emissiveTextureIndex = mat.additionalValues.at("emissiveTexture").TextureIndex();
      material.texCoordSets.emissive = mat.additionalValues.at("emissiveTexture").TextureTexCoord();
    }

    // Check for KHR_materials_emissive_strength extension
    if (mat.extensions.find("KHR_materials_emissive_strength") != mat.extensions.end()) {
      auto ext = mat.extensions.find("KHR_materials_emissive_strength");
      if (ext->second.Has("emissiveStrength")) {
        material.emissiveStrength = static_cast<float>(ext->second.Get("emissiveStrength").GetNumberAsDouble());
      }
    }

    // Check for KHR_materials_unlit extension
    if (mat.extensions.find("KHR_materials_unlit") != mat.extensions.end()) {
      material.unlit = true;
    }

    // Check for KHR_materials_pbrSpecularGlossiness extension (alternative workflow)
    if (mat.extensions.find("KHR_materials_pbrSpecularGlossiness") != mat.extensions.end()) {
      auto ext = mat.extensions.find("KHR_materials_pbrSpecularGlossiness");
      if (ext->second.Has("specularGlossinessTexture")) {
        auto index = ext->second.Get("specularGlossinessTexture").Get("index");
        material.extension.specularGlossinessTextureIndex = index.Get<int>();
        auto texCoord = ext->second.Get("specularGlossinessTexture").Get("texCoord");
        material.texCoordSets.specularGlossiness = texCoord.Get<int>();
      }
      if (ext->second.Has("diffuseTexture")) {
        auto index = ext->second.Get("diffuseTexture").Get("index");
        material.extension.diffuseTextureIndex = index.Get<int>();
      }
      if (ext->second.Has("diffuseFactor")) {
        auto factor = ext->second.Get("diffuseFactor");
        for (uint32_t i = 0; i < factor.ArrayLen(); i++) {
          auto val = factor.Get(i);
          material.extension.diffuseFactor[i] = val.IsNumber() ? static_cast<float>(val.Get<double>()) : static_cast<float>(val.Get<int>());
        }
      }
      if (ext->second.Has("specularFactor")) {
        auto factor = ext->second.Get("specularFactor");
        for (uint32_t i = 0; i < factor.ArrayLen(); i++) {
          auto val = factor.Get(i);
          material.extension.specularFactor[i] = val.IsNumber() ? static_cast<float>(val.Get<double>()) : static_cast<float>(val.Get<int>());
        }
      }
      material.pbrWorkflows.specularGlossiness = true;
      material.pbrWorkflows.metallicRoughness = false;
    }

    // Set material index for identification
    material.materialIndex = static_cast<uint32_t>(model.materials.size());

    spdlog::info("Material '{}': index={}, metallic={}, roughness={}, baseColor=({},{},{},{})", mat.name, material.materialIndex, material.metallicFactor,
                 material.roughnessFactor, material.baseColorFactor.r, material.baseColorFactor.g, material.baseColorFactor.b, material.baseColorFactor.a);
    model.materials.push_back(material);
  }

  // Create a default material if no materials are defined
  if (model.materials.empty()) {
    Material defaultMaterial{};
    defaultMaterial.metallicFactor = 0.0f;
    defaultMaterial.roughnessFactor = 1.0f;
    defaultMaterial.baseColorFactor = glm::vec4(1.0f);
    defaultMaterial.materialIndex = 0;
    model.materials.push_back(defaultMaterial);
    spdlog::info("No materials defined in model, using default material");
  }

  spdlog::info("Loaded {} materials", model.materials.size());
}

void ModelManager::getNodeVertexCounts(const tinygltf::Node &node, const tinygltf::Model &model, size_t &vertexCount, size_t &indexCount) {
  if (node.children.size() > 0) {
    for (size_t i = 0; i < node.children.size(); i++) {
      getNodeVertexCounts(model.nodes[node.children[i]], model, vertexCount, indexCount);
    }
  }
  if (node.mesh != -1) {
    const tinygltf::Mesh &mesh = model.meshes[node.mesh];
    for (size_t i = 0; i < mesh.primitives.size(); i++) {
      auto &primitive = mesh.primitives[i];
      vertexCount += model.accessors[primitive.attributes.find("POSITION")->second].count;
      if (primitive.indices != -1) {
        indexCount += model.accessors[primitive.indices].count;
      }
    }
  }
}

// void ModelManager::loadNode(Node *parent, const tinygltf::Node &node, uint32_t nodeIndex, const tinygltf::Model &model, LoaderInfo &loaderInfo, float globalscale) {
//   Node *newNode = new Node{};
//   newNode->index = nodeIndex;
//   newNode->parent = parent;
//   newNode->name = node.name;
//   newNode->skinIndex = node.skin;
//   newNode->matrix = glm::mat4(1.0f);

//   // Generate local node matrix
//   glm::vec3 translation = glm::vec3(0.0f);
//   if (node.translation.size() == 3) {
//     translation = glm::make_vec3(node.translation.data());
//     newNode->translation = translation;
//   }
//   glm::mat4 rotation = glm::mat4(1.0f);
//   if (node.rotation.size() == 4) {
//     glm::quat q = glm::make_quat(node.rotation.data());
//     newNode->rotation = glm::mat4(q);
//   }
//   glm::vec3 scale = glm::vec3(1.0f);
//   if (node.scale.size() == 3) {
//     scale = glm::make_vec3(node.scale.data());
//     newNode->scale = scale;
//   }
//   if (node.matrix.size() == 16) {
//     newNode->matrix = glm::make_mat4x4(node.matrix.data());
//   };

//   // Node with children
//   if (node.children.size() > 0) {
//     for (size_t i = 0; i < node.children.size(); i++) {
//       loadNode(newNode, model.nodes[node.children[i]], node.children[i], model, loaderInfo, globalscale);
//     }
//   }

//   // Node contains mesh data
//   if (node.mesh > -1) {
//     const tinygltf::Mesh mesh = model.meshes[node.mesh];
//     Mesh *newMesh = new Mesh(newNode->matrix);
//     for (size_t j = 0; j < mesh.primitives.size(); j++) {
//       const tinygltf::Primitive &primitive = mesh.primitives[j];
//       uint32_t vertexStart = static_cast<uint32_t>(loaderInfo.vertexPos);
//       uint32_t indexStart = static_cast<uint32_t>(loaderInfo.indexPos);
//       uint32_t indexCount = 0;
//       uint32_t vertexCount = 0;
//       glm::vec3 posMin{};
//       glm::vec3 posMax{};
//       bool hasSkin = false;
//       bool hasIndices = primitive.indices > -1;
//       // Vertices
//       {
//         const float *bufferPos = nullptr;
//         const float *bufferNormals = nullptr;
//         const float *bufferTexCoordSet0 = nullptr;
//         const float *bufferTexCoordSet1 = nullptr;
//         const float *bufferColorSet0 = nullptr;
//         const void *bufferJoints = nullptr;
//         const float *bufferWeights = nullptr;

//         int posByteStride;
//         int normByteStride;
//         int uv0ByteStride;
//         int uv1ByteStride;
//         int color0ByteStride;
//         int jointByteStride;
//         int weightByteStride;

//         int jointComponentType;

//         // Position attribute is required
//         assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

//         const tinygltf::Accessor &posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
//         const tinygltf::BufferView &posView = model.bufferViews[posAccessor.bufferView];
//         bufferPos = reinterpret_cast<const float *>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
//         posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
//         posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);
//         vertexCount = static_cast<uint32_t>(posAccessor.count);
//         posByteStride = posAccessor.ByteStride(posView) ? (posAccessor.ByteStride(posView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);

//         if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
//           const tinygltf::Accessor &normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
//           const tinygltf::BufferView &normView = model.bufferViews[normAccessor.bufferView];
//           bufferNormals = reinterpret_cast<const float *>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
//           normByteStride = normAccessor.ByteStride(normView) ? (normAccessor.ByteStride(normView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
//         }

//         // UVs
//         if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
//           const tinygltf::Accessor &uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
//           const tinygltf::BufferView &uvView = model.bufferViews[uvAccessor.bufferView];
//           bufferTexCoordSet0 = reinterpret_cast<const float *>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
//           uv0ByteStride = uvAccessor.ByteStride(uvView) ? (uvAccessor.ByteStride(uvView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
//         }
//         if (primitive.attributes.find("TEXCOORD_1") != primitive.attributes.end()) {
//           const tinygltf::Accessor &uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_1")->second];
//           const tinygltf::BufferView &uvView = model.bufferViews[uvAccessor.bufferView];
//           bufferTexCoordSet1 = reinterpret_cast<const float *>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
//           uv1ByteStride = uvAccessor.ByteStride(uvView) ? (uvAccessor.ByteStride(uvView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
//         }

//         // Vertex colors
//         if (primitive.attributes.find("COLOR_0") != primitive.attributes.end()) {
//           const tinygltf::Accessor &accessor = model.accessors[primitive.attributes.find("COLOR_0")->second];
//           const tinygltf::BufferView &view = model.bufferViews[accessor.bufferView];
//           bufferColorSet0 = reinterpret_cast<const float *>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
//           color0ByteStride = accessor.ByteStride(view) ? (accessor.ByteStride(view) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
//         }

//         // Skinning
//         // Joints
//         if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end()) {
//           const tinygltf::Accessor &jointAccessor = model.accessors[primitive.attributes.find("JOINTS_0")->second];
//           const tinygltf::BufferView &jointView = model.bufferViews[jointAccessor.bufferView];
//           bufferJoints = &(model.buffers[jointView.buffer].data[jointAccessor.byteOffset + jointView.byteOffset]);
//           jointComponentType = jointAccessor.componentType;
//           jointByteStride = jointAccessor.ByteStride(jointView) ? (jointAccessor.ByteStride(jointView) / tinygltf::GetComponentSizeInBytes(jointComponentType))
//                                                                 : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
//         }

//         if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end()) {
//           const tinygltf::Accessor &weightAccessor = model.accessors[primitive.attributes.find("WEIGHTS_0")->second];
//           const tinygltf::BufferView &weightView = model.bufferViews[weightAccessor.bufferView];
//           bufferWeights = reinterpret_cast<const float *>(&(model.buffers[weightView.buffer].data[weightAccessor.byteOffset + weightView.byteOffset]));
//           weightByteStride =
//               weightAccessor.ByteStride(weightView) ? (weightAccessor.ByteStride(weightView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
//         }

//         hasSkin = (bufferJoints && bufferWeights);

//         for (size_t v = 0; v < posAccessor.count; v++) {
//           Vertex &vert = loaderInfo.vertexBuffer[loaderInfo.vertexPos];
//           vert.pos = glm::vec4(glm::make_vec3(&bufferPos[v * posByteStride]), 1.0f);
//           vert.normal = glm::normalize(glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * normByteStride]) : glm::vec3(0.0f)));
//           vert.uv0 = bufferTexCoordSet0 ? glm::make_vec2(&bufferTexCoordSet0[v * uv0ByteStride]) : glm::vec3(0.0f);
//           vert.uv1 = bufferTexCoordSet1 ? glm::make_vec2(&bufferTexCoordSet1[v * uv1ByteStride]) : glm::vec3(0.0f);
//           vert.color = bufferColorSet0 ? glm::make_vec4(&bufferColorSet0[v * color0ByteStride]) : glm::vec4(1.0f);

//           if (hasSkin) {
//             switch (jointComponentType) {
//               case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
//                 const uint16_t *buf = static_cast<const uint16_t *>(bufferJoints);
//                 vert.joint0 = glm::uvec4(glm::make_vec4(&buf[v * jointByteStride]));
//                 break;
//               }
//               case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
//                 const uint8_t *buf = static_cast<const uint8_t *>(bufferJoints);
//                 vert.joint0 = glm::vec4(glm::make_vec4(&buf[v * jointByteStride]));
//                 break;
//               }
//               default:
//                 // Not supported by spec
//                 std::cerr << "Joint component type " << jointComponentType << " not supported!" << std::endl;
//                 break;
//             }
//           } else {
//             vert.joint0 = glm::vec4(0.0f);
//           }
//           vert.weight0 = hasSkin ? glm::make_vec4(&bufferWeights[v * weightByteStride]) : glm::vec4(0.0f);
//           // Fix for all zero weights
//           if (glm::length(vert.weight0) == 0.0f) {
//             vert.weight0 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
//           }
//           loaderInfo.vertexPos++;
//         }
//       }
//       // Indices
//       if (hasIndices) {
//         const tinygltf::Accessor &accessor = model.accessors[primitive.indices > -1 ? primitive.indices : 0];
//         const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
//         const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

//         indexCount = static_cast<uint32_t>(accessor.count);
//         const void *dataPtr = &(buffer.data[accessor.byteOffset + bufferView.byteOffset]);

//         switch (accessor.componentType) {
//           case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
//             const uint32_t *buf = static_cast<const uint32_t *>(dataPtr);
//             for (size_t index = 0; index < accessor.count; index++) {
//               loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
//               loaderInfo.indexPos++;
//             }
//             break;
//           }
//           case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
//             const uint16_t *buf = static_cast<const uint16_t *>(dataPtr);
//             for (size_t index = 0; index < accessor.count; index++) {
//               loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
//               loaderInfo.indexPos++;
//             }
//             break;
//           }
//           case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
//             const uint8_t *buf = static_cast<const uint8_t *>(dataPtr);
//             for (size_t index = 0; index < accessor.count; index++) {
//               loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
//               loaderInfo.indexPos++;
//             }
//             break;
//           }
//           default:
//             std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
//             return;
//         }
//       }
//       Primitive *newPrimitive = new Primitive(indexStart, indexCount, vertexCount, primitive.material > -1 ? materials[primitive.material] : materials.back());
//       newPrimitive->setBoundingBox(posMin, posMax);
//       newMesh->primitives.push_back(newPrimitive);
//     }
//     // Mesh BB from BBs of primitives
//     for (auto p : newMesh->primitives) {
//       if (p->bb.valid && !newMesh->bb.valid) {
//         newMesh->bb = p->bb;
//         newMesh->bb.valid = true;
//       }
//       newMesh->bb.min = glm::min(newMesh->bb.min, p->bb.min);
//       newMesh->bb.max = glm::max(newMesh->bb.max, p->bb.max);
//     }
//     newNode->mesh = newMesh;
//   }
//   if (parent) {
//     parent->children.push_back(newNode);
//   } else {
//     nodes.push_back(newNode);
//   }
//   linearNodes.push_back(newNode);
// }