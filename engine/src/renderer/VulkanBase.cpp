#include "VulkanBase.hpp"
#include <glfw/glfw3.h>
#include <iostream>

void VulkanBase::test(){
    InitVulkan();
    std::cout<<"test passed"<<std::endl;
}

void VulkanBase::InitVulkan(){
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = name.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    u32 glfwExtensionCount = 0;
    const char** glfwExtensions;

    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;
    //empty for now
    createInfo.enabledLayerCount = 0;

    //if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        //TODO:LOG and quit
    //}

}