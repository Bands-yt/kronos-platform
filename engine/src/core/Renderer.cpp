#include "core/Renderer.hpp"

#include "core/CascadeSplitMath.hpp"
#include "core/Hierarchy.hpp"
#include "core/ResourcePaths.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <unordered_map>

namespace engine::core {

namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool layerAvailable(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::any_of(layers.begin(), layers.end(), [&](const VkLayerProperties& l) {
        return std::strcmp(l.layerName, name) == 0;
    });
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::fprintf(stderr, "[vulkan] %s\n", data->pMessage);
    }
    return VK_FALSE;
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsyncEnabled) {
    // Kronos ("UI/UX Revamp" -- "Performance & Stability", "whistling
    // sound when Studio opens"): real, identified root cause -- MAILBOX
    // presents as fast as the GPU can produce frames, uncapped by the
    // display's own refresh rate. On an idle/near-static scene (the Home
    // Screen, an empty Studio viewport) that means sustained
    // near-100%-duty-cycle GPU work for zero visible benefit -- a
    // textbook cause of both fan ramp-up and coil whine. FIFO is real
    // vsync: guaranteed present-mode support (unlike MAILBOX, which
    // isn't universally available -- see the fallback below), capped to
    // the display's own refresh rate, and the real, standard default
    // every editor/engine uses for exactly this "idle quietly" reason.
    //
    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Graphics: VSync"): MAILBOX's real, legitimate use case
    // (lower input latency for a fast-paced game, at the real cost of
    // burning full GPU power even when idle) is now the real, explicit,
    // user-facing graphics setting this comment always said it should
    // be -- see Renderer::setVsyncEnabled(). Real, honest fallback if
    // the physical device doesn't actually support MAILBOX (the Vulkan
    // spec never guarantees it): stay on FIFO rather than silently
    // requesting an unsupported mode.
    if (!vsyncEnabled) {
        for (VkPresentModeKHR mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t width, uint32_t height) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return caps.currentExtent;
    }
    VkExtent2D extent{width, height};
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return extent;
}

std::vector<char> readBinaryFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<char> data(static_cast<size_t>(size > 0 ? size : 0));
    if (!data.empty()) {
        size_t read = std::fread(data.data(), 1, data.size(), f);
        (void)read;
    }
    std::fclose(f);
    return data;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &info, nullptr, &module);
    return module;
}

} // namespace

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::initialize(const CreateInfo& info) {
    window_ = info.window;
    appName_ = info.appName;
    validationEnabled_ = info.enableValidation;
    framesInFlight_ = std::max(1u, info.framesInFlight);

    if (volkInitialize() != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: volkInitialize failed -- is the Vulkan loader (libvulkan.so.1 / vulkan-1.dll) installed?\n");
        return false;
    }

    if (!createInstance()) return false;
    volkLoadInstance(instance_);

    if (!createSurface()) return false;
    if (!pickPhysicalDevice()) return false;
    if (!createLogicalDevice()) return false;
    volkLoadDevice(device_);

    if (!createAllocator()) return false;

    // Sprint 14 ("RTX Upgrade" Phase 2): real, optional -- only
    // initialized if createLogicalDevice() already real-confirmed this
    // physical device supports ray query + acceleration structures (see
    // rayTracingSupported_'s own comment). A failure here downgrades to
    // "ray tracing unsupported" rather than aborting Renderer::initialize()
    // entirely -- the existing CSM shadow pass never depended on this.
    if (rayTracingSupported_ && !rayTracingScene_.initialize(allocator_, device_, physicalDevice_, *queueFamilies_.graphics, graphicsQueue_)) {
        std::fprintf(stderr, "Renderer: RayTracingScene::initialize failed -- continuing with ray-traced shadows disabled.\n");
        rayTracingSupported_ = false;
    }

    if (!createSwapchain()) return false;
    if (!createDepthResources()) return false;
    if (!createCommandPoolAndBuffers()) return false;
    if (!createSyncObjects()) return false;
    if (!createShadowResources()) return false;         // before createSceneDescriptorResources(): that
    if (!createSceneDescriptorResources()) return false; // function's binding-1 writes need frame.shadowArrayView to already exist
    if (!createInstanceBuffers()) return false;           // needs frames_ sized -- same requirement as shadow resources
    if (!createParticleResources()) return false;          // same requirement, plus needs allocator_ for the quad mesh
    if (!createSkinningDescriptorResources()) return false; // same requirement -- see FrameSync's skinning fields
    if (!createPostProcessResources()) return false;       // sampler + descriptor set layouts/pool (not per-frame-sized)
    if (!createMaterialResources()) return false;          // also not per-frame-sized -- must exist before createScenePipeline()'s 2-set layout
    // Needs defaultWhiteTexture_ (createMaterialResources) for its
    // pre-fill, and must precede createScenePipeline() which consumes the
    // layout. A failure here is not fatal: bindless simply stays off.
    if (!createBindlessResources()) {
        std::fprintf(stderr, "Renderer: bindless setup failed -- continuing with per-material descriptors.\n");
        destroyBindlessResources();
        bindlessSupported_ = false;
    }
    if (!createScenePipeline()) return false;
    if (!createGlassPipeline()) return false;             // needs sceneDescriptorSetLayout_ only -- see its own comment
    if (!createSkinnedScenePipeline()) return false;       // needs sceneDescriptorSetLayout_/materialDescriptorSetLayout_/skinningDescriptorSetLayout_ to already exist
    if (!createShadowPipeline()) return false;             // reuses scenePipelineLayout_ -- must come after createScenePipeline()
    if (!createInstancedScenePipeline()) return false;     // also reuses scenePipelineLayout_
    if (!createParticlePipeline()) return false;           // own pipeline layout (Sprint 16 soft-particle depth) -- needs postProcessResources' postProcessSingleSetLayout_ to already exist
    if (!createPostProcessPipelines()) return false;       // needs postProcessResources' set layouts to already exist
    if (!createSkyPipeline()) return false;                // reuses sceneDescriptorSetLayout_, same as createShadowPipeline()

    std::fprintf(stdout, "Renderer: initialized (%u images, %u frames in flight, depth=%d)\n",
                 static_cast<uint32_t>(swapchainImages_.size()), framesInFlight_, static_cast<int>(depthFormat_));
    return true;
}

bool Renderer::createInstance() {
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = appName_.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "RobloxStyleEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> extensions = window_->requiredInstanceExtensions();

    bool useValidation = validationEnabled_;
    if (useValidation && !layerAvailable(kValidationLayer)) {
        std::fprintf(stderr, "Renderer: validation requested but %s is not installed -- continuing without it.\n", kValidationLayer);
        useValidation = false;
    }
    if (useValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    if (useValidation) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &kValidationLayer;

        debugCreateInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        createInfo.pNext = &debugCreateInfo;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateInstance failed.\n");
        return false;
    }

    if (useValidation) {
        volkLoadInstanceOnly(instance_);
        if (vkCreateDebugUtilsMessengerEXT) {
            vkCreateDebugUtilsMessengerEXT(instance_, &debugCreateInfo, nullptr, &debugMessenger_);
        }
    }
    return true;
}

bool Renderer::createSurface() {
    surface_ = window_->createSurface(instance_);
    return surface_ != VK_NULL_HANDLE;
}

Renderer::QueueFamilyIndices Renderer::findQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilyIndices indices;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
        if (presentSupport) {
            indices.present = i;
        }
        if (indices.isComplete()) break;
    }
    return indices;
}

bool Renderer::isDeviceSuitable(VkPhysicalDevice device) const {
    QueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.isComplete()) return false;

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, available.data());
    bool hasSwapchain = std::any_of(available.begin(), available.end(), [](const VkExtensionProperties& e) {
        return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
    });
    if (!hasSwapchain) return false;

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(device, &features2);
    if (!features13.dynamicRendering || !features13.synchronization2) return false;

    uint32_t formatCount = 0, presentModeCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr);
    return formatCount > 0 && presentModeCount > 0;
}

int Renderer::scoreDevice(VkPhysicalDevice device) const {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);
    int score = 0;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 500;
    score += static_cast<int>(props.limits.maxImageDimension2D);
    return score;
}

bool Renderer::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        std::fprintf(stderr, "Renderer: no Vulkan-capable physical devices found.\n");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;
    for (auto device : devices) {
        if (!isDeviceSuitable(device)) continue;
        int score = scoreDevice(device);
        if (score > bestScore) {
            bestScore = score;
            best = device;
        }
    }

    if (best == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: no device supports the required swapchain + dynamicRendering + synchronization2 feature set.\n");
        return false;
    }

    physicalDevice_ = best;
    queueFamilies_ = findQueueFamilies(physicalDevice_);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    std::fprintf(stdout, "Renderer: selected physical device \"%s\"\n", props.deviceName);
    return true;
}

// Sprint 14 ("RTX Upgrade" Phase 2): real, honest capability query --
// checks both real extension *presence* (vkEnumerateDeviceExtensionProperties)
// and real feature *support* (vkGetPhysicalDeviceFeatures2, the driver's
// own reported booleans, not just "the extension exists"), matching the
// exact real two-layer check the Sprint 14 feasibility spike already
// confirmed passes on this environment's real RTX 5060. VK_KHR_ray_query
// (inline ray tracing from scene.frag) needs VK_KHR_ray_query +
// VK_KHR_acceleration_structure + VK_KHR_deferred_host_operations --
// deliberately NOT VK_KHR_ray_tracing_pipeline, which is only needed for
// a separate raygen/miss/hit-shader pipeline this pass doesn't build
// (see RayTracingScene.hpp's own header comment on why ray query is the
// real, simpler, sufficient technique for a pure shadow-visibility
// test). bufferDeviceAddress is Vulkan-1.2-core (this engine already
// targets 1.3) but still needs its own real feature bit explicitly
// requested at device creation -- API version alone doesn't enable it.
bool Renderer::checkRayTracingSupport(VkPhysicalDevice device) const {
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, available.data());
    auto hasExt = [&](const char* name) {
        return std::any_of(available.begin(), available.end(),
                            [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
    };
    if (!hasExt(VK_KHR_RAY_QUERY_EXTENSION_NAME) || !hasExt(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) ||
        !hasExt(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)) {
        return false;
    }

    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    rayQueryFeatures.pNext = &asFeatures;
    asFeatures.pNext = &features12;

    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &rayQueryFeatures;
    vkGetPhysicalDeviceFeatures2(device, &features2);

    return rayQueryFeatures.rayQuery && asFeatures.accelerationStructure && features12.bufferDeviceAddress;
}

// Kronos ("Bindless Descriptors"): probes VK_EXT_descriptor_indexing.
//
// Follows checkRayTracingSupport()'s own convention exactly -- a device
// that lacks this gets the existing per-draw descriptor path unchanged,
// because a missing optional capability is a graceful fallback here, not
// a fatal error.
//
// The four features below are the ones a global texture array actually
// needs: a runtime-sized array in the shader, non-uniform indexing into
// it (different draws index different slots in one command), partially
// bound sets (not every slot is populated), and update-after-bind
// (textures are registered while the set is already in use).
bool Renderer::checkBindlessSupport(VkPhysicalDevice device) const {
    VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &indexingFeatures;
    vkGetPhysicalDeviceFeatures2(device, &features2);

    return indexingFeatures.runtimeDescriptorArray &&
           indexingFeatures.shaderSampledImageArrayNonUniformIndexing &&
           indexingFeatures.descriptorBindingPartiallyBound &&
           indexingFeatures.descriptorBindingSampledImageUpdateAfterBind;
}

bool Renderer::createLogicalDevice() {
    std::set<uint32_t> uniqueFamilies = {*queueFamilies_.graphics, *queueFamilies_.present};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    float priority = 1.0f;
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        queueInfos.push_back(qci);
    }

    // Sprint 14: real, optional ray tracing feature chain -- only
    // populated (and only chained in below) if checkRayTracingSupport()
    // real-confirmed this physical device actually supports it. A
    // device that doesn't gets exactly the pre-Sprint-14 device creation
    // path, unchanged -- real, graceful degradation, the same "missing
    // optional capability is a real, honest fallback, not a fatal error"
    // convention this class already uses for the validation layer.
    rayTracingSupported_ = checkRayTracingSupport(physicalDevice_);
    bindlessSupported_ = checkBindlessSupport(physicalDevice_);
    std::fprintf(stdout, "Renderer: bindless descriptors %s, ray tracing %s\n",
                 bindlessSupported_ ? "supported" : "unavailable (using per-draw descriptors)",
                 rayTracingSupported_ ? "supported" : "unavailable");

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &features13;

    std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // Descriptor indexing is set on features12, NOT on a separate
    // VkPhysicalDeviceDescriptorIndexingFeatures.
    //
    // The two structs describe the same features, and chaining both is
    // ambiguous: with features12 also present (the ray tracing path adds
    // it) the driver honoured features12's defaults -- all false -- and
    // silently ignored the separate struct. Validation caught it as three
    // "...UpdateAfterBind was not enabled" errors at layout creation.
    //
    // features12 is therefore chained unconditionally now, since bindless
    // needs it even on a device without ray tracing.
    bool needFeatures12 = bindlessSupported_ || rayTracingSupported_;
    if (bindlessSupported_) {
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        features12.descriptorBindingPartiallyBound = VK_TRUE;
        features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
        features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    }
    if (needFeatures12) {
        features12.pNext = features13.pNext;
        features13.pNext = &features12;
    }

    if (rayTracingSupported_) {
        features12.bufferDeviceAddress = VK_TRUE;
        asFeatures.pNext = features12.pNext;
        features12.pNext = &asFeatures;
        asFeatures.accelerationStructure = VK_TRUE;
        asFeatures.pNext = &rayQueryFeatures;
        rayQueryFeatures.rayQuery = VK_TRUE;

        deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    }

    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pNext = &features2;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDevice failed.\n");
        return false;
    }

    vkGetDeviceQueue(device_, *queueFamilies_.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, *queueFamilies_.present, 0, &presentQueue_);
    std::fprintf(stdout, "Renderer: ray-traced shadows %s (VK_KHR_ray_query%s)\n",
                 rayTracingSupported_ ? "supported" : "not supported on this device/driver",
                 rayTracingSupported_ ? " + VK_KHR_acceleration_structure real-enabled" : "");
    return true;
}

bool Renderer::createAllocator() {
    // volk defines VK_NO_PROTOTYPES before its first include of
    // <vulkan/vulkan.h> (see VmaImpl.cpp), which pushes VMA into its
    // "dynamic function loading" mode rather than the static-link default.
    // In that mode VMA needs vkGetInstanceProcAddr/vkGetDeviceProcAddr
    // supplied explicitly via pVulkanFunctions and bootstraps every other
    // function itself from those two -- it does NOT automatically resolve
    // through volk's already-loaded global function pointers of the same
    // name. Omitting this is a silent null-function-pointer crash in a
    // release (NDEBUG) build: VMA's own internal check for this is a
    // VMA_ASSERT, which compiles to nothing without assertions enabled.
    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.instance = instance_;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;
    // Sprint 14 ("RTX Upgrade" Phase 2): real, required VMA opt-in --
    // any buffer a VmaAllocator creates that will ever have
    // vkGetBufferDeviceAddress() called on it (RayTracingScene's real
    // BLAS/TLAS/scratch/instance buffers, see that class's own comment)
    // needs this allocator-level flag set, or VMA doesn't know to pick a
    // real device-address-compatible memory type -- vmaCreateBuffer()
    // real-fails for those buffers without it (found by launching
    // engine_runtime and hitting exactly that real, immediate failure
    // the first time RayTracingScene::initialize() ran). Gated on
    // rayTracingSupported_ (already real-decided by createLogicalDevice(),
    // which always runs before this function) so a non-RT-capable
    // device's allocator is completely unchanged.
    if (rayTracingSupported_) {
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    }

    if (vmaCreateAllocator(&allocatorInfo, &allocator_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateAllocator failed.\n");
        return false;
    }
    return true;
}

bool Renderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, presentModes.data());

    VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
    VkPresentModeKHR presentMode = choosePresentMode(presentModes, vsyncEnabled_);
    VkExtent2D extent = chooseExtent(caps, window_->width(), window_->height());

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t familyIndices[] = {*queueFamilies_.graphics, *queueFamilies_.present};
    if (queueFamilies_.graphics != queueFamilies_.present) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = familyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = caps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateSwapchainKHR failed.\n");
        return false;
    }

    swapchainFormat_ = surfaceFormat.format;
    swapchainExtent_ = extent;

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &actualCount, nullptr);
    swapchainImages_.resize(actualCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &actualCount, swapchainImages_.data());

    swapchainImageViews_.resize(actualCount);
    for (size_t i = 0; i < actualCount; ++i) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = swapchainImages_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat_;
        viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &viewInfo, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkCreateImageView failed for swapchain image %zu.\n", i);
            return false;
        }
    }
    return true;
}

void Renderer::destroySwapchain() {
    for (auto view : swapchainImageViews_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swapchainImageViews_.clear();
    swapchainImages_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

bool Renderer::createDepthResources() {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {swapchainExtent_.width, swapchainExtent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Sprint 16 ("Cinematic Graphics"): SAMPLED_BIT added on top of the
    // original DEPTH_STENCIL_ATTACHMENT_BIT -- the new cinematic pass
    // (see drawCinematicPass()) reads this same depth buffer back as a
    // real texture for SSAO/DOF/motion-blur world-position reconstruction,
    // the first consumer of the *main* depth buffer as a sampled image
    // (the shadow-cascade depth array already was, see
    // initShadowResourcesFor()).
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    // Depth targets are large, long-lived, and device-local-only -- a
    // dedicated allocation avoids sharing a suballocated block with
    // smaller, shorter-lived resources for no benefit.
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &depthImage_, &depthAllocation_, nullptr) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateImage (depth) failed.\n");
        return false;
    }

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat_;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &viewInfo, nullptr, &depthImageView_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateImageView (depth) failed.\n");
        return false;
    }
    return true;
}

void Renderer::destroyDepthResources() {
    if (depthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, depthImage_, depthAllocation_);
        depthImage_ = VK_NULL_HANDLE;
    }
}

bool Renderer::initShadowResourcesFor(FrameSync& frame) {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {kShadowMapResolution, kShadowMapResolution, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = kCascadeCount; // one array image, kCascadeCount layers -- see FrameSync's comment
    imageInfo.format = kShadowFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &frame.shadowImage, &frame.shadowAllocation, nullptr) !=
        VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateImage (shadow cascade array) failed.\n");
        return false;
    }

    VkImageViewCreateInfo arrayViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    arrayViewInfo.image = frame.shadowImage;
    arrayViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    arrayViewInfo.format = kShadowFormat;
    arrayViewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kCascadeCount};
    if (vkCreateImageView(device_, &arrayViewInfo, nullptr, &frame.shadowArrayView) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateImageView (shadow array) failed.\n");
        return false;
    }

    for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade) {
        VkImageViewCreateInfo layerViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        layerViewInfo.image = frame.shadowImage;
        layerViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        layerViewInfo.format = kShadowFormat;
        layerViewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, cascade, 1};
        if (vkCreateImageView(device_, &layerViewInfo, nullptr, &frame.shadowCascadeViews[cascade]) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkCreateImageView (shadow cascade %u) failed.\n", cascade);
            return false;
        }
    }
    return true;
}

void Renderer::destroyShadowResourcesFor(FrameSync& frame) {
    for (auto& cascadeView : frame.shadowCascadeViews) {
        if (cascadeView != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, cascadeView, nullptr);
            cascadeView = VK_NULL_HANDLE;
        }
    }
    if (frame.shadowArrayView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.shadowArrayView, nullptr);
        frame.shadowArrayView = VK_NULL_HANDLE;
    }
    if (frame.shadowImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.shadowImage, frame.shadowAllocation);
        frame.shadowImage = VK_NULL_HANDLE;
    }
}

bool Renderer::createShadowResources() {
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    // CLAMP_TO_EDGE rather than CLAMP_TO_BORDER: scene.frag's computeShadow()
    // checks the projected UV against [0,1] itself and returns "fully lit"
    // outside that range, so the sampler's own edge behavior never
    // actually matters -- no border color setup needed.
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &shadowSampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateSampler (shadow) failed.\n");
        return false;
    }

    for (auto& frame : frames_) {
        if (!initShadowResourcesFor(frame)) return false;
    }

    std::fprintf(stdout, "Renderer: shadow resources created (%ux%u x%u cascades, %u frame(s))\n", kShadowMapResolution,
                 kShadowMapResolution, kCascadeCount, framesInFlight_);
    return true;
}

void Renderer::destroyShadowResources() {
    for (auto& frame : frames_) {
        destroyShadowResourcesFor(frame);
    }
    if (shadowSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, shadowSampler_, nullptr);
        shadowSampler_ = VK_NULL_HANDLE;
    }
}

bool Renderer::createShadowPipeline() {
    // Own pipeline layout, not scenePipelineLayout_ -- CSM's shadow pass
    // needs to tell shadow.vert which cascade it's rendering into
    // (ShadowPushConstants::cascadeIndex, to index SceneUBO.lightViewProj[]),
    // a different push-constant shape than the main pass's
    // ObjectPushConstants. Same descriptor set layout either way (both
    // need the UBO). See SceneTypes.hpp's ShadowPushConstants comment.
    VkPushConstantRange shadowPushRange{};
    shadowPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    shadowPushRange.offset = 0;
    shadowPushRange.size = sizeof(ShadowPushConstants);

    VkPipelineLayoutCreateInfo shadowLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    shadowLayoutInfo.setLayoutCount = 1;
    shadowLayoutInfo.pSetLayouts = &sceneDescriptorSetLayout_;
    shadowLayoutInfo.pushConstantRangeCount = 1;
    shadowLayoutInfo.pPushConstantRanges = &shadowPushRange;
    if (vkCreatePipelineLayout(device_, &shadowLayoutInfo, nullptr, &shadowPipelineLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreatePipelineLayout (shadow) failed.\n");
        return false;
    }

    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/shadow.vert.spv");
    if (vertCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shader \"%s/shadow.vert.spv\".\n", shaderDir.c_str());
        return false;
    }
    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    if (vertModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule (shadow) failed.\n");
        return false;
    }

    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = vertModule;
    stage.pName = "main";

    auto bindingDesc = Vertex::bindingDescription();
    auto attrDescs = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    // Depth bias: pushes shadow-caster depth slightly away from the light
    // so a surface doesn't self-shadow from floating-point/precision
    // error alone -- the pipeline-level complement to computeShadow()'s
    // slope-scaled shader-side bias in scene.frag (belt and suspenders;
    // either alone is usually enough, both together is more robust across
    // different surface angles).
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // No color attachments at all for a depth-only pass -- an empty
    // (attachmentCount=0) color-blend state is valid and correct here,
    // not a placeholder.
    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.depthAttachmentFormat = kShadowFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 1; // vertex only -- see shadow.vert's own comment on why this is valid
    pipelineInfo.pStages = &stage;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = shadowPipelineLayout_;

    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline_);
    vkDestroyShaderModule(device_, vertModule, nullptr);

    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateGraphicsPipelines (shadow) failed.\n");
        return false;
    }
    return true;
}

