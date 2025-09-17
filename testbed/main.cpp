#include <spdlog/spdlog.h>

#include <cstdlib>  // For EXIT_SUCCESS / EXIT_FAILURE
#include <exception>

#include "scenes/TriangleScene.hpp"

int main() {
  try {
    // TODO:
    //  ModelLoadTesting test;
    //  test.runAllTests();
    TriangleScene app;
    app.run();
  } catch (const std::exception& e) {
    SPDLOG_ERROR("Unhandled exception: {}", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}