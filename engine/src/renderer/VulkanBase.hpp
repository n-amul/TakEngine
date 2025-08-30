#pragma once

#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "BufferManager.hpp"
#include "CommandBufferUtils.hpp"
#include "VulkanContext.hpp"
#include "defines.hpp"

class GLFWwindow;

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};
/*
struture of this class
Generic Vulkan Boilerplate
  Instance (VkInstance)
  Physical Device (VkPhysicalDevice)
  Logical Device (VkDevice)
  Surface (VkSurfaceKHR)
  Swapchain (VkSwapchainKHR)
  Swapchain Images (obtained from swapchain)
  Swapchain Image Views (VkImageView)
  Render Pass (VkRenderPass)
  Framebuffers (VkFramebuffer)
  Command Pool (VkCommandPool)
  Command Buffers (VkCommandBuffer)
  Queue (VkQueue)
  Semaphores (VkSemaphore)
  Fences (VkFence)
  Events (VkEvent)
  Pipeline Layout (VkPipelineLayout) - could be shared
  Descriptor Set Layout (VkDescriptorSetLayout) - could be shared
  Descriptor Pool (VkDescriptorPool)
  Pipeline Cache (VkPipelineCache)
  Debug Utils Messenger (VkDebugUtilsMessengerEXT)
  Validation Layers
Child-Specific Objects (Per-Model/Per-Material/Per-Draw)
  Descriptor Sets (VkDescriptorSet)
  Buffers (VkBuffer)
  Vertex Buffers
  Index Buffers
  Uniform Buffers
  Storage Buffers
  Images/Textures (VkImage)
  Image Views (VkImageView)
  Buffer Views (VkBufferView)
  Samplers (VkSampler) - could be shared
  Pipeline (VkPipeline)
  Shader Modules (VkShaderModule)
*/
class TAK_API VulkanBase {
 public:
  virtual ~VulkanBase() = default;
  void run();

 protected:
  // Pure virtual methods that derived classes must implement
  virtual void createPipeline() = 0;
  virtual void loadResources() = 0;
  virtual void recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t imageIndex) = 0;
  virtual void cleanupResources() = 0;

  // Optional virtual methods
  virtual void updateScene(float deltaTime) {}
  virtual void onResize(int width, int height) {}

  virtual void initWindow();
  virtual void initVulkan();

  // Helper functions accessible to derived classes
  VkShaderModule createShaderModule(const std::vector<char>& code);

  // Main loop and drawing
  void mainLoop();
  void drawFrame();
  void cleanup();

  // Core Vulkan setup methods
  void createInstance();
  void createSurface();
  void createSwapChain();
  void createImageViews();
  void createRenderPass();  // need revise for complex scenes
  void createFramebuffers();
  void createCommandPool();
  void createCommandBuffers();
  void createSyncObjects();
  void recreateSwapChain();
  void cleanupSwapChain();

  void recordCommandBuffer(VkCommandBuffer commandBuffer, u32 imageIndex);

  // Device selection
  void pickPhysicalDevice();
  void createLogicalDevice();
  std::optional<uint32_t> findQueueFamilies(VkPhysicalDevice device);
  bool isDeviceSuitable(VkPhysicalDevice device);
  bool checkDeviceExtensionSupport(VkPhysicalDevice device);

  // Swap chain support
  SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
  VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
  VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

  // Extensions
  std::vector<const char*> getRequiredExtensions();

  // Protected members accessible to derived classes
  GLFWwindow* window = nullptr;
  u32 window_width = 1920;
  u32 window_height = 1080;
  std::string title = "Vulkan Engine";
  std::string name = "vulkanBase";
  std::chrono::high_resolution_clock::time_point lastFrameTime;

  std::shared_ptr<VulkanContext> context;
  std::shared_ptr<CommandBufferUtils> cmdUtils;
  std::shared_ptr<BufferManager> bufferManager;

  VkInstance instance;
  VkSurfaceKHR surface;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device;
  VkQueue graphicsQueue;
  VkQueue presentQueue;

  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;
  std::vector<VkImageView> swapChainImageViews;

  VkRenderPass renderPass;
  std::vector<VkFramebuffer> swapChainFramebuffers;

  VkCommandPool commandPool;           // For main rendering
  VkCommandPool transientCommandPool;  // For short-lived operations
  std::vector<VkCommandBuffer> commandBuffers;

  // Synchronization
  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;

  const int MAX_FRAMES_IN_FLIGHT = 2;
  u32 currentFrame = 0;
  bool framebufferResized = false;

  // Validation layers
#ifdef NDEBUG
  const bool enableValidationLayers = false;
#else
  const bool enableValidationLayers = true;
#endif

  VkDebugUtilsMessengerEXT debugMessenger;
  const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};
  const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

 private:
  // Validation layer setup
  void setupDebugMessenger();
  bool checkValidationLayerSupport();

  // Callbacks
  static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

  static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
  static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};