void Renderer::destroyShadowPipeline() {
    if (shadowPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, shadowPipeline_, nullptr);
        shadowPipeline_ = VK_NULL_HANDLE;
    }
    if (shadowPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, shadowPipelineLayout_, nullptr);
        shadowPipelineLayout_ = VK_NULL_HANDLE;
    }
}

Renderer::CascadeData Renderer::computeCascades(const Camera& camera, float aspectRatio) const {
    CascadeData result;

    // Practical split scheme (Zhang et al.): blends a purely logarithmic
    // split (matches perspective's natural depth precision falloff, but
    // puts too much of the shadow budget far away for typical third-
    // person camera distances) with a purely uniform split (opposite
    // problem) via splitLambda. 0.5 is a standard, unremarkable starting
    // point -- not tuned against this scene specifically.
    constexpr float splitLambda = 0.5f;
    float nearDist = camera.nearPlane;
    float farDist = kShadowMaxDistance; // not camera.farPlane -- see this method's declaration comment

    std::array<float, kCascadeCount> splitDepths =
        computeCascadeSplitDepths<kCascadeCount>(nearDist, farDist, splitLambda);

    glm::vec3 lightDir = glm::normalize(lighting_.directionWS);
    // glm::lookAt is degenerate when its forward and up vectors are
    // parallel -- guards against a light pointing (near-)straight down.
    glm::vec3 up = std::abs(lightDir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

    float previousSplit = nearDist;
    for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade) {
        float splitNear = previousSplit;
        float splitFar = splitDepths[cascade];
        previousSplit = splitFar;

        // The 8 corners of the camera frustum's sub-volume between
        // [splitNear, splitFar], in world space -- via the inverse of a
        // perspective/view matrix built for just that sub-range, evaluated
        // at NDC's 8 corner combinations.
        glm::mat4 subProj = glm::perspective(glm::radians(camera.verticalFovDegrees), aspectRatio, splitNear, splitFar);
        subProj[1][1] *= -1.0f;
        glm::mat4 invSubViewProj = glm::inverse(subProj * camera.viewMatrix());

        std::array<glm::vec3, 8> corners{};
        size_t cornerIndex = 0;
        for (int x = 0; x < 2; ++x) {
            for (int y = 0; y < 2; ++y) {
                for (int z = 0; z < 2; ++z) {
                    glm::vec4 ndc(2.0f * static_cast<float>(x) - 1.0f, 2.0f * static_cast<float>(y) - 1.0f,
                                  static_cast<float>(z), 1.0f);
                    glm::vec4 worldPos = invSubViewProj * ndc;
                    corners[cornerIndex++] = glm::vec3(worldPos) / worldPos.w;
                }
            }
        }

        glm::vec3 frustumCenter(0.0f);
        for (const glm::vec3& corner : corners) frustumCenter += corner;
        frustumCenter /= 8.0f;

        // A bounding-sphere radius (not a tight box) for placing the
        // light's eye position: rounded up to a coarse 1/16-unit step so
        // small camera movements don't perturb it by a sub-step amount,
        // the same stability goal the texel-snapping below serves for the
        // ortho volume itself.
        float radius = 0.0f;
        for (const glm::vec3& corner : corners) {
            radius = std::max(radius, glm::length(corner - frustumCenter));
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        glm::vec3 lightEye = frustumCenter - lightDir * (radius + kShadowDepthPadding);
        glm::mat4 lightView = glm::lookAt(lightEye, frustumCenter, up);

        glm::vec3 minBounds(std::numeric_limits<float>::max());
        glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
        for (const glm::vec3& corner : corners) {
            glm::vec3 lightSpaceCorner = glm::vec3(lightView * glm::vec4(corner, 1.0f));
            minBounds = glm::min(minBounds, lightSpaceCorner);
            maxBounds = glm::max(maxBounds, lightSpaceCorner);
        }

        // Stable splits: snap the ortho volume's X/Y bounds to whole
        // shadow-texel increments in light space. Without this, panning
        // the camera shifts the light-space bounding box by a fractional
        // texel every frame, which reads as shadow edges shimmering even
        // though nothing in the scene moved -- snapping makes the
        // cascade's world-space footprint jump in whole-texel steps
        // instead, which is imperceptible.
        float texelSizeX = (maxBounds.x - minBounds.x) / static_cast<float>(kShadowMapResolution);
        if (texelSizeX > 0.0f) {
            minBounds.x = std::floor(minBounds.x / texelSizeX) * texelSizeX;
            maxBounds.x = minBounds.x + static_cast<float>(kShadowMapResolution) * texelSizeX;
        }
        float texelSizeY = (maxBounds.y - minBounds.y) / static_cast<float>(kShadowMapResolution);
        if (texelSizeY > 0.0f) {
            minBounds.y = std::floor(minBounds.y / texelSizeY) * texelSizeY;
            maxBounds.y = minBounds.y + static_cast<float>(kShadowMapResolution) * texelSizeY;
        }

        // glm::lookAt's view space looks down -Z, so light-space corner Z
        // values are negative in front of the eye -- negate to get plain
        // forward distances for glm::ortho's near/far (same convention as
        // Camera::projectionMatrix's near/far). kShadowDepthPadding
        // extends the near side back toward the light -- see its
        // declaration comment.
        float orthoNear = -maxBounds.z - kShadowDepthPadding;
        float orthoFar = -minBounds.z;

        glm::mat4 lightProj = glm::ortho(minBounds.x, maxBounds.x, minBounds.y, maxBounds.y, orthoNear, orthoFar);
        lightProj[1][1] *= -1.0f; // same Vulkan clip-space Y-flip as Camera::projectionMatrix

        result.lightViewProj[cascade] = lightProj * lightView;
        result.splitDepths[cascade] = splitFar;
        result.depthRanges[cascade] = orthoFar - orthoNear;
    }

    return result;
}

void Renderer::drawShadowPass(VkCommandBuffer cmd, FrameSync& frame, ECS& ecs, MeshLibrary& meshLibrary) {
    // Cascade matrices are read from the SceneUBO (already written by
    // drawSceneInto() before this runs, once, shared with the main pass's
    // fragment shader) -- not passed as a parameter here, since this
    // function only needs shadow.vert to know which cascade *index* it's
    // rendering (pushed per-draw below), not the matrices themselves.

    // One barrier covering all kCascadeCount layers of the array image,
    // not one per layer -- transitionImage's layerCount parameter exists
    // for exactly this.
    transitionImage(cmd, frame.shadowImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT, kCascadeCount);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout_, 0, 1,
                             &frame.sceneDescriptorSet, 0, nullptr);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(kShadowMapResolution), static_cast<float>(kShadowMapResolution),
                         0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {kShadowMapResolution, kShadowMapResolution}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    auto view = ecs.view<Transform, Renderable>();

    // One full depth pass per cascade -- kCascadeCount times the shadow
    // draw-call count versus the old single-cascade pass, the real,
    // unavoidable cost of CSM. Each pass targets a different single-layer
    // 2D view (frame.shadowCascadeViews[cascade]) of the same array image.
    for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade) {
        VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depthAttachment.imageView = frame.shadowCascadeViews[cascade];
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
        renderingInfo.renderArea = {{0, 0}, {kShadowMapResolution, kShadowMapResolution}};
        renderingInfo.layerCount = 1;
        renderingInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(cmd, &renderingInfo);

        for (auto entity : view) {
            auto& renderable = view.get<Renderable>(entity);
            if (!renderable.visible || !renderable.castsShadow) continue;

            const Mesh* mesh = meshLibrary.get(renderable.meshHandle);
            if (!mesh) continue;

            ShadowPushConstants push{};
            // Kronos (Alpha Roadmap Phase 2, "Scene graph stability"):
            // real parent-aware world transform -- see
            // core::hierarchy::computeWorldMatrix()'s own comment. Byte-
            // identical to transform.matrix() alone for the overwhelming
            // majority of entities (anything with no Hierarchy component,
            // i.e. everything that existed before this feature).
            push.model = hierarchy::computeWorldMatrix(ecs, entity);
            push.cascadeIndex = static_cast<int32_t>(cascade);
            vkCmdPushConstants(cmd, shadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

            VkDeviceSize offset = 0;
            VkBuffer vb = mesh->vertexBuffer();
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
            vkCmdBindIndexBuffer(cmd, mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh->indexCount(), 1, 0, 0, 0);
            recordDraw(mesh->indexCount(), 1);
        }

        vkCmdEndRendering(cmd);
    }

    transitionImage(cmd, frame.shadowImage, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, kCascadeCount);
}

void Renderer::recreateSwapchain() {
    vkDeviceWaitIdle(device_);
    destroyDepthResources();
    destroySwapchain();
    createSwapchain();
    createDepthResources();
    // The image count can change on resize, so these must be rebuilt.
    if (device_ != VK_NULL_HANDLE && !renderFinishedPerImage_.empty()) (void)createPerImageSemaphores();
    framebufferResized_ = false;
}

bool Renderer::createCommandPoolAndBuffers() {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = *queueFamilies_.graphics;
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateCommandPool failed.\n");
        return false;
    }

    frames_.resize(framesInFlight_);
    std::vector<VkCommandBuffer> buffers(framesInFlight_);

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = framesInFlight_;
    if (vkAllocateCommandBuffers(device_, &allocInfo, buffers.data()) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkAllocateCommandBuffers failed.\n");
        return false;
    }
    for (uint32_t i = 0; i < framesInFlight_; ++i) {
        frames_[i].commandBuffer = buffers[i];
    }
    return true;
}

bool Renderer::createSyncObjects() {
    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (auto& frame : frames_) {
        if (vkCreateSemaphore(device_, &semInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &semInfo, nullptr, &frame.renderFinished) != VK_SUCCESS ||
            vkCreateFence(device_, &fenceInfo, nullptr, &frame.inFlight) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: failed to create per-frame sync objects.\n");
            return false;
        }
    }

    if (!createPerImageSemaphores()) return false;
    return true;
}

// One render-finished semaphore per swapchain image -- see the member's
// own comment for why per-frame was wrong. Recreated with the swapchain,
// since the image count can change on resize.

// Kronos ("Bindless Descriptors"): one global texture array, bound once
// per frame instead of a per-material descriptor set per draw.
bool Renderer::createBindlessResources() {
    if (!bindlessSupported_) return true; // graceful: the per-material path stays in use

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &bindlessSampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: failed to create the bindless sampler.\n");
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = kBindlessTextureCapacity;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // PARTIALLY_BOUND: not every slot is populated.
    // UPDATE_AFTER_BIND: textures are registered while the set is already
    // bound by in-flight command buffers.
    VkDescriptorBindingFlags bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                             VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                             VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flagsInfo.bindingCount = 1;
    flagsInfo.pBindingFlags = &bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.pNext = &flagsInfo;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &bindlessSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: failed to create the bindless descriptor set layout.\n");
        return false;
    }

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kBindlessTextureCapacity};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &bindlessPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: failed to create the bindless descriptor pool.\n");
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = bindlessPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &bindlessSetLayout_;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &bindlessSet_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: failed to allocate the bindless descriptor set.\n");
        return false;
    }

    // Pre-fill every slot with the default white texture. PARTIALLY_BOUND
    // permits unwritten slots, but only while nothing samples them --
    // pre-filling removes that footgun rather than relying on every future
    // shader index being valid.
    //
    // White is the right default for albedo/metallic/roughness/AO, but the
    // WRONG one for a normal map: sampling white decodes to a tangent-space
    // normal of (1,1,1), which visibly wrecks the shading of every entity
    // that simply has no normal map. Slot 1 is therefore reserved and
    // written with the flat-normal default instead, mirroring what
    // getOrCreateMaterialDescriptorSet() already does per-material.
    if (defaultWhiteTexture_.isValid()) {
        std::vector<VkDescriptorImageInfo> infos(kBindlessTextureCapacity,
                                                  VkDescriptorImageInfo{bindlessSampler_, defaultWhiteTexture_.view(),
                                                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = bindlessSet_;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = kBindlessTextureCapacity;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = infos.data();
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    // Claim the two reserved keys first, so a fresh table hands them slots
    // 0 and 1 before any real texture can take them.
    (void)bindlessTable_.acquire(kBindlessWhiteKey);
    (void)bindlessTable_.acquire(kBindlessFlatNormalKey);
    bindlessTable_.clearPendingWrites(); // both are written here, not lazily

    if (defaultFlatNormalTexture_.isValid()) {
        VkDescriptorImageInfo normalInfo{bindlessSampler_, defaultFlatNormalTexture_.view(),
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = bindlessSet_;
        write.dstBinding = 0;
        write.dstArrayElement = kBindlessFlatNormalSlot;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &normalInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    bindlessInitialised_ = true;
    std::fprintf(stderr, "Renderer: bindless texture array active -- %u slots, set 2 (%s frag variant).\n",
                 kBindlessTextureCapacity, rayTracingSupported_ ? "scene_rt_bindless" : "scene_bindless");
    return true;
}

void Renderer::destroyBindlessResources() {
    if (bindlessPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, bindlessPool_, nullptr);
        bindlessPool_ = VK_NULL_HANDLE;
    }
    if (bindlessSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, bindlessSetLayout_, nullptr);
        bindlessSetLayout_ = VK_NULL_HANDLE;
    }
    if (bindlessSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, bindlessSampler_, nullptr);
        bindlessSampler_ = VK_NULL_HANDLE;
    }
    bindlessSet_ = VK_NULL_HANDLE;
    bindlessTable_.clear();
    bindlessInitialised_ = false;
}

uint32_t Renderer::bindlessSlotFor(uint32_t handle, TextureLibrary& textureLibrary, uint32_t defaultSlot) {
    if (!bindlessInitialised_) return defaultSlot;

    const Texture* texture = handle != Renderable::kInvalidHandle ? textureLibrary.get(handle) : nullptr;
    const bool usable = texture != nullptr && texture->isValid();
    // The caller's default slot is already pre-filled; an unusable handle
    // maps there rather than allocating a slot for nothing.
    if (!usable) return defaultSlot;

    // Keys 0 and 1 are the reserved defaults (see createBindlessResources()).
    const uint64_t key = static_cast<uint64_t>(handle) + kBindlessReservedKeyCount;
    const uint32_t slot = bindlessTable_.acquire(key);
    if (slot == BindlessTextureTable::kInvalidSlot) {
        // Exhausted. Degrade to the default rather than indexing out of
        // range, which would sample whatever descriptor happens to sit there.
        return defaultSlot;
    }

    // Write the descriptor only for slots the table reports as new.
    if (!bindlessTable_.pendingWrites().empty()) {
        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorImageInfo> infos;
        writes.reserve(bindlessTable_.pendingWrites().size());
        infos.reserve(bindlessTable_.pendingWrites().size());
        for (uint32_t pending : bindlessTable_.pendingWrites()) {
            if (pending != slot) continue; // only this frame's newly-acquired slot is resolvable here
            infos.push_back(VkDescriptorImageInfo{bindlessSampler_, texture->view(),
                                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = bindlessSet_;
            write.dstBinding = 0;
            write.dstArrayElement = pending;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &infos.back();
            writes.push_back(write);
        }
        if (!writes.empty()) vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        bindlessTable_.clearPendingWrites();
    }
    return slot;
}

glm::uvec4 Renderer::packTextureIndices(const Renderable& renderable, TextureLibrary& textureLibrary) {
    if (!bindlessInitialised_) return glm::uvec4(0u);

    const uint32_t albedo    = bindlessSlotFor(renderable.albedoTexture, textureLibrary, kBindlessWhiteSlot);
    const uint32_t normal    = bindlessSlotFor(renderable.normalTexture, textureLibrary, kBindlessFlatNormalSlot);
    const uint32_t metallic  = bindlessSlotFor(renderable.metallicTexture, textureLibrary, kBindlessWhiteSlot);
    const uint32_t roughness = bindlessSlotFor(renderable.roughnessTexture, textureLibrary, kBindlessWhiteSlot);
    const uint32_t ao        = bindlessSlotFor(renderable.aoTexture, textureLibrary, kBindlessWhiteSlot);

    // Two 16-bit slots per component. kBindlessTextureCapacity is 2048, so
    // every slot fits in 16 bits with room to spare -- the packing exists
    // to keep ObjectPushConstants inside the 128-byte guaranteed minimum,
    // not to save memory. Must match the unpacking in scene.frag.
    return glm::uvec4(albedo | (normal << 16), metallic | (roughness << 16), ao, 0u);
}

bool Renderer::createPerImageSemaphores() {
    // Deliberately does NOT touch the bindless resources. This runs again
    // on every swapchain recreation (see recreateSwapchain()), and
    // scenePipelineLayout_ holds bindlessSetLayout_ for the lifetime of
    // the pipelines -- destroying it here would leave every scene draw
    // binding a destroyed descriptor set after the first window resize.
    destroyPerImageSemaphores();

    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    renderFinishedPerImage_.resize(swapchainImages_.size(), VK_NULL_HANDLE);
    for (VkSemaphore& semaphore : renderFinishedPerImage_) {
        if (vkCreateSemaphore(device_, &semInfo, nullptr, &semaphore) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: failed to create a per-swapchain-image semaphore.\n");
            return false;
        }
    }
    return true;
}

void Renderer::destroyPerImageSemaphores() {
    for (VkSemaphore& semaphore : renderFinishedPerImage_) {
        if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device_, semaphore, nullptr);
        semaphore = VK_NULL_HANDLE;
    }
    renderFinishedPerImage_.clear();
}

bool Renderer::initSceneDescriptorResourcesFor(FrameSync& frame) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = sizeof(SceneUBO);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo resultInfo{};
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &frame.sceneUboBuffer, &frame.sceneUboAllocation,
                         &resultInfo) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateBuffer (scene UBO) failed.\n");
        return false;
    }
    frame.sceneUboMapped = resultInfo.pMappedData; // persistently mapped -- no per-frame map/unmap needed

    VkDescriptorSetAllocateInfo dsAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsAllocInfo.descriptorPool = sceneDescriptorPool_;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts = &sceneDescriptorSetLayout_;
    if (vkAllocateDescriptorSets(device_, &dsAllocInfo, &frame.sceneDescriptorSet) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (scene) failed.\n");
        return false;
    }

    VkDescriptorBufferInfo bufInfo{frame.sceneUboBuffer, 0, sizeof(SceneUBO)};
    // shadowArrayView is the VK_IMAGE_VIEW_TYPE_2D_ARRAY view over all
    // kCascadeCount layers -- what the fragment shader's sampler2DArray
    // binding actually samples through (see FrameSync's comment). Must
    // already exist -- callers run initShadowResourcesFor(frame) first.
    VkDescriptorImageInfo shadowImageInfo{shadowSampler_, frame.shadowArrayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    std::vector<VkWriteDescriptorSet> writes;
    VkWriteDescriptorSet uboWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    uboWrite.dstSet = frame.sceneDescriptorSet;
    uboWrite.dstBinding = 0;
    uboWrite.descriptorCount = 1;
    uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboWrite.pBufferInfo = &bufInfo;
    writes.push_back(uboWrite);

    VkWriteDescriptorSet shadowWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    shadowWrite.dstSet = frame.sceneDescriptorSet;
    shadowWrite.dstBinding = 1;
    shadowWrite.descriptorCount = 1;
    shadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowWrite.pImageInfo = &shadowImageInfo;
    writes.push_back(shadowWrite);

    // Valid to write a descriptor referencing frame.shadowArrayView
    // before that image has actually been transitioned to
    // SHADER_READ_ONLY_OPTIMAL for the first time -- the descriptor
    // write only records *intent*; the image must be in that layout
    // by the time a draw call actually samples through it, which
    // drawShadowPass()'s post-pass transition guarantees every frame.
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // Sprint 14 ("RTX Upgrade" Phase 2): binding 2, the real TLAS --
    // written separately (see updateRayTracedShadowDescriptor()'s own
    // comment), with whatever real (possibly still-empty)
    // rayTracingScene_.tlas() already holds by this point (rayTracingScene_
    // is real-initialized before createSceneDescriptorResources() ever
    // runs -- see Renderer::initialize()'s own call order).
    updateRayTracedShadowDescriptor(frame);
    return true;
}

