#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "core/kmemory.hpp"

int main() {
  // memory system
  memory_init();  // Initialize tracking

  // Allocate memory with different tags
  void* array_block = memory_alloc(128, MEMORY_TAG_ARRAY);
  void* string_block = memory_alloc(64, MEMORY_TAG_STRING);

  // Log current memory usage
  memory_log();

  // Free the memory
  memory_free(array_block, 128, MEMORY_TAG_ARRAY);
  memory_free(string_block, 64, MEMORY_TAG_STRING);

  // Log after free to confirm tracking
  memory_log();

  memory_shutdown();  // Should report no leaks

  return 0;
}
