#include <iostream>
#include <stdexcept>

#include <GLFW/glfw3.h>   // Be sure your include path matches
#include "renderer/VulkanBase.hpp"

int main() {
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW\n";
    return EXIT_FAILURE;
  }

  try {
    // 2. Create and test the VulkanBase instance
    VulkanBase app;
    app.test();            // Calls InitVulkan() → prints "test passed"
  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << '\n';
    glfwTerminate();
    return EXIT_FAILURE;
  }

  // 3. Clean up GLFW and exit
  glfwTerminate();
  return EXIT_SUCCESS;
}