void Renderer::updateRayTracedShadowDescriptor(FrameSync& frame) {
    if (!rayTracingSupported_) return;

    VkWriteDescriptorSetAccelerationStructureKHR asWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    VkAccelerationStructureKHR tlas = rayTracingScene_.tlas();
    asWrite.accelerationStructureCount = 1;
    asWrite.pAccelerationStructures = &tlas;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.pNext = &asWrite;
    write.dstSet = frame.sceneDescriptorSet;
    write.dstBinding = 2;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    // Kronos ("Rendering Fidelity Foundation" Phase 1.3): binding 3, the
    // real materials buffer hybrid RT reflections read -- written
    // alongside binding 2 every time this function runs (same call sites,
    // same "the underlying handle may have just reallocated" trigger, see
    // this function's own call sites' comments). A real, valid (if
    // possibly zero-length-backed) buffer exists from the moment
    // RayTracingScene::initialize() first calls rebuild({}), same
    // "always something real and valid bound" guarantee binding 2 above
    // already has.
    VkDescriptorBufferInfo materialsInfo{rayTracingScene_.materialsBuffer(), 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet materialsWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    materialsWrite.dstSet = frame.sceneDescriptorSet;
    materialsWrite.dstBinding = 3;
    materialsWrite.descriptorCount = 1;
    materialsWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialsWrite.pBufferInfo = &materialsInfo;

    std::array<VkWriteDescriptorSet, 2> writes{write, materialsWrite};
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void Renderer::destroySceneDescriptorResourcesFor(FrameSync& frame) {
    if (frame.sceneUboBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, frame.sceneUboBuffer, frame.sceneUboAllocation);
        frame.sceneUboBuffer = VK_NULL_HANDLE;
    }
    // frame.sceneDescriptorSet is NOT individually freed here --
    // sceneDescriptorPool_ was created without
    // VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT (destroying the
    // whole pool is the only way to reclaim its sets, same as every
    // other descriptor pool in this file); it's simply never bound again
    // once its owning frame/auxiliary scene is torn down.
}

bool Renderer::createSceneDescriptorResources() {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding shadowBinding{};
    shadowBinding.binding = 1;
    shadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowBinding.descriptorCount = 1;
    shadowBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // only scene.frag's computeShadow() samples it

    std::vector<VkDescriptorSetLayoutBinding> bindings{uboBinding, shadowBinding};

    // Sprint 14 ("RTX Upgrade" Phase 2): binding 2, the real TLAS --
    // only added to this layout at all when rayTracingSupported_ (decided
    // once, at device-creation time, before this function ever runs).
    // Every pipeline sharing sceneDescriptorSetLayout_ (shadow/instanced/
    // particle/sky, see createScenePipeline()'s own comment on why they
    // all reuse scenePipelineLayout_) is unaffected either way -- their
    // shaders simply never declare binding 2, which Vulkan doesn't
    // require them to. Only scene.frag's own real ray-tracing-capable
    // variant (scene_rt.frag, selected in createScenePipeline() when
    // rayTracingSupported_) statically references it.
    VkDescriptorSetLayoutBinding tlasBinding{};
    if (rayTracingSupported_) {
        tlasBinding.binding = 2;
        tlasBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        tlasBinding.descriptorCount = 1;
        tlasBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(tlasBinding);
    }

    // Kronos ("Rendering Fidelity Foundation" Phase 1.3): binding 3, the
    // real per-instance material storage buffer
    // (RayTracingScene::materialsBuffer()) hybrid RT reflections read to
    // shade a ray-query hit -- same rayTracingSupported_ gating as
    // binding 2 above (this binding is meaningless without a real TLAS
    // to hit-test against in the first place).
    VkDescriptorSetLayoutBinding materialsBinding{};
    if (rayTracingSupported_) {
        materialsBinding.binding = 3;
        materialsBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        materialsBinding.descriptorCount = 1;
        materialsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(materialsBinding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &sceneDescriptorSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorSetLayout failed.\n");
        return false;
    }

    // Sized for framesInFlight_ *plus* kMaxAuxiliaryScenes up front --
    // Vulkan descriptor pools can't grow, and createAuxiliaryScene()
    // allocates one more scene descriptor set from this same pool per
    // studio::PreviewScene. See AuxiliarySceneHandle's doc comment.
    uint32_t totalSceneSlots = framesInFlight_ + static_cast<uint32_t>(kMaxAuxiliaryScenes);
    std::vector<VkDescriptorPoolSize> poolSizes{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, totalSceneSlots},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, totalSceneSlots},
    };
    if (rayTracingSupported_) {
        poolSizes.push_back({VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, totalSceneSlots});
        poolSizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, totalSceneSlots});
    }
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = totalSceneSlots;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &sceneDescriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorPool (scene) failed.\n");
        return false;
    }

    for (auto& frame : frames_) {
        if (!initSceneDescriptorResourcesFor(frame)) return false;
    }
    return true;
}

void Renderer::destroySceneDescriptorResources() {
    for (auto& frame : frames_) {
        destroySceneDescriptorResourcesFor(frame);
    }
    if (sceneDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, sceneDescriptorPool_, nullptr);
        sceneDescriptorPool_ = VK_NULL_HANDLE;
    }
    if (sceneDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, sceneDescriptorSetLayout_, nullptr);
        sceneDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
}

bool Renderer::initInstanceBufferFor(FrameSync& frame) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = sizeof(InstanceData) * kMaxInstancesPerFrame;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo resultInfo{};
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &frame.instanceBuffer, &frame.instanceAllocation,
                         &resultInfo) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateBuffer (instance) failed.\n");
        return false;
    }
    frame.instanceMapped = resultInfo.pMappedData; // persistently mapped, same pattern as sceneUboBuffer
    return true;
}

void Renderer::destroyInstanceBufferFor(FrameSync& frame) {
    if (frame.instanceBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, frame.instanceBuffer, frame.instanceAllocation);
        frame.instanceBuffer = VK_NULL_HANDLE;
    }
}

bool Renderer::createInstanceBuffers() {
    for (auto& frame : frames_) {
        if (!initInstanceBufferFor(frame)) return false;
    }
    return true;
}

void Renderer::destroyInstanceBuffers() {
    for (auto& frame : frames_) {
        destroyInstanceBufferFor(frame);
    }
}

bool Renderer::initParticleInstanceBufferFor(FrameSync& frame) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = sizeof(ParticleInstanceData) * ParticleSystem::kMaxParticles;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo resultInfo{};
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &frame.particleInstanceBuffer,
                         &frame.particleInstanceAllocation, &resultInfo) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateBuffer (particle instance) failed.\n");
        return false;
    }
    frame.particleInstanceMapped = resultInfo.pMappedData;
    return true;
}

void Renderer::destroyParticleInstanceBufferFor(FrameSync& frame) {
    if (frame.particleInstanceBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, frame.particleInstanceBuffer, frame.particleInstanceAllocation);
        frame.particleInstanceBuffer = VK_NULL_HANDLE;
    }
}

bool Renderer::createParticleResources() {
    particleQuadMesh_ = Mesh::createQuad(allocator_, device_, commandPool_, graphicsQueue_, 0.5f);
    if (particleQuadMesh_.vertexBuffer() == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: Mesh::createQuad (particle) failed.\n");
        return false;
    }

    for (auto& frame : frames_) {
        if (!initParticleInstanceBufferFor(frame)) return false;
    }
    return true;
}

void Renderer::destroyParticleResources() {
    for (auto& frame : frames_) {
        destroyParticleInstanceBufferFor(frame);
    }
    particleQuadMesh_.destroy(allocator_);
}

bool Renderer::initSkinningResourcesFor(FrameSync& frame) {
    for (uint32_t slot = 0; slot < kMaxSkinnedDrawsPerFrame; ++slot) {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = sizeof(glm::mat4) * kMaxJointsPerSkeleton;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo resultInfo{};
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &frame.skinningUboBuffers[slot],
                             &frame.skinningUboAllocations[slot], &resultInfo) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vmaCreateBuffer (skinning UBO slot %u) failed.\n", slot);
            return false;
        }
        frame.skinningUboMapped[slot] = resultInfo.pMappedData;

        VkDescriptorSetAllocateInfo dsAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dsAllocInfo.descriptorPool = skinningDescriptorPool_;
        dsAllocInfo.descriptorSetCount = 1;
        dsAllocInfo.pSetLayouts = &skinningDescriptorSetLayout_;
        if (vkAllocateDescriptorSets(device_, &dsAllocInfo, &frame.skinningDescriptorSets[slot]) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (skinning slot %u) failed.\n", slot);
            return false;
        }

        VkDescriptorBufferInfo bufInfo{frame.skinningUboBuffers[slot], 0, sizeof(glm::mat4) * kMaxJointsPerSkeleton};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = frame.skinningDescriptorSets[slot];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
    return true;
}

void Renderer::destroySkinningResourcesFor(FrameSync& frame) {
    for (uint32_t slot = 0; slot < kMaxSkinnedDrawsPerFrame; ++slot) {
        if (frame.skinningUboBuffers[slot] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, frame.skinningUboBuffers[slot], frame.skinningUboAllocations[slot]);
            frame.skinningUboBuffers[slot] = VK_NULL_HANDLE;
        }
    }
}

bool Renderer::createSkinningDescriptorResources() {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &skinningDescriptorSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorSetLayout (skinning) failed.\n");
        return false;
    }

    // One descriptor set per skinning slot, per FrameSync (frames_[] slot
    // or auxiliaryScenes_[] slot) -- same "sized for framesInFlight_ +
    // kMaxAuxiliaryScenes up front" reasoning as sceneDescriptorPool_/
    // postProcessDescriptorPool_ (Vulkan descriptor pools can't grow).
    uint32_t totalSkinningSlots =
        (framesInFlight_ + static_cast<uint32_t>(kMaxAuxiliaryScenes)) * kMaxSkinnedDrawsPerFrame;
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, totalSkinningSlots};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = totalSkinningSlots;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &skinningDescriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorPool (skinning) failed.\n");
        return false;
    }

    for (auto& frame : frames_) {
        if (!initSkinningResourcesFor(frame)) return false;
    }
    return true;
}

void Renderer::destroySkinningDescriptorResources() {
    for (auto& frame : frames_) {
        destroySkinningResourcesFor(frame);
    }
    if (skinningDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, skinningDescriptorPool_, nullptr);
        skinningDescriptorPool_ = VK_NULL_HANDLE;
    }
    if (skinningDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, skinningDescriptorSetLayout_, nullptr);
        skinningDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
}

// Sprint 14 ("Performance Mode"): one real toggle bundling several
// concrete, independently-real rendering-cost reductions -- the lever
// for keeping "stable 180 FPS" reachable once heavier real costs (ray-
// traced shadows) are switched on:
//   - CSM (and scene_rt.frag's own CSM fallback): 3x3 PCF -> single-tap
//     shadow sampling (scene.renderFlags.y, see both fragment shaders).
//   - Bloom: the real extract+blur fragment-shader pass is skipped
//     entirely (frame.bloomImage is real-cleared to black instead --
//     see drawBloomAndComposite()), not just visually zeroed via
//     intensity=0 (which would still pay the full extract-shader cost
//     for no visible result).
//   - Particles: real per-frame draw count clamped to
//     kPerformanceModeMaxParticles, well under ParticleSystem::kMaxParticles
//     -- fewer real instanced quads, less real fragment-shader overdraw.
// Deliberately also forces ray-traced shadows off when enabling: ray
// tracing is a real *additional* cost this toggle exists to make room
// for, not something Performance Mode would ever combine with real
// CSM-cost cuts at the same time.
void Renderer::setPerformanceMode(bool enabled) {
    performanceModeEnabled_ = enabled;
    if (enabled) {
        rayTracedShadowsEnabled_ = false;
        cinematicModeEnabled_ = false; // see setCinematicMode()'s own comment on this mutual exclusion
    }
}

void Renderer::setVsyncEnabled(bool enabled) {
    if (vsyncEnabled_ == enabled) return; // real, honest no-op -- no swapchain rebuild for an unchanged value
    vsyncEnabled_ = enabled;
    if (swapchain_ != VK_NULL_HANDLE) recreateSwapchain();
}

void Renderer::setColorblindMode(int mode) { colorblindModeIndex_ = std::clamp(mode, 0, 3); }

// Sprint 16 ("Cinematic Graphics") -- see this method's own public
// declaration comment in Renderer.hpp for the real mutual-exclusion
// reasoning with Performance Mode.
void Renderer::setCinematicMode(bool enabled) {
    cinematicModeEnabled_ = enabled;
    if (enabled) performanceModeEnabled_ = false;
}

bool Renderer::createScenePipeline() {
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR); // see src/CMakeLists.txt's shader-compile step
    auto vertCode = readBinaryFile(shaderDir + "/scene.vert.spv");
    // Sprint 14 ("RTX Upgrade" Phase 2): scene_rt.frag (the real ray-query-
    // capable variant, see that file's own header comment) is chosen
    // once here, at pipeline-creation time, purely from real hardware
    // capability (rayTracingSupported_, decided in createLogicalDevice()
    // before this function ever runs) -- never re-decided per frame. The
    // real runtime ON/OFF toggle (setRayTracedShadowsEnabled()) is a
    // dynamic branch *inside* scene_rt.frag itself (see its
    // computeShadow()), not a pipeline swap -- one pipeline object either
    // way, for the entire lifetime of this Renderer.
    // Kronos ("Bindless Descriptors"): the same one-time, capability-driven
    // choice, now across two axes. bindlessInitialised_ (not merely
    // bindlessSupported_) is the condition, so a device that supports
    // bindless but failed to create the descriptor resources still gets a
    // shader whose set=1 material bindings actually exist.
    std::string fragShaderName;
    if (rayTracingSupported_) {
        fragShaderName = bindlessInitialised_ ? "scene_rt_bindless.frag.spv" : "scene_rt.frag.spv";
    } else {
        fragShaderName = bindlessInitialised_ ? "scene_bindless.frag.spv" : "scene.frag.spv";
    }
    auto fragCode = readBinaryFile(shaderDir + "/" + fragShaderName);
    if (vertCode.empty() || fragCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shaders from \"%s\" -- did the build run engine_shaders?\n",
                     shaderDir.c_str());
        return false;
    }

    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule failed.\n");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    auto bindingDesc = Vertex::bindingDescription();
    auto attrDescs = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    // CULL_NONE: correctness over an (unverified) winding-order
    // optimization -- see this class's header comment. Purely a perf
    // trade for a closed mesh; zero visual difference either way.
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // no MSAA yet -- §4.1 post-stack TODO

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE; // no transparency pass yet, §4.1 TODO

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ObjectPushConstants);

    // Two sets: 0 = per-frame scene data (UBO + shadow sampler, unchanged),
    // 1 = per-material textures (see createMaterialResources()) -- reused
    // as-is by createInstancedScenePipeline()/createParticlePipeline(),
    // whose shaders simply never declare a set=1 binding, so Vulkan never
    // requires it to be bound for those draws (only createScenePipeline()'s
    // individually-drawn entities bind set=1, see drawSceneInto()'s loop).
    // Set 2 is the global bindless texture array, added only when the
    // device supports it. Set 1 stays declared so the non-bindless path,
    // and the other pipelines that share this layout, are unchanged.
    std::vector<VkDescriptorSetLayout> setLayouts{sceneDescriptorSetLayout_, materialDescriptorSetLayout_};
    if (bindlessInitialised_ && bindlessSetLayout_ != VK_NULL_HANDLE) setLayouts.push_back(bindlessSetLayout_);

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &scenePipelineLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreatePipelineLayout failed.\n");
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, fragModule, nullptr);
        return false;
    }

    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 1;
    VkFormat hdrFormat = kHDRFormat; // renders into the internal HDR intermediate, not the final presentable image
    renderingInfo.pColorAttachmentFormats = &hdrFormat;
    renderingInfo.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = scenePipelineLayout_;

    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &scenePipeline_);

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateGraphicsPipelines failed.\n");
        return false;
    }
    return true;
}

void Renderer::destroyScenePipeline() {
    if (scenePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, scenePipeline_, nullptr);
        scenePipeline_ = VK_NULL_HANDLE;
    }
    if (scenePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, scenePipelineLayout_, nullptr);
        scenePipelineLayout_ = VK_NULL_HANDLE;
    }
}

// Kronos ("Real-Time Rendering Evolved" trailer) -- see shaders/glass.frag's
// own header comment for the real Fresnel+refract() technique this
// pipeline drives. Deliberately mirrors createScenePipeline()'s own
// shape (same vertex input layout -- glass meshes are ordinary
// Mesh::createPlane() output, same depth test/write, opaque overwrite,
// no blending: the shader itself computes the final "what you see
// through/off the glass" color directly, so there's nothing to blend),
// just with a smaller, dedicated pipeline layout (set=0 only, no
// material-texture set=1) and its own, smaller push constant range.
bool Renderer::createGlassPipeline() {
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/glass.vert.spv");
    auto fragCode = readBinaryFile(shaderDir + "/glass.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled glass shaders from \"%s\".\n", shaderDir.c_str());
        return false;
    }

    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule (glass) failed.\n");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    auto bindingDesc = Vertex::bindingDescription();
    auto attrDescs = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE; // the shader computes the final color directly -- see this function's own comment

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(GlassPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &sceneDescriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &glassPipelineLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreatePipelineLayout (glass) failed.\n");
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, fragModule, nullptr);
        return false;
    }

    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 1;
    VkFormat hdrFormat = kHDRFormat;
    renderingInfo.pColorAttachmentFormats = &hdrFormat;
    renderingInfo.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = glassPipelineLayout_;

    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &glassPipeline_);

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateGraphicsPipelines (glass) failed.\n");
        return false;
    }
    return true;
}

void Renderer::destroyGlassPipeline() {
    if (glassPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, glassPipeline_, nullptr);
        glassPipeline_ = VK_NULL_HANDLE;
    }
    if (glassPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, glassPipelineLayout_, nullptr);
        glassPipelineLayout_ = VK_NULL_HANDLE;
    }
}

bool Renderer::createSkinnedScenePipeline() {
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/scene_skinned.vert.spv");
    // Deliberately NOT the bindless variant, unlike the opaque and
    // instanced pipelines. skinnedScenePipelineLayout_ already uses set 2
    // for the per-entity skinning UBO, and the bindless array is declared
    // at set 2 -- they cannot coexist. Nothing is lost: skinned entities
    // have no per-entity textured materials by design (see the default
    // material set bound in drawSkinnedEntities()), so there is nothing
    // for a bindless lookup to find.
    auto fragCode = readBinaryFile(shaderDir + "/scene.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shaders from \"%s\" (skinned).\n", shaderDir.c_str());
        return false;
    }

    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule failed (skinned).\n");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // Two vertex bindings: 0 = base Vertex (position/normal/uv/tangent),
    // 1 = GpuSkinVertex (joint indices/weights) -- see RiggedMesh.hpp.
    std::array<VkVertexInputBindingDescription, 2> bindingDescs{Vertex::bindingDescription(),
                                                                  GpuSkinVertex::bindingDescription()};
    auto vertexAttrs = Vertex::attributeDescriptions();
    auto skinAttrs = GpuSkinVertex::attributeDescriptions(4); // locations 0-3 are Vertex's -- see that call's comment
    std::vector<VkVertexInputAttributeDescription> attrDescs;
    attrDescs.reserve(vertexAttrs.size() + skinAttrs.size());
    attrDescs.insert(attrDescs.end(), vertexAttrs.begin(), vertexAttrs.end());
    attrDescs.insert(attrDescs.end(), skinAttrs.begin(), skinAttrs.end());

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescs.size());
    vertexInput.pVertexBindingDescriptions = bindingDescs.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // same correctness-over-perf reasoning as createScenePipeline()
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ObjectPushConstants);

    // Three sets: 0 = scene UBO+shadow (sceneDescriptorSetLayout_, shared
    // with every other opaque pipeline), 1 = material textures
    // (materialDescriptorSetLayout_, also shared), 2 = this draw's bone
    // matrices (skinningDescriptorSetLayout_, new) -- a genuinely
    // different set signature from scenePipelineLayout_'s, hence its own
    // VkPipelineLayout object rather than reusing that one (see this
    // member's declaration comment in Renderer.hpp).
    std::array<VkDescriptorSetLayout, 3> setLayouts{sceneDescriptorSetLayout_, materialDescriptorSetLayout_,
                                                      skinningDescriptorSetLayout_};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &skinnedScenePipelineLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreatePipelineLayout failed (skinned).\n");
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, fragModule, nullptr);
        return false;
    }

    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 1;
    VkFormat hdrFormat = kHDRFormat;
    renderingInfo.pColorAttachmentFormats = &hdrFormat;
    renderingInfo.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = skinnedScenePipelineLayout_;

    VkResult result =
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skinnedScenePipeline_);

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateGraphicsPipelines failed (skinned).\n");
        return false;
    }
    return true;
}

void Renderer::destroySkinnedScenePipeline() {
    if (skinnedScenePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, skinnedScenePipeline_, nullptr);
        skinnedScenePipeline_ = VK_NULL_HANDLE;
    }
    if (skinnedScenePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, skinnedScenePipelineLayout_, nullptr);
        skinnedScenePipelineLayout_ = VK_NULL_HANDLE;
    }
}

bool Renderer::createInstancedScenePipeline() {
    // Reuses scenePipelineLayout_ rather than creating its own: same
    // descriptor set layout (the UBO/shadow-sampler binding), and while
    // the layout's push-constant range is sized for ObjectPushConstants,
    // scene_instanced.vert never declares a push_constant block at all --
    // an unused reservation in the layout costs nothing, and a pipeline
    // is free to not consume every resource its layout makes available.
    // Not reusing the layout would mean a second, functionally-identical
    // VkPipelineLayout object for no benefit.
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/scene_instanced.vert.spv");
    // Same bindless variant selection as createScenePipeline() -- see there.
    auto fragCode = readBinaryFile(shaderDir + (bindlessInitialised_ ? "/scene_bindless.frag.spv" : "/scene.frag.spv"));
    if (vertCode.empty() || fragCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shaders from \"%s\" (instanced).\n", shaderDir.c_str());
        return false;
    }

    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule failed (instanced).\n");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // Two vertex input bindings: 0 = per-vertex Vertex data (rate=VERTEX,
    // shared with the non-instanced pipeline), 1 = per-instance InstanceData
    // (rate=INSTANCE) -- see SceneTypes.hpp's InstanceData and its
    // binding/attribute description methods.
    auto vertexBinding = Vertex::bindingDescription();
    auto instanceBinding = InstanceData::bindingDescription();
    std::array<VkVertexInputBindingDescription, 2> bindings{vertexBinding, instanceBinding};

    auto vertexAttrs = Vertex::attributeDescriptions();
    auto instanceAttrs = InstanceData::attributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> allAttrs;
    allAttrs.reserve(vertexAttrs.size() + instanceAttrs.size());
    allAttrs.insert(allAttrs.end(), vertexAttrs.begin(), vertexAttrs.end());
    allAttrs.insert(allAttrs.end(), instanceAttrs.begin(), instanceAttrs.end());

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(allAttrs.size());
    vertexInput.pVertexAttributeDescriptions = allAttrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 1;
    VkFormat hdrFormat = kHDRFormat; // renders into the internal HDR intermediate, not the final presentable image
    renderingInfo.pColorAttachmentFormats = &hdrFormat;
    renderingInfo.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = scenePipelineLayout_;

    VkResult result =
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &instancedScenePipeline_);

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateGraphicsPipelines failed (instanced).\n");
        return false;
    }
    return true;
}

void Renderer::destroyInstancedScenePipeline() {
    if (instancedScenePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, instancedScenePipeline_, nullptr);
        instancedScenePipeline_ = VK_NULL_HANDLE;
    }
}

