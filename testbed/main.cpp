#include <spdlog/spdlog.h>

#include <cstdlib>
#include <exception>

#include "scenes/ModelScene.hpp"
#include "scenes/PBRIBLScene.hpp"
#include "scenes/TriangleScene.hpp"
#include "testing/ModelTest.hpp"

int main(int argc, char* argv[]) {
  try {
    spdlog::info("Main started");

    int selected = 3;  // default to PBRIBLScene
    if (argc == 2) {
      if (std::strlen(argv[1]) == 1 && std::isdigit(argv[1][0])) {
        selected = argv[1][0] - '0';
      } else {
        spdlog::warn("Invalid input. Please choose 1, 2, or 3.");
        return 0;
      }
    } else if (argc > 2) {
      spdlog::warn("Too many arguments. Choose only 1 number (1-3).");
      return 0;
    }
    VulkanBase* test = nullptr;
    switch (selected) {
      case 1:
        test = new TriangleScene();
        break;
      case 2:
        test = new ModelTest();
        break;
      case 3:
        test = new PBRIBLScene();
        break;
      default:
        spdlog::warn("Unknown option. Choose 1, 2, or 3.");
        return 0;
    }

    test->run();
    delete test;

    spdlog::info("Main ended normally");
    return 0;

  } catch (const std::exception& e) {
    spdlog::error("Exception in main: {}", e.what());
    return 1;
  }
}