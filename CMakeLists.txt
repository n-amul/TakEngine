cmake_minimum_required(VERSION 3.20)
project(TakEngine LANGUAGES CXX)

# Build third‑party libraries as static by default.
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

include(FetchContent)

add_subdirectory(engine)
add_subdirectory(testbed)