bool Renderer::createParticlePipeline() {
    // Reuses scenePipelineLayout_ -- same reasoning as
    // createInstancedScenePipeline(): all per-particle data comes through
    // instanced vertex attributes (ParticleInstanceData), so particle.vert
    // declares no push_constant block at all.
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/particle.vert.spv");
    auto fragCode = readBinaryFile(shaderDir + "/particle.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shaders from \"%s\" (particle).\n", shaderDir.c_str());
        return false;
    }

    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule failed (particle).\n");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    auto vertexBinding = Vertex::bindingDescription();
    auto instanceBinding = ParticleInstanceData::bindingDescription();
    std::array<VkVertexInputBindingDescription, 2> bindings{vertexBinding, instanceBinding};

    auto vertexAttrs = Vertex::attributeDescriptions();
    auto instanceAttrs = ParticleInstanceData::attributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> allAttrs;
    allAttrs.reserve(vertexAttrs.size() + instanceAttrs.size());
    allAttrs.insert(allAttrs.end(), vertexAttrs.begin(), vertexAttrs.end());
    allAttrs.insert(allAttrs.end(), instanceAttrs.begin(), instanceAttrs.end());

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(allAttrs.size());
    vertexInput.pVertexAttributeDescriptions = allAttrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // billboards always face the camera, but NONE costs nothing extra here
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;   // still occluded by solid world geometry
    depthStencil.depthWriteEnable = VK_FALSE; // particles don't occlude each other or write depth -- standard for blended fx
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // Additive blending, modulated by alpha -- no back-to-front sort
    // dependency the way regular (over) alpha blending would need, which
    // is exactly why this pass uses it instead: sorting thousands of
    // particles per frame is real cost this pass doesn't need to pay yet.
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 1;
    VkFormat hdrFormat = kHDRFormat; // renders into the internal HDR intermediate, not the final presentable image
    renderingInfo.pColorAttachmentFormats = &hdrFormat;
    renderingInfo.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;

    // Sprint 16: dedicated pipeline layout (not scenePipelineLayout_) --
    // set0 is the same real sceneDescriptorSetLayout_ every opaque
    // pipeline binds, set1 is postProcessSingleSetLayout_ (one combined-
    // image-sampler, reused as-is -- the exact same binding *shape*
    // luminance's own set already uses, just pointed at depth here) for
    // real soft-particle depth sampling. See this pipeline's own header
    // comment on why it doesn't extend scenePipelineLayout_ instead.
    std::array<VkDescriptorSetLayout, 2> particleSetLayouts{sceneDescriptorSetLayout_, postProcessSingleSetLayout_};
    VkPipelineLayoutCreateInfo particleLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    particleLayoutInfo.setLayoutCount = static_cast<uint32_t>(particleSetLayouts.size());
    particleLayoutInfo.pSetLayouts = particleSetLayouts.data();
    if (vkCreatePipelineLayout(device_, &particleLayoutInfo, nullptr, &particlePipelineLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreatePipelineLayout failed (particle).\n");
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, fragModule, nullptr);
        return false;
    }
    pipelineInfo.layout = particlePipelineLayout_;

    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &particlePipeline_);

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateGraphicsPipelines failed (particle).\n");
        return false;
    }
    return true;
}

void Renderer::destroyParticlePipeline() {
    if (particlePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, particlePipelineLayout_, nullptr);
        particlePipelineLayout_ = VK_NULL_HANDLE;
    }
    if (particlePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, particlePipeline_, nullptr);
        particlePipeline_ = VK_NULL_HANDLE;
    }
}

bool Renderer::createMaterialResources() {
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &materialSampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateSampler (material) failed.\n");
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // 0=albedo, 1=normal, 2=metallic, 3=roughness, 4=ao -- see Components.hpp's Renderable texture fields.
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{binding, binding, binding, binding, binding};
    for (uint32_t i = 0; i < bindings.size(); ++i) bindings[i].binding = i;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &materialDescriptorSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorSetLayout (material) failed.\n");
        return false;
    }

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxMaterialDescriptorSets * 5};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = kMaxMaterialDescriptorSets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &materialDescriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorPool (material) failed.\n");
        return false;
    }

    defaultWhiteTexture_ =
        Texture::createSolidColor(255, 255, 255, 255, allocator_, device_, commandPool_, graphicsQueue_);
    defaultFlatNormalTexture_ =
        Texture::createSolidColor(128, 128, 255, 255, allocator_, device_, commandPool_, graphicsQueue_);
    if (!defaultWhiteTexture_.isValid() || !defaultFlatNormalTexture_.isValid()) {
        std::fprintf(stderr, "Renderer: default material texture creation failed.\n");
        return false;
    }
    return true;
}

void Renderer::destroyMaterialResources() {
    materialDescriptorCache_.clear(); // the sets themselves are freed when materialDescriptorPool_ is destroyed below
    defaultWhiteTexture_.destroy(allocator_, device_);
    defaultFlatNormalTexture_.destroy(allocator_, device_);
    if (materialDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, materialDescriptorPool_, nullptr);
        materialDescriptorPool_ = VK_NULL_HANDLE;
    }
    if (materialDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, materialDescriptorSetLayout_, nullptr);
        materialDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
    if (materialSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, materialSampler_, nullptr);
        materialSampler_ = VK_NULL_HANDLE;
    }
}

VkDescriptorSet Renderer::getOrCreateMaterialDescriptorSet(const Renderable& renderable, TextureLibrary& textureLibrary) {
    std::array<uint32_t, 5> key{renderable.albedoTexture, renderable.normalTexture, renderable.metallicTexture,
                                 renderable.roughnessTexture, renderable.aoTexture};
    auto cached = materialDescriptorCache_.find(key);
    if (cached != materialDescriptorCache_.end()) return cached->second;

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = materialDescriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &materialDescriptorSetLayout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &set) != VK_SUCCESS) {
        std::fprintf(stderr,
                     "Renderer: vkAllocateDescriptorSets (material) failed -- out of kMaxMaterialDescriptorSets "
                     "(%u)? Falling back to whatever set=1 was last bound for this entity.\n",
                     kMaxMaterialDescriptorSets);
        return VK_NULL_HANDLE;
    }

    auto resolveView = [&](uint32_t handle, const Texture& fallback) -> VkImageView {
        const Texture* tex = handle != Renderable::kInvalidHandle ? textureLibrary.get(handle) : nullptr;
        return (tex != nullptr && tex->isValid()) ? tex->view() : fallback.view();
    };

    std::array<VkDescriptorImageInfo, 5> imageInfos{};
    imageInfos[0] = {materialSampler_, resolveView(renderable.albedoTexture, defaultWhiteTexture_),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[1] = {materialSampler_, resolveView(renderable.normalTexture, defaultFlatNormalTexture_),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[2] = {materialSampler_, resolveView(renderable.metallicTexture, defaultWhiteTexture_),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[3] = {materialSampler_, resolveView(renderable.roughnessTexture, defaultWhiteTexture_),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[4] = {materialSampler_, resolveView(renderable.aoTexture, defaultWhiteTexture_),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    std::array<VkWriteDescriptorSet, 5> writes{};
    for (uint32_t i = 0; i < writes.size(); ++i) {
        writes[i] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &imageInfos[i];
    }
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    materialDescriptorCache_[key] = set;
    return set;
}

bool Renderer::createPostProcessResources() {
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &postProcessSampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateSampler (post-process) failed.\n");
        return false;
    }

    VkDescriptorSetLayoutBinding singleBinding{};
    singleBinding.binding = 0;
    singleBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    singleBinding.descriptorCount = 1;
    singleBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo singleLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    singleLayoutInfo.bindingCount = 1;
    singleLayoutInfo.pBindings = &singleBinding;
    if (vkCreateDescriptorSetLayout(device_, &singleLayoutInfo, nullptr, &postProcessSingleSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorSetLayout (post-process single) failed.\n");
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 2> dualBindings{singleBinding, singleBinding};
    dualBindings[1].binding = 1;

    VkDescriptorSetLayoutCreateInfo dualLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dualLayoutInfo.bindingCount = static_cast<uint32_t>(dualBindings.size());
    dualLayoutInfo.pBindings = dualBindings.data();
    if (vkCreateDescriptorSetLayout(device_, &dualLayoutInfo, nullptr, &postProcessDualSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorSetLayout (post-process dual) failed.\n");
        return false;
    }

    // Sprint 16 ("Cinematic Graphics"): a dedicated NEAREST-filtered
    // sampler for reading depth back as a texture (drawCinematicPass())
    // -- see depthSampler_'s own declaration comment for why this can't
    // just reuse postProcessSampler_ (LINEAR). Its descriptor set layout
    // reuses dualBindings' shape (2 combined-image-sampler bindings) but
    // gets its own distinct VkDescriptorSetLayout object since it's
    // conceptually a different pass's inputs (hdrColor + depth, not HDR +
    // bloom), even though the binding shape happens to match.
    VkSamplerCreateInfo depthSamplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    depthSamplerInfo.magFilter = VK_FILTER_NEAREST;
    depthSamplerInfo.minFilter = VK_FILTER_NEAREST;
    depthSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device_, &depthSamplerInfo, nullptr, &depthSampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateSampler (depth) failed.\n");
        return false;
    }
    if (vkCreateDescriptorSetLayout(device_, &dualLayoutInfo, nullptr, &cinematicDescriptorSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorSetLayout (cinematic) failed.\n");
        return false;
    }

    // 7 sets (bloomExtract + composite + cinematic + luminance +
    // particleDepth + Kronos "Rendering Fidelity Foundation" Phase 1.2's
    // volumetric-fog input + Kronos "Rendering Fidelity" SSR-fallback's
    // own input) per frame-in-flight *plus* per auxiliary scene slot (see
    // AuxiliarySceneHandle's doc comment -- each studio::PreviewScene's
    // ensurePostProcessTargets() call draws from this same pool); 11
    // combined-image-sampler descriptors per slot (1 for bloomExtract's
    // single binding, 2 for composite's dual bindings, 2 for cinematic's
    // dual bindings, 1 for luminance's single binding, 1 for
    // particleDepth's single binding, 2 for the fog input's own dual
    // bindings, 2 for the SSR input's own dual bindings -- see
    // FrameSync::fogInputDescriptorSet's/ssrInputDescriptorSet's own
    // comments).
    uint32_t totalPostProcessSlots = framesInFlight_ + static_cast<uint32_t>(kMaxAuxiliaryScenes);
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, totalPostProcessSlots * 11};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    // FREE_DESCRIPTOR_SET_BIT: ensurePostProcessTargets() frees and
    // reallocates these sets on every resize (window resize, Studio
    // viewport panel resize), not just once at startup.
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = totalPostProcessSlots * 7;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &postProcessDescriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateDescriptorPool (post-process) failed.\n");
        return false;
    }
    return true;
}

void Renderer::destroyPostProcessResources() {
    if (postProcessDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, postProcessDescriptorPool_, nullptr);
        postProcessDescriptorPool_ = VK_NULL_HANDLE;
    }
    if (cinematicDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, cinematicDescriptorSetLayout_, nullptr);
        cinematicDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
    if (depthSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, depthSampler_, nullptr);
        depthSampler_ = VK_NULL_HANDLE;
    }
    if (postProcessDualSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, postProcessDualSetLayout_, nullptr);
        postProcessDualSetLayout_ = VK_NULL_HANDLE;
    }
    if (postProcessSingleSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, postProcessSingleSetLayout_, nullptr);
        postProcessSingleSetLayout_ = VK_NULL_HANDLE;
    }
    if (postProcessSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, postProcessSampler_, nullptr);
        postProcessSampler_ = VK_NULL_HANDLE;
    }
}

bool Renderer::createPostProcessPipelines() {
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/fullscreen.vert.spv");
    if (vertCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shader \"%s/fullscreen.vert.spv\".\n", shaderDir.c_str());
        return false;
    }
    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    if (vertModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule failed (fullscreen.vert).\n");
        return false;
    }

    // Fixed-function state shared by both full-screen-triangle passes: no
    // vertex input at all (see shaders/fullscreen.vert), no depth test/
    // write, no blending (bloom_extract writes a fresh half-res target;
    // composite overwrites the destination outright).
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // --- Bloom extract: writes into the half-res HDR bloom target ---
    VkFormat hdrFormat = kHDRFormat;
    bool ok = true;
    {
        auto fragCode = readBinaryFile(shaderDir + "/bloom_extract.frag.spv");
        VkShaderModule fragModule = fragCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, fragCode);
        if (fragCode.empty() || fragModule == VK_NULL_HANDLE) {
            std::fprintf(stderr, "Renderer: failed to load shaders for bloom extract pipeline.\n");
            ok = false;
        } else {
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(BloomPushConstants);

            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &postProcessSingleSetLayout_;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &bloomExtractPipelineLayout_) == VK_SUCCESS;

            if (ok) {
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vertModule;
                stages[0].pName = "main";
                stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = fragModule;
                stages[1].pName = "main";

                VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &hdrFormat;

                VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages;
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = bloomExtractPipelineLayout_;

                ok = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &bloomExtractPipeline_) ==
                     VK_SUCCESS;
            }
            vkDestroyShaderModule(device_, fragModule, nullptr);
        }
    }
    if (!ok) {
        std::fprintf(stderr, "Renderer: bloom extract pipeline creation failed.\n");
        vkDestroyShaderModule(device_, vertModule, nullptr);
        return false;
    }

    // --- Composite: writes into the caller-provided final color target
    // (engine_runtime's swapchain image or Studio's OffscreenTarget --
    // both always swapchainFormat_, see OffscreenTarget::ensureSize()'s
    // call site in StudioApp.cpp) ---
    {
        auto fragCode = readBinaryFile(shaderDir + "/composite.frag.spv");
        VkShaderModule fragModule = fragCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, fragCode);
        if (fragCode.empty() || fragModule == VK_NULL_HANDLE) {
            std::fprintf(stderr, "Renderer: failed to load shaders for composite pipeline.\n");
            ok = false;
        } else {
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(CompositePushConstants);

            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &postProcessDualSetLayout_;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &compositePipelineLayout_) == VK_SUCCESS;

            if (ok) {
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vertModule;
                stages[0].pName = "main";
                stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = fragModule;
                stages[1].pName = "main";

                VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &swapchainFormat_;

                VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages;
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = compositePipelineLayout_;

                ok = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compositePipeline_) ==
                     VK_SUCCESS;
            }
            vkDestroyShaderModule(device_, fragModule, nullptr);
        }
    }

    if (!ok) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        std::fprintf(stderr, "Renderer: composite pipeline creation failed.\n");
        return false;
    }

    // --- Sprint 16 ("Cinematic Graphics") cinematic pass: frame.hdrImage +
    // depth (read) -> frame.cinematicImage (write, full-res kHDRFormat --
    // see FrameSync's own comment) ---
    {
        auto fragCode = readBinaryFile(shaderDir + "/cinematic.frag.spv");
        VkShaderModule fragModule = fragCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, fragCode);
        if (fragCode.empty() || fragModule == VK_NULL_HANDLE) {
            std::fprintf(stderr, "Renderer: failed to load shaders for cinematic pipeline.\n");
            ok = false;
        } else {
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(CinematicPushConstants);

            std::array<VkDescriptorSetLayout, 2> setLayouts{sceneDescriptorSetLayout_, cinematicDescriptorSetLayout_};
            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
            layoutInfo.pSetLayouts = setLayouts.data();
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &cinematicPipelineLayout_) == VK_SUCCESS;

            if (ok) {
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vertModule;
                stages[0].pName = "main";
                stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = fragModule;
                stages[1].pName = "main";

                VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &hdrFormat;

                VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages;
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = cinematicPipelineLayout_;

                ok = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &cinematicPipeline_) ==
                     VK_SUCCESS;
            }
            vkDestroyShaderModule(device_, fragModule, nullptr);
        }
    }

    if (!ok) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        std::fprintf(stderr, "Renderer: cinematic pipeline creation failed.\n");
        return false;
    }

    // --- Kronos ("Rendering Fidelity Foundation" Phase 1.2): real
    // raymarched volumetric fog + light shafts. frame.hdrImage + depth
    // (read) -> frame.fogImage (write, full-res kHDRFormat) -- see
    // shaders/volumetric_fog.frag's own header comment. Own pipeline
    // layout (own push-constant range/struct), but reuses
    // cinematicDescriptorSetLayout_ as-is for set=1 (same real 2-binding
    // hdrColor+sceneDepth shape this pass needs). ---
    {
        auto fragCode = readBinaryFile(shaderDir + "/volumetric_fog.frag.spv");
        VkShaderModule fragModule = fragCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, fragCode);
        if (fragCode.empty() || fragModule == VK_NULL_HANDLE) {
            std::fprintf(stderr, "Renderer: failed to load shaders for volumetric fog pipeline.\n");
            ok = false;
        } else {
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(VolumetricFogPushConstants);

            std::array<VkDescriptorSetLayout, 2> setLayouts{sceneDescriptorSetLayout_, cinematicDescriptorSetLayout_};
            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
            layoutInfo.pSetLayouts = setLayouts.data();
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &volumetricFogPipelineLayout_) == VK_SUCCESS;

            if (ok) {
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vertModule;
                stages[0].pName = "main";
                stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = fragModule;
                stages[1].pName = "main";

                VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &hdrFormat;

                VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages;
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = volumetricFogPipelineLayout_;

                ok = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                                &volumetricFogPipeline_) == VK_SUCCESS;
            }
            vkDestroyShaderModule(device_, fragModule, nullptr);
        }
    }

    if (!ok) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        std::fprintf(stderr, "Renderer: volumetric fog pipeline creation failed.\n");
        return false;
    }

    // --- Kronos ("Rendering Fidelity" -- SSR fallback pass): real
    // screen-space reflections. frame.hdrImage + depth (read) ->
    // frame.ssrImage (write, full-res kHDRFormat) -- see
    // shaders/ssr.frag's own header comment. Own pipeline layout (own
    // push-constant range/struct), reuses cinematicDescriptorSetLayout_
    // as-is for set=1, same real 2-binding hdrColor+sceneDepth shape
    // volumetricFogPipelineLayout_ above already reuses it for. ---
    {
        auto fragCode = readBinaryFile(shaderDir + "/ssr.frag.spv");
        VkShaderModule fragModule = fragCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, fragCode);
        if (fragCode.empty() || fragModule == VK_NULL_HANDLE) {
            std::fprintf(stderr, "Renderer: failed to load shaders for SSR pipeline.\n");
            ok = false;
        } else {
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(SSRPushConstants);

            std::array<VkDescriptorSetLayout, 2> setLayouts{sceneDescriptorSetLayout_, cinematicDescriptorSetLayout_};
            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
            layoutInfo.pSetLayouts = setLayouts.data();
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &ssrPipelineLayout_) == VK_SUCCESS;

            if (ok) {
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vertModule;
                stages[0].pName = "main";
                stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = fragModule;
                stages[1].pName = "main";

                VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &hdrFormat;

                VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages;
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = ssrPipelineLayout_;

                ok = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &ssrPipeline_) ==
                     VK_SUCCESS;
            }
            vkDestroyShaderModule(device_, fragModule, nullptr);
        }
    }

    if (!ok) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        std::fprintf(stderr, "Renderer: SSR pipeline creation failed.\n");
        return false;
    }

    // --- Sprint 16 auto-exposure: frame.hdrImage (read) -> a real 1x1
    // luminance target (write) -- see FrameSync's own comment and
    // Renderer::drawLuminancePass(). Reuses postProcessSingleSetLayout_
    // as-is (bloom extract's own layout shape: one combined-image-sampler
    // binding) rather than a bespoke layout. ---
    {
        auto fragCode = readBinaryFile(shaderDir + "/luminance.frag.spv");
        VkShaderModule fragModule = fragCode.empty() ? VK_NULL_HANDLE : createShaderModule(device_, fragCode);
        if (fragCode.empty() || fragModule == VK_NULL_HANDLE) {
            std::fprintf(stderr, "Renderer: failed to load shaders for luminance pipeline.\n");
            ok = false;
        } else {
            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &postProcessSingleSetLayout_;
            ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &luminancePipelineLayout_) == VK_SUCCESS;

            if (ok) {
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vertModule;
                stages[0].pName = "main";
                stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = fragModule;
                stages[1].pName = "main";

                VkFormat luminanceFormat = VK_FORMAT_R32_SFLOAT;
                VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &luminanceFormat;

                VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages;
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = luminancePipelineLayout_;

                ok = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &luminancePipeline_) ==
                     VK_SUCCESS;
            }
            vkDestroyShaderModule(device_, fragModule, nullptr);
        }
    }

    vkDestroyShaderModule(device_, vertModule, nullptr);

    if (!ok) {
        std::fprintf(stderr, "Renderer: luminance pipeline creation failed.\n");
        return false;
    }
    return true;
}

bool Renderer::createSkyPipeline() {
    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    auto vertCode = readBinaryFile(shaderDir + "/fullscreen.vert.spv");
    auto fragCode = readBinaryFile(shaderDir + "/sky.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        std::fprintf(stderr, "Renderer: failed to read compiled shaders for the sky pipeline.\n");
        return false;
    }
    VkShaderModule vertModule = createShaderModule(device_, vertCode);
    VkShaderModule fragModule = createShaderModule(device_, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Renderer: vkCreateShaderModule failed for the sky pipeline.\n");
        if (vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, vertModule, nullptr);
        if (fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, fragModule, nullptr);
        return false;
    }

    // Same set 0 every scene/shadow pipeline already binds (SceneUBO +
    // the shadow map array) -- sky.frag only reads a handful of SceneUBO
    // fields (invViewProj, viewPositionWS, skyZenithColor/skyHorizonColor)
    // and never touches binding 1 at all, the same "declares/binds more
    // than it reads" shape createShadowPipeline() already established
    // for this exact set layout.
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &sceneDescriptorSetLayout_;
    bool ok = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &skyPipelineLayout_) == VK_SUCCESS;

    if (ok) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";
        stages[1] = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

        // No vertex input at all (shaders/fullscreen.vert), no depth
        // test/write (a background pass must never occlude or be
        // occluded by depth -- scene geometry drawn afterward with a
        // normal depth test naturally overdraws whatever it covers), no
        // blending (this pass writes every covered pixel outright).
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkFormat hdrFormat = kHDRFormat; // writes into frame.hdrView, same target scene geometry draws into
        VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &hdrFormat;
        renderingInfo.depthAttachmentFormat = depthFormat_; // must match the render scope's real depth attachment format even though this pipeline never tests/writes it

        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = skyPipelineLayout_;

        ok = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skyPipeline_) == VK_SUCCESS;
    }

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (!ok) {
        std::fprintf(stderr, "Renderer: sky pipeline creation failed.\n");
        return false;
    }
    return true;
}

