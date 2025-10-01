#include <spdlog/spdlog.h>

#include <cstdlib>
#include <exception>

#include "scenes/TriangleScene.hpp"
#include "testing/ModelTest.hpp"

int main() {
  try {
    spdlog::info("Main function started");

    ModelTest modelTest;
    modelTest.run();  // This calls initWindow(), initVulkan(), mainLoop(), cleanup()

    spdlog::info("Main function ending normally");
    return 0;

  } catch (const std::exception& e) {
    spdlog::error("Exception in main: {}", e.what());
    return 1;
  }
}