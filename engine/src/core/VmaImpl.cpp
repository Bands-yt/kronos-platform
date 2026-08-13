// The one translation unit that compiles VMA's implementation. Every other
// file just includes <vk_mem_alloc.h> for declarations.
//
// <volk.h> must be included first: vk_mem_alloc.h pulls in
// <vulkan/vulkan.h> internally, and volk.h already defined VK_NO_PROTOTYPES
// before its own (first) inclusion of that header, so the second
// inclusion here is a no-op against the same include guard rather than a
// conflicting redeclaration.
//
// That same VK_NO_PROTOTYPES also flips VMA into "dynamic function
// loading" mode (VMA_DYNAMIC_VULKAN_FUNCTIONS) instead of the static-link
// default -- it does NOT automatically resolve calls through volk's
// global function-pointer variables. Renderer::createAllocator() passes
// vkGetInstanceProcAddr/vkGetDeviceProcAddr explicitly via
// VmaAllocatorCreateInfo::pVulkanFunctions to bootstrap it; omitting that
// is a null-function-pointer crash that only reproduces in release builds
// (VMA's own guard against it is a VMA_ASSERT, compiled out under NDEBUG)
// -- found by actually running this, not by reading the header.
#include <volk.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