void Renderer::destroySkyPipeline() {
    if (skyPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, skyPipeline_, nullptr);
        skyPipeline_ = VK_NULL_HANDLE;
    }
    if (skyPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, skyPipelineLayout_, nullptr);
        skyPipelineLayout_ = VK_NULL_HANDLE;
    }
}

void Renderer::destroyPostProcessPipelines() {
    if (luminancePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, luminancePipeline_, nullptr);
        luminancePipeline_ = VK_NULL_HANDLE;
    }
    if (luminancePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, luminancePipelineLayout_, nullptr);
        luminancePipelineLayout_ = VK_NULL_HANDLE;
    }
    if (cinematicPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, cinematicPipeline_, nullptr);
        cinematicPipeline_ = VK_NULL_HANDLE;
    }
    if (cinematicPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, cinematicPipelineLayout_, nullptr);
        cinematicPipelineLayout_ = VK_NULL_HANDLE;
    }
    if (volumetricFogPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, volumetricFogPipeline_, nullptr);
        volumetricFogPipeline_ = VK_NULL_HANDLE;
    }
    if (volumetricFogPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, volumetricFogPipelineLayout_, nullptr);
        volumetricFogPipelineLayout_ = VK_NULL_HANDLE;
    }
    if (ssrPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, ssrPipeline_, nullptr);
        ssrPipeline_ = VK_NULL_HANDLE;
    }
    if (ssrPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, ssrPipelineLayout_, nullptr);
        ssrPipelineLayout_ = VK_NULL_HANDLE;
    }
    if (compositePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, compositePipeline_, nullptr);
        compositePipeline_ = VK_NULL_HANDLE;
    }
    if (compositePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, compositePipelineLayout_, nullptr);
        compositePipelineLayout_ = VK_NULL_HANDLE;
    }
    if (bloomExtractPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, bloomExtractPipeline_, nullptr);
        bloomExtractPipeline_ = VK_NULL_HANDLE;
    }
    if (bloomExtractPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, bloomExtractPipelineLayout_, nullptr);
        bloomExtractPipelineLayout_ = VK_NULL_HANDLE;
    }
}

bool Renderer::ensurePostProcessTargets(FrameSync& frame, VkExtent2D extent, VkImageView depthView) {
    bool sizeUnchanged = frame.hdrImage != VK_NULL_HANDLE && frame.postProcessExtent.width == extent.width &&
                          frame.postProcessExtent.height == extent.height;
    if (sizeUnchanged) {
        // Sprint 16: the hdrImage/bloomImage side is already correctly
        // sized -- only the (much cheaper) cinematic-target lazy-alloc and
        // source-repoint checks below might still need to run, so this
        // deliberately doesn't early-return the way the pre-Sprint-16
        // version of this function did. Kronos Phase 1.2: fog must be
        // ensured *before* cinematic -- see ensureVolumetricFogTargets()'s
        // own comment. Kronos ("Rendering Fidelity" -- SSR): SSR must be
        // ensured before fog -- see ensureSSRTargets()'s own comment.
        if (!ensureSSRTargets(frame, extent, depthView)) return false;
        if (!ensureVolumetricFogTargets(frame, extent, depthView)) return false;
        if (!ensureCinematicTarget(frame, extent, depthView)) return false;
        if (!ensureLuminanceTarget(frame)) return false;
        if (!ensureParticleDepthDescriptor(frame, depthView)) return false;
        return true;
    }

    destroyPostProcessTargets(frame);
    frame.postProcessExtent = extent;

    VkExtent2D bloomExtent{std::max(1u, static_cast<uint32_t>(static_cast<float>(extent.width) * kBloomDownsampleFactor)),
                            std::max(1u, static_cast<uint32_t>(static_cast<float>(extent.height) * kBloomDownsampleFactor))};

    auto createTarget = [&](VkExtent2D targetExtent, VkImage& outImage, VmaAllocation& outAlloc,
                             VkImageView& outView) -> bool {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {targetExtent.width, targetExtent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = kHDRFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &outImage, &outAlloc, nullptr) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vmaCreateImage (post-process target) failed.\n");
            return false;
        }

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = outImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = kHDRFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &viewInfo, nullptr, &outView) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkCreateImageView (post-process target) failed.\n");
            return false;
        }
        return true;
    };

    if (!createTarget(extent, frame.hdrImage, frame.hdrAllocation, frame.hdrView)) return false;
    if (!createTarget(bloomExtent, frame.bloomImage, frame.bloomAllocation, frame.bloomView)) return false;

    VkDescriptorSetAllocateInfo singleAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    singleAllocInfo.descriptorPool = postProcessDescriptorPool_;
    singleAllocInfo.descriptorSetCount = 1;
    singleAllocInfo.pSetLayouts = &postProcessSingleSetLayout_;
    if (vkAllocateDescriptorSets(device_, &singleAllocInfo, &frame.bloomExtractDescriptorSet) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (bloom extract) failed.\n");
        return false;
    }

    VkDescriptorSetAllocateInfo dualAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dualAllocInfo.descriptorPool = postProcessDescriptorPool_;
    dualAllocInfo.descriptorSetCount = 1;
    dualAllocInfo.pSetLayouts = &postProcessDualSetLayout_;
    if (vkAllocateDescriptorSets(device_, &dualAllocInfo, &frame.compositeDescriptorSet) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (composite) failed.\n");
        return false;
    }

    // Valid to reference views here before they're ever transitioned to
    // SHADER_READ_ONLY_OPTIMAL for the first time -- same "the write only
    // records intent" reasoning as the shadow sampler descriptor write in
    // createSceneDescriptorResources().
    VkDescriptorImageInfo hdrImageInfo{postProcessSampler_, frame.hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo bloomImageInfo{postProcessSampler_, frame.bloomView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    std::array<VkWriteDescriptorSet, 3> writes{};
    writes[0] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[0].dstSet = frame.bloomExtractDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &hdrImageInfo;

    writes[1] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[1].dstSet = frame.compositeDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &hdrImageInfo;

    writes[2] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[2].dstSet = frame.compositeDescriptorSet;
    writes[2].dstBinding = 1;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &bloomImageInfo;

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    // hdrView is correct after a fresh (re)allocation above -- see the
    // callers below. Kronos Phase 1.2: fog must be ensured before
    // cinematic here too, same reasoning as the "size unchanged" path
    // above. Kronos ("Rendering Fidelity" -- SSR): SSR must be ensured
    // before fog, same reasoning.
    frame.cinematicSourceBound = false;
    frame.fogSourceBoundForComposite = false;
    frame.ssrSourceBoundForComposite = false;
    if (!ensureSSRTargets(frame, extent, depthView)) return false;
    if (!ensureVolumetricFogTargets(frame, extent, depthView)) return false;
    if (!ensureCinematicTarget(frame, extent, depthView)) return false;
    if (!ensureLuminanceTarget(frame)) return false;
    if (!ensureParticleDepthDescriptor(frame, depthView)) return false;
    return true;
}

// Kronos ("Rendering Fidelity" -- SSR fallback pass): lazily allocates
// frame.ssrImage/ssrView/ssrInputDescriptorSet the first time SSR is
// actually enabled for this frame slot (never eagerly -- same convention
// as ensureVolumetricFogTargets() below), and keeps
// frame.ssrInputDescriptorSet's own bindings current. This descriptor
// set's binding 0 is *never* conditional -- it always points at
// frame.hdrView, since SSR (like fog) can never legally read its own
// output. Must run before ensureVolumetricFogTargets() -- see this
// function's own declaration comment in Renderer.hpp.
bool Renderer::ensureSSRTargets(FrameSync& frame, VkExtent2D extent, VkImageView depthView) {
    if (ssrEnabled_ && frame.ssrImage == VK_NULL_HANDLE) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {extent.width, extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = kHDRFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &frame.ssrImage, &frame.ssrAllocation, nullptr) !=
            VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vmaCreateImage (SSR target) failed.\n");
            return false;
        }

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = frame.ssrImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = kHDRFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &viewInfo, nullptr, &frame.ssrView) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkCreateImageView (SSR target) failed.\n");
            return false;
        }

        VkDescriptorSetAllocateInfo allocSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocSetInfo.descriptorPool = postProcessDescriptorPool_;
        allocSetInfo.descriptorSetCount = 1;
        allocSetInfo.pSetLayouts = &cinematicDescriptorSetLayout_; // reused shape -- see this field's own Renderer.hpp comment
        if (vkAllocateDescriptorSets(device_, &allocSetInfo, &frame.ssrInputDescriptorSet) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (SSR input) failed.\n");
            return false;
        }
    }

    if (frame.ssrInputDescriptorSet != VK_NULL_HANDLE) {
        // Rewritten every call, same "depthView can legitimately change on
        // a resize" reasoning as ensureCinematicTarget()'s own identical
        // comment -- and always hdrView/depthView, never conditional (see
        // this function's own header comment).
        VkDescriptorImageInfo hdrInfo{postProcessSampler_, frame.hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo depthInfo{depthSampler_, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[0].dstSet = frame.ssrInputDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &hdrInfo;
        writes[1] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[1].dstSet = frame.ssrInputDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &depthInfo;
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    return true;
}

// Kronos ("Rendering Fidelity Foundation" Phase 1.2): lazily allocates
// frame.fogImage/fogView/fogInputDescriptorSet the first time volumetric
// fog is actually enabled for this frame slot (never eagerly -- same
// convention as ensureCinematicTarget() below), and keeps
// frame.fogInputDescriptorSet's own bindings current. Unlike
// ensureCinematicTarget(), this descriptor set's binding 0 is *never*
// conditional -- it always points at frame.hdrView, since the fog pass
// can never legally read its own output (see FrameSync::fogInputDescriptorSet's
// own comment). Must run before ensureCinematicTarget() -- see this
// function's own declaration comment in Renderer.hpp. Kronos ("Rendering
// Fidelity" -- SSR): unlike its own binding 0, this function's binding 0
// (below) *is* conditional -- frame.ssrView if SSR ran, else
// frame.hdrView -- so volumetric fog picks up SSR's reflections rather
// than bypassing them (fog should haze reflections too, matching real
// atmosphere-over-everything behavior).
bool Renderer::ensureVolumetricFogTargets(FrameSync& frame, VkExtent2D extent, VkImageView depthView) {
    if (volumetricFogEnabled_ && frame.fogImage == VK_NULL_HANDLE) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {extent.width, extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = kHDRFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &frame.fogImage, &frame.fogAllocation, nullptr) !=
            VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vmaCreateImage (volumetric fog target) failed.\n");
            return false;
        }

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = frame.fogImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = kHDRFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &viewInfo, nullptr, &frame.fogView) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkCreateImageView (volumetric fog target) failed.\n");
            return false;
        }

        VkDescriptorSetAllocateInfo allocSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocSetInfo.descriptorPool = postProcessDescriptorPool_;
        allocSetInfo.descriptorSetCount = 1;
        allocSetInfo.pSetLayouts = &cinematicDescriptorSetLayout_; // reused shape -- see this field's own Renderer.hpp comment
        if (vkAllocateDescriptorSets(device_, &allocSetInfo, &frame.fogInputDescriptorSet) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (volumetric fog input) failed.\n");
            return false;
        }
    }

    if (frame.fogInputDescriptorSet != VK_NULL_HANDLE) {
        // Rewritten every call, same "depthView can legitimately change on
        // a resize" reasoning as ensureCinematicTarget()'s own identical
        // comment. Binding 0 conditional on SSR (see this function's own
        // header comment) -- same "always just rewrite it" reasoning
        // ensureCinematicTarget() already applies to its own fog condition.
        VkImageView fogInput = (ssrEnabled_ && frame.ssrView != VK_NULL_HANDLE) ? frame.ssrView : frame.hdrView;
        VkDescriptorImageInfo hdrInfo{postProcessSampler_, fogInput, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo depthInfo{depthSampler_, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[0].dstSet = frame.fogInputDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &hdrInfo;
        writes[1] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[1].dstSet = frame.fogInputDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &depthInfo;
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    return true;
}

// Sprint 16: lazily allocates frame.cinematicImage/cinematicView/
// cinematicDescriptorSet the first time Cinematic Mode is actually
// enabled for this frame slot (never eagerly -- most FrameSync slots,
// e.g. every Studio preview scene, never turn it on), and repoints
// bloomExtractDescriptorSet's/compositeDescriptorSet's binding 0 between
// hdrView and cinematicView whenever isCinematicModeEnabled() has
// changed since the last time this ran for `frame`. Once allocated, the
// cinematic target is kept (not freed) if the mode is later disabled --
// cheap to keep, avoids alloc/free churn on every toggle. Kronos Phase 1.2:
// this pass's own *input* (binding 0 of frame.cinematicDescriptorSet) is
// now also conditional -- frame.fogView if volumetric fog is on, else
// frame.hdrView -- so Cinematic Mode picks up fog's output rather than
// bypassing it, and the *final* bloom/composite source below is a real
// 3-way choice (hdr/fog/cinematic) instead of a plain boolean.
bool Renderer::ensureCinematicTarget(FrameSync& frame, VkExtent2D extent, VkImageView depthView) {
    if (cinematicModeEnabled_ && frame.cinematicImage == VK_NULL_HANDLE) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {extent.width, extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = kHDRFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &frame.cinematicImage, &frame.cinematicAllocation,
                            nullptr) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vmaCreateImage (cinematic target) failed.\n");
            return false;
        }

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = frame.cinematicImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = kHDRFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &viewInfo, nullptr, &frame.cinematicView) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkCreateImageView (cinematic target) failed.\n");
            return false;
        }

        VkDescriptorSetAllocateInfo allocSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocSetInfo.descriptorPool = postProcessDescriptorPool_;
        allocSetInfo.descriptorSetCount = 1;
        allocSetInfo.pSetLayouts = &cinematicDescriptorSetLayout_;
        if (vkAllocateDescriptorSets(device_, &allocSetInfo, &frame.cinematicDescriptorSet) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (cinematic) failed.\n");
            return false;
        }
    }

    if (frame.cinematicDescriptorSet != VK_NULL_HANDLE) {
        // Rewritten every call (not just on first allocation): depthView
        // can legitimately change (a resize recreates the caller's depth
        // buffer) even on a call where frame.cinematicImage itself didn't
        // need to move -- cheap enough (2 descriptor writes) to just
        // always keep it current rather than tracking a third "did depth
        // change" flag. Kronos Phase 1.2: binding 0 is now conditional --
        // frame.fogView if volumetric fog ran this frame, else
        // frame.hdrView -- same "always just rewrite it" reasoning applies
        // just as well to this condition as it already did to depthView.
        VkImageView cinematicInput = (volumetricFogEnabled_ && frame.fogView != VK_NULL_HANDLE) ? frame.fogView : frame.hdrView;
        VkDescriptorImageInfo hdrInfo{postProcessSampler_, cinematicInput, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo depthInfo{depthSampler_, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[0].dstSet = frame.cinematicDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &hdrInfo;
        writes[1] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[1].dstSet = frame.cinematicDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &depthInfo;
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Kronos Phase 1.2 / ("Rendering Fidelity" -- SSR): the real "front of
    // the chain" is now a 4-way choice -- cinematicView (if Cinematic Mode
    // ran; it already consumed fog's own output as *its* input above, so
    // it's always the final choice when it ran at all), else fogView (if
    // fog ran; it already consumed SSR's own output as *its* input, see
    // ensureVolumetricFogTargets()'s own comment), else ssrView (if only
    // SSR ran), else hdrView (none ran). Three independent tracked bools
    // since any one toggle changing independently must trigger this rewrite.
    bool desiredCinematicBound = cinematicModeEnabled_;
    bool desiredFogBound = volumetricFogEnabled_;
    bool desiredSSRBound = ssrEnabled_;
    if ((frame.cinematicSourceBound != desiredCinematicBound || frame.fogSourceBoundForComposite != desiredFogBound ||
         frame.ssrSourceBoundForComposite != desiredSSRBound) &&
        (frame.cinematicImage != VK_NULL_HANDLE || frame.fogImage != VK_NULL_HANDLE || frame.ssrImage != VK_NULL_HANDLE)) {
        // Real validity checks, not just the raw enabled flags -- e.g. if
        // Cinematic Mode is on but its own target failed to allocate this
        // frame (descriptor pool exhaustion, logged there) while fog's
        // target *did* succeed, this must fall through to fogView, not
        // reference a real, but null, cinematicView.
        VkImageView source = (cinematicModeEnabled_ && frame.cinematicImage != VK_NULL_HANDLE) ? frame.cinematicView
                              : (volumetricFogEnabled_ && frame.fogImage != VK_NULL_HANDLE)     ? frame.fogView
                              : (ssrEnabled_ && frame.ssrImage != VK_NULL_HANDLE)                ? frame.ssrView
                                                                                                  : frame.hdrView;
        VkDescriptorImageInfo sourceInfo{postProcessSampler_, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[0].dstSet = frame.bloomExtractDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &sourceInfo;
        writes[1] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[1].dstSet = frame.compositeDescriptorSet;
        writes[1].dstBinding = 0;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &sourceInfo;
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        frame.cinematicSourceBound = desiredCinematicBound;
        frame.fogSourceBoundForComposite = desiredFogBound;
        frame.ssrSourceBoundForComposite = desiredSSRBound;
    }
    return true;
}

// Sprint 16 auto-exposure -- see FrameSync's own comment. A one-time
// allocation (no resize logic): a 1x1 target's size never depends on
// `extent`, unlike ensureCinematicTarget()'s targets.
bool Renderer::ensureLuminanceTarget(FrameSync& frame) {
    if (!cinematicModeEnabled_ || frame.luminanceImage != VK_NULL_HANDLE) return true;

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {1, 1, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &frame.luminanceImage, &frame.luminanceAllocation, nullptr) !=
        VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateImage (luminance target) failed.\n");
        return false;
    }

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = frame.luminanceImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &viewInfo, nullptr, &frame.luminanceView) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateImageView (luminance target) failed.\n");
        return false;
    }

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = sizeof(float);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo bufferAllocInfo{};
    bufferAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    // HOST_ACCESS_RANDOM (not SEQUENTIAL_WRITE): this buffer is *written
    // by the GPU* (vkCmdCopyImageToBuffer) and *read by the CPU* -- the
    // same real readback direction/hint publishing::captureThumbnailToFile()'s
    // own staging buffer already uses, not a CPU-writes-first pattern.
    bufferAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo resultInfo{};
    if (vmaCreateBuffer(allocator_, &bufferInfo, &bufferAllocInfo, &frame.luminanceReadbackBuffer,
                         &frame.luminanceReadbackAllocation, &resultInfo) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateBuffer (luminance readback) failed.\n");
        return false;
    }
    frame.luminanceReadbackMapped = resultInfo.pMappedData;
    *static_cast<float*>(frame.luminanceReadbackMapped) = kAutoExposureTargetLuminance; // real, sane initial value -- not garbage

    VkDescriptorSetAllocateInfo allocSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocSetInfo.descriptorPool = postProcessDescriptorPool_;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &postProcessSingleSetLayout_;
    if (vkAllocateDescriptorSets(device_, &allocSetInfo, &frame.luminanceDescriptorSet) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (luminance) failed.\n");
        return false;
    }
    VkDescriptorImageInfo hdrInfo{postProcessSampler_, frame.hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = frame.luminanceDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &hdrInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
}

// Sprint 16 soft-particle depth fade -- see this method's own .hpp
// comment on why it always runs (not gated on Cinematic Mode).
bool Renderer::ensureParticleDepthDescriptor(FrameSync& frame, VkImageView depthView) {
    if (frame.particleDepthDescriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = postProcessDescriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &postProcessSingleSetLayout_;
        if (vkAllocateDescriptorSets(device_, &allocInfo, &frame.particleDepthDescriptorSet) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (particle depth) failed.\n");
            return false;
        }
    }
    VkDescriptorImageInfo depthInfo{depthSampler_, depthView, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = frame.particleDepthDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &depthInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
}

void Renderer::destroyPostProcessTargets(FrameSync& frame) {
    if (frame.particleDepthDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.particleDepthDescriptorSet);
        frame.particleDepthDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.luminanceDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.luminanceDescriptorSet);
        frame.luminanceDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.luminanceReadbackBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, frame.luminanceReadbackBuffer, frame.luminanceReadbackAllocation);
        frame.luminanceReadbackBuffer = VK_NULL_HANDLE;
        frame.luminanceReadbackMapped = nullptr;
    }
    if (frame.luminanceView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.luminanceView, nullptr);
        frame.luminanceView = VK_NULL_HANDLE;
    }
    if (frame.luminanceImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.luminanceImage, frame.luminanceAllocation);
        frame.luminanceImage = VK_NULL_HANDLE;
    }
    if (frame.cinematicDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.cinematicDescriptorSet);
        frame.cinematicDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.cinematicView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.cinematicView, nullptr);
        frame.cinematicView = VK_NULL_HANDLE;
    }
    if (frame.cinematicImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.cinematicImage, frame.cinematicAllocation);
        frame.cinematicImage = VK_NULL_HANDLE;
    }
    frame.cinematicSourceBound = false;
    if (frame.fogInputDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.fogInputDescriptorSet);
        frame.fogInputDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.fogView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.fogView, nullptr);
        frame.fogView = VK_NULL_HANDLE;
    }
    if (frame.fogImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.fogImage, frame.fogAllocation);
        frame.fogImage = VK_NULL_HANDLE;
    }
    frame.fogSourceBoundForComposite = false;
    if (frame.ssrInputDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.ssrInputDescriptorSet);
        frame.ssrInputDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.ssrView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.ssrView, nullptr);
        frame.ssrView = VK_NULL_HANDLE;
    }
    if (frame.ssrImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.ssrImage, frame.ssrAllocation);
        frame.ssrImage = VK_NULL_HANDLE;
    }
    frame.ssrSourceBoundForComposite = false;
    if (frame.bloomExtractDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.bloomExtractDescriptorSet);
        frame.bloomExtractDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.compositeDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.compositeDescriptorSet);
        frame.compositeDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.hdrView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.hdrView, nullptr);
        frame.hdrView = VK_NULL_HANDLE;
    }
    if (frame.hdrImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.hdrImage, frame.hdrAllocation);
        frame.hdrImage = VK_NULL_HANDLE;
    }
    if (frame.bloomView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.bloomView, nullptr);
        frame.bloomView = VK_NULL_HANDLE;
    }
    if (frame.bloomImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.bloomImage, frame.bloomAllocation);
        frame.bloomImage = VK_NULL_HANDLE;
    }
    frame.postProcessExtent = {0, 0};
}

