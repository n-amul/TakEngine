#pragma once

#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>

#include <optional>
#include <string>
#include <vector>

#include "defines.hpp"
// -----------------------------------------------------------------------------
// VulkanBase
// -----------------------------------------------------------------------------

class GLFWwindow;

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};

class TAK_API VulkanBase {
 public:
  void run();

 private:
  void initWindow();
  void initVulkan();
  void createInstance();
  void createSurface();
  void mainLoop();
  void cleanup();

  std::vector<const char*> getRequiredExtensions();

  GLFWwindow* window = nullptr;
  u32 window_width = 1920;
  u32 window_height = 1080;
  std::string title = "Vulkan Engine";
  std::string name = "vulkanBase";

  VkInstance instance;
  VkSurfaceKHR surface;

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

  //-----------------------------Device pickup-----------------------
  const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device;
  void pickPhysicalDevice();
  void createLogicalDevice();
  std::optional<uint32_t> findQueueFamilies(VkPhysicalDevice device);
  bool isDeviceSuitable(VkPhysicalDevice device);
  bool checkDeviceExtensionSupport(VkPhysicalDevice device);

  VkQueue graphicsQueue;
  VkQueue presentQueue;
  //---------------------------swapchain & image view-------------------------
  SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
  VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
  VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
  void createSwapChain();
  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;

  std::vector<VkImageView> swapChainImageViews;
  void createImageViews();
  //-------------------------graphics pipline-----------------------------------
  void createGraphicsPipeline();
  VkShaderModule createShaderModule(const std::vector<char>& code);
  VkPipelineLayout pipelineLayout;
  VkPipeline graphicsPipeline;
  //-----------------------renderpass--------------------
  void createRenderPass();
  VkRenderPass renderPass;

  //---------------------frame buffer-----------
  /*
    Swapchain gives you an image index.
    Use the framebuffer that wraps the matching image view.
    The render pass + pipeline decide how that framebuffer is drawn into.
  */
  std::vector<VkFramebuffer> swapChainFramebuffers;
  void createFramebuffers();

  VkCommandPool commandPool;  // memory manager for command buffers
  void createCommandPool();
  VkCommandBuffer commandBuffer;
  void createCommandBuffer();
  void recordCommandBuffer(VkCommandBuffer commandBuffer, u32 imageIndex);
};