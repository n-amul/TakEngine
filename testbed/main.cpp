#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <iostream>

static void errorCallback(int code, const char* msg)
{
    std::cerr << "[GLFW error " << code << "] " << msg << '\n';
}

int main()
{
    glfwSetErrorCallback(errorCallback);

    if (!glfwInit())
        return EXIT_FAILURE;

    if (!glfwVulkanSupported())
    {
        std::cerr << "Vulkan loader (vulkan-1.dll) not found - install the SDK or runtime.\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // “Vulkan window”
    GLFWwindow* win = glfwCreateWindow(800, 600, "TakEngine", nullptr, nullptr);
    if (!win)
    {
        std::cerr << "glfwCreateWindow failed.\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::cout << "Window created - entering loop\n";

    while (!glfwWindowShouldClose(win))
    {
        glfwPollEvents();
        // render();  // nothing yet
    }

    glfwDestroyWindow(win);
    glfwTerminate();
    return EXIT_SUCCESS;
}