void Renderer::drawSSRPass(VkCommandBuffer cmd, FrameSync& frame, VkExtent2D extent) {
    // Real, direct bypass -- see this method's own header comment in
    // Renderer.hpp. Nothing below records so much as a barrier when SSR
    // is off.
    if (!ssrEnabled_) return;
    if (frame.ssrImage == VK_NULL_HANDLE || frame.ssrInputDescriptorSet == VK_NULL_HANDLE) {
        // ensureSSRTargets() failed this frame (already logged there) --
        // skip rather than recording a draw against a null target;
        // frame.hdrImage still flows into whatever's next unchanged
        // (ssrSourceBoundForComposite only flips once the target exists,
        // see ensureCinematicTarget()'s own tri-state -> 4-way logic).
        return;
    }

    transitionImage(cmd, frame.ssrImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = frame.ssrView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // fully overwritten every pixel
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssrPipeline_);
    std::array<VkDescriptorSet, 2> sets{frame.sceneDescriptorSet, frame.ssrInputDescriptorSet};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssrPipelineLayout_, 0,
                             static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

    SSRPushConstants push{};
    push.maxDistance = ssrMaxDistance_;
    push.thickness = ssrThickness_;
    vkCmdPushConstants(cmd, ssrPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    recordDraw(3, 1);

    vkCmdEndRendering(cmd);

    transitionImage(cmd, frame.ssrImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
}

void Renderer::drawVolumetricFogPass(VkCommandBuffer cmd, FrameSync& frame, VkExtent2D extent) {
    // Real, direct bypass -- see this method's own header comment in
    // Renderer.hpp. Nothing below records so much as a barrier when
    // volumetric fog is off.
    if (!volumetricFogEnabled_) return;
    if (frame.fogImage == VK_NULL_HANDLE || frame.fogInputDescriptorSet == VK_NULL_HANDLE) {
        // ensureVolumetricFogTargets() failed this frame (already logged
        // there) -- skip rather than recording a draw against a null
        // target; frame.hdrImage still flows into whatever's next
        // unchanged (fogSourceBoundForComposite only flips once the
        // target exists, see ensureCinematicTarget()'s own tri-state logic).
        return;
    }

    transitionImage(cmd, frame.fogImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = frame.fogView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // fully overwritten every pixel
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, volumetricFogPipeline_);
    std::array<VkDescriptorSet, 2> sets{frame.sceneDescriptorSet, frame.fogInputDescriptorSet};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, volumetricFogPipelineLayout_, 0,
                             static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

    VolumetricFogPushConstants push{};
    push.scatteringIntensity = volumetricFogScatteringIntensity_;
    push.maxDistance = volumetricFogMaxDistance_;
    push.stepCount = volumetricFogStepCount_;
    push.ambientFogContribution = volumetricFogAmbientContribution_;
    push.groundDensityMultiplier = volumetricFogGroundDensityMultiplier_;
    push.aloftDensityMultiplier = volumetricFogAloftDensityMultiplier_;
    push.groundHeightY = volumetricFogGroundHeightY_;
    push.falloffHeight = volumetricFogFalloffHeight_;
    vkCmdPushConstants(cmd, volumetricFogPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    recordDraw(3, 1);

    vkCmdEndRendering(cmd);

    transitionImage(cmd, frame.fogImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
}

void Renderer::drawCinematicPass(VkCommandBuffer cmd, FrameSync& frame, VkExtent2D extent,
                                  const glm::mat4& previousViewProj) {
    // Real, direct bypass -- see this method's own header comment in
    // Renderer.hpp. Nothing below records so much as a barrier when
    // Cinematic Mode is off.
    if (!cinematicModeEnabled_) return;
    if (frame.cinematicImage == VK_NULL_HANDLE || frame.cinematicDescriptorSet == VK_NULL_HANDLE) {
        // ensureCinematicTarget() failed this frame (already logged
        // there, e.g. descriptor pool exhaustion) -- skip rather than
        // recording a draw against a null target; frame.hdrImage still
        // flows into bloom_extract unchanged (cinematicSourceBound only
        // flips once the target exists, see ensureCinematicTarget()).
        return;
    }

    // Sprint 16 auto-exposure: read back *this slot's own previous*
    // luminance measurement (real, stall-free -- guaranteed complete by
    // the same per-slot vkWaitForFences() that already gates reuse of
    // every other persistently-mapped FrameSync buffer, e.g.
    // sceneUboMapped; see FrameSync::luminanceReadbackMapped's own
    // comment), then temporally adapt frame.autoExposureValue toward the
    // exposure that measurement implies. This frame's *own* new
    // measurement is rendered at the bottom of this function, for the
    // *next* time this slot is used to read back.
    if (frame.luminanceReadbackMapped != nullptr) {
        float measuredLuminance = *static_cast<float*>(frame.luminanceReadbackMapped);
        float targetExposure = kAutoExposureTargetLuminance / std::max(measuredLuminance, 0.01f);
        frame.autoExposureValue += (targetExposure - frame.autoExposureValue) * kAutoExposureAdaptRate;
        frame.autoExposureValue = std::clamp(frame.autoExposureValue, 0.05f, 20.0f);
    }

    transitionImage(cmd, frame.cinematicImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = frame.cinematicView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // fully overwritten every pixel
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cinematicPipeline_);
    std::array<VkDescriptorSet, 2> sets{frame.sceneDescriptorSet, frame.cinematicDescriptorSet};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cinematicPipelineLayout_, 0,
                             static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

    CinematicPushConstants push{};
    push.previousViewProj = previousViewProj;
    push.focusDistance = dofFocusDistance_;
    push.focusRange = dofFocusRange_;
    push.maxCoCRadiusPx = dofMaxCoCRadiusPx_;
    push.motionBlurStrength = motionBlurStrength_;
    push.ssaoRadius = ssaoRadius_;
    push.ssaoStrength = ssaoStrength_;
    // Kronos ("Critical Visual Fixes" -- "High Quality Graphics
    // Blurriness"): real -- was unconditionally 1.0f, so any Cinematic
    // Mode caller (including RuntimeShell's own "High" quality preset,
    // which never tunes DOF for gameplay distances) got the class-default
    // DOF params applied full-strength. See setDepthOfFieldEnabled()'s
    // own comment.
    push.dofEnabled = depthOfFieldEnabled_ ? 1.0f : 0.0f;
    push.ssaoEnabled = 1.0f;
    push.heatDistortionStrength = heatDistortionEnabled_ ? heatDistortionStrength_ : 0.0f;
    push.time = totalElapsedTimeSeconds_;
    vkCmdPushConstants(cmd, cinematicPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    recordDraw(3, 1);

    vkCmdEndRendering(cmd);

    transitionImage(cmd, frame.cinematicImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    drawLuminancePass(cmd, frame);
}

// Sprint 16 auto-exposure -- renders this frame's real average-luminance
// measurement (shaders/luminance.frag, sourced from frame.hdrImage, which
// is already SHADER_READ_ONLY_OPTIMAL by this point -- see
// drawSceneIntoImpl()'s own transition) into frame.luminanceImage, then
// copies it into frame.luminanceReadbackBuffer for drawCinematicPass() to
// read back the *next* time this slot is used (see that read site's own
// comment on why that's stall-free).
void Renderer::drawLuminancePass(VkCommandBuffer cmd, FrameSync& frame) {
    if (frame.luminanceImage == VK_NULL_HANDLE || frame.luminanceDescriptorSet == VK_NULL_HANDLE) return;

    transitionImage(cmd, frame.luminanceImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = frame.luminanceView;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkExtent2D oneByOne{1, 1};
    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, oneByOne};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &attachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    VkViewport viewport{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, oneByOne};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, luminancePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, luminancePipelineLayout_, 0, 1,
                             &frame.luminanceDescriptorSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    recordDraw(3, 1);

    vkCmdEndRendering(cmd);

    transitionImage(cmd, frame.luminanceImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyImageToBuffer(cmd, frame.luminanceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, frame.luminanceReadbackBuffer,
                            1, &region);
}

void Renderer::drawBloomAndComposite(VkCommandBuffer cmd, FrameSync& frame, VkImage colorImage, VkImageView colorView,
                                      VkExtent2D extent, glm::vec2 sunScreenUV, bool sunVisible, bool applyBloom) {
    // frame.hdrImage (and, if Cinematic Mode ran this frame,
    // frame.cinematicImage) is already in SHADER_READ_ONLY_OPTIMAL by the
    // time this runs -- see drawSceneIntoImpl()'s own transition, done
    // once there rather than duplicated here and in drawCinematicPass().
    VkExtent2D bloomExtent = frame.postProcessExtent; // set by ensurePostProcessTargets(); recompute the half-res size to match
    bloomExtent.width = std::max(1u, static_cast<uint32_t>(static_cast<float>(extent.width) * kBloomDownsampleFactor));
    bloomExtent.height = std::max(1u, static_cast<uint32_t>(static_cast<float>(extent.height) * kBloomDownsampleFactor));

    // --- Bloom extract: frame.hdrImage (read) -> frame.bloomImage (write) ---
    transitionImage(cmd, frame.bloomImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo bloomAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    bloomAttachment.imageView = frame.bloomView;
    bloomAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    bloomAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo bloomRenderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    bloomRenderingInfo.renderArea = {{0, 0}, bloomExtent};
    bloomRenderingInfo.layerCount = 1;
    bloomRenderingInfo.colorAttachmentCount = 1;
    bloomRenderingInfo.pColorAttachments = &bloomAttachment;

    // Kronos ("Avatar Preview Rendering" pre-launch fix): real bloom
    // bleed, not the sky/lighting values themselves, was washing out
    // close-up preview renders (HomeAvatarPreview's "Your Avatar" box,
    // Studio's AvatarEditor/CataloguePanel/etc.) -- confirmed the actual
    // UBO sky colors reach the shader correctly (verified live via a
    // temporary debug print), so the remaining candidate was
    // post-process: bloomThreshold_'s real default (1.0, HDR linear)
    // is easily exceeded by a well-lit close-up subject under this
    // preset's own 2.6-intensity key light, and a tight preview frame
    // where the subject fills most of the image has proportionally far
    // more of its area affected by that bloom bleed than a normal
    // full-scene outdoor shot would. `applyBloom=false` for every
    // auxiliary/preview scene (see drawSceneInto()'s AuxiliarySceneHandle
    // overload) reuses this exact same real, already-proven
    // "performanceModeEnabled_" zero-bloom path -- a real "product
    // photography lightbox" backdrop shouldn't show HDR bloom blowout at
    // all, independent of whether this fully explains every washed-out
    // report.
    if (performanceModeEnabled_ || !applyBloom) {
        // Sprint 14 ("Performance Mode"): a real clear (VK_ATTACHMENT_LOAD_OP_CLEAR,
        // no draw call at all) instead of running bloomExtractPipeline_'s
        // real fragment shader across every bloom-res pixel -- a real,
        // direct GPU cost cut, not just a visual no-op (setBloomSettings'
        // intensity=0 would still pay the full extract-shader cost for
        // zero visible result; this skips the shader entirely). The
        // composite pass below reads this same real, now-black
        // frame.bloomImage unchanged, contributing zero bloom.
        bloomAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        bloomAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        vkCmdBeginRendering(cmd, &bloomRenderingInfo);
        vkCmdEndRendering(cmd);
    } else {
        bloomAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // fully overwritten every pixel -- no need to clear or preserve

        vkCmdBeginRendering(cmd, &bloomRenderingInfo);
        VkViewport bloomViewport{0.0f, 0.0f, static_cast<float>(bloomExtent.width),
                                  static_cast<float>(bloomExtent.height), 0.0f, 1.0f};
        VkRect2D bloomScissor{{0, 0}, bloomExtent};
        vkCmdSetViewport(cmd, 0, 1, &bloomViewport);
        vkCmdSetScissor(cmd, 0, 1, &bloomScissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomExtractPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomExtractPipelineLayout_, 0, 1,
                                 &frame.bloomExtractDescriptorSet, 0, nullptr);
        BloomPushConstants bloomPush{bloomThreshold_, bloomSoftKnee_};
        vkCmdPushConstants(cmd, bloomExtractPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(bloomPush), &bloomPush);
        vkCmdDraw(cmd, 3, 1, 0, 0); // full-screen triangle -- no vertex/index buffer bound, see shaders/fullscreen.vert
        recordDraw(3, 1);

        vkCmdEndRendering(cmd);
    }

    transitionImage(cmd, frame.bloomImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    // --- Composite: frame.hdrImage + frame.bloomImage (read) -> colorImage (write) ---
    transitionImage(cmd, colorImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_NONE,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = colorView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo compositeRenderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    compositeRenderingInfo.renderArea = {{0, 0}, extent};
    compositeRenderingInfo.layerCount = 1;
    compositeRenderingInfo.colorAttachmentCount = 1;
    compositeRenderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &compositeRenderingInfo);
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipelineLayout_, 0, 1,
                             &frame.compositeDescriptorSet, 0, nullptr);
    CompositePushConstants compositePush{};
    // Sprint 16: with Cinematic Mode on, this slot's own real, temporally-
    // adapted auto-exposure value (see drawCinematicPass()'s readback)
    // drives exposure instead of the flat manual exposure_ -- "dynamic
    // exposure (auto-exposure curve)" from the brief. With it off, this
    // composite pass stays byte-for-byte the pre-Sprint-16 result
    // (vignette/CA/god-rays at their real zero-effect value, saturation
    // at 1.0 == no-op mix()).
    compositePush.exposure = (cinematicModeEnabled_ && autoExposureEnabled_) ? frame.autoExposureValue : exposure_;
    compositePush.bloomIntensity = bloomIntensity_;
    if (cinematicModeEnabled_) {
        compositePush.vignetteStrength = vignetteStrength_;
        compositePush.chromaticAberrationStrength = chromaticAberrationStrength_;
        compositePush.saturation = saturation_;
        compositePush.godRayStrength = godRayStrength_;
        compositePush.sunScreenX = sunScreenUV.x;
        compositePush.sunScreenY = sunScreenUV.y;
        compositePush.sunVisible = sunVisible ? 1.0f : 0.0f;
    } else {
        compositePush.saturation = 1.0f;
    }
    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Accessibility: Colorblind modes"): real, applies
    // regardless of Cinematic Mode -- colorblind correction is an
    // accessibility need, not a cosmetic grading choice, so it stays
    // active even with every other composite-pass effect at its own
    // real, cinematic-mode-off zero value.
    compositePush.colorblindMode = static_cast<float>(colorblindModeIndex_);
    vkCmdPushConstants(cmd, compositePipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(compositePush),
                        &compositePush);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    recordDraw(3, 1);

    vkCmdEndRendering(cmd);
    // colorImage left in COLOR_ATTACHMENT_OPTIMAL -- same contract
    // drawSceneInto() has always had; the caller (renderFrame() or
    // StudioApp's prePassCallback) transitions it onward from here.
}

void Renderer::transitionImage(VkCommandBuffer cmd, VkImage image,
                                VkImageLayout oldLayout, VkImageLayout newLayout,
                                VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess,
                                VkPipelineStageFlags2 srcStage, VkPipelineStageFlags2 dstStage,
                                VkImageAspectFlags aspect, uint32_t layerCount) const {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, layerCount};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void Renderer::recordDraw(uint32_t indexOrVertexCount, uint32_t instanceCount) {
    frameDrawCalls_ += 1;
    frameTriangles_ += (static_cast<uint64_t>(indexOrVertexCount) / 3) * instanceCount;
}

void Renderer::drawSceneIntoImpl(FrameSync& frame, VkCommandBuffer cmd, VkImage colorImage, VkImageView colorView,
                                  VkImage depthImage, VkImageView depthView, VkExtent2D extent, const Camera& camera,
                                  ECS& ecs, MeshLibrary& meshLibrary, const ParticleSystem& particleSystem,
                                  TextureLibrary& textureLibrary, RiggedMeshLibrary* riggedMeshLibrary,
                                  bool applyWeatherEffects, bool applyBloom, bool suppressSunDisk,
                                  bool useFlatBackground) {
    // (Re)creates frame.hdrImage/bloomImage if `extent` changed since the
    // last call -- see FrameSync's comment and OffscreenTarget::ensureSize()
    // for the same lazy-resize pattern applied elsewhere in this codebase.
    if (!ensurePostProcessTargets(frame, extent, depthView)) {
        std::fprintf(stderr, "Renderer: ensurePostProcessTargets failed -- skipping this frame's scene draw.\n");
        return;
    }

    float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);

    // Sprint 14 ("RTX Upgrade" Phase 2): real, per-frame TLAS rebuild --
    // only collects/rebuilds when the user's own real toggle is on (a
    // real, honest early-out otherwise, matching every other optional
    // real cost in this function). Mirrors drawShadowPass()'s own
    // shadow-caster filter (`visible && castsShadow`) exactly, so a
    // ray-traced shadow and the existing CSM rasterized one agree on
    // which entities cast a shadow at all -- see RayTracingScene.hpp's
    // own header comment for why only MeshSource-described Box/Plane
    // entities can actually participate this pass. Kronos ("Rendering
    // Fidelity Foundation" Phase 1.3): also rebuilds when
    // rtReflectionsEnabled_ alone is on (shadows off) -- reflections need
    // this exact same real TLAS to trace against even with shadow tracing
    // disabled; without this real `||`, enabling reflections alone would
    // silently trace against a stale or empty TLAS with no error.
    if (rayTracedShadowsEnabled_ || rtReflectionsEnabled_) {
        std::vector<RayTracingScene::Instance> rtInstances;
        auto rtView = ecs.view<Transform, Renderable, MeshSource>();
        for (auto entity : rtView) {
            const auto& renderable = rtView.get<Renderable>(entity);
            if (!renderable.visible || !renderable.castsShadow) continue;
            const auto& meshSource = rtView.get<MeshSource>(entity);
            if (meshSource.kind != MeshSourceKind::Box && meshSource.kind != MeshSourceKind::Plane) continue;
            const auto& transform = rtView.get<Transform>(entity);
            RayTracingScene::Instance rtInstance{meshSource.kind, meshSource.params, transform.matrix()};
            // Kronos Phase 1.3: real material data riding along for
            // reflections -- see RayTracingScene::Instance's own comment.
            // Harmless to always populate (a real, tiny 5-float copy)
            // even on a frame where only shadows are enabled.
            rtInstance.baseColor = renderable.baseColor;
            rtInstance.metallic = renderable.metallic;
            rtInstance.roughness = renderable.roughness;
            rtInstances.push_back(rtInstance);
        }
        VkAccelerationStructureKHR tlasBefore = rayTracingScene_.tlas();
        VkBuffer materialsBufferBefore = rayTracingScene_.materialsBuffer();
        rayTracingScene_.rebuild(rtInstances);
        if (rayTracingScene_.tlas() != tlasBefore || rayTracingScene_.materialsBuffer() != materialsBufferBefore) {
            // The grow-only TLAS and/or materials buffer had to reallocate
            // this frame, producing a real, different handle -- every
            // frame's own descriptor set (not just this one) needs to
            // real-point at the new one before it's next used, since each
            // FrameSync/AuxiliarySceneHandle owns an independent real
            // descriptor set. The two buffers can reallocate independently
            // (different byte-size-per-instance, different starting
            // capacity), so either one changing must trigger this same
            // real propagation -- not just the TLAS.
            for (auto& f : frames_) updateRayTracedShadowDescriptor(f);
            for (auto& aux : auxiliaryScenes_) updateRayTracedShadowDescriptor(aux);
        }
    }

    // The UBO (including every cascade's lightViewProj) is filled once,
    // before either pass runs: the shadow pass's vertex shader indexes
    // lightViewProj[cascadeIndex], and the main pass's fragment shader
    // needs the whole array too (to re-derive the right cascade's
    // light-space position for shadow sampling) -- both read it through
    // the same descriptor set, so there's exactly one write, not one per pass.
    CascadeData cascades = computeCascades(camera, aspectRatio);

    SceneUBO ubo{};
    ubo.view = camera.viewMatrix();
    ubo.proj = camera.projectionMatrix(aspectRatio);
    ubo.invViewProj = glm::inverse(ubo.proj * ubo.view); // one CPU-side inverse/frame -- see this field's own comment in SceneTypes.hpp
    for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade) {
        ubo.lightViewProj[cascade] = cascades.lightViewProj[cascade];
    }
    ubo.cascadeSplitsView = glm::vec4(cascades.splitDepths[0], cascades.splitDepths[1], cascades.splitDepths[2], 0.0f);
    ubo.cascadeBiasScale = glm::vec4(cascades.depthRanges[0] / kReferenceShadowDepthRange,
                                      cascades.depthRanges[1] / kReferenceShadowDepthRange,
                                      cascades.depthRanges[2] / kReferenceShadowDepthRange, 0.0f);
    // Kronos ("Rendering Fidelity Foundation" Phase 1.1): real weather
    // composed on top of whatever setLighting() last provided -- see
    // applyWeather()'s own comment for why Clear weather is an exact,
    // zero-cost no-op here (`effectiveLighting` is just `lighting_` by
    // reference-equal value in that case). Every caller of setLighting()
    // gets weather "for free," including this same struct's own
    // WeatherProfile::wetness feeding renderFlags.z below.
    //
    // Kronos ("Avatar Scene Lighting Calibration Pass"): `applyWeatherEffects`
    // (false for every auxiliary/preview scene, see this function's own
    // header comment) real-substitutes `weatherProfileFor(WeatherKind::Clear)`
    // in place of the outdoor world's own current blended weather --
    // `applyWeather()` already treats Clear as a real, exact identity, so
    // this is a genuine no-op for preview scenes, not an approximation.
    WeatherProfile weatherProfile =
        applyWeatherEffects ? currentBlendedProfile(weatherState_) : weatherProfileFor(WeatherKind::Clear);
    SceneLighting effectiveLighting = applyWeather(lighting_, weatherProfile);
    ubo.lightDirectionWS = glm::vec4(glm::normalize(effectiveLighting.directionWS), 0.0f);
    ubo.lightColorIntensity = glm::vec4(effectiveLighting.color, effectiveLighting.intensity);
    ubo.viewPositionWS = glm::vec4(camera.position, 1.0f);
    ubo.ambientColor = glm::vec4(effectiveLighting.ambient, 0.0f);
    ubo.ambientGroundColor = glm::vec4(effectiveLighting.ambientGround, 0.0f);
    ubo.fogColorDensity = glm::vec4(effectiveLighting.fogColor, effectiveLighting.fogDensity);
    ubo.skyZenithColor = glm::vec4(effectiveLighting.skyZenithColor, 0.0f);
    ubo.skyHorizonColor = glm::vec4(effectiveLighting.skyHorizonColor, 0.0f);
    // Sprint 14: real-true only with BOTH the user's own toggle on AND a
    // real, currently non-empty TLAS -- see this field's own comment in
    // SceneTypes.hpp. scene_rt.frag never attempts a real ray query
    // against a real, valid-but-empty (zero-instance) TLAS just because
    // the user flipped the toggle before any real shadow-caster existed.
    ubo.renderFlags.x = (rayTracedShadowsEnabled_ && rayTracingScene_.hasValidTlas()) ? 1.0f : 0.0f;
    ubo.renderFlags.y = performanceModeEnabled_ ? 1.0f : 0.0f;
    // Kronos ("Rendering Fidelity Foundation" Phase 1.1): real
    // wet-surface response -- see WeatherProfile::wetness's own comment.
    // 0 at Clear (weatherProfile.overrideStrength == 0 forces wetness
    // toward Clear's own 0.0 as any transition settles, see
    // currentBlendedProfile()), so this is a real no-op end-state, not
    // just a currently-unused slot. z was documented std140 padding
    // before this -- w remains real, unused padding.
    ubo.renderFlags.z = std::clamp(weatherProfile.wetness, 0.0f, 1.0f);
    // Kronos ("Four RTX Maps" Phase 5c): real underwater caustic-light
    // strength -- see setUnderwaterCausticsEnabled()'s own comment. 0 (the
    // default, every non-Underwater map) is a real, exact identity in both
    // scene.frag and scene_rt.frag's own matching hook. w was documented
    // std140 padding before this.
    ubo.renderFlags.w = underwaterCausticsEnabled_ ? 1.0f : 0.0f;

    // Sprint 16 point lights -- copy up to kMaxPointLights real entries;
    // extras are silently dropped (see SceneLighting::pointLights' own
    // comment), remaining UBO slots stay zero-intensity from SceneUBO{}'s
    // own default member initializers above, a real no-op in
    // computePointLights() below.
    uint32_t pointLightCount = std::min<uint32_t>(static_cast<uint32_t>(lighting_.pointLights.size()), kMaxPointLights);
    for (uint32_t i = 0; i < pointLightCount; ++i) {
        const SceneLighting::PointLight& light = lighting_.pointLights[i];
        ubo.pointLightPositionRadius[i] = glm::vec4(light.position, light.radius);
        ubo.pointLightColorIntensity[i] = glm::vec4(light.color, light.intensity);
    }
    // Kronos (Alpha Roadmap Phase 3, "Component system"): real entity-
    // driven point lights -- every live Light+Transform entity contributes
    // a real slot here too, filling whatever budget the manually-authored
    // SceneLighting::pointLights entries above didn't already use (same
    // kMaxPointLights cap, extras silently dropped, matching that array's
    // own established convention). World position goes through
    // hierarchy::computeWorldMatrix() rather than raw Transform::position
    // so a Light parented under a moving rig tracks it correctly -- see
    // Components.hpp's own Light comment.
    auto lightView = ecs.view<Light, Transform>();
    for (auto entity : lightView) {
        if (pointLightCount >= kMaxPointLights) break;
        const Light& light = lightView.get<Light>(entity);
        if (!light.enabled) continue;
        glm::vec3 worldPosition = glm::vec3(hierarchy::computeWorldMatrix(ecs, entity)[3]);
        ubo.pointLightPositionRadius[pointLightCount] = glm::vec4(worldPosition, light.radius);
        ubo.pointLightColorIntensity[pointLightCount] = glm::vec4(light.color, light.intensity);
        ++pointLightCount;
    }
    ubo.pointLightCount = glm::vec4(static_cast<float>(pointLightCount), 0.0f, 0.0f, 0.0f);
    // Kronos ("Rendering Fidelity Foundation" Phase 1.3): same real
    // "toggle AND a currently-valid TLAS" gating as renderFlags.x above --
    // see this field's own comment in SceneTypes.hpp.
    ubo.reflectionParams.x = (rtReflectionsEnabled_ && rayTracingScene_.hasValidTlas()) ? 1.0f : 0.0f;
    ubo.reflectionParams.y = reflectionRoughCutoff_;
    ubo.reflectionParams.z = reflectionMirrorCutoff_;
    // Kronos ("Rendering Fidelity" -- full atmospheric-scattering skybox):
    // see SceneUBO::atmosphereParams's own comment.
    ubo.atmosphereParams.x = atmosphereScatteringEnabled_ ? 1.0f : 0.0f;
    ubo.atmosphereParams.y = atmosphereSunIntensity_;
    ubo.atmosphereParams.z = atmosphereMieStrength_;
    // Kronos ("Avatar Preview Rendering" pre-launch fix): real, previously
    // spare .w slot -- see shaders/sky.frag's own comment on why every
    // auxiliary/preview scene suppresses the sun disk/glow entirely.
    ubo.atmosphereParams.w = suppressSunDisk ? 1.0f : 0.0f;
    // Kronos ("Rendering Fidelity" -- volumetric cloud layer): see
    // SceneUBO::cloudParams's own comment.
    ubo.cloudParams.x = cloudsEnabled_ ? 1.0f : 0.0f;
    ubo.cloudParams.y = cloudCoverage_;
    ubo.cloudParams.z = cloudSpeed_;
    ubo.cloudParams.w = totalElapsedTimeSeconds_;
    // Kronos ("Rendering Fidelity" -- ray-traced bounce lighting/GI): see
    // SceneUBO::giParams's own comment. Same real "toggle AND a
    // currently-valid TLAS" gating renderFlags.x/reflectionParams.x above
    // already use.
    ubo.giParams.x = (rtGIEnabled_ && rayTracingScene_.hasValidTlas()) ? 1.0f : 0.0f;
    ubo.giParams.y = rtGIIntensity_;
    std::memcpy(frame.sceneUboMapped, &ubo, sizeof(ubo)); // persistently mapped -- no map/unmap round trip

    drawShadowPass(cmd, frame, ecs, meshLibrary);

    // Renders into frame.hdrImage (the internal HDR intermediate), not
    // colorImage directly -- drawBloomAndComposite() at the end of this
    // function reads frame.hdrImage back and writes the caller's
    // colorImage/colorView as its own final step. See this method's
    // declaration comment.
    transitionImage(cmd, frame.hdrImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT);
    transitionImage(cmd, depthImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = frame.hdrView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // Kronos ("Avatar Preview Rendering" pre-launch fix -- direct,
    // guaranteed hardware-level override, not another indirect shader
    // tuning pass): every auxiliary/preview scene (useFlatBackground)
    // gets a real, fixed dark-slate clear color here, and the sky pass
    // below is skipped for it entirely -- no sky.frag draw call at all,
    // so nothing (gradient, sun disk, atmosphere, clouds, any future sky
    // feature) can ever paint over this clear value for these scenes.
    // The main viewport is completely unaffected (useFlatBackground
    // stays false there, exact prior behavior).
    colorAttachment.clearValue.color =
        useFlatBackground ? VkClearColorValue{{0.08f, 0.09f, 0.13f, 1.0f}} : VkClearColorValue{{0.04f, 0.045f, 0.06f, 1.0f}};

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = depthView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // STORE, not DONT_CARE: the soft-particle pass below samples this
    // depth after the opaque pass ends, so its contents must survive.
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Real, basic procedural sky (task category 3) -- drawn first, depth
    // test/write both disabled (see createSkyPipeline()), so every scene
    // draw below naturally overdraws whatever it actually covers and the
    // sky gradient only ever shows through where nothing else was drawn.
    // Same fullscreen-triangle draw shape drawBloomAndComposite() already
    // uses (vkCmdDraw(3,...), no vertex/index buffer bound).
    // Kronos ("Avatar Preview Rendering" pre-launch fix): skipped
    // entirely for useFlatBackground scenes -- see colorAttachment's own
    // clearValue comment just above. The clear color alone is the whole
    // background for these scenes now.
    if (!useFlatBackground) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipelineLayout_, 0, 1,
                                 &frame.sceneDescriptorSet, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        recordDraw(3, 1);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 0, 1,
                             &frame.sceneDescriptorSet, 0, nullptr);
    // set=2 (the bindless texture array) -- bound ONCE per pass, not per
    // entity. That is the entire point of the migration: the per-draw
    // vkCmdBindDescriptorSets for set=1 in the loop below disappears.
    if (bindlessInitialised_ && bindlessSet_ != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 2, 1, &bindlessSet_, 0,
                                 nullptr);
    }

    // TODO(frame graph, §4.1): everything below this line is the opaque
    // (Forward+) pass -- see this method's declaration in Renderer.hpp for
    // where shadows/RT/transparency/post attach around it.
    auto view = ecs.view<Transform, Renderable>();
    for (auto entity : view) {
        auto& renderable = view.get<Renderable>(entity);
        // instanced entities are drawn by drawInstancedBatches() below,
        // via a batched vkCmdDrawIndexed(instanceCount=N) instead of one
        // individual draw + push-constant update each -- see
        // Components.hpp's Renderable::instanced. Glass/water entities
        // (transmission > 0) are drawn by the dedicated glass loop right
        // below instead of this opaque one -- see Renderable::transmission's
        // own comment and shaders/glass.frag's header comment.
        if (!renderable.visible || renderable.instanced || renderable.transmission > 0.0f) continue;

        const Mesh* mesh = meshLibrary.get(renderable.meshHandle);
        if (!mesh) continue;

        ObjectPushConstants push{};
        push.model = hierarchy::computeWorldMatrix(ecs, entity);
        push.baseColor = renderable.baseColor;
        push.metallicRoughness = glm::vec4(renderable.metallic, renderable.roughness, renderable.normalIntensity,
                                            renderable.useTriplanarProjection ? 1.0f : 0.0f);
        push.emissive = glm::vec4(renderable.emissiveColor, renderable.emissiveIntensity);
        push.textureIndices = packTextureIndices(renderable, textureLibrary);
        vkCmdPushConstants(cmd, scenePipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(push), &push);

        if (!bindlessInitialised_) {
            // set=1 (material textures) -- bound per-entity since it varies
            // per-entity, unlike set=0 (bound once above the loop). A
            // VK_NULL_HANDLE result (pool exhausted) just skips the rebind,
            // leaving whatever set=1 was last bound -- see this function's
            // doc comment on why that degrade is acceptable.
            //
            // The bindless variant declares no set=1 at all (the whole
            // point: one descriptor set bound once per pass instead of a
            // rebind per entity), so this per-draw work is skipped entirely
            // rather than allocating sets nothing will read.
            VkDescriptorSet materialSet = getOrCreateMaterialDescriptorSet(renderable, textureLibrary);
            if (materialSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 1, 1, &materialSet,
                                         0, nullptr);
            }
        }

        VkDeviceSize offset = 0;
        VkBuffer vb = mesh->vertexBuffer();
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh->indexCount(), 1, 0, 0, 0);
        recordDraw(mesh->indexCount(), 1);
    }

    // Kronos ("Real-Time Rendering Evolved" trailer): real glass/water --
    // a second, small pass over the same entity view, routed to
    // glassPipeline_ instead of scenePipeline_ (see shaders/glass.frag's
    // own header comment). Still inside this same vkCmdBeginRendering
    // scope, still depth-testing/writing against the same depth
    // attachment the opaque loop above just wrote, so glass correctly
    // sits behind/in front of opaque geometry -- only its own shading
    // technique differs, not its place in the depth-sorted world.
    if (glassPipeline_ != VK_NULL_HANDLE) {
        bool boundGlassPipeline = false;
        for (auto entity : view) {
            auto& renderable = view.get<Renderable>(entity);
            if (!renderable.visible || renderable.transmission <= 0.0f) continue;
            const Mesh* mesh = meshLibrary.get(renderable.meshHandle);
            if (!mesh) continue;

            if (!boundGlassPipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, glassPipeline_);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, glassPipelineLayout_, 0, 1,
                                         &frame.sceneDescriptorSet, 0, nullptr);
                boundGlassPipeline = true;
            }

            GlassPushConstants push{};
            push.model = hierarchy::computeWorldMatrix(ecs, entity);
            push.tintColor = glm::vec4(glm::vec3(renderable.baseColor), renderable.transmission);
            push.params = glm::vec4(renderable.transmissionIor, renderable.roughness, 0.0f, 0.0f);
            vkCmdPushConstants(cmd, glassPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                sizeof(push), &push);

            VkDeviceSize offset = 0;
            VkBuffer vb = mesh->vertexBuffer();
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
            vkCmdBindIndexBuffer(cmd, mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh->indexCount(), 1, 0, 0, 0);
            recordDraw(mesh->indexCount(), 1);
        }
    }

    // Instanced draws don't get per-instance textures (see Components.hpp's
    // Renderable texture-field comment) -- but scene_instanced.vert also
    // feeds the shared scene.frag, which now declares set=1 sampler
    // bindings, so *something* valid must be bound at set=1 for these
    // draws too, or sampling an unbound descriptor is undefined behavior.
    // Binding the default (all-fallback) material set once, before the
    // whole batch, keeps every instanced draw's set=1 valid without
    // needing per-instance-correct textures.
    Renderable defaultMaterial{};
    VkDescriptorSet defaultMaterialSet = getOrCreateMaterialDescriptorSet(defaultMaterial, textureLibrary);
    if (defaultMaterialSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 1, 1, &defaultMaterialSet,
                                 0, nullptr);
    }
    drawInstancedBatches(cmd, frame, ecs, meshLibrary, textureLibrary);
    // Its own pipeline (not scenePipelineLayout_-based), so it rebinds
    // pipeline/set=0 itself the first time it actually has something to
    // draw -- see its own doc comment. A no-op (no rebind at all) when
    // `riggedMeshLibrary` is null or nothing in `ecs` has a
    // SkinnedRenderable.
    drawSkinnedEntities(cmd, frame, ecs, riggedMeshLibrary, textureLibrary);

    vkCmdEndRendering(cmd);

    // Kronos: soft particles run in their own pass, after everything that
    // writes depth.
    //
    // They SAMPLE the depth buffer while also depth-testing against it.
    // Doing that inside the opaque pass meant the image was in
    // DEPTH_ATTACHMENT_OPTIMAL while a descriptor declared it shader-
    // readable -- VUID-vkCmdDrawIndexed-imageLayout-00344, ten times a
    // frame. DEPTH_READ_ONLY_OPTIMAL is the layout that legitimately
    // permits both at once, and it is only valid because particles
    // already disable depth writes.
    //
    // This also moves particles after skinned meshes, which is a
    // correctness improvement in its own right: blended effects belong
    // last, so solid geometry occludes them properly.
    if (depthImage != VK_NULL_HANDLE) {
        transitionImage(cmd, depthImage, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT, 1);

        VkRenderingAttachmentInfo particleColor = colorAttachment;
        // LOAD, not CLEAR: this pass composites onto what was just drawn.
        particleColor.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

        VkRenderingAttachmentInfo particleDepth = depthAttachment;
        particleDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        particleDepth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        particleDepth.storeOp = VK_ATTACHMENT_STORE_OP_NONE;

        VkRenderingInfo particlePass{VK_STRUCTURE_TYPE_RENDERING_INFO};
        particlePass.renderArea = {{0, 0}, extent};
        particlePass.layerCount = 1;
        particlePass.colorAttachmentCount = 1;
        particlePass.pColorAttachments = &particleColor;
        particlePass.pDepthAttachment = &particleDepth;

        vkCmdBeginRendering(cmd, &particlePass);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        drawParticles(cmd, frame, particleSystem);
        vkCmdEndRendering(cmd);

        // Back to attachment layout so the next frame's clear/write is
        // valid, and so any later pass sees the layout it expects.
        transitionImage(cmd, depthImage, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                         VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
    } else {
        // No depth image handle available (an auxiliary scene path):
        // keep the previous behaviour rather than skipping particles.
        vkCmdBeginRendering(cmd, &renderingInfo);
        drawParticles(cmd, frame, particleSystem);
        vkCmdEndRendering(cmd);
    }

    // Sprint 16: capture this frame slot's own camera history *before*
    // overwriting it -- see FrameSync::previousViewProj's own comment on
    // why this is per-slot, not Renderer-wide, and why "N frames back"
    // under multi-buffering is a real, honest, accepted imprecision. On
    // this slot's very first use (hasPreviousViewProj == false), falling
    // back to *this* frame's own view-proj is deliberate: it makes the
    // reprojected velocity exactly zero for that one frame instead of an
    // undefined/garbage large jump from an uninitialized matrix.
    glm::mat4 currentViewProj = ubo.proj * ubo.view;
    glm::mat4 previousViewProjForThisFrame = frame.hasPreviousViewProj ? frame.previousViewProj : currentViewProj;
    frame.previousViewProj = currentViewProj;
    frame.hasPreviousViewProj = true;

    // frame.hdrImage is done being written by the opaque/instanced/
    // particle pass above -- transitioned here, once, so both
    // drawCinematicPass() (if it runs) and drawBloomAndComposite()'s
    // bloom-extract stage can read it as a texture without either one
    // needing to know whether the other already did this.
    transitionImage(cmd, frame.hdrImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    // Depth is done being written for this frame -- Sprint 16's cinematic
    // pass (if it runs at all, see drawCinematicPass()'s own real bypass)
    // needs to sample it back as a real texture for SSAO/DOF/motion-blur
    // world-position reconstruction.
    transitionImage(cmd, depthImage, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT);

    // Kronos ("Rendering Fidelity" -- SSR fallback pass): real
    // screen-space reflections run *first* in the post-FX chain, before
    // volumetric fog -- reads frame.hdrImage (just transitioned above) +
    // depth, writes frame.ssrImage. Reflections should themselves be
    // hazed by volumetric fog (real atmosphere sits between the camera
    // and everything, including a reflective surface), not the other way
    // around -- see ensureVolumetricFogTargets()'s own comment for how
    // its input becomes conditional on this once SSR is enabled.
    drawSSRPass(cmd, frame, extent);

    // Kronos ("Rendering Fidelity Foundation" Phase 1.2): real volumetric
    // fog runs next in the post-FX chain -- reads frame.hdrImage/frame.ssrImage
    // (just transitioned/written above) + depth, writes frame.fogImage. See
    // ensureCinematicTarget()'s own comment for why this has to happen
    // before drawCinematicPass(): Cinematic Mode's own input is
    // conditional on fog's output once fog is enabled.
    drawVolumetricFogPass(cmd, frame, extent);

    drawCinematicPass(cmd, frame, extent, previousViewProjForThisFrame);

    // Sprint 16 god rays: the sun's own real screen-space position this
    // frame, projected from a point far along the *reverse* of the light's
    // travel direction (lightDirectionWS is where the light travels *to*,
    // so the light source itself sits back along -directionWS) -- the
    // standard technique for giving a directional light a real screen
    // position to scatter a radial light-shaft effect from. `w > 0`
    // (rather than checking the projected NDC bounds) is what
    // "in front of the camera" actually means after a perspective divide;
    // composite.frag's own clamp handles a sun that's in front of the
    // camera but outside the visible screen rect.
    glm::vec3 towardSun = -glm::normalize(lighting_.directionWS);
    glm::vec4 sunClip = currentViewProj * glm::vec4(camera.position + towardSun * 1000.0f, 1.0f);
    bool sunVisible = sunClip.w > 0.0f;
    glm::vec2 sunScreenUV = sunVisible ? (glm::vec2(sunClip.x, sunClip.y) / sunClip.w) * 0.5f + 0.5f : glm::vec2(0.5f);

    // Bloom + exposure + tonemap: reads frame.hdrImage (or, with
    // Cinematic Mode on, frame.cinematicImage -- see
    // ensureCinematicTarget()'s source-repoint logic) back, writes the
    // caller's actual colorImage/colorView as the final step -- see that
    // method's comment for the pass structure.
    drawBloomAndComposite(cmd, frame, colorImage, colorView, extent, sunScreenUV, sunVisible, applyBloom);
}

void Renderer::drawSceneInto(VkCommandBuffer cmd, VkImage colorImage, VkImageView colorView, VkImage depthImage,
                              VkImageView depthView, VkExtent2D extent, const Camera& camera, ECS& ecs,
                              MeshLibrary& meshLibrary, const ParticleSystem& particleSystem,
                              TextureLibrary& textureLibrary, RiggedMeshLibrary* riggedMeshLibrary) {
    drawSceneIntoImpl(frames_[currentFrame_], cmd, colorImage, colorView, depthImage, depthView, extent, camera, ecs,
                       meshLibrary, particleSystem, textureLibrary, riggedMeshLibrary);
}

void Renderer::drawSceneInto(AuxiliarySceneHandle handle, VkCommandBuffer cmd, VkImage colorImage,
                              VkImageView colorView, VkImage depthImage, VkImageView depthView, VkExtent2D extent,
                              const Camera& camera, ECS& ecs, MeshLibrary& meshLibrary,
                              const ParticleSystem& particleSystem, TextureLibrary& textureLibrary,
                              RiggedMeshLibrary* riggedMeshLibrary) {
    if (handle == kInvalidAuxiliaryScene || handle >= auxiliaryScenes_.size()) {
        std::fprintf(stderr, "Renderer: drawSceneInto() called with an invalid AuxiliarySceneHandle.\n");
        return;
    }
    // Kronos ("Avatar Scene Lighting Calibration Pass"): real,
    // explicit `false` -- see drawSceneIntoImpl()'s own header comment
    // on why every auxiliary/preview scene (Home's avatar preview,
    // every Studio PreviewScene consumer) must not inherit the outdoor
    // world's own live weather perturbation.
    // Kronos ("Avatar Preview Rendering" pre-launch fix): also real,
    // explicit `false` for bloom -- see drawBloomAndComposite()'s own
    // comment on why a close-up preview's bloom bleed reads as a much
    // larger washed-out effect than the same settings produce on a
    // normal full-scene shot. Real, explicit `true` for suppressSunDisk
    // -- see shaders/sky.frag's own comment. And real, explicit `true`
    // for useFlatBackground -- the direct, guaranteed hardware-level
    // override: a fixed dark-slate clear color with the sky pass
    // skipped entirely, see this function's own header comment.
    drawSceneIntoImpl(auxiliaryScenes_[handle], cmd, colorImage, colorView, depthImage, depthView, extent, camera, ecs,
                       meshLibrary, particleSystem, textureLibrary, riggedMeshLibrary, /*applyWeatherEffects=*/false,
                       /*applyBloom=*/false, /*suppressSunDisk=*/true, /*useFlatBackground=*/true);
}

Renderer::AuxiliarySceneHandle Renderer::createAuxiliaryScene() {
    if (auxiliaryScenes_.size() >= kMaxAuxiliaryScenes) {
        std::fprintf(stderr, "Renderer: createAuxiliaryScene() failed -- kMaxAuxiliaryScenes (%zu) already in use.\n",
                     kMaxAuxiliaryScenes);
        return kInvalidAuxiliaryScene;
    }

    auxiliaryScenes_.emplace_back();
    AuxiliarySceneHandle handle = auxiliaryScenes_.size() - 1;
    FrameSync& frame = auxiliaryScenes_[handle];

    // Same dependency order as initialize()'s createShadowResources() ->
    // createSceneDescriptorResources() call sequence: the scene
    // descriptor set's binding-1 write needs frame.shadowArrayView to
    // already exist. No semaphore/fence/command buffer/post-process
    // targets are created here -- the latter are lazily created by
    // ensurePostProcessTargets() on this slot's first drawSceneInto()
    // call (frame.postProcessExtent defaults to {0,0}, same as every
    // frames_[] slot), and this slot is never submitted/presented on its
    // own so it needs no sync primitives of its own.
    if (!initShadowResourcesFor(frame) || !initSceneDescriptorResourcesFor(frame) || !initInstanceBufferFor(frame) ||
        !initParticleInstanceBufferFor(frame) || !initSkinningResourcesFor(frame)) {
        std::fprintf(stderr, "Renderer: createAuxiliaryScene() failed to allocate GPU resources for slot %zu.\n", handle);
        destroyAuxiliaryScene(handle);
        return kInvalidAuxiliaryScene;
    }
    return handle;
}

void Renderer::destroyAuxiliaryScene(AuxiliarySceneHandle handle) {
    if (handle == kInvalidAuxiliaryScene || handle >= auxiliaryScenes_.size()) return;
    FrameSync& frame = auxiliaryScenes_[handle];
    destroyPostProcessTargets(frame);
    destroySceneDescriptorResourcesFor(frame);
    destroyShadowResourcesFor(frame);
    destroyInstanceBufferFor(frame);
    destroyParticleInstanceBufferFor(frame);
    destroySkinningResourcesFor(frame);
    // frame itself stays in auxiliaryScenes_ (zeroed out, unusable) --
    // see the handle-stability comment on auxiliaryScenes_'s declaration.
}

void Renderer::drawInstancedBatches(VkCommandBuffer cmd, FrameSync& frame, ECS& ecs, MeshLibrary& meshLibrary,
                                     TextureLibrary& textureLibrary) {
    // Bucket by meshHandle -- every entity sharing a mesh in one bucket
    // becomes one draw call, regardless of how many entities that is.
    std::unordered_map<uint32_t, std::vector<InstanceData>> buckets;

    for (auto entity : ecs.view<Transform, Renderable>()) {
        auto* renderable = ecs.tryGetComponent<Renderable>(entity);
        if (renderable == nullptr || !renderable->visible || !renderable->instanced) continue;

        auto* transform = ecs.tryGetComponent<Transform>(entity);
        if (transform == nullptr) continue;

        InstanceData data{};
        data.model = transform->matrix();
        data.baseColor = renderable->baseColor;
        data.metallicRoughness = glm::vec4(renderable->metallic, renderable->roughness, renderable->normalIntensity,
                                            renderable->useTriplanarProjection ? 1.0f : 0.0f);
        data.emissive = glm::vec4(renderable->emissiveColor, renderable->emissiveIntensity);
        data.textureIndices = packTextureIndices(*renderable, textureLibrary);
        buckets[renderable->meshHandle].push_back(data);
    }

    if (buckets.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, instancedScenePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 0, 1, &frame.sceneDescriptorSet,
                             0, nullptr);
    // Bound here too rather than relying on the opaque loop above: this
    // shares scenePipelineLayout_, but nothing guarantees the opaque loop
    // ran (it returns early when no entity matches).
    if (bindlessInitialised_ && bindlessSet_ != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 2, 1, &bindlessSet_, 0,
                                 nullptr);
    }

    auto* instanceCursor = static_cast<InstanceData*>(frame.instanceMapped);
    uint32_t writtenInstances = 0;

    for (auto& [meshHandle, instances] : buckets) {
        const Mesh* mesh = meshLibrary.get(meshHandle);
        if (mesh == nullptr) continue;

        uint32_t count = static_cast<uint32_t>(instances.size());
        if (writtenInstances + count > kMaxInstancesPerFrame) {
            // Clamp rather than overflow frame.instanceBuffer -- see
            // kMaxInstancesPerFrame's declaration comment. Logged once per
            // offending bucket rather than silently dropping instances.
            std::fprintf(stderr,
                         "Renderer: instanced draw batch for mesh %u exceeds kMaxInstancesPerFrame (%u) -- "
                         "clamping, some instances won't render this frame.\n",
                         meshHandle, kMaxInstancesPerFrame);
            count = kMaxInstancesPerFrame > writtenInstances ? kMaxInstancesPerFrame - writtenInstances : 0;
            if (count == 0) break;
        }

        std::memcpy(instanceCursor + writtenInstances, instances.data(), count * sizeof(InstanceData));

        VkDeviceSize vertexOffset = 0;
        VkBuffer vb = mesh->vertexBuffer();
        VkBuffer instanceVb = frame.instanceBuffer;
        std::array<VkBuffer, 2> vertexBuffers{vb, instanceVb};
        std::array<VkDeviceSize, 2> vertexOffsets{vertexOffset, writtenInstances * sizeof(InstanceData)};
        vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers.data(), vertexOffsets.data());
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh->indexCount(), count, 0, 0, 0);
        recordDraw(mesh->indexCount(), count);

        writtenInstances += count;
    }
}

