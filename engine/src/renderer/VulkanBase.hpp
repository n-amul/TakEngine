#pragma once

#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>

#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "BufferManager.hpp"
#include "CommandBufferUtils.hpp"
#include "ModelManager.hpp"
#include "TextureManager.hpp"
#include "VulkanContext.hpp"
#include "core/QuaternionCamera.hpp"
#include "defines.hpp"

class GLFWwindow;

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};

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
  virtual void onKeyEvent(int key, int scancode, int action, int mods) {}  // Optional key handling
  virtual void onMouseMove(double xpos, double ypos) {}                    // Optional mouse handling

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
  void createRenderPass();
  void createFramebuffers();
  void createCommandPool();
  void createCommandBuffers();
  void createDepthResources();
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

  // Input handling
  void processInput(float deltaTime);

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
  std::shared_ptr<TextureManager> textureManager;
  std::shared_ptr<ModelManager> modelManager;

  VkInstance instance;
  VkSurfaceKHR surface;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device;
  VkQueue graphicsQueue;
  VkQueue presentQueue;
  VkPhysicalDeviceFeatures deviceFeatures;
  u32 queueFamilyIndex = UINT32_MAX;

  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;
  std::vector<VkImageView> swapChainImageViews;

  VkRenderPass renderPass;
  std::vector<VkFramebuffer> swapChainFramebuffers;

  VkCommandPool commandPool;
  VkCommandPool transientCommandPool;
  std::vector<VkCommandBuffer> commandBuffers;

  // Synchronization
  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;

  const int MAX_FRAMES_IN_FLIGHT = 2;
  u32 currentFrame = 0;
  bool framebufferResized = false;

  TextureManager::Texture depthBuffer;

  // Camera system
  QuaternionCamera camera;

  // Input state
  bool firstMouse = true;
  double lastX = 0.0;
  double lastY = 0.0;
  bool mouseCaptured = true;  // Start with mouse captured

  // Validation layers
#ifdef NDEBUG
  const bool enableValidationLayers = false;
#else
  const bool enableValidationLayers = true;
#endif

  VkDebugUtilsMessengerEXT debugMessenger;
  const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};
  const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  // Skybox specific objs
  VkDescriptorSetLayout skyboxDescriptorSetLayout;
  std::vector<VkDescriptorSet> skyboxDescriptorSets;
  VkPipeline skyboxPipeline = VK_NULL_HANDLE;
  VkPipelineLayout skyboxPipelineLayout = VK_NULL_HANDLE;
  BufferManager::Buffer skyboxVertexBuffer;
  BufferManager::Buffer skyboxIndexBuffer;
  std::vector<BufferManager::Buffer> skyboxUniformBuffers;
  std::vector<void*> skyboxUniformBuffersMapped;
  TextureManager::Texture skyboxTexture;
  struct SkyboxUniformBufferObject {
    glm::mat4 view;  // View matrix with translation removed
    glm::mat4 proj;
  };
  // pos only
  struct SkyboxVertex {
    glm::vec3 pos;
    static VkVertexInputBindingDescription getBindingDescription() {
      VkVertexInputBindingDescription bindingDescription{};
      bindingDescription.binding = 0;
      bindingDescription.stride = sizeof(SkyboxVertex);
      bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
      return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 1> getAttributeDescriptions() {
      std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions{};
      attributeDescriptions[0].binding = 0;
      attributeDescriptions[0].location = 0;
      attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
      attributeDescriptions[0].offset = offsetof(SkyboxVertex, pos);
      return attributeDescriptions;
    }
  };
  // z-up system (x-right, y-forward, z-up) - corrected for front face culling
  const std::vector<SkyboxVertex> skyboxVertices = {
      {{-1.0f, -1.0f, -1.0f}},  // 0: left-back-bottom
      {{-1.0f, 1.0f, -1.0f}},   // 1: left-front-bottom
      {{1.0f, 1.0f, -1.0f}},    // 2: right-front-bottom
      {{1.0f, -1.0f, -1.0f}},   // 3: right-back-bottom
      {{-1.0f, -1.0f, 1.0f}},   // 4: left-back-top
      {{-1.0f, 1.0f, 1.0f}},    // 5: left-front-top
      {{1.0f, 1.0f, 1.0f}},     // 6: right-front-top
      {{1.0f, -1.0f, 1.0f}}     // 7: right-back-top
  };

  const std::vector<uint16_t> skyboxIndices = {
      // Bottom face (z = -1) - reverse winding for front culling
      0, 1, 2, 2, 3, 0,

      // Top face (z = 1)
      4, 7, 6, 6, 5, 4,

      // Back face (y = -1)
      0, 3, 7, 7, 4, 0,

      // Front face (y = 1)
      1, 5, 6, 6, 2, 1,

      // Left face (x = -1)
      0, 4, 5, 5, 1, 0,

      // Right face (x = 1)
      2, 6, 7, 7, 3, 2};

 private:
  // Validation layer setup
  void setupDebugMessenger();
  bool checkValidationLayerSupport();

  // Callbacks
  static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

  static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
  static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
  static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
  static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
  static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
  static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
};