add_executable(testbed main.cpp)
target_compile_features(testbed PUBLIC cxx_std_17)

# Pulls in engine.dll — which already contains GLFW, GLM, Vulkan link flags
target_link_libraries(testbed PRIVATE engine)

# Copy engine.dll next to the executable after build (Windows only)
if (WIN32)
    add_custom_command(TARGET testbed POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:engine>
                $<TARGET_FILE_DIR:testbed>)
endif()