void Renderer::drawParticles(VkCommandBuffer cmd, FrameSync& frame, const ParticleSystem& particleSystem) {
    const std::vector<Particle>& particles = particleSystem.liveParticles();
    if (particles.empty()) return;

    // Sprint 14 ("Performance Mode"): a real, direct overdraw reduction
    // -- fewer real instanced quads drawn (and so real fragment-shader
    // invocations for blended-particle fill rate), well under the real
    // ParticleSystem::kMaxParticles ceiling. Real, honest degrade: the
    // real particle *simulation* (ParticleSystem::update()) is unaffected
    // -- only how many of its real live particles this draw call actually
    // renders this frame.
    static constexpr uint32_t kPerformanceModeMaxParticles = 1024;
    size_t particleCap = performanceModeEnabled_
                              ? std::min<size_t>(ParticleSystem::kMaxParticles, kPerformanceModeMaxParticles)
                              : ParticleSystem::kMaxParticles;
    uint32_t count = static_cast<uint32_t>(std::min(particles.size(), particleCap));

    auto* instanceCursor = static_cast<ParticleInstanceData*>(frame.particleInstanceMapped);
    for (uint32_t i = 0; i < count; ++i) {
        const Particle& p = particles[i];
        instanceCursor[i].positionSize = glm::vec4(p.position, p.currentSize());
        instanceCursor[i].color = p.currentColor();
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline_);
    // Sprint 16: particlePipelineLayout_ (not scenePipelineLayout_, see
    // that member's own comment) -- set0 the same real scene UBO, set1
    // this frame's real sampled depth (soft-particle fade, see
    // shaders/particle.frag). A null particleDepthDescriptorSet (only
    // possible if ensureParticleDepthDescriptor() failed this frame,
    // already logged there) skips set1's bind -- the shader would sample
    // an unbound descriptor otherwise, undefined behavior -- so this
    // degrades to no particles drawn at all rather than that, matching
    // this engine's own "skip the risky draw, don't crash" convention.
    if (frame.particleDepthDescriptorSet == VK_NULL_HANDLE) return;
    std::array<VkDescriptorSet, 2> particleSets{frame.sceneDescriptorSet, frame.particleDepthDescriptorSet};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipelineLayout_, 0,
                             static_cast<uint32_t>(particleSets.size()), particleSets.data(), 0, nullptr);

    VkDeviceSize vertexOffset = 0;
    VkBuffer quadVb = particleQuadMesh_.vertexBuffer();
    std::array<VkBuffer, 2> vertexBuffers{quadVb, frame.particleInstanceBuffer};
    std::array<VkDeviceSize, 2> vertexOffsets{vertexOffset, VkDeviceSize{0}};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers.data(), vertexOffsets.data());
    vkCmdBindIndexBuffer(cmd, particleQuadMesh_.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, particleQuadMesh_.indexCount(), count, 0, 0, 0);
    recordDraw(particleQuadMesh_.indexCount(), count);
}

