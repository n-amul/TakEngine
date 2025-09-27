#include "ModelManager.hpp"

#include <spdlog/spdlog.h>

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

  Model::LoaderInfo loaderInfo{};
  size_t vertexCount = 0;
  size_t indexCount = 0;

  if (fileLoaded) {
    model.extensions = gltfModel.extensionsUsed;
    for (auto& extension : model.extensions) {
      // If this model uses basis universal compressed textures, we need to transcode them
      if (extension == "KHR_texture_basisu") {
        spdlog::info("Model uses KHR_texture_basisu, initializing basisu transcoder");
        basist::basisu_transcoder_init();
      }
    }
    // load resources
    // sampler
    loadTextures(model, gltfModel);
    //...
  }
  return model;
}

void ModelManager::destroyModel(Model* model) {}

void ModelManager::loadTextures(Model& model, tinygltf::Model& gltfModel) {
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
    model.textures.push_back(textureManager->createTextureFromGLTFImage(image, model.filePath, model.textureSamplers[tex.sampler], context->graphicsQueue));
  }
}
