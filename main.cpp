#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cassert>
#include <cmath>
#include <ctime>
#include <functional>

#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include "c:/VulkanSDK/1.1.108.0/Include/vulkan/vulkan.h"
//#pragma comment(linker, "/subsystem:windows")
#pragma comment(lib, "C:/VulkanSDK/1.1.108.0/Lib/vulkan-1.lib")

typedef unsigned char byte;

void readFile(const char* filename, std::vector<byte>& v)
{
    FILE* file = fopen(filename, "rb");
    assert(file);

    while (true)
    {
        int c = fgetc(file);

        if (c == EOF)
            break;

        v.push_back(c);
    }

    fclose(file);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CLOSE:
    {
        PostQuitMessage(0);
        break;
    }
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    return 0;
}

int main()
{
    /**************************************************************************
    Window
    Purpose: to have a window
    */
    const char* wndClassName = "mywindow";
    HINSTANCE hinstance = GetModuleHandle(0);
    HBRUSH bg = CreateSolidBrush(RGB(255, 0, 0));
    uint32_t width = 800;
    uint32_t height = 600;

    WNDCLASS wc = { };
    ZeroMemory(&wc, sizeof(WNDCLASS));
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hinstance;
    wc.lpszClassName = wndClassName;
    wc.hbrBackground = bg;
    RegisterClass(&wc);

    DWORD wndStyle = WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX;
    RECT r = { 0, 0, width, height };
    // this tells you what should be the window size if r is rect for client
    // IMPORTANT. window client, swap chain and VkImages (render target) dimensions must match
    AdjustWindowRect(&r, wndStyle, false);
    HWND hwnd = CreateWindowEx(0, wndClassName, "Vulkan", wndStyle, 100, 100, 
        r.right - r.left, r.bottom - r.top, 0, 0, hinstance, 0);
    assert(hwnd != 0);
    ShowWindow(hwnd, SW_SHOW);

    /**************************************************************************
    VkInstance
    Purpose: check if vulkan driver is present and to query for physical devices
    it also creates win32 surface
    */
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;

    // all optional
    appInfo.pApplicationName = "Vk1";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    const char* ext[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };

    VkInstanceCreateInfo vkInstanceArgs = {};
    vkInstanceArgs.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    vkInstanceArgs.pApplicationInfo = &appInfo;
    vkInstanceArgs.enabledExtensionCount = 2;
    vkInstanceArgs.ppEnabledExtensionNames = ext;

    // enable this to see some diagnostic
    // requires vulkan 1.1.106 or higher, prints to stdout by default
    vkInstanceArgs.enabledLayerCount = 1;
    vkInstanceArgs.ppEnabledLayerNames = layers;

    VkInstance vkInstance;
    assert(vkCreateInstance(&vkInstanceArgs, nullptr, &vkInstance) == VK_SUCCESS);

    /**************************************************************************
    Physical device and queue index
    Purpose: needed to create logical device and queue
    */
    uint32_t queueIndex = -1;
    uint32_t gpuCount = 0;
    uint32_t queueFamilyCount = 0;
    VkPhysicalDeviceProperties gpuProperties;
    VkPhysicalDeviceFeatures gpuFeatures;
    std::vector<VkPhysicalDevice> gpus;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    std::vector<VkQueueFamilyProperties> queueFamilies;

    vkEnumeratePhysicalDevices(vkInstance, &gpuCount, nullptr);
    gpus.resize(gpuCount);
    vkEnumeratePhysicalDevices(vkInstance, &gpuCount, gpus.data());

    for (uint32_t i = 0; i < gpuCount; i++)
    {
        vkGetPhysicalDeviceProperties(gpus[i], &gpuProperties);
        vkGetPhysicalDeviceFeatures(gpus[i], &gpuFeatures);
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &queueFamilyCount, nullptr);
        queueFamilies.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &queueFamilyCount, queueFamilies.data());

        // check if VK_KHR_SWAPCHAIN_EXTENSION_NAME is supported
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(gpus[i], nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(gpus[i], nullptr, &extensionCount, availableExtensions.data());

        bool swapChainSupported = false;
        for (int i = 0; i < availableExtensions.size(); i++)
        {
            if (strcmp(availableExtensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) != 0)
            {
                swapChainSupported = true;
                break;
            }
        }

        if (!swapChainSupported)
            continue;

        for (uint32_t j = 0; j < queueFamilyCount; j++)
        {
            // lets limit this to discrete GPU
            // queue must have graphics VK_QUEUE_GRAPHICS_BIT and present bit and 
            // VK_QUEUE_TRANSFER_BIT (its guaranteed that if graphics is supported then transfer is supported)
            if (gpuProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && 
                queueFamilies[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                queueIndex = j;
                // physical device doesnt have to be created
                physicalDevice = gpus[i];
                break;
            }
        }

        if (physicalDevice != VK_NULL_HANDLE)
            break;
    }

    assert(queueIndex != -1);
    assert(physicalDevice != VK_NULL_HANDLE);

    /**************************************************************************
    Logical device and command queue
    Purpose: device needed for nearly everything in vulkan, queue needed to execute commands
    */
    float queuePriority = 1.0f;

    // its possible that more than one queue is needed so its would require multiple VkDeviceQueueCreateInfo
    VkDeviceQueueCreateInfo queueArgs = {};
    queueArgs.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueArgs.queueFamilyIndex = queueIndex;
    queueArgs.queueCount = 1;
    queueArgs.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures = {};

    const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo deviceArgs = {};
    deviceArgs.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceArgs.pQueueCreateInfos = &queueArgs;
    deviceArgs.queueCreateInfoCount = 1;
    deviceArgs.pEnabledFeatures = &deviceFeatures;
    deviceArgs.enabledExtensionCount = 1;
    deviceArgs.ppEnabledExtensionNames = extensions;

    // create device creates logical device and all the queues
    VkDevice device;
    assert(vkCreateDevice(physicalDevice, &deviceArgs, nullptr, &device) == VK_SUCCESS);
    // queues are created, query for one found before (queueIndex)
    VkQueue queue;
    vkGetDeviceQueue(device, queueIndex, 0, &queue);

    /**************************************************************************
    Surface
    Purpose: to connect vulkan (more specifically swapchain) with window
    */
    VkWin32SurfaceCreateInfoKHR surfaceArgs = {};
    surfaceArgs.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceArgs.hinstance = hinstance;
    surfaceArgs.hwnd = hwnd;

    VkSurfaceKHR surface;
    assert(vkCreateWin32SurfaceKHR(vkInstance, &surfaceArgs, nullptr, &surface) == VK_SUCCESS);

    // check if the queue support presentation, its possbile that there is a different queue for this
    // for now, lets hope that selected queue supports it
    VkBool32 physicalDeviceSupportSurface = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueIndex, surface, &physicalDeviceSupportSurface);

    assert(physicalDeviceSupportSurface == VK_TRUE);
    
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);

    uint32_t formatCount;
    std::vector<VkSurfaceFormatKHR> surfaceFormats;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    surfaceFormats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data());

    // see if format available
    VkSurfaceFormatKHR surfaceFormat = {};
    for (uint32_t i = 0; i < formatCount; i++)
    {
        // this is the most optimal combination
        if (surfaceFormats[i].format == VK_FORMAT_B8G8R8A8_UNORM && surfaceFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            surfaceFormat = surfaceFormats[i];
            break;
        }
    }

    assert(surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM);

    // can be used to enumerate present modes
    // commented out because VK_PRESENT_MODE_FIFO_KHR used ant it's always present
    //uint32_t presentModeCount;
    //std::vector<VkPresentModeKHR> surfacePresentationModes;
    //vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    //assert(presentModeCount != 0);
    //surfacePresentationModes.resize(presentModeCount);
    //vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, 
    //    &presentModeCount, surfacePresentationModes.data());

    // VK_PRESENT_MODE_FIFO_KHR always exists
    VkPresentModeKHR surfacePresentationMode = VK_PRESENT_MODE_FIFO_KHR; // vsync;
    //VkPresentModeKHR surfacePresentationMode = VK_PRESENT_MODE_IMMEDIATE_KHR; // no vsync;

    /**************************************************************************
    Swap Chain
    Purpose: actually surface doesnt connect to the vulkan, swap chain does and
    surface connects to swap chain.
    Creating swapchain with vulkan creates VkImage as well which is actual vulkan object
    where vulkan renders pixels to
    */
    VkExtent2D swapChainExtent = { width,height };
    // recommendation is minimum + 1
    uint32_t frameBufferCount = surfaceCapabilities.minImageCount + 1;

    // check if max is not exceeded
    if (surfaceCapabilities.maxImageCount > 0 && frameBufferCount > surfaceCapabilities.maxImageCount)
        frameBufferCount = surfaceCapabilities.maxImageCount;

    VkSwapchainCreateInfoKHR swapChainArgs = {};
    swapChainArgs.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapChainArgs.surface = surface;
    swapChainArgs.minImageCount = frameBufferCount;
    swapChainArgs.imageFormat = surfaceFormat.format;
    swapChainArgs.imageColorSpace = surfaceFormat.colorSpace;
    swapChainArgs.imageExtent = swapChainExtent;
    // this is 1 unless your render is more than 2D
    swapChainArgs.imageArrayLayers = 1;
    // idk what that is
    swapChainArgs.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    // this flag has best performance if there is only one queue
    swapChainArgs.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // this needs to be set if there is more than one queue, e.g. one for graphics and one for present
    //swapChainArgs.queueFamilyIndexCount = 2;
    //swapChainArgs.pQueueFamilyIndices = queueFamilyIndices;
    // idk what that is
    swapChainArgs.preTransform = surfaceCapabilities.currentTransform;
    // idk what that is
    swapChainArgs.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapChainArgs.presentMode = surfacePresentationMode;
    // idk what that is
    swapChainArgs.clipped = VK_TRUE;

    VkSwapchainKHR swapChain;
    assert(vkCreateSwapchainKHR(device, &swapChainArgs, nullptr, &swapChain) == VK_SUCCESS);

    // images were created with swapchain
    // count was specified above but vulkan might have created more images than that
    vkGetSwapchainImagesKHR(device, swapChain, &frameBufferCount, nullptr);
    std::vector<VkImage> swapChainImages;
    swapChainImages.resize(frameBufferCount);
    vkGetSwapchainImagesKHR(device, swapChain, &frameBufferCount, swapChainImages.data());

    /**************************************************************************
    VkImageView
    Purpose: some helper object for VkImage (used here as render target)
    */
    std::vector<VkImageView> swapChainImageViews;
    swapChainImageViews.resize(frameBufferCount);

    for (size_t i = 0; i < frameBufferCount; i++)
    {
        VkImageViewCreateInfo imageViewArgs = {};
        imageViewArgs.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewArgs.image = swapChainImages[i];
        imageViewArgs.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewArgs.format = surfaceFormat.format;
        imageViewArgs.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewArgs.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewArgs.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewArgs.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewArgs.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewArgs.subresourceRange.baseMipLevel = 0;
        imageViewArgs.subresourceRange.levelCount = 1;
        imageViewArgs.subresourceRange.baseArrayLayer = 0;
        imageViewArgs.subresourceRange.layerCount = 1;

        assert(vkCreateImageView(device, &imageViewArgs, nullptr, &swapChainImageViews[i]) == VK_SUCCESS);
    }

    /************************************************************************************
    Descriptor Layout
    Purpose: idk, some helper object for descriptor
    */
    VkDescriptorSetLayoutBinding uboLayoutBinding = {};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutBinding = {};
    descriptorSetLayoutBinding.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutBinding.bindingCount = 1;
    descriptorSetLayoutBinding.pBindings = &uboLayoutBinding;

    VkDescriptorSetLayout descriptorSetLayout;
    assert(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutBinding, nullptr, &descriptorSetLayout) == VK_SUCCESS);

    /**************************************************************************
    Shaders
    */
    std::vector<byte> vsCode;
    std::vector<byte> psCode;

    readFile("vert.spv", vsCode);
    readFile("frag.spv", psCode);

    VkShaderModuleCreateInfo shaderCreateInfo = {};
    shaderCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderCreateInfo.codeSize = vsCode.size();
    // WARNING pCode is pointer to int so bytes must be aligned to 4byte
    shaderCreateInfo.pCode = (const uint32_t*)vsCode.data();

    VkShaderModule vsModule;
    assert(vkCreateShaderModule(device, &shaderCreateInfo, nullptr, &vsModule) == VK_SUCCESS);

    shaderCreateInfo.codeSize = psCode.size();
    // WARNING pCode is pointer to int so bytes must be aligned to 4byte
    shaderCreateInfo.pCode = (const uint32_t*)psCode.data();

    VkShaderModule psModule;
    assert(vkCreateShaderModule(device, &shaderCreateInfo, nullptr, &psModule) == VK_SUCCESS);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vsModule;
    vertShaderStageInfo.pName = "main";
    // you can set constants in shaders with this
    //vertShaderStageInfo.pSpecializationInfo = nullptr;

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = psModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    /**************************************************************************
    Vertex Input
    */
    VkVertexInputBindingDescription vertexInputBindingDescription = {};
    vertexInputBindingDescription.binding = 0;
    vertexInputBindingDescription.stride = 5 * sizeof(float);
    vertexInputBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertexInputAttributeDescription[2] = { {},{} };
    vertexInputAttributeDescription[0].binding = 0;
    vertexInputAttributeDescription[0].location = 0;
    vertexInputAttributeDescription[0].format = VK_FORMAT_R32G32_SFLOAT;
    vertexInputAttributeDescription[0].offset = 0;
    vertexInputAttributeDescription[1].binding = 0;
    vertexInputAttributeDescription[1].location = 1;
    vertexInputAttributeDescription[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexInputAttributeDescription[1].offset = sizeof(float) * 2;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &vertexInputBindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributeDescription;

    /**************************************************************************
    Viewport
    */
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapChainExtent.width;
    viewport.height = (float)swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = swapChainExtent;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    /**************************************************************************
    Rasterizer
    */
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f; // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

    /**************************************************************************
    Multisampling
    */
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f; // Optional
    multisampling.pSampleMask = nullptr; // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE; // Optional
    
    /**************************************************************************
    Blend
    */
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    /**************************************************************************
    Render pass and subpass
    */
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = surfaceFormat.format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    VkRenderPass renderPass;
    assert(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) == VK_SUCCESS);


    /**************************************************************************
    Pipeline
    */
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    VkPipelineLayout pipelineLayout;
    assert(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) == VK_SUCCESS);

    VkPipelineInputAssemblyStateCreateInfo pipelineInputAssembly = {};
    pipelineInputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    pipelineInputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    pipelineInputAssembly.primitiveRestartEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &pipelineInputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkPipeline graphicsPipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) == VK_SUCCESS);

    // it can be destroyed now because modelues were compiled to machine code and stored in the pipeline
    vkDestroyShaderModule(device, psModule, nullptr);
    vkDestroyShaderModule(device, vsModule, nullptr);

    /**************************************************************************
    Frame buffer
    */
    std::vector<VkFramebuffer> swapChainFramebuffers;
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); i++)
    {
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = swapChainImageViews.data() + i;
        framebufferInfo.width = swapChainExtent.width;
        framebufferInfo.height = swapChainExtent.height;
        framebufferInfo.layers = 1;

        assert(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) == VK_SUCCESS);
    }

    /**************************************************************************
    Command pool
    */
    VkCommandPoolCreateInfo commandPoolCreateInfo = {};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.queueFamilyIndex = queueIndex;
    // this can be used to indicate that commands will be shortlived
    //commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool commandPool;
    assert(vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool) == VK_SUCCESS);
    
    /**************************************************************************
    Vertex buffer
    */
    std::vector<float> vertices{
            -0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
            0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
            -0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
            0.5f, 0.5f, 0.0f, 1.0f, 1.0f,
    };

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    
    // staging buffer
    VkBufferCreateInfo stagingBufferInfo = {};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = sizeof(float) * vertices.size();
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer stagingBuffer;
    assert(vkCreateBuffer(device, &stagingBufferInfo, nullptr, &stagingBuffer) == VK_SUCCESS);

    // memoryTypeBits is a bitmask and contains one bit set for every supported memory type for the resource. 
    // Bit i is set if and only if the memory type i in the VkPhysicalDeviceMemoryProperties structure for the physical device is supported for the resource
    VkMemoryRequirements memRequirementsForStagingBuffer;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirementsForStagingBuffer);

    uint32_t desiredStagingBufferFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t stagingBufferMemoryTypeIndex = -1;

    for (int i = 0; i < memProperties.memoryTypeCount; i++)
    {
        // expression (A & B) == B
        // means that A must have at least all bits of B set (can have more)
        if ((memRequirementsForStagingBuffer.memoryTypeBits & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & desiredStagingBufferFlags) == desiredStagingBufferFlags)
        {
            stagingBufferMemoryTypeIndex = i;
            break;
        }
    }

    assert(stagingBufferMemoryTypeIndex != -1);

    VkMemoryAllocateInfo stagingBufferMemoryAllocInfo = {};
    stagingBufferMemoryAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingBufferMemoryAllocInfo.allocationSize = memRequirementsForStagingBuffer.size;
    stagingBufferMemoryAllocInfo.memoryTypeIndex = stagingBufferMemoryTypeIndex;

    VkDeviceMemory stagingBufferMemory;
    // WARNING: this call should be keept to minimum, allocate a bunch of memory at once and then use offset to use one chunk for multiple buffers
    assert(vkAllocateMemory(device, &stagingBufferMemoryAllocInfo, nullptr, &stagingBufferMemory) == VK_SUCCESS);
    vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);

    void* mappedStagingBufferMemory = nullptr;
    vkMapMemory(device, stagingBufferMemory, 0, stagingBufferInfo.size, 0, &mappedStagingBufferMemory);
    memcpy(mappedStagingBufferMemory, vertices.data(), (size_t)stagingBufferInfo.size);
    vkUnmapMemory(device, stagingBufferMemory);    

    // vertex buffer    
    VkBufferCreateInfo vertexBufferCreateInfo = {};
    vertexBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferCreateInfo.size = sizeof(float) * vertices.size();
    vertexBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vertexBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer vertexBuffer;
    assert(vkCreateBuffer(device, &vertexBufferCreateInfo, nullptr, &vertexBuffer) == VK_SUCCESS);

    VkMemoryRequirements vertexBufferMemRequirements;
    vkGetBufferMemoryRequirements(device, vertexBuffer, &vertexBufferMemRequirements);

    uint32_t desiredVertexBufferMemoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    uint32_t vertexBuffermemoryTypeIndex = -1;

    for (int i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((vertexBufferMemRequirements.memoryTypeBits & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & desiredVertexBufferMemoryFlags) == desiredVertexBufferMemoryFlags)
        {
            vertexBuffermemoryTypeIndex = i;
            break;
        }
    }

    assert(vertexBuffermemoryTypeIndex != -1);

    VkMemoryAllocateInfo vertexBufferMemoryAllocInfo = {};
    vertexBufferMemoryAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vertexBufferMemoryAllocInfo.allocationSize = vertexBufferMemRequirements.size;
    vertexBufferMemoryAllocInfo.memoryTypeIndex = vertexBuffermemoryTypeIndex;

    VkDeviceMemory vertexBufferMemory;
    assert(vkAllocateMemory(device, &vertexBufferMemoryAllocInfo, nullptr, &vertexBufferMemory) == VK_SUCCESS);
    vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);    

    // copy from staging to vertex buffer
    VkCommandBufferAllocateInfo stagingToVertexCopyCommandAllocInfo = {};
    stagingToVertexCopyCommandAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    stagingToVertexCopyCommandAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    stagingToVertexCopyCommandAllocInfo.commandPool = commandPool;
    stagingToVertexCopyCommandAllocInfo.commandBufferCount = 1;

    VkCommandBuffer stagingToVertexCopyCommand;
    assert(vkAllocateCommandBuffers(device, &stagingToVertexCopyCommandAllocInfo, &stagingToVertexCopyCommand) == VK_SUCCESS);

    VkCommandBufferBeginInfo stagingToVertexCopyCommandBeginInfo = {};
    stagingToVertexCopyCommandBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    stagingToVertexCopyCommandBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    assert(vkBeginCommandBuffer(stagingToVertexCopyCommand, &stagingToVertexCopyCommandBeginInfo) == VK_SUCCESS);

    VkBufferCopy stagingToVertexCopyRegion = {};
    stagingToVertexCopyRegion.srcOffset = 0;
    stagingToVertexCopyRegion.dstOffset = 0;
    stagingToVertexCopyRegion.size = sizeof(float) * vertices.size();
    vkCmdCopyBuffer(stagingToVertexCopyCommand, stagingBuffer, vertexBuffer, 1, &stagingToVertexCopyRegion);

    assert(vkEndCommandBuffer(stagingToVertexCopyCommand) == VK_SUCCESS);

    VkSubmitInfo stagingToVertexCopyCommandSubmitInfo = {};
    stagingToVertexCopyCommandSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    stagingToVertexCopyCommandSubmitInfo.commandBufferCount = 1;
    stagingToVertexCopyCommandSubmitInfo.pCommandBuffers = &stagingToVertexCopyCommand;

    vkQueueSubmit(queue, 1, &stagingToVertexCopyCommandSubmitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    // this stuff is no longer needed
    vkFreeCommandBuffers(device, commandPool, 1, &stagingToVertexCopyCommand);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    /**************************************************************************
    Uniform buffer (and descriptor pool and set to bind them)
    Purpose: to set data for shaders every frame
    */
    struct { float scale, x, y; } transform;

    VkBufferCreateInfo uniformBufferCreateInfo = {};
    uniformBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    uniformBufferCreateInfo.size = sizeof(transform);
    uniformBufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    uniformBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer uniformBuffer;
    assert(vkCreateBuffer(device, &uniformBufferCreateInfo, nullptr, &uniformBuffer) == VK_SUCCESS);

    VkMemoryRequirements uniformBuffermemRequirements;
    vkGetBufferMemoryRequirements(device, uniformBuffer, &uniformBuffermemRequirements);

    uint32_t desiredUniformBufferMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t uniformBufferMemoryTypeIndex = -1;

    for (int i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((uniformBuffermemRequirements.memoryTypeBits & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & desiredUniformBufferMemoryFlags) == desiredUniformBufferMemoryFlags)
        {
            uniformBufferMemoryTypeIndex = i;
            break;
        }
    }

    assert(uniformBufferMemoryTypeIndex != -1);

    VkMemoryAllocateInfo uniformBufferMemoryAllocInfo = {};
    uniformBufferMemoryAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    uniformBufferMemoryAllocInfo.allocationSize = uniformBuffermemRequirements.size;
    uniformBufferMemoryAllocInfo.memoryTypeIndex = uniformBufferMemoryTypeIndex;

    VkDeviceMemory uniformBufferMemory;
    assert(vkAllocateMemory(device, &uniformBufferMemoryAllocInfo, nullptr, &uniformBufferMemory) == VK_SUCCESS);
    vkBindBufferMemory(device, uniformBuffer, uniformBufferMemory, 0);

    // descriptor pool and sets
    VkDescriptorPoolSize descriptorPoolSize = {};
    descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorPoolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
    descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.poolSizeCount = 1;
    descriptorPoolCreateInfo.pPoolSizes = &descriptorPoolSize;
    descriptorPoolCreateInfo.maxSets = 1;

    VkDescriptorPool descriptorPool;
    assert(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool) == VK_SUCCESS);

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;

    // tutorial used 3 sets (1 per frame) but since I only render one frame at a time
    // create only 1 set
    VkDescriptorSet descriptorSet;
    // You don't need to explicitly clean up descriptor sets, because they will be automatically freed when the descriptor pool is destroyed
    assert(vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &descriptorSet) == VK_SUCCESS);

    VkDescriptorBufferInfo descriptorBufferInfo = {};
    descriptorBufferInfo.buffer = uniformBuffer;
    descriptorBufferInfo.offset = 0;
    descriptorBufferInfo.range = sizeof(transform);

    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &descriptorBufferInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    /**************************************************************************
    Command buffers
    */
    VkCommandBufferAllocateInfo drawCommandAllocInfo = {};
    drawCommandAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    drawCommandAllocInfo.commandPool = commandPool;
    drawCommandAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    drawCommandAllocInfo.commandBufferCount = swapChainFramebuffers.size();

    std::vector<VkCommandBuffer> drawCommands;
    drawCommands.resize(swapChainFramebuffers.size());
    assert(vkAllocateCommandBuffers(device, &drawCommandAllocInfo, drawCommands.data()) == VK_SUCCESS);

    for (size_t i = 0; i < drawCommands.size(); i++)
    {
        VkCommandBufferBeginInfo drawCommandBeginInfo = {};
        drawCommandBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        drawCommandBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        assert(vkBeginCommandBuffer(drawCommands[i], &drawCommandBeginInfo) == VK_SUCCESS);

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.framebuffer = swapChainFramebuffers[i];
        renderPassBeginInfo.renderArea.offset = { 0, 0 };
        renderPassBeginInfo.renderArea.extent = swapChainExtent;

        VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(drawCommands[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(drawCommands[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        VkDeviceSize vbOffsets[] = { 0 };
        vkCmdBindVertexBuffers(drawCommands[i], 0, 1, &vertexBuffer, vbOffsets);
        vkCmdBindDescriptorSets(drawCommands[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdDraw(drawCommands[i], vertices.size(), 1, 0, 0);
        vkCmdEndRenderPass(drawCommands[i]);

        assert(vkEndCommandBuffer(drawCommands[i]) == VK_SUCCESS);
    }

    /**************************************************************************
    Semaphores
    */
    VkSemaphoreCreateInfo semaphoreCreateInfo = {};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    assert(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &imageAvailableSemaphore) == VK_SUCCESS);
    assert(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &renderFinishedSemaphore) == VK_SUCCESS);

    //
    // program loop ***********************************************************
    //
    MSG msg;
    int frame = 0;

    while (true)
    {
        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (msg.message == WM_QUIT)
            break;

        //
        // draw ***************************************************************
        //
        uint32_t imageIndex;
        // vkAcquireNextImageKHR returns non success if surface changes (more accurately, if surface is not available for presenting) for example when window is resized or minimalized
        // im only handling minimalization by stoping this draw call
        if (vkAcquireNextImageKHR(device, swapChain, LLONG_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex) != VK_SUCCESS)
        {
            continue;
        }

        transform.scale = (sinf(frame / 30.0f) + 1) / 2.0f;
        transform.x = 0;
        transform.y = sinf(frame / 100.0f);

        void* data = nullptr;
        vkMapMemory(device, uniformBufferMemory, 0, sizeof(transform), 0, &data);
        memcpy(data, &transform, sizeof(transform));
        vkUnmapMemory(device, uniformBufferMemory);

        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        VkSubmitInfo drawCommandSubmitInfo = {};
        drawCommandSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        drawCommandSubmitInfo.waitSemaphoreCount = 1;
        drawCommandSubmitInfo.pWaitSemaphores = &imageAvailableSemaphore;
        drawCommandSubmitInfo.pWaitDstStageMask = waitStages;
        drawCommandSubmitInfo.commandBufferCount = 1;
        drawCommandSubmitInfo.pCommandBuffers = &drawCommands[imageIndex];
        drawCommandSubmitInfo.signalSemaphoreCount = 1;
        drawCommandSubmitInfo.pSignalSemaphores = &renderFinishedSemaphore;

        assert(vkQueueSubmit(queue, 1, &drawCommandSubmitInfo, VK_NULL_HANDLE) == VK_SUCCESS);

        // i dont know what that is
        /*VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;*/

        // i dont know what that is
        //VkRenderPassCreateInfo renderPassInfo = {};
        //renderPassInfo.dependencyCount = 1;
        //renderPassInfo.pDependencies = &dependency;

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapChain;
        presentInfo.pImageIndices = &imageIndex;

        vkQueuePresentKHR(queue, &presentInfo);

        // this is NOT OPTIMAL way to wait until queue is done before continuing
        // without this it's possible that draw() function will be called faster than gpu can actually render and this will make program grow in memory usage
        vkQueueWaitIdle(queue);

        frame++;
    }

    //
    // clean up ***************************************************************
    //
    // this is so all queues are finished and dont destroy anything before that
    vkDeviceWaitIdle(device);

    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);
    vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
    vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);

    for (size_t i = 0; i < swapChainFramebuffers.size(); i++)
        vkDestroyFramebuffer(device, swapChainFramebuffers[i], nullptr);

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    vkDestroyBuffer(device, uniformBuffer, nullptr);
    vkFreeMemory(device, uniformBufferMemory, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);

    for (size_t i = 0; i < swapChainImageViews.size(); i++)
        vkDestroyImageView(device, swapChainImageViews[i], nullptr);

    vkDestroySwapchainKHR(device, swapChain, nullptr);
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(vkInstance, nullptr);

    DestroyWindow(hwnd);
    UnregisterClass(wndClassName, hinstance);

    return 0;
}