void Renderer::drawSkinnedEntities(VkCommandBuffer cmd, FrameSync& frame, ECS& ecs, RiggedMeshLibrary* riggedMeshLibrary,
                                    TextureLibrary& textureLibrary) {
    if (riggedMeshLibrary == nullptr) return;

    auto view = ecs.view<Transform, SkinnedRenderable>();
    bool boundPipeline = false;
    uint32_t slot = 0;

    for (auto entity : view) {
        auto& skinned = view.get<SkinnedRenderable>(entity);
        if (!skinned.visible) continue;

        const RiggedMesh* riggedMesh = riggedMeshLibrary->get(skinned.riggedMeshHandle);
        if (riggedMesh == nullptr) continue;

        if (slot >= kMaxSkinnedDrawsPerFrame) {
            std::fprintf(stderr,
                         "Renderer: drawSkinnedEntities() skipped an entity -- kMaxSkinnedDrawsPerFrame (%u) already "
                         "used this call.\n",
                         kMaxSkinnedDrawsPerFrame);
            continue;
        }

        if (!boundPipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedScenePipeline_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedScenePipelineLayout_, 0, 1,
                                     &frame.sceneDescriptorSet, 0, nullptr);
            // set=1: skinned entities don't support textured materials in
            // this pass (a real, stated scope boundary -- see
            // Components.hpp's SkinnedRenderable comment) -- the default
            // (all-fallback) material set keeps set=1 valid for every
            // skinned draw, same precedent the instanced-batch path above
            // already established for its own per-instance-texture
            // limitation.
            Renderable defaultMaterial{};
            VkDescriptorSet defaultMaterialSet = getOrCreateMaterialDescriptorSet(defaultMaterial, textureLibrary);
            if (defaultMaterialSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedScenePipelineLayout_, 1, 1,
                                         &defaultMaterialSet, 0, nullptr);
            }
            boundPipeline = true;
        }

        // Writes this entity's current pose into its own, independent
        // UBO slot (never shared with any other skinned draw this same
        // call) -- see FrameSync's skinning fields' doc comment.
        uint32_t jointCount = std::min(static_cast<uint32_t>(skinned.skinningMatrices.size()), kMaxJointsPerSkeleton);
        std::memcpy(frame.skinningUboMapped[slot], skinned.skinningMatrices.data(), sizeof(glm::mat4) * jointCount);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedScenePipelineLayout_, 2, 1,
                                 &frame.skinningDescriptorSets[slot], 0, nullptr);

        ObjectPushConstants push{};
        push.model = hierarchy::computeWorldMatrix(ecs, entity);
        push.baseColor = skinned.baseColor;
        push.metallicRoughness = glm::vec4(skinned.metallic, skinned.roughness, 1.0f, 0.0f);
        push.emissive = glm::vec4(skinned.emissiveColor, skinned.emissiveIntensity);
        vkCmdPushConstants(cmd, skinnedScenePipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(push), &push);

        VkDeviceSize offset = 0;
        std::array<VkBuffer, 2> vertexBuffers{riggedMesh->mesh().vertexBuffer(), riggedMesh->skinBuffer()};
        std::array<VkDeviceSize, 2> vertexOffsets{offset, offset};
        vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers.data(), vertexOffsets.data());
        vkCmdBindIndexBuffer(cmd, riggedMesh->mesh().indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, riggedMesh->mesh().indexCount(), 1, 0, 0, 0);
        recordDraw(riggedMesh->mesh().indexCount(), 1);

        ++slot;
    }
}

bool Renderer::renderFrame() {
    // Wall-clock time since the previous renderFrame() call -- the actual
    // presented frame rate, not GameLoop's fixed simulation tick rate
    // (those two only coincide if vsync/present rate happens to match the
    // fixed timestep, which isn't guaranteed). First call ever has no
    // previous timestamp to diff against, so frameTimeMs/fps just stay 0
    // for that one frame rather than reporting a nonsensical huge delta.
    auto now = std::chrono::steady_clock::now();
    if (lastFrameTimestamp_.time_since_epoch().count() != 0) {
        float frameTimeMs = std::chrono::duration<float, std::milli>(now - lastFrameTimestamp_).count();
        lastMetrics_.frameTimeMs = frameTimeMs;
        lastMetrics_.fps = frameTimeMs > 0.0f ? 1000.0f / frameTimeMs : 0.0f;
        // Kronos ("Rendering Fidelity Foundation" Phase 1.1): real weather
        // transitions tick on the same real wall-clock delta as the FPS
        // counter above -- swapchain-presented frames only (this mirrors
        // lastFrameTimestamp_'s own existing scope), not GameLoop's fixed
        // simulation tick. AuxiliaryScene-only capture paths (trailer
        // capture) don't call renderFrame() at all, so weather stays
        // static (whatever it last was) during those -- a real, honest
        // limitation, not a bug, matching this frame's own established
        // "AuxiliaryScene is a separate, less-trusted path" precedent.
        tickWeather(weatherState_, frameTimeMs / 1000.0f);
        totalElapsedTimeSeconds_ += frameTimeMs / 1000.0f;
    }
    lastFrameTimestamp_ = now;
    frameDrawCalls_ = 0;
    frameTriangles_ = 0;

    FrameSync& frame = frames_[currentFrame_];

    vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return true;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "Renderer: vkAcquireNextImageKHR failed.\n");
        return false;
    }

    vkResetFences(device_, 1, &frame.inFlight);

    VkCommandBuffer cmd = frame.commandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    if (prePassCallback_) {
        prePassCallback_(cmd);
    }

    VkImage image = swapchainImages_[imageIndex];

    if (sceneEcs_ != nullptr && sceneCamera_ != nullptr && sceneMeshLibrary_ != nullptr &&
        sceneParticleSystem_ != nullptr && sceneTextureLibrary_ != nullptr) {
        // Real scene fills the whole swapchain -- engine_runtime's path.
        // Kronos ("Avatar System"): sceneRiggedMeshLibrary_ now real-passed
        // through -- see setScene()'s own header comment for the real bug
        // this fixes (a real, GPU-uploaded skinned avatar body that was
        // never actually submitted to a draw call on this exact path).
        drawSceneInto(cmd, image, swapchainImageViews_[imageIndex], depthImage_, depthImageView_, swapchainExtent_,
                      *sceneCamera_, *sceneEcs_, *sceneMeshLibrary_, *sceneParticleSystem_, *sceneTextureLibrary_,
                      sceneRiggedMeshLibrary_);
    } else {
        // No scene set -- Studio's path: a plain clear behind its docked
        // ImGui panels (its own scene render, if any, already happened in
        // the prePass hook above, targeting Studio's own offscreen
        // viewport target, not the swapchain).
        transitionImage(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colorAttachment.imageView = swapchainImageViews_[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = {{0.02f, 0.02f, 0.03f, 1.0f}};

        VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
        renderingInfo.renderArea = {{0, 0}, swapchainExtent_};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(cmd, &renderingInfo);
        vkCmdEndRendering(cmd);
    }

    if (overlayCallback_) {
        VkRenderingAttachmentInfo overlayAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        overlayAttachment.imageView = swapchainImageViews_[imageIndex];
        overlayAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        overlayAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // draw on top, don't clear
        overlayAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo overlayInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
        overlayInfo.renderArea = {{0, 0}, swapchainExtent_};
        overlayInfo.layerCount = 1;
        overlayInfo.colorAttachmentCount = 1;
        overlayInfo.pColorAttachments = &overlayAttachment;

        vkCmdBeginRendering(cmd, &overlayInfo);
        overlayCallback_(cmd, swapchainImageViews_[imageIndex], swapchainExtent_);
        vkCmdEndRendering(cmd);
    }

    transitionImage(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

    if (VkResult endResult = vkEndCommandBuffer(cmd); endResult != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkEndCommandBuffer failed (VkResult=%d).\n", static_cast<int>(endResult));
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    // Per-image, not per-frame: see renderFinishedPerImage_'s comment.
    // Falls back to the per-frame semaphore only if the vector is somehow
    // unsized, which would mean swapchain creation failed anyway.
    VkSemaphore renderFinishedSemaphore = imageIndex < renderFinishedPerImage_.size()
                                               ? renderFinishedPerImage_[imageIndex]
                                               : frame.renderFinished;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphore;

    if (VkResult submitResult = vkQueueSubmit(graphicsQueue_, 1, &submitInfo, frame.inFlight); submitResult != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkQueueSubmit failed (VkResult=%d).\n", static_cast<int>(submitResult));
        return false;
    }

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || framebufferResized_ ||
        window_->wasResized()) {
        window_->clearResizedFlag();
        recreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkQueuePresentKHR failed.\n");
        return false;
    }

    lastMetrics_.drawCalls = frameDrawCalls_;
    lastMetrics_.triangleCount = frameTriangles_;

    // vmaGetHeapBudgets: VMA's own live tracking of what's actually
    // resident/available per memory heap -- real numbers from the driver,
    // not something this engine estimates. Summed across every heap
    // (typically device-local + host-visible on a discrete GPU) for one
    // overall figure; a more detailed view could break this out per heap
    // later if a Studio panel ever wants that granularity.
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
    std::vector<VmaBudget> budgets(memProps.memoryHeapCount);
    vmaGetHeapBudgets(allocator_, budgets.data());
    uint64_t usedBytes = 0;
    uint64_t budgetBytes = 0;
    for (const VmaBudget& budget : budgets) {
        usedBytes += budget.usage;
        budgetBytes += budget.budget;
    }
    lastMetrics_.gpuMemoryUsedBytes = usedBytes;
    lastMetrics_.gpuMemoryBudgetBytes = budgetBytes;

    if (logMetricsToStdout_ && std::chrono::duration<float>(now - lastMetricsLogTimestamp_).count() >= 1.0f) {
        lastMetricsLogTimestamp_ = now;
        std::fprintf(stdout, "Renderer: %.1f fps (%.2f ms) | %u draw calls | %llu tris | GPU %.0f/%.0f MB\n",
                     lastMetrics_.fps, lastMetrics_.frameTimeMs, lastMetrics_.drawCalls,
                     static_cast<unsigned long long>(lastMetrics_.triangleCount),
                     static_cast<double>(lastMetrics_.gpuMemoryUsedBytes) / (1024.0 * 1024.0),
                     static_cast<double>(lastMetrics_.gpuMemoryBudgetBytes) / (1024.0 * 1024.0));
    }

    currentFrame_ = (currentFrame_ + 1) % framesInFlight_;
    return true;
}

void Renderer::shutdown() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    destroyShadowPipeline();
    destroyInstancedScenePipeline();
    destroyParticlePipeline();
    destroyPostProcessPipelines();
    destroySkyPipeline();
    destroySkinnedScenePipeline();
    destroyGlassPipeline();
    destroyScenePipeline();

    // Safety net: any studio::PreviewScene that didn't call
    // destroyAuxiliaryScene() itself before this (it should have --
    // PreviewScene::destroy() does -- but this must not depend on every
    // caller getting shutdown ordering right) gets torn down here, before
    // the pool/layout/sampler those slots' resources belong to are
    // destroyed below.
    for (AuxiliarySceneHandle handle = 0; handle < auxiliaryScenes_.size(); ++handle) {
        destroyAuxiliaryScene(handle);
    }
    auxiliaryScenes_.clear();

    destroySceneDescriptorResources(); // uses frames_ -- must run before the loop below clears it
    destroyShadowResources();          // also uses frames_ -- same reason
    destroyInstanceBuffers();          // also uses frames_ -- same reason
    destroyParticleResources();        // also uses frames_ -- same reason (and destroys particleQuadMesh_)
    destroySkinningDescriptorResources(); // also uses frames_ -- same reason
    // destroyPostProcessTargets() (per-frame) frees descriptor sets from
    // postProcessDescriptorPool_, so it must run before
    // destroyPostProcessResources() destroys that pool.
    for (auto& frame : frames_) {
        destroyPostProcessTargets(frame);
    }
    destroyPostProcessResources();
    // Runs after destroyScenePipeline() above -- scenePipelineLayout_
    // (already destroyed by then) is what actually referenced
    // materialDescriptorSetLayout_ at creation time.
    destroyMaterialResources();
    // Same ordering reason as destroyMaterialResources() above:
    // scenePipelineLayout_ referenced bindlessSetLayout_ at creation time
    // and has already been destroyed by this point.
    //
    // Neither of these ran at shutdown before: destroyBindlessResources()
    // was only reachable from createDeviceResources()' failure path, and
    // destroyPerImageSemaphores() only from inside its own create
    // function -- so the bindless sampler/layout/pool/set and every
    // per-swapchain-image semaphore were still alive when vkDestroyDevice
    // was called.
    destroyBindlessResources();
    destroyPerImageSemaphores();

    for (auto& frame : frames_) {
        if (frame.imageAvailable) vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
        if (frame.renderFinished) vkDestroySemaphore(device_, frame.renderFinished, nullptr);
        if (frame.inFlight) vkDestroyFence(device_, frame.inFlight, nullptr);
    }
    frames_.clear();

    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }

    // Sprint 14: owns its own real acceleration structures/buffers
    // against allocator_/device_ -- must shut down before either is
    // destroyed below, same ordering contract as everything else in this
    // function.
    rayTracingScene_.shutdown();

    if (device_ != VK_NULL_HANDLE) {
        destroyDepthResources();
        destroySwapchain();
    }

    // NOTE(ordering contract): any MeshLibrary this Renderer's scene draws
    // from must be destroyed (MeshLibrary::destroyAll(renderer.allocator()))
    // by its owner *before* shutdown() reaches this point -- Application
    // and StudioApp both do this in their own shutdown(), immediately
    // before calling renderer_.shutdown(). Mesh buffers are VMA
    // allocations; destroying the allocator out from under them would be
    // a use-after-free on the next frame that never comes, but is still
    // the kind of bug worth stating the contract for explicitly.
    if (allocator_ != nullptr) {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }

    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    if (debugMessenger_ != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT) {
        vkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
        debugMessenger_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

} // namespace engine::core
