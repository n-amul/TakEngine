#include "ModelManager.hpp"

#include <spdlog/spdlog.h>

#include <stdexcept>

ModelManager::Model ModelManager::createModelFromFile(const std::string& filename, float scale) {
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

  auto loadImageDataFunc = [](tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, int req_width, int req_height,
                              const unsigned char* bytes, int size, void* userData) -> bool {
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
    throw std::runtime_error("Could not load gltf file: " + error);
  }

  model.extensions = gltfModel.extensionsUsed;
  for (auto& extension : model.extensions) {
    // If this model uses basis universal compressed textures, we need to transcode them
    if (extension == "KHR_texture_basisu") {
      spdlog::info("Model uses KHR_texture_basisu, KTX will handle transcoding");
      // KTX library handles basis universal internally, no initialization needed
    }
  }
  // load texture,sampler, and materials
  loadTextures(model, gltfModel);
  loadMaterials(model, gltfModel);
  // load node
  tak::LoaderInfo loaderInfo{};
  size_t vertexCount = 0;
  size_t indexCount = 0;
  const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
  // Get vertex and index buffer sizes up-front
  for (size_t i = 0; i < scene.nodes.size(); i++) {
    getNodeVertexCounts(gltfModel.nodes[scene.nodes[i]], gltfModel, vertexCount, indexCount);
  }
  spdlog::info("vectexCount:{} indexCount{}", vertexCount, indexCount);
  loaderInfo.vertexBuffer.resize(vertexCount);
  loaderInfo.indexBuffer.resize(indexCount);
  // no default scene handle
  for (size_t i = 0; i < scene.nodes.size(); i++) {
    const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
    loadNode(nullptr, node, scene.nodes[i], model, gltfModel, loaderInfo, scale);
  }
  spdlog::info("# of nodes: {}", model.nodes.size());
  spdlog::info("# of linear nodes: {}", model.linearNodes.size());

  // animation
  if (gltfModel.animations.size() > 0) {
    loadAnimations(model, gltfModel);
  }
  loadSkins(model, gltfModel);
  spdlog::info("# of animation: {}", model.animations.size());
  spdlog::info("# of skins: {}", model.skins.size());
  uint32_t meshIndex = 0;
  for (auto node : model.linearNodes) {
    // Assign skins
    if (node->skinIndex > -1) {
      node->skin = model.skins[node->skinIndex];
    }
    // Initial pose
    if (node->mesh) {
      node->mesh->index = meshIndex++;
      node->update();
    }
  }
  // fill vertex buffer
  size_t vertexBufferSize = vertexCount * sizeof(tak::Vertex);
  size_t indexBufferSize = indexCount * sizeof(uint32_t);
  assert(vertexBufferSize > 0);
  // gpu local buffer (TODO: make this batch to use one command buffer)
  model.vertices = bufferManager->createGPULocalBuffer(loaderInfo.vertexBuffer.data(), vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  model.indices = bufferManager->createGPULocalBuffer(loaderInfo.indexBuffer.data(), indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

  getSceneDimensions(model);

  return model;
}

void ModelManager::updateAnimation(ModelManager::Model& model, int index, float time) {
  if (model.animations.empty()) {
    spdlog::info(".glTF does not contain animation.");
    return;
  }
  if (index > static_cast<uint32_t>(model.animations.size()) - 1) {
    spdlog::info("No animation with index {}", index);
    return;
  }
  tak::Animation& animation = model.animations[index];

  bool updated = false;
  for (auto& channel : animation.channels) {
    tak::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
    if (sampler.inputs.size() > sampler.outputsVec4.size()) {
      continue;
    }

    for (size_t i = 0; i < sampler.inputs.size() - 1; i++) {
      if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
        float u = std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
        if (u <= 1.0f) {
          switch (channel.path) {
            case tak::AnimationChannel::PathType::TRANSLATION:
              sampler.translate(i, time, channel.node);
              break;
            case tak::AnimationChannel::PathType::SCALE:
              sampler.scale(i, time, channel.node);
              break;
            case tak::AnimationChannel::PathType::ROTATION:
              sampler.rotate(i, time, channel.node);
              break;
          }
          updated = true;
        }
      }
    }
  }
  if (updated) {
    for (auto& node : model.nodes) {
      node->update();
    }
  }
}

void ModelManager::loadTextures(Model& model, tinygltf::Model& gltfModel) {
  // samplers
  model.textureSamplers = textureManager->loadTextureSamplers(gltfModel);
  // textures
  for (tinygltf::Texture& tex : gltfModel.textures) {
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
    if (tex.sampler > -1) {
      model.textures.push_back(textureManager->createTextureFromGLTFImage(image, model.filePath, model.textureSamplers[tex.sampler], context->graphicsQueue));
    } else {
      TextureManager::TextureSampler texSamplerDefault = {VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                                          VK_SAMPLER_ADDRESS_MODE_REPEAT};
      model.textures.push_back(textureManager->createTextureFromGLTFImage(image, model.filePath, texSamplerDefault, context->graphicsQueue));
    }
  }
}

void ModelManager::loadMaterials(Model& model, tinygltf::Model& gltfModel) {
  // Create materials from glTF model
  for (tinygltf::Material& mat : gltfModel.materials) {
    tak::Material material{};
    material.doubleSided = mat.doubleSided;
    if (mat.alphaMode == "OPAQUE") {
      material.alphaMode = tak::Material::ALPHAMODE_OPAQUE;
    } else if (mat.alphaMode == "MASK") {
      material.alphaMode = tak::Material::ALPHAMODE_MASK;
      material.alphaCutoff = 0.5f;
    } else if (mat.alphaMode == "BLEND") {
      material.alphaMode = tak::Material::ALPHAMODE_BLEND;
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
      spdlog::info("unlit");
      material.unlit = true;
    }
    // Check for KHR_materials_pbrSpecularGlossiness extension (alternative workflow)
    if (mat.extensions.find("KHR_materials_pbrSpecularGlossiness") != mat.extensions.end()) {
      spdlog::info("KHR_materials_pbrSpecularGlossiness");
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
    tak::Material defaultMaterial{};
    defaultMaterial.metallicFactor = 0.0f;
    defaultMaterial.roughnessFactor = 1.0f;
    defaultMaterial.baseColorFactor = glm::vec4(1.0f);
    defaultMaterial.materialIndex = 0;
    model.materials.push_back(defaultMaterial);
    spdlog::info("No materials defined in model, using default material");
  }

  spdlog::info("Loaded {} materials", model.materials.size());
}

void ModelManager::loadAnimations(Model& model, tinygltf::Model& gltfModel) {
  for (tinygltf::Animation& anim : gltfModel.animations) {
    tak::Animation animation{};
    animation.name = anim.name;
    if (anim.name.empty()) {
      animation.name = std::to_string(model.animations.size());
    }
    // Samplers
    for (auto& samp : anim.samplers) {
      tak::AnimationSampler sampler{};

      if (samp.interpolation == "LINEAR") {
        sampler.interpolation = tak::AnimationSampler::InterpolationType::LINEAR;
      } else if (samp.interpolation == "STEP") {
        sampler.interpolation = tak::AnimationSampler::InterpolationType::STEP;
      } else if (samp.interpolation == "CUBICSPLINE") {
        sampler.interpolation = tak::AnimationSampler::InterpolationType::CUBICSPLINE;
      }
      // Read sampler input time values
      {
        const tinygltf::Accessor& accessor = gltfModel.accessors[samp.input];
        const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

        assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

        const void* dataPtr = &buffer.data[accessor.byteOffset + bufferView.byteOffset];
        const float* buf = static_cast<const float*>(dataPtr);
        for (size_t index = 0; index < accessor.count; index++) {
          sampler.inputs.push_back(buf[index]);
        }
        for (auto input : sampler.inputs) {
          if (input < animation.start) {
            animation.start = input;
          };
          if (input > animation.end) {
            animation.end = input;
          }
        }
      }
      // Read sampler output T/R/S values
      {
        const tinygltf::Accessor& accessor = gltfModel.accessors[samp.output];
        const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

        assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

        const void* dataPtr = &buffer.data[accessor.byteOffset + bufferView.byteOffset];

        switch (accessor.type) {
          case TINYGLTF_TYPE_VEC3: {
            const glm::vec3* buf = static_cast<const glm::vec3*>(dataPtr);
            for (size_t index = 0; index < accessor.count; index++) {
              sampler.outputsVec4.push_back(glm::vec4(buf[index], 0.0f));
              sampler.outputs.push_back(buf[index][0]);
              sampler.outputs.push_back(buf[index][1]);
              sampler.outputs.push_back(buf[index][2]);
            }
            break;
          }
          case TINYGLTF_TYPE_VEC4: {
            const glm::vec4* buf = static_cast<const glm::vec4*>(dataPtr);
            for (size_t index = 0; index < accessor.count; index++) {
              sampler.outputsVec4.push_back(buf[index]);
              sampler.outputs.push_back(buf[index][0]);
              sampler.outputs.push_back(buf[index][1]);
              sampler.outputs.push_back(buf[index][2]);
              sampler.outputs.push_back(buf[index][3]);
            }
            break;
          }
          default: {
            spdlog::info("unknown type");
            break;
          }
        }
      }

      animation.samplers.push_back(sampler);
    }

    // Channels
    for (auto& source : anim.channels) {
      tak::AnimationChannel channel{};

      if (source.target_path == "rotation") {
        channel.path = tak::AnimationChannel::PathType::ROTATION;
      }
      if (source.target_path == "translation") {
        channel.path = tak::AnimationChannel::PathType::TRANSLATION;
      }
      if (source.target_path == "scale") {
        channel.path = tak::AnimationChannel::PathType::SCALE;
      }
      if (source.target_path == "weights") {
        spdlog::info("weights not yet supported, skipping channel");
        continue;
      }
      channel.samplerIndex = source.sampler;
      channel.node = nodeFromIndex(source.target_node, model);
      if (!channel.node) {
        continue;
      }

      animation.channels.push_back(channel);
    }

    model.animations.push_back(animation);
  }
}

void ModelManager::getNodeVertexCounts(const tinygltf::Node& node, const tinygltf::Model& model, size_t& vertexCount, size_t& indexCount) {
  if (node.children.size() > 0) {
    for (size_t i = 0; i < node.children.size(); i++) {
      getNodeVertexCounts(model.nodes[node.children[i]], model, vertexCount, indexCount);
    }
  }
  if (node.mesh > -1) {
    const tinygltf::Mesh& mesh = model.meshes[node.mesh];
    for (size_t i = 0; i < mesh.primitives.size(); i++) {
      auto& primitive = mesh.primitives[i];
      vertexCount += model.accessors[primitive.attributes.find("POSITION")->second].count;
      if (primitive.indices > -1) {
        indexCount += model.accessors[primitive.indices].count;
      }
    }
  }
}

tak::Node* ModelManager::findNode(tak::Node* parent, uint32_t index) {
  tak::Node* nodeFound = nullptr;
  if (parent->index == index) {
    return parent;
  }
  for (auto& child : parent->children) {
    nodeFound = findNode(child, index);
    if (nodeFound) {
      break;
    }
  }
  return nodeFound;
}

tak::Node* ModelManager::nodeFromIndex(uint32_t index, const Model& model) {
  tak::Node* nodeFound = nullptr;
  for (auto& node : model.nodes) {
    nodeFound = findNode(node, index);
    if (nodeFound) {
      break;
    }
  }
  return nodeFound;
}

void ModelManager::getSceneDimensions(Model& model) {
  // Calculate binary volume hierarchy for all nodes in the scene
  for (auto node : model.linearNodes) {
    calculateBoundingBox(node, nullptr, model);
  }

  model.dimensions.min = glm::vec3(FLT_MAX);
  model.dimensions.max = glm::vec3(-FLT_MAX);

  for (auto node : model.linearNodes) {
    if (node->bvh.valid) {
      model.dimensions.min = glm::min(model.dimensions.min, node->bvh.min);
      model.dimensions.max = glm::max(model.dimensions.max, node->bvh.max);
    }
  }

  // Calculate scene aabb
  model.aabb = glm::scale(glm::mat4(1.0f), glm::vec3(model.dimensions.max[0] - model.dimensions.min[0], model.dimensions.max[1] - model.dimensions.min[1],
                                                     model.dimensions.max[2] - model.dimensions.min[2]));
  model.aabb[3][0] = model.dimensions.min[0];
  model.aabb[3][1] = model.dimensions.min[1];
  model.aabb[3][2] = model.dimensions.min[2];

  glm::vec3 size = model.dimensions.max - model.dimensions.min;
  glm::vec3 center = (model.dimensions.min + model.dimensions.max) * 0.5f;

  spdlog::info("Scene bounds: min=({:.2f}, {:.2f}, {:.2f}), max=({:.2f}, {:.2f}, {:.2f})", model.dimensions.min.x, model.dimensions.min.y, model.dimensions.min.z,
               model.dimensions.max.x, model.dimensions.max.y, model.dimensions.max.z);
  spdlog::info("Scene size: {:.2f} x {:.2f} x {:.2f}, center: ({:.2f}, {:.2f}, {:.2f})", size.x, size.y, size.z, center.x, center.y, center.z);
}

void ModelManager::calculateBoundingBox(tak::Node* node, tak::Node* parent, Model& model) {
  tak::BoundingBox parentBvh = parent ? parent->bvh : tak::BoundingBox(model.dimensions.min, model.dimensions.max);

  if (node->mesh) {
    if (node->mesh->bb.valid) {
      node->aabb = node->mesh->bb.getAABB(node->getMatrix());
      if (node->children.size() == 0) {
        node->bvh.min = node->aabb.min;
        node->bvh.max = node->aabb.max;
        node->bvh.valid = true;
      }
    }
  }

  parentBvh.min = glm::min(parentBvh.min, node->bvh.min);
  parentBvh.max = glm::min(parentBvh.max, node->bvh.max);

  for (auto& child : node->children) {
    calculateBoundingBox(child, node, model);
  }
}

void ModelManager::drawNode(tak::Node* node, VkCommandBuffer cmdbuf) {
  if (node->mesh) {
    for (tak::Primitive* primitive : node->mesh->primitives) {
      vkCmdDrawIndexed(cmdbuf, primitive->indexCount, 1, primitive->firstIndex, 0, 0);
    }
  }
  for (auto& child : node->children) {
    drawNode(child, cmdbuf);
  }
}

void ModelManager::loadNode(tak::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, Model& model, const tinygltf::Model& gltfModel, tak::LoaderInfo& loaderInfo,
                            float globalscale) {
  tak::Node* newNode = new tak::Node{};
  newNode->index = nodeIndex;
  newNode->parent = parent;
  newNode->name = node.name;
  newNode->skinIndex = node.skin;

  newNode->matrix = glm::mat4(1.0f);
  // Generate local node matrix
  glm::vec3 translation = glm::vec3(0.0f);
  if (node.translation.size() == 3) {
    translation = glm::make_vec3(node.translation.data());
    newNode->translation = translation;
  }
  glm::mat4 rotation = glm::mat4(1.0f);
  if (node.rotation.size() == 4) {
    glm::quat q = glm::make_quat(node.rotation.data());
    newNode->rotation = glm::mat4(q);
  }
  glm::vec3 scale = glm::vec3(1.0f);
  if (node.scale.size() == 3) {
    scale = glm::make_vec3(node.scale.data());
    newNode->scale = scale;
  }
  if (node.matrix.size() == 16) {
    newNode->matrix = glm::make_mat4x4(node.matrix.data());
  };

  // Node with children
  if (node.children.size() > 0) {
    for (size_t i = 0; i < node.children.size(); i++) {
      loadNode(newNode, gltfModel.nodes[node.children[i]], node.children[i], model, gltfModel, loaderInfo, globalscale);
    }
  }

  if (node.mesh > -1) {
    spdlog::info("Node '{}' has mesh index {}", node.name, node.mesh);
    const tinygltf::Mesh mesh = gltfModel.meshes[node.mesh];
    tak::Mesh* newMesh = new tak::Mesh(newNode->matrix);
    for (size_t i = 0; i < mesh.primitives.size(); i++) {
      const tinygltf::Primitive& primitive = mesh.primitives[i];
      uint32_t vertexStart = static_cast<uint32_t>(loaderInfo.vertexPos);
      uint32_t indexStart = static_cast<uint32_t>(loaderInfo.indexPos);
      uint32_t indexCount = 0;
      uint32_t vertexCount = 0;
      glm::vec3 posMin{};
      glm::vec3 posMax{};
      bool hasSkin = false;
      bool hasIndices = primitive.indices > -1;
      // Vertex
      {
        const float* bufferPos = nullptr;
        const float* bufferNormals = nullptr;
        const float* bufferTexCoordSet0 = nullptr;
        const float* bufferTexCoordSet1 = nullptr;
        const float* bufferColorSet0 = nullptr;
        const void* bufferJoints = nullptr;
        const float* bufferWeights = nullptr;
        const float* bufferTangent = nullptr;

        int posByteStride;
        int normByteStride;
        int uv0ByteStride;
        int uv1ByteStride;
        int color0ByteStride;
        int jointByteStride;
        int weightByteStride;
        int jointComponentType;
        int tanByteStride;

        // Position attribute is required
        assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

        const tinygltf::Accessor& posAccessor = gltfModel.accessors[primitive.attributes.find("POSITION")->second];
        const tinygltf::BufferView& posView = gltfModel.bufferViews[posAccessor.bufferView];
        bufferPos = reinterpret_cast<const float*>(&(gltfModel.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
        posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
        posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);
        vertexCount = static_cast<uint32_t>(posAccessor.count);
        posByteStride = posAccessor.ByteStride(posView) ? (posAccessor.ByteStride(posView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);

        if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
          const tinygltf::Accessor& normAccessor = gltfModel.accessors[primitive.attributes.find("NORMAL")->second];
          const tinygltf::BufferView& normView = gltfModel.bufferViews[normAccessor.bufferView];
          bufferNormals = reinterpret_cast<const float*>(&(gltfModel.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
          normByteStride = normAccessor.ByteStride(normView) ? (normAccessor.ByteStride(normView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
        }

        // UVs
        if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
          const tinygltf::Accessor& uvAccessor = gltfModel.accessors[primitive.attributes.find("TEXCOORD_0")->second];
          const tinygltf::BufferView& uvView = gltfModel.bufferViews[uvAccessor.bufferView];
          bufferTexCoordSet0 = reinterpret_cast<const float*>(&(gltfModel.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
          uv0ByteStride = uvAccessor.ByteStride(uvView) ? (uvAccessor.ByteStride(uvView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
        }
        if (primitive.attributes.find("TEXCOORD_1") != primitive.attributes.end()) {
          const tinygltf::Accessor& uvAccessor = gltfModel.accessors[primitive.attributes.find("TEXCOORD_1")->second];
          const tinygltf::BufferView& uvView = gltfModel.bufferViews[uvAccessor.bufferView];
          bufferTexCoordSet1 = reinterpret_cast<const float*>(&(gltfModel.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
          uv1ByteStride = uvAccessor.ByteStride(uvView) ? (uvAccessor.ByteStride(uvView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
        }
        // Vertex colors
        if (primitive.attributes.find("COLOR_0") != primitive.attributes.end()) {
          const tinygltf::Accessor& accessor = gltfModel.accessors[primitive.attributes.find("COLOR_0")->second];
          const tinygltf::BufferView& view = gltfModel.bufferViews[accessor.bufferView];
          bufferColorSet0 = reinterpret_cast<const float*>(&(gltfModel.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
          color0ByteStride = accessor.ByteStride(view) ? (accessor.ByteStride(view) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
        }

        // Skinning
        // Joints
        if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end()) {
          const tinygltf::Accessor& jointAccessor = gltfModel.accessors[primitive.attributes.find("JOINTS_0")->second];
          const tinygltf::BufferView& jointView = gltfModel.bufferViews[jointAccessor.bufferView];
          bufferJoints = &(gltfModel.buffers[jointView.buffer].data[jointAccessor.byteOffset + jointView.byteOffset]);
          jointComponentType = jointAccessor.componentType;
          jointByteStride = jointAccessor.ByteStride(jointView) ? (jointAccessor.ByteStride(jointView) / tinygltf::GetComponentSizeInBytes(jointComponentType))
                                                                : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
        }

        if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end()) {
          const tinygltf::Accessor& weightAccessor = gltfModel.accessors[primitive.attributes.find("WEIGHTS_0")->second];
          const tinygltf::BufferView& weightView = gltfModel.bufferViews[weightAccessor.bufferView];
          bufferWeights = reinterpret_cast<const float*>(&(gltfModel.buffers[weightView.buffer].data[weightAccessor.byteOffset + weightView.byteOffset]));
          weightByteStride =
              weightAccessor.ByteStride(weightView) ? (weightAccessor.ByteStride(weightView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
        }
        if (primitive.attributes.find("TANGENT") != primitive.attributes.end()) {
          const tinygltf::Accessor& tangentAccessor = gltfModel.accessors[primitive.attributes.find("TANGENT")->second];
          const tinygltf::BufferView& tangentView = gltfModel.bufferViews[tangentAccessor.bufferView];
          bufferTangent = reinterpret_cast<const float*>(&(gltfModel.buffers[tangentView.buffer].data[tangentAccessor.byteOffset + tangentView.byteOffset]));

          tanByteStride = tangentAccessor.ByteStride(tangentView) ? (tangentAccessor.ByteStride(tangentView) / sizeof(float))
                                                                  : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);  // 4 floats: x,y,z,w
        }

        hasSkin = (bufferJoints && bufferWeights);

        for (size_t v = 0; v < posAccessor.count; v++) {
          tak::Vertex& vert = loaderInfo.vertexBuffer[loaderInfo.vertexPos];
          vert.pos = glm::vec4(glm::make_vec3(&bufferPos[v * posByteStride]), 1.0f);
          vert.normal = glm::normalize(glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * normByteStride]) : glm::vec3(0.0f)));
          vert.uv0 = bufferTexCoordSet0 ? glm::make_vec2(&bufferTexCoordSet0[v * uv0ByteStride]) : glm::vec3(0.0f);
          vert.uv1 = bufferTexCoordSet1 ? glm::make_vec2(&bufferTexCoordSet1[v * uv1ByteStride]) : glm::vec3(0.0f);
          vert.color = bufferColorSet0 ? glm::make_vec4(&bufferColorSet0[v * color0ByteStride]) : glm::vec4(1.0f);
          vert.tangent = bufferTangent ? glm::make_vec4(&bufferTangent[v * tanByteStride]) : glm::vec4(1.0f);

          if (hasSkin) {
            switch (jointComponentType) {
              case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                const uint16_t* buf = static_cast<const uint16_t*>(bufferJoints);
                vert.joint0 = glm::uvec4(glm::make_vec4(&buf[v * jointByteStride]));
                break;
              }
              case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                const uint8_t* buf = static_cast<const uint8_t*>(bufferJoints);
                vert.joint0 = glm::vec4(glm::make_vec4(&buf[v * jointByteStride]));
                break;
              }
              default:
                // Not supported by spec
                spdlog::error("Joint component type {} not supported", jointComponentType);
                break;
            }
          } else {
            vert.joint0 = glm::vec4(0.0f);
          }
          vert.weight0 = hasSkin ? glm::make_vec4(&bufferWeights[v * weightByteStride]) : glm::vec4(0.0f);
          // Fix for all zero weights
          if (glm::length(vert.weight0) == 0.0f) {
            vert.weight0 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
          }
          loaderInfo.vertexPos++;
        }
      }
      // Indices
      if (hasIndices) {
        const tinygltf::Accessor& accessor = gltfModel.accessors[primitive.indices > -1 ? primitive.indices : 0];
        const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

        indexCount = static_cast<uint32_t>(accessor.count);
        const void* dataPtr = &(buffer.data[accessor.byteOffset + bufferView.byteOffset]);

        switch (accessor.componentType) {
          case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
            const uint32_t* buf = static_cast<const uint32_t*>(dataPtr);
            for (size_t index = 0; index < accessor.count; index++) {
              loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
              loaderInfo.indexPos++;
            }
            break;
          }
          case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
            const uint16_t* buf = static_cast<const uint16_t*>(dataPtr);
            for (size_t index = 0; index < accessor.count; index++) {
              loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
              loaderInfo.indexPos++;
            }
            break;
          }
          case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
            const uint8_t* buf = static_cast<const uint8_t*>(dataPtr);
            for (size_t index = 0; index < accessor.count; index++) {
              loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
              loaderInfo.indexPos++;
            }
            break;
          }
          default:
            spdlog::error("Index component type {} not supported", accessor.componentType);
            return;
        }
        spdlog::info("Primitive for mesh {}: vertexStart={}, indexStart={}, indexCount={}, vertexCount={}", mesh.name, vertexStart, indexStart, indexCount, vertexCount);
      }

      uint32_t materialIndex = primitive.material > -1 ? primitive.material : static_cast<uint32_t>(model.materials.size() - 1);
      tak::Primitive* newPrimitive = new tak::Primitive(indexStart, indexCount, vertexCount, materialIndex);
      newPrimitive->setBoundingBox(posMin, posMax);
      newMesh->primitives.push_back(newPrimitive);
    }
    // Mesh BB from BBs of primitives
    for (auto p : newMesh->primitives) {
      if (p->bb.valid && !newMesh->bb.valid) {
        newMesh->bb = p->bb;
        newMesh->bb.valid = true;
      }
      newMesh->bb.min = glm::min(newMesh->bb.min, p->bb.min);
      newMesh->bb.max = glm::max(newMesh->bb.max, p->bb.max);
    }
    newNode->mesh = newMesh;
    spdlog::info("Assigned mesh to node '{}' with {} primitives", node.name, newMesh->primitives.size());
  } else {
    spdlog::info("Node '{}' has no mesh (index: {})", node.name, node.mesh);
  }

  if (parent) {
    parent->children.push_back(newNode);
  } else {
    model.nodes.push_back(newNode);
  }
  model.linearNodes.push_back(newNode);
}

void ModelManager::loadSkins(Model& model, tinygltf::Model& gltfModel) {
  for (tinygltf::Skin& source : gltfModel.skins) {
    tak::Skin* newSkin = new tak::Skin{};
    newSkin->name = source.name;

    // Find skeleton root node
    if (source.skeleton > -1) {
      newSkin->skeletonRoot = nodeFromIndex(source.skeleton, model);
    }

    // Find joint nodes
    for (int jointIndex : source.joints) {
      tak::Node* node = nodeFromIndex(jointIndex, model);
      if (node) {
        newSkin->joints.push_back(nodeFromIndex(jointIndex, model));
      }
    }

    // Get inverse bind matrices from buffer
    if (source.inverseBindMatrices > -1) {
      const tinygltf::Accessor& accessor = gltfModel.accessors[source.inverseBindMatrices];
      const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
      const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];
      newSkin->inverseBindMatrices.resize(accessor.count);
      memcpy(newSkin->inverseBindMatrices.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::mat4));
    }

    if (newSkin->joints.size() > MAX_NUM_JOINTS) {
      spdlog::warn(
          "Skin {} has {} joints, which is higher than the supported maximum of {}. "
          "glTF scene may display wrong/incomplete",
          newSkin->name, newSkin->joints.size(), MAX_NUM_JOINTS);
    }

    model.skins.push_back(newSkin);
  }
}

void ModelManager::destroyModel(Model& model) {
  bufferManager->destroyBuffer(model.vertices);
  bufferManager->destroyBuffer(model.indices);
  for (int i = 0; i < model.textures.size(); i++) {
    textureManager->destroyTexture(model.textures[i]);
  }
  model.textureSamplers.resize(0);

  for (auto node : model.nodes) {
    delete node;  // children nodes deletes recursively
  }
  model.materials.resize(0);
  model.animations.resize(0);
  model.nodes.resize(0);
  model.linearNodes.resize(0);
  model.extensions.resize(0);
  for (auto skin : model.skins) {
    delete skin;
  }
  model.skins.resize(0);
};