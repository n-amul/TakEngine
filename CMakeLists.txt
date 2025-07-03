cmake_minimum_required(VERSION 3.20)
project(TakEngine LANGUAGES CXX)

# Build third‑party libraries as static by default.
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

if(MSVC)
    add_compile_options(/wd4996)
endif()

add_subdirectory(engine)
add_subdirectory(testbed)
