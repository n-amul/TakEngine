#pragma once

#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>

#include <string>
#include <vector>

#include "defines.hpp"
// -----------------------------------------------------------------------------
// VulkanBase
// -----------------------------------------------------------------------------

class GLFWwindow;

class TAK_API VulkanBase {
 public:
  void run();

 private:
  void initWindow();
  void initVulkan();
  void createInstance();
  void mainLoop();
  void cleanup();

  GLFWwindow* window = nullptr;
  u32 window_width = 1920;
  u32 window_height = 1080;
  std::string title = "Vulkan Engine";
  std::string name = "vulkanBase";

  VkInstance instance;

  //----------------------------- validation layer---------------------------
#ifdef NDEBUG
  const bool enableValidationLayers = false;
#else
  const bool enableValidationLayers = true;
#endif
  VkDebugUtilsMessengerEXT debugMessenger;

  const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};

  void setupDebugMessenger();
  bool checkValidationLayerSupport();
  std::vector<const char*> getRequiredExtensions();

  //-----------------------------Device pickup-----------------------
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device;
  void pickPhysicalDevice();
  void createLogicalDevice();
  VkQueue graphicsQueue;
};