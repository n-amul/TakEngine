#pragma once

#include "defines.hpp"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>
// -----------------------------------------------------------------------------
// VulkanBase
// -----------------------------------------------------------------------------

class GLFWwindow;

class TAK_API VulkanBase{
public:
  void run();
  void test();
private:
  void InitVulkan();
  void mainLoop();
  void cleanup();

  GLFWwindow* window = nullptr;
  u32 window_width = 0;
  u32 window_height = 0;
  std::string title = "Vulkan Base";
  std::string name = "vulkanBase";
};