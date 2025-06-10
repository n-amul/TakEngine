#include "engine.h"

#include <vulkan/vulkan.h>     // Vulkan SDK
#include <glm/glm.hpp>         // GLM math
#include <iostream>

namespace engine {

void Engine::printInfo() const
{
    // --- Vulkan version ---
    uint32_t ver = 0;
    if (vkEnumerateInstanceVersion(&ver) == VK_SUCCESS) {
        std::cout<<"success"<<std::endl;
    } else {
        
    }

    // --- GLM demo ---
    glm::vec3 v{1.0f, 2.0f, 3.0f};
}

} // namespace engine