#include <spdlog/spdlog.h>

#include <cstdlib>
#include <exception>

#include "scenes/TriangleScene.hpp"
#include "testing/ModelTest.hpp"

int main() {
  try {
    TriangleScene test;
    test.run();
  } catch (const std::exception& e) {
    spdlog::error("Application error: {}", e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}