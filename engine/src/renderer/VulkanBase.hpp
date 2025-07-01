#pragma once

// ──────────────────────────────────────────────────────────────
// Standard library
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "CommandLineParser.hpp"
#include "VulkanBuffer.h"
#include "VulkanDebug.h"
#include "VulkanDevice.h"
#include "VulkanInitializers.hpp"
#include "VulkanSwapChain.h"
#include "VulkanTexture.h"
#include "VulkanTools.h"
#include "VulkanUIOverlay.h"
#include "benchmark.hpp"
#include "camera.hpp"
#include "keycodes.hpp"

class VulkanBase {
 private:
  // ──────────────── Window / input ────────────────
  GLFWwindow *window_{nullptr};
  uint32_t destWidth_{1280};
  uint32_t destHeight_{720};
  bool resizing_{false};

  static void framebufferResizeCallback(GLFWwindow *win, int w, int h) {}

  static void mouseMoveCallback(GLFWwindow *win, double x, double y) {}

  void handleMouseMove(int32_t x, int32_t y);

  // ──────────────── Vulkan helper ────────────────
  void createPipelineCache();
  void createCommandPool();
  void createSynchronizationPrimitives();
  void createSurface();  // Uses glfwCreateWindowSurface
  void createSwapChain();
  void createCommandBuffers();
  void destroyCommandBuffers();

  // Misc
  void nextFrame();
  void updateOverlay();

 protected:
  // ──────────────── Timing ────────────────
  uint32_t frameCounter{0};
  uint32_t lastFPS{0};
  std::chrono::time_point<std::chrono::high_resolution_clock> lastTimestamp;

  // ──────────────── Vulkan core objects ────────────────
  VkInstance instance{VK_NULL_HANDLE};
  VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
  VkPhysicalDeviceProperties deviceProperties{};
  VkPhysicalDeviceFeatures deviceFeatures{};
  VkPhysicalDeviceMemoryProperties deviceMemoryProperties{};
  VkDevice device{VK_NULL_HANDLE};
  VkQueue queue{VK_NULL_HANDLE};
  VkFormat depthFormat{VK_FORMAT_UNDEFINED};
  VkCommandPool cmdPool{VK_NULL_HANDLE};
  VkPipelineStageFlags submitPipelineStages{
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  std::vector<VkCommandBuffer> drawCmdBuffers;
  VkRenderPass renderPass{VK_NULL_HANDLE};
  std::vector<VkFramebuffer> frameBuffers;
  uint32_t currentBuffer{0};
  VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
  std::vector<VkShaderModule> shaderModules;
  VkPipelineCache pipelineCache{VK_NULL_HANDLE};
  VulkanSwapChain swapChain;

  struct {
    VkSemaphore presentComplete{VK_NULL_HANDLE};
    VkSemaphore renderComplete{VK_NULL_HANDLE};
  } semaphores;

  std::vector<VkFence> waitFences;

  // ──────────────── Members preserved from original ────────────────
  bool requiresStencil{false};
  bool prepared{false};
  bool resized{false};
  bool viewUpdated{false};
  uint32_t width{1280};
  uint32_t height{720};
  float frameTimer{1.0f};
  float timer{0.0f};
  float timerSpeed{0.25f};
  bool paused{false};

  vks::UIOverlay ui;
  vks::Benchmark benchmark;
  vks::VulkanDevice *vulkanDevice{nullptr};
  Camera camera;

  std::string title{"Vulkan Example"};
  std::string name{"vulkanExample"};
  uint32_t apiVersion{VK_API_VERSION_1_0};

  struct {
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
  } depthStencil;

  VkClearColorValue defaultClearColor{{0.025f, 0.025f, 0.025f, 1.0f}};

  // ──────────────── Settings (unchanged) ────────────────
  struct Settings {
    bool validation{false};
    bool fullscreen{false};
    bool vsync{false};
    bool overlay{true};
  } settings;

 public:
  static std::vector<const char *> args;

  VulkanBase();
  virtual ~VulkanBase();

  // ──────────────── Initialisation ────────────────
  bool initVulkan();  // sets up instance, device, swapchain, etc.
  void initWindow();  // creates GLFW window & surface

  // ──────────────── Main loop ────────────────
  void renderLoop();
  virtual void renderFrame();  // default acquire‑submit‑present

  // ──────────────── Sample hooks ────────────────
  virtual void render() = 0;           // draw contents
  virtual void buildCommandBuffers();  // build drawCmdBuffers (swapchain‑size)
  virtual void windowResized();        // recreate resources after resize
  virtual void setupRenderPass();      // override to create custom renderpass
  virtual void setupFrameBuffer();   // create framebuffers per‑swapchain‑image
  virtual void setupDepthStencil();  // create default depth stencil image
  virtual void prepare();            // load assets, etc.
  virtual void
  getEnabledFeatures();  // enable device features before device creation
  virtual void
  getEnabledExtensions();  // enable device extensions before device creation

  // ──────────────── Input hooks ────────────────
  virtual void keyPressed(int key) { (void)key; }
  virtual void mouseMoved(double x, double y, bool &handled) {
    (void)x;
    (void)y;
    handled = false;
  }
  virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay) { (void)overlay; }

  // ──────────────── Helpers ────────────────
  VkPipelineShaderStageCreateInfo loadShader(const std::string &file,
                                             VkShaderStageFlagBits stage);
};
// clang‑format on
