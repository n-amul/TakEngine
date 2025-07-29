# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

I am new to vulkan graphics programming and want to learn concept as easy as possible. My first goal is to draw triangle.
I want to know step by step.
## Build System

This is a CMake-based C++ project using C++17. Use these commands for development:

```bash
# Configure and build (from project root)
cmake -B build
cmake --build build

# Run the testbed application
./build/testbed/testbed.exe  # Windows
./build/testbed/testbed      # Linux
```

The project builds a shared library (engine.dll/libengine.so) and a testbed executable that uses it.

## Architecture

**TakEngine** is a game engine built around Vulkan rendering with these core components:

### Project Structure
- `engine/` - Main engine library (builds as shared library)
  - `src/core/` - Core systems (memory management, clock)
  - `src/renderer/` - Vulkan-based rendering system
  - `src/containers/` - Custom data structures (dynamic arrays)
- `testbed/` - Test application that uses the engine

### Key Systems

**Memory Management** (`core/kmemory.hpp`): Custom memory allocator with tagging system for tracking different allocation types (textures, entities, etc.). All allocations go through `memory_alloc()` and `memory_free()` with specific memory tags.

**Vulkan Renderer** (`renderer/VulkanBase.hpp`): Core Vulkan setup and management. Handles instance creation, validation layers (debug builds), and basic rendering pipeline setup.

**Type System** (`defines.hpp`): Custom type definitions (u32, i32, f32, etc.) and cross-platform macros for DLL export/import. Uses `TAK_API` for public engine functions.

### Dependencies
- **Vulkan SDK** (system requirement)
- **GLFW 3.4** (windowing, fetched via CMake)
- **GLM 0.9.9.8** (math library, fetched via CMake) 
- **spdlog 1.12.0** (logging, fetched via CMake)

### Platform Support
- Windows (64-bit) with MSVC
- Linux with GCC/Clang
- Validation layers enabled in debug builds

### Engine API
Engine functions are exported with `TAK_API` macro. Core systems must be initialized before use:
- `memory_init()` before any allocations
- VulkanBase class handles Vulkan initialization

The testbed demonstrates basic engine usage by creating a VulkanBase instance and calling `run()`.