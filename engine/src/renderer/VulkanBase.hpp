#pragma once

#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>

#include <array>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

#include "defines.hpp"
// -----------------------------------------------------------------------------
// VulkanBase
/*
  generic Vulkan boilerplate
  Window creation & resize handling

  Vulkan instance, surface, device & queues

  Swapchain + framebuffers

  Render pass (maybe basic one in base, overridable)

  Command pool, synchronization objects

  The main loop & frame presentation
*/
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
  void drawFrame();
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
  //--------------------------call backs-----------------------------------
  static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                      VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                      void* pUserData) {
    switch (messageSeverity) {
      case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        spdlog::error("validation layer: {}", pCallbackData->pMessage);
        break;
      case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        spdlog::warn("validation layer: {}", pCallbackData->pMessage);
        break;
      default:  // INFO & VERBOSE
        spdlog::info("validation layer: {}", pCallbackData->pMessage);
    }
    return VK_FALSE;
  }
  static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
  }
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
  void recreateSwapChain();
  void cleanupSwapChain();
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
  std::vector<VkCommandBuffer> commandBuffers;
  void createCommandBuffers();
  void recordCommandBuffer(VkCommandBuffer commandBuffer, u32 imageIndex);

  //---------------------sync objects----------------
  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;
  bool framebufferResized = false;
  const int MAX_FRAMES_IN_FLIGHT = 2;
  void createSyncObjects();
  u32 currentFrame = 0;
  //--------------------call backs----------------
  static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

  // ---------------------vertex buffer-----------------
  VkBuffer vertexBuffer;
  VkDeviceMemory vertexBufferMemory;
  void createVertexBuffer();
  struct Vertex {
    static VkVertexInputBindingDescription getBindingDescription() {
      VkVertexInputBindingDescription bindingDescription{};
      bindingDescription.binding = 0;
      bindingDescription.stride = sizeof(Vertex);
      bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

      return bindingDescription;
    }
    glm::vec2 pos;
    glm::vec3 color;
  };
  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
  // temporary data
  const std::vector<Vertex> vertices = {
      {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}}, {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}}, {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}};

  static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions();
};