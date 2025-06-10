#pragma once

#include "common.h"

namespace engine {

class ENGINE_API Engine {
public:
    void printInfo() const;   // Prints Vulkan version and a GLM vector
};

}  // namespace engine