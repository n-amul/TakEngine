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
#include "ui.hpp"

class GLFWwindow;

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};
/*
  PASS 1: G-BUFFER (Geometry Pass) -> PASS 2: SSAO -> PASS 3: LIGHTING -> PASS 4: SKYBOX ->PASS 5: TRANSPARENT
  -> PASS 6: POST-PROCESSING
*/
class TAK_API VulkanDeferredBase {
 public:
  virtual ~VulkanDeferredBase() = default;
  void run();

 protected:
  // Pure virtual methods that derived classes must implement
  virtual void createGeometryPipeline() = 0;  // pass1
  // virtual void createssaoPipeline() = 0;      // 2
  // virtual void createLightingPipeline() = 0;  // 3
  // virtual void createSkyboxPipeline() = 0;
  // virtual void createTransparentPipeline() = 0;
  // virtual void createPostProcessingPipelines() = 0;  // bloomPipeline,toneMappingPipeline,fxaaPipeline
  // Core pipelines
  VkPipeline gBufferPipeline;   // MRT output, depth write
  VkPipeline lightingPipeline;  // Fullscreen, reads G-buffer + IBL
  VkPipeline skyboxPipeline;    // Depth test ≤, no depth write

  // Optional but common
  VkPipeline ssaoPipeline;         // Fullscreen
  VkPipeline ssaoBlurPipeline;     // Fullscreen
  VkPipeline transparentPipeline;  // Forward, blending, depth read-only

  // Post-processing
  VkPipeline bloomDownsamplePipeline;
  VkPipeline bloomUpsamplePipeline;
  VkPipeline toneMappingPipeline;  // Includes gamma correction
  VkPipeline fxaaPipeline;         // Or TAA

  virtual void loadResources() = 0;
  virtual void recordGeometryCommands(VkCommandBuffer commandBuffer) = 0;
  // virtual void recordLightingCommands(VkCommandBuffer commandBuffer) = 0;
  virtual void cleanupResources() = 0;

  // Optional virtual methods
  virtual void updateScene(float deltaTime) {}
  virtual void onResize(int width, int height) {}
  virtual void onKeyEvent(int key, int scancode, int action, int mods) {}
  virtual void onMouseMove(double xpos, double ypos) {}
  virtual void onMouseButton(int button, int action, int mods) {};

  virtual void initWindow();
  virtual void initVulkan();

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
  void createRenderPasses();
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

  // Deferred rendering specific
  void createGBuffer();
  void cleanupGBuffer();

  // Protected members accessible to derived classes
  GLFWwindow* window = nullptr;
  u32 window_width = 1920;
  u32 window_height = 1080;
  std::string title = "Vulkan Deferred Renderer";
  std::string name = "vulkanDeferredBase";
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

  // Render passes for deferred rendering
  VkRenderPass geometryRenderPass;
  VkRenderPass lightingRenderPass;

  // Framebuffers
  std::vector<VkFramebuffer> geometryFramebuffers;
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

  TextureManager::Texture depthBuffer;  //
  // G-Buffer components
  struct GBuffer {
    TextureManager::Texture normal;    // RGB=normal (encoded), A=metallic
    TextureManager::Texture albedo;    // RGB=albedo, A=AO
    TextureManager::Texture material;  // R=roughness, GBA=emissive

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    std::vector<VkDescriptorSet> descriptorSets;  // One per swap chain image
  } gBuffer;

  // Camera system
  QuaternionCamera camera;

  // Input state
  bool firstMouse = true;
  double lastX = 0.0;
  double lastY = 0.0;
  bool mouseCaptured = true;

  VkPipelineShaderStageCreateInfo loadShader(std::string filename, VkShaderStageFlagBits shaderStage);

  // Full-screen quad for lighting pass
  struct FullscreenQuad {
    BufferManager::Buffer vertexBuffer;
    BufferManager::Buffer indexBuffer;
    u32 indexCount;
  } fullscreenQuad;

  void createFullscreenQuad();

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
  static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                      VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                      void* pUserData);

  static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
  static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
  static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
  static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
  static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
  static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
};