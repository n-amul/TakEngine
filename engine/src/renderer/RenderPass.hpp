#pragma once
#include <vulkan/vulkan.h>

namespace tak {
class RenderPass {
 public:
  RenderPass(VkDevice device, VkFormat colorFormat, VkFormat depthFormat = VK_FORMAT_UNDEFINED);
  ~RenderPass();

  RenderPass(const RenderPass&) = delete;
  RenderPass& operator=(const RenderPass&) = delete;

  // Allow move
  RenderPass(RenderPass&& other) noexcept;
  RenderPass& operator=(RenderPass&& other) noexcept;

  VkRenderPass get() const { return renderPass; }

 private:
  void createRenderPass(VkFormat colorFormat, VkFormat depthFormat);

  VkDevice device = VK_NULL_HANDLE;
  VkRenderPass renderPass = VK_NULL_HANDLE;
};
}  // namespace tak