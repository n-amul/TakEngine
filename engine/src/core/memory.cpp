#include "memory.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <cstdlib>  // malloc / free

#ifdef _DEBUG
#include <atomic>
static std::array<std::atomic<u32>, MEMORY_TAG_MAX_TAGS> g_bytes;
static std::array<std::atomic<u32>, MEMORY_TAG_MAX_TAGS> g_allocs;
#else
// No tracking in release builds
#endif

// Tag names for human‑readable logs – keep order in sync with enum.
static constexpr const char* TAG_NAMES[MEMORY_TAG_MAX_TAGS] = {"Unknown",  "Array",  "LinearAllocator", "DArray", "Dict",       "RingQueue",
                                                               "BST",      "String", "Application",     "Job",    "Texture",    "MaterialInstance",
                                                               "Renderer", "Game",   "Transform",       "Entity", "EntityNode", "Scene"};

// -------------------------------------------------------------------------
// API implementation
// -------------------------------------------------------------------------
void memory_init(void) {
#ifdef _DEBUG
  for (auto& a : g_bytes) {
    a.store(0);
  }
  for (auto& a : g_allocs) {
    a.store(0);
  }
#endif
}

void memory_shutdown(void) {
#ifdef _DEBUG
  memory_log();
  for (u32 i = 0; i < MEMORY_TAG_MAX_TAGS; ++i) {
    if (g_allocs[i].load() != 0) {
      SPDLOG_ERROR("[Memory] Leak: {} allocs ({} bytes) {}", g_allocs[i].load(), g_bytes[i].load(), TAG_NAMES[i]);
    }
  }
#endif
}

void* memory_alloc(u32 size, memory_tag tag) {
  void* p = std::malloc(size);
  if (!p) {
    SPDLOG_ERROR("[Memory] malloc failed ({} bytes).", size);
    return nullptr;
  }
#ifdef _DEBUG
  g_bytes[CAST_U32(tag)].fetch_add(size);
  g_allocs[CAST_U32(tag)].fetch_add(1);
#endif
  return p;
}

void memory_free(void* block, u32 size, memory_tag tag) {
  if (!block) return;
#ifdef _DEBUG
  g_bytes[CAST_U32(tag)].fetch_sub(size);
  g_allocs[CAST_U32(tag)].fetch_sub(1);
#endif
  std::free(block);
}

u32 memory_bytes(memory_tag tag) {
#ifdef _DEBUG
  return g_bytes[CAST_U32(tag)].load();
#else
  return 0;
#endif
}

u32 memory_allocs(memory_tag tag) {
#ifdef _DEBUG
  return g_allocs[CAST_U32(tag)].load();
#else
  return 0;
#endif
}

void memory_log(void) {
#ifdef _DEBUG
  SPDLOG_INFO("==== Memory Usage (bytes | allocs) ====");
  for (u32 i = 0; i < MEMORY_TAG_MAX_TAGS; ++i) {
    auto b = g_bytes[i].load();
    auto a = g_allocs[i].load();
    if (b || a) {
      SPDLOG_INFO("{} : {} | {}", TAG_NAMES[i], b, a);
    }
  }
  SPDLOG_INFO("========================================");
#endif
}
