#include "core/Renderer.hpp"
#include "core/render/RendererInternal.hpp"
#include "core/SceneSpatialIndex.hpp"
#include "core/StaticBatching.hpp"
#include "core/GpuFeatures.hpp"

#include "core/CascadeSplitMath.hpp"
#include "core/Hierarchy.hpp"
#include "core/ResourcePaths.hpp"
#include "core/SwapchainSelection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <unordered_map>


namespace engine::core {

using namespace renderer_internal;

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
    // Every Mesh created from here on is also a BLAS build input.
    setRayTracingGeometryUsage(rayTracingSupported_);

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
    // Kronos ("Cinematic Camera Physics & Post-Processing Pipeline" --
    // real 3D LUT support): a real, always-valid default -- see
    // uploadColorGradingLut()'s own header comment for why the composite
    // pass's LUT binding must never be null/unwritten. No FrameSync/
    // AuxiliarySceneHandle has allocated a compositeDescriptorSet yet at
    // this point in initialize() (see ensurePostProcessTargets()'s own
    // "lazily created on first drawSceneInto()" comment) -- that's fine,
    // rewriteCompositeLutBindings() inside this call is then a real no-op
    // loop, and each slot's own later first-allocation write in
    // ensurePostProcessTargets() reads lutImageView_ fresh.
    {
        std::string lutError;
        if (!resetColorGradingLutToIdentity(lutError)) {
            std::fprintf(stderr, "Renderer: failed to create the default identity color-grading LUT: %s\n",
                         lutError.c_str());
            return false;
        }
    }
    // Needs physicalDevice_/device_ only, and must precede every
    // createXPipeline() call below -- each one now passes pipelineCache_
    // instead of VK_NULL_HANDLE. A failure here is not fatal: pipelineCache_
    // stays VK_NULL_HANDLE, which vkCreateGraphicsPipelines already treats
    // identically to today's existing behavior.
    if (!createPipelineCache()) {
        std::fprintf(stderr, "Renderer: pipeline cache setup failed -- continuing without one.\n");
    }
    if (!initRenderModules()) return false;
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

bool Renderer::createPipelineCache() {
    std::string exeDir = executableDirectory();
    pipelineCacheFilePath_ = (exeDir.empty() ? std::string(".") : exeDir) + "/.kronos_pipeline_cache.bin";

    VkPhysicalDeviceProperties deviceProps{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &deviceProps);

    std::vector<char> cacheData;
    std::ifstream file(pipelineCacheFilePath_, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        std::streamsize size = file.tellg();
        if (size >= static_cast<std::streamsize>(sizeof(VkPipelineCacheHeaderVersionOne))) {
            file.seekg(0, std::ios::beg);
            cacheData.resize(static_cast<size_t>(size));
            if (!file.read(cacheData.data(), size)) {
                cacheData.clear();
            }
        }
    }

    // A cache blob is only valid for the exact GPU/driver that produced it
    // -- vendorID/deviceID/pipelineCacheUUID all have to match, or
    // vkCreatePipelineCache is allowed to (and some drivers will) reject
    // the whole blob outright. Checking this ourselves means a stale blob
    // (GPU swap, driver update) is silently discarded here rather than
    // surfacing as a vkCreatePipelineCache failure.
    if (!cacheData.empty()) {
        VkPipelineCacheHeaderVersionOne header{};
        std::memcpy(&header, cacheData.data(), sizeof(header));
        bool matches = header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
                       header.vendorID == deviceProps.vendorID &&
                       header.deviceID == deviceProps.deviceID &&
                       std::memcmp(header.pipelineCacheUUID, deviceProps.pipelineCacheUUID, VK_UUID_SIZE) == 0;
        if (!matches) {
            cacheData.clear();
        }
    }

    VkPipelineCacheCreateInfo createInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    createInfo.initialDataSize = cacheData.size();
    createInfo.pInitialData = cacheData.empty() ? nullptr : cacheData.data();

    if (vkCreatePipelineCache(device_, &createInfo, nullptr, &pipelineCache_) != VK_SUCCESS) {
        pipelineCache_ = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void Renderer::savePipelineCacheToDisk() {
    if (pipelineCache_ == VK_NULL_HANDLE || pipelineCacheFilePath_.empty()) return;

    size_t dataSize = 0;
    if (vkGetPipelineCacheData(device_, pipelineCache_, &dataSize, nullptr) != VK_SUCCESS || dataSize == 0) {
        return;
    }
    std::vector<char> data(dataSize);
    if (vkGetPipelineCacheData(device_, pipelineCache_, &dataSize, data.data()) != VK_SUCCESS) {
        return;
    }

    std::ofstream file(pipelineCacheFilePath_, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return;
    file.write(data.data(), static_cast<std::streamsize>(dataSize));
}

void Renderer::destroyPipelineCache() {
    if (pipelineCache_ != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
        pipelineCache_ = VK_NULL_HANDLE;
    }
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
    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(physicalDevice_, &supportedFeatures);
    depthClampSupported_ = supportedFeatures.depthClamp == VK_TRUE;
    features2.features.depthClamp = supportedFeatures.depthClamp;
    // The HDR pass writes velocity to attachment 1 with a different write mask (glass masks it off).
    if (!supportedFeatures.independentBlend) {
        std::fprintf(stderr, "Renderer: independentBlend is required for the velocity attachment.\n");
        return false;
    }
    features2.features.independentBlend = VK_TRUE;

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

    gpuFeatures_ = queryGpuFeatures(physicalDevice_);
    GpuFeatureEnables optionalFeatures(gpuFeatures_);
    optionalFeatures.apply(&features2, deviceExtensions);
    std::fprintf(stdout, "Renderer: %s\n", gpuFeatures_.summary().c_str());

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

    Texture::setHostImageCopy(gpuFeatures_.hostCopyToShaderReadLayout);
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
    VkExtent2D extent = chooseExtent(caps, window_->pixelWidth(), window_->pixelHeight());

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


void Renderer::deferDestroy(std::function<void()> destroy) {
    deferredDestroys_.emplace_back(frameSerial_ + frames_.size() + 1, std::move(destroy));
}

void Renderer::waitIdle() const {
    if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
}


void Renderer::shutdown() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    deferredDestroys_.clear();

    destroyShadowPipeline();
    destroyInstancedScenePipeline();
    destroyParticlePipeline();
    destroyPostProcessPipelines();
    destroySkyPipeline();
    destroySkinnedScenePipeline();
    destroyGlassPipeline();
    destroySurfacePipelines();
    destroyScenePipeline();

    // Persist whatever every createXPipeline() call above just compiled
    // into pipelineCache_ back to disk, so the next process launch's
    // createPipelineCache() starts from it instead of an empty cache.
    // Must run after the pipelines are destroyed (pipelineCache_ itself
    // stays valid either way, but this keeps the "tear down what this
    // sprint added" block self-contained) and before vkDestroyDevice below.
    savePipelineCacheToDisk();
    destroyPipelineCache();

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
    shutdownRenderModules();

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
        if (frame.timestamps) vkDestroyQueryPool(device_, frame.timestamps, nullptr);
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
