#include <spdlog/spdlog.h>

#include <cstdlib>
#include <exception>

#include "scenes/ModelScene.hpp"
#include "scenes/TriangleScene.hpp"
#include "testing/ModelTest.hpp"

int main() {
  try {
    spdlog::info("Main function started");
    ModelScene test;
    test.run();
    // ModelScene modelScene;
    // modelScene.run();

    spdlog::info("Main function ending normally");
    return 0;

  } catch (const std::exception& e) {
    spdlog::error("Exception in main: {}", e.what());
    return 1;
  }
}