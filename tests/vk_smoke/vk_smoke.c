// SPDX-License-Identifier: MIT
// vk_smoke — Vulkan 1.0 端到端冒烟测试（radeon-windows 项目）。
//
// 目的：在 terakan（或任意 Vulkan ICD）安装后，程序化验证
//   [1] Loader 可用，实例可创建；
//   [2] 物理设备枚举（打印 AMD 设备属性/内存堆/队列族）；
//   [3] 逻辑设备 + compute 队列可创建；
//   [4] HOST_VISIBLE 内存分配 + SSBO 绑定 + 描述符写入；
//   [5] 计算 pipeline（内置 SPIR-V：data[x] = data[x]*2 + 1）可创建；
//   [6] 命令缓冲提交 + vkQueueWaitIdle 后结果正确。
//
// 这是 M1/M4 验收门（"vkcube 出三角"）之前的自动化最低标准：
// ICD→loader→驱动→内核 KMD 提交全链路至少跑通一次 compute。
//
// 说明：不依赖 vulkan.h，全部通过 LoadLibrary("vulkan-1.dll") +
// vkGetInstanceProcAddr / vkGetDeviceProcAddr 动态加载，结构与枚举
// 按 Vulkan 1.0 规范手工声明（与 WDK 侧 gpu_probe 同一套路，
// 便于 llvm-mingw 交叉编译）。退出码：0=PASS，1=FAIL。
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "spirv_compound.h"   // RokSpirvCompound / ROK_SPIRV_NUM_WORDS

// ---------------------------------------------------------------------------
// Vulkan 最小 ABI（对照 Vulkan 1.0 规范 / registry vk.xml 手工声明）
// ---------------------------------------------------------------------------
#define ROKVK __stdcall   // Vulkan 宿主约定：VKAPI_CALL = __stdcall (win32)

typedef void*    RokVkHandle;
typedef uint64_t RokVkDeviceSize;

#define ROK_VK_MAKE_VERSION(major, minor, patch) \
    ((((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) | ((uint32_t)(patch)))
#define ROK_VK_API_VERSION_1_0 ROK_VK_MAKE_VERSION(1, 0, 0)

// VkStructureType（registry vk.xml 核对值）
#define ROK_VK_STRUCTURE_TYPE_APPLICATION_INFO              0u
#define ROK_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO          1u
#define ROK_VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO      2u
#define ROK_VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO            3u
#define ROK_VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO          5u
#define ROK_VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO            12u
#define ROK_VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO     16u
#define ROK_VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO  29u
#define ROK_VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO   30u
#define ROK_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO 32u
#define ROK_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO   33u
#define ROK_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO  34u
#define ROK_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET          35u
#define ROK_VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO      39u
#define ROK_VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO  40u
#define ROK_VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO     42u
#define ROK_VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO 18u

// 枚举 / 位掩码（registry 核对值）
#define ROK_VK_QUEUE_GRAPHICS_BIT       0x1u
#define ROK_VK_QUEUE_COMPUTE_BIT        0x2u
#define ROK_VK_MEMORY_PROPERTY_HOST_VISIBLE   0x2u
#define ROK_VK_MEMORY_PROPERTY_HOST_COHERENT  0x4u
#define ROK_VK_BUFFER_USAGE_STORAGE_BUFFER    0x20u
#define ROK_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER 7u
#define ROK_VK_SHADER_STAGE_COMPUTE_BIT       0x20u
#define ROK_VK_COMMAND_BUFFER_LEVEL_PRIMARY   0u
#define ROK_VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT 0x1u
#define ROK_VK_SHARING_MODE_EXCLUSIVE         0u
#define ROK_VK_PIPELINE_BIND_POINT_COMPUTE    2u

#define ROK_VK_MAX_PHYSICAL_DEVICE_NAME_SIZE 256

typedef struct _RokVkApplicationInfo {
    uint32_t    sType;
    const void* pNext;
    const char* pApplicationName;
    uint32_t    applicationVersion;
    const char* pEngineName;
    uint32_t    engineVersion;
    uint32_t    apiVersion;
} RokVkApplicationInfo;

typedef struct _RokVkInstanceCreateInfo {
    uint32_t     sType;
    const void*  pNext;
    uint32_t     flags;
    const RokVkApplicationInfo* pApplicationInfo;
    uint32_t     enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t     enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
} RokVkInstanceCreateInfo;

typedef struct _RokVkPhysicalDevicePropsPrefix {
    uint32_t    apiVersion;
    uint32_t    driverVersion;
    uint32_t    vendorID;
    uint32_t    deviceID;
    uint32_t    deviceType;
    char        deviceName[ROK_VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint8_t     pipelineCacheUUID[16];
    // 之后为 VkPhysicalDeviceLimits / sparseProperties —— 大缓冲承载，只读前缀。
} RokVkPhysicalDevicePropsPrefix;

typedef struct _RokVkExtent3D { uint32_t width, height, depth; } RokVkExtent3D;

typedef struct _RokVkQueueFamilyProperties {
    uint32_t     queueFlags;
    uint32_t     queueCount;
    uint32_t     timestampValidBits;
    RokVkExtent3D minImageTransferGranularity;
} RokVkQueueFamilyProperties;

typedef struct _RokVkMemoryType {
    uint32_t propertyFlags;
    uint32_t heapIndex;
} RokVkMemoryType;

typedef struct _RokVkMemoryHeap {
    RokVkDeviceSize size;
    uint32_t        flags;
} RokVkMemoryHeap;

typedef struct _RokVkPhysicalDeviceMemoryProperties {
    uint32_t        memoryTypeCount;
    RokVkMemoryType memoryTypes[32];
    uint32_t        memoryHeapCount;
    RokVkMemoryHeap memoryHeaps[16];
} RokVkPhysicalDeviceMemoryProperties;

typedef struct _RokVkDeviceQueueCreateInfo {
    uint32_t     sType;
    const void*  pNext;
    uint32_t     flags;
    uint32_t     queueFamilyIndex;
    uint32_t     queueCount;
    const float* pQueuePriorities;
} RokVkDeviceQueueCreateInfo;

typedef struct _RokVkDeviceCreateInfo {
    uint32_t     sType;
    const void*  pNext;
    uint32_t     flags;
    uint32_t     queueCreateInfoCount;
    const RokVkDeviceQueueCreateInfo* pQueueCreateInfos;
    uint32_t     enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t     enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
    const void*  pEnabledFeatures;   // const VkPhysicalDeviceFeatures*（NULL 即可）
} RokVkDeviceCreateInfo;

typedef struct _RokVkBufferCreateInfo {
    uint32_t      sType;
    const void*   pNext;
    uint32_t      flags;
    RokVkDeviceSize size;
    uint32_t      usage;
    uint32_t      sharingMode;
    uint32_t      queueFamilyIndexCount;
    const uint32_t* pQueueFamilyIndices;
} RokVkBufferCreateInfo;

typedef struct _RokVkMemoryRequirements {
    RokVkDeviceSize size;
    RokVkDeviceSize alignment;
    uint32_t        memoryTypeBits;
} RokVkMemoryRequirements;

typedef struct _RokVkMemoryAllocateInfo {
    uint32_t       sType;
    const void*    pNext;
    RokVkDeviceSize allocationSize;
    uint32_t       memoryTypeIndex;
} RokVkMemoryAllocateInfo;

typedef struct _RokVkDescriptorSetLayoutBinding {
    uint32_t     binding;
    uint32_t     descriptorType;
    uint32_t     descriptorCount;
    uint32_t     stageFlags;
    const void*  pImmutableSamplers;
} RokVkDescriptorSetLayoutBinding;

typedef struct _RokVkDescriptorSetLayoutCreateInfo {
    uint32_t    sType;
    const void* pNext;
    uint32_t    flags;
    uint32_t    bindingCount;
    const RokVkDescriptorSetLayoutBinding* pBindings;
} RokVkDescriptorSetLayoutCreateInfo;

typedef struct _RokVkDescriptorPoolSize {
    uint32_t type;
    uint32_t descriptorCount;
} RokVkDescriptorPoolSize;

typedef struct _RokVkDescriptorPoolCreateInfo {
    uint32_t    sType;
    const void* pNext;
    uint32_t    flags;
    uint32_t    maxSets;
    uint32_t    poolSizeCount;
    const RokVkDescriptorPoolSize* pPoolSizes;
} RokVkDescriptorPoolCreateInfo;

typedef struct _RokVkDescriptorSetAllocateInfo {
    uint32_t        sType;
    const void*     pNext;
    RokVkHandle     descriptorPool;
    uint32_t        descriptorSetCount;
    const RokVkHandle* pSetLayouts;
} RokVkDescriptorSetAllocateInfo;

typedef struct _RokVkDescriptorBufferInfo {
    RokVkHandle    buffer;
    RokVkDeviceSize offset;
    RokVkDeviceSize range;
} RokVkDescriptorBufferInfo;

typedef struct _RokVkWriteDescriptorSet {
    uint32_t    sType;
    const void* pNext;
    RokVkHandle dstSet;
    uint32_t    dstBinding;
    uint32_t    dstArrayElement;
    uint32_t    descriptorCount;
    uint32_t    descriptorType;
    const void* pImageInfo;
    const RokVkDescriptorBufferInfo* pBufferInfo;
    const void* pTexelBufferView;
} RokVkWriteDescriptorSet;

typedef struct _RokVkShaderModuleCreateInfo {
    uint32_t      sType;
    const void*   pNext;
    uint32_t      flags;
    size_t        codeSize;
    const uint32_t* pCode;
} RokVkShaderModuleCreateInfo;

typedef struct _RokVkPipelineShaderStageCreateInfo {
    uint32_t     sType;
    const void*  pNext;
    uint32_t     flags;
    uint32_t     stage;
    RokVkHandle  module;
    const char*  pName;
    const void*  pSpecializationInfo;
} RokVkPipelineShaderStageCreateInfo;

typedef struct _RokVkPipelineLayoutCreateInfo {
    uint32_t    sType;
    const void* pNext;
    uint32_t    flags;
    uint32_t    setLayoutCount;
    const RokVkHandle* pSetLayouts;
    uint32_t    pushConstantRangeCount;
    const void* pPushConstantRanges;
} RokVkPipelineLayoutCreateInfo;

typedef struct _RokVkComputePipelineCreateInfo {
    uint32_t    sType;
    const void* pNext;
    uint32_t    flags;
    RokVkPipelineShaderStageCreateInfo stage;
    RokVkHandle layout;
    RokVkHandle basePipelineHandle;
    int32_t     basePipelineIndex;
} RokVkComputePipelineCreateInfo;

typedef struct _RokVkCommandPoolCreateInfo {
    uint32_t    sType;
    const void* pNext;
    uint32_t    flags;
    uint32_t    queueFamilyIndex;
} RokVkCommandPoolCreateInfo;

typedef struct _RokVkCommandBufferAllocateInfo {
    uint32_t    sType;
    const void* pNext;
    RokVkHandle commandPool;
    uint32_t    level;
    uint32_t    commandBufferCount;
} RokVkCommandBufferAllocateInfo;

typedef struct _RokVkCommandBufferBeginInfo {
    uint32_t    sType;
    const void* pNext;
    uint32_t    flags;
    const void* pInheritanceInfo;
} RokVkCommandBufferBeginInfo;

typedef struct _RokVkSubmitInfo {
    uint32_t              sType;
    const void*           pNext;
    uint32_t              waitSemaphoreCount;
    const RokVkHandle*    pWaitSemaphores;
    const uint32_t*       pWaitDstStageMask;
    uint32_t              signalSemaphoreCount;
    const RokVkHandle*    pSignalSemaphores;
    uint32_t              commandBufferCount;
    const RokVkHandle*    pCommandBuffers;
} RokVkSubmitInfo;

// ---------------------------------------------------------------------------
// 函数指针
// ---------------------------------------------------------------------------
#define ROK_PFN(name) typedef int32_t (ROKVK *P##name)

ROK_PFN(vkCreateInstance)(const RokVkInstanceCreateInfo*, const void*, RokVkHandle*);
ROK_PFN(vkEnumeratePhysicalDevices)(RokVkHandle, uint32_t*, RokVkHandle*);
ROK_PFN(vkGetPhysicalDeviceProperties)(RokVkHandle, void*);
ROK_PFN(vkGetPhysicalDeviceQueueFamilyProperties)(RokVkHandle, uint32_t*, RokVkQueueFamilyProperties*);
ROK_PFN(vkGetPhysicalDeviceMemoryProperties)(RokVkHandle, RokVkPhysicalDeviceMemoryProperties*);
ROK_PFN(vkCreateDevice)(RokVkHandle, const RokVkDeviceCreateInfo*, const void*, RokVkHandle*);
ROK_PFN(vkGetDeviceQueue)(RokVkHandle, uint32_t, uint32_t, RokVkHandle*);
ROK_PFN(vkCreateBuffer)(RokVkHandle, const RokVkBufferCreateInfo*, const void*, RokVkHandle*);
ROK_PFN(vkGetBufferMemoryRequirements)(RokVkHandle, RokVkHandle, RokVkMemoryRequirements*);
ROK_PFN(vkAllocateMemory)(RokVkHandle, const RokVkMemoryAllocateInfo*, const void*, RokVkHandle*);
ROK_PFN(vkBindBufferMemory)(RokVkHandle, RokVkHandle, RokVkHandle, RokVkDeviceSize);
ROK_PFN(vkMapMemory)(RokVkHandle, RokVkHandle, RokVkDeviceSize, RokVkDeviceSize, uint32_t, void**);
ROK_PFN(vkCreateDescriptorSetLayout)(RokVkHandle, const RokVkDescriptorSetLayoutCreateInfo*, const void*, RokVkHandle*);
ROK_PFN(vkCreateDescriptorPool)(RokVkHandle, const RokVkDescriptorPoolCreateInfo*, const void*, RokVkHandle*);
ROK_PFN(vkAllocateDescriptorSets)(RokVkHandle, const RokVkDescriptorSetAllocateInfo*, RokVkHandle*);
ROK_PFN(vkUpdateDescriptorSets)(RokVkHandle, uint32_t, const RokVkWriteDescriptorSet*, uint32_t, const void*);
ROK_PFN(vkCreateShaderModule)(RokVkHandle, const RokVkShaderModuleCreateInfo*, const void*, RokVkHandle*);
ROK_PFN(vkCreatePipelineLayout)(RokVkHandle, const RokVkPipelineLayoutCreateInfo*, const void*, RokVkHandle*);
ROK_PFN(vkCreateComputePipelines)(RokVkHandle, RokVkHandle, uint32_t, const RokVkComputePipelineCreateInfo*, const void*, RokVkHandle*);
ROK_PFN(vkCreateCommandPool)(RokVkHandle, const RokVkCommandPoolCreateInfo*, const void*, RokVkHandle*);
ROK_PFN(vkAllocateCommandBuffers)(RokVkHandle, const RokVkCommandBufferAllocateInfo*, RokVkHandle*);
ROK_PFN(vkBeginCommandBuffer)(RokVkHandle, const RokVkCommandBufferBeginInfo*);
ROK_PFN(vkEndCommandBuffer)(RokVkHandle);
ROK_PFN(vkCmdBindPipeline)(RokVkHandle, uint32_t, RokVkHandle);
ROK_PFN(vkCmdBindDescriptorSets)(RokVkHandle, uint32_t, RokVkHandle, uint32_t, uint32_t, const RokVkHandle*, uint32_t, const uint32_t*);
ROK_PFN(vkCmdDispatch)(RokVkHandle, uint32_t, uint32_t, uint32_t);
ROK_PFN(vkQueueSubmit)(RokVkHandle, uint32_t, const RokVkSubmitInfo*, RokVkHandle);
ROK_PFN(vkQueueWaitIdle)(RokVkHandle);
#undef ROK_PFN

// 绑定点/元素数（与 SPIR-V 一致：std430 SSBO，binding=0，8 个 uint）
#define ROK_SMOKE_ELEMENTS 8u

static int32_t g_lastResult;
#define ROK_CHECK(msg, pfn)                                               \
    do {                                                                  \
        g_lastResult = (int32_t)(pfn);                                    \
        if (g_lastResult != 0) {                                          \
            printf("  [FAIL] %s : VkResult=%d\n", (msg), g_lastResult);   \
            goto fail;                                                    \
        }                                                                 \
        printf("  [ok]   %s\n", (msg));                                   \
    } while (0)

static int RunSmoke(void)
{
    HMODULE vkdll;
    void *(WINAPI *vkGetInstanceProcAddr)(RokVkHandle, const char*);
    void *(WINAPI *vkGetDeviceProcAddr)(RokVkHandle, const char*);

    RokVkHandle instance = NULL, phys = NULL, device = NULL, queue = NULL;
    RokVkHandle buffer = NULL, memory = NULL, setLayout = NULL, descPool = NULL;
    RokVkHandle descSet = NULL, shaderModule = NULL, pipelineLayout = NULL;
    RokVkHandle pipeline = NULL, commandPool = NULL, commandBuffer = NULL;
    RokVkHandle *devices = NULL;
    uint32_t *queueFamilyIndices = NULL;
    RokVkQueueFamilyProperties *queueProps = NULL;
    void *mapped = NULL;
    float prio = 1.0f;

    uint32_t i, nPhys = 0, nQueues = 0;
    uint32_t computeFamily = 0xFFFFFFFFu;
    uint32_t verify[ROK_SMOKE_ELEMENTS];
    int ret = 1;

    // ---- [0] loader ----
    vkdll = LoadLibraryA("vulkan-1.dll");
    if (vkdll == NULL)
    {
        printf("  [FAIL] vulkan-1.dll 未找到（Vulkan Runtime / loader 未安装）\n");
        return 1;
    }
    printf("  [ok]   vulkan-1.dll 加载\n");

    vkGetInstanceProcAddr = (void *(WINAPI *)(RokVkHandle, const char*))
        (void *)GetProcAddress(vkdll, "vkGetInstanceProcAddr");
    vkGetDeviceProcAddr = (void *(WINAPI *)(RokVkHandle, const char*))
        (void *)GetProcAddress(vkdll, "vkGetDeviceProcAddr");
    if (vkGetInstanceProcAddr == NULL || vkGetDeviceProcAddr == NULL)
    {
        printf("  [FAIL] loader 导出不完整\n");
        return 1;
    }

    // ---- [1] instance ----
    {
        RokVkApplicationInfo app;
        RokVkInstanceCreateInfo ici;
        PvkCreateInstance pCreateInstance =
            (PvkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");

        memset(&app, 0, sizeof(app));
        app.sType = ROK_VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "rok-vk-smoke";
        app.applicationVersion = 1;
        app.pEngineName = "radeon-windows";
        app.engineVersion = 1;
        app.apiVersion = ROK_VK_API_VERSION_1_0;

        memset(&ici, 0, sizeof(ici));
        ici.sType = ROK_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ici.pApplicationInfo = &app;

        if (pCreateInstance == NULL)
        {
            printf("  [FAIL] vkCreateInstance 缺失\n");
            return 1;
        }
        ROK_CHECK("vkCreateInstance", pCreateInstance(&ici, NULL, &instance));
    }

    // ---- [2] 物理设备 ----
    {
        PvkEnumeratePhysicalDevices pEnum =
            (PvkEnumeratePhysicalDevices)vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
        PvkGetPhysicalDeviceProperties pProps =
            (PvkGetPhysicalDeviceProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties");
        PvkGetPhysicalDeviceQueueFamilyProperties pQFP =
            (PvkGetPhysicalDeviceQueueFamilyProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
        PvkGetPhysicalDeviceMemoryProperties pMem =
            (PvkGetPhysicalDeviceMemoryProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceMemoryProperties");

        unsigned char propsBuf[1024];
        RokVkPhysicalDevicePropsPrefix *props;
        int amdFound = -1;

        ROK_CHECK("vkEnumeratePhysicalDevices(count)", pEnum(instance, &nPhys, NULL));
        printf("  [info] physical devices: %u\n", nPhys);
        if (nPhys == 0)
        {
            printf("  [FAIL] 没有任何物理设备（ICD 未安装？检查 VK_ICD_FILENAMES）\n");
            return 1;
        }

        devices = (RokVkHandle *)HeapAlloc(GetProcessHeap(), 0, sizeof(RokVkHandle) * nPhys);
        if (devices == NULL) { printf("  [FAIL] OOM\n"); return 1; }
        ROK_CHECK("vkEnumeratePhysicalDevices(list)", pEnum(instance, &nPhys, devices));

        for (i = 0; i < nPhys; i++)
        {
            memset(propsBuf, 0, sizeof(propsBuf));
            pProps(devices[i], propsBuf);
            props = (RokVkPhysicalDevicePropsPrefix *)propsBuf;
            printf("  ---- VkPhysicalDevice #%u ----\n", i);
            printf("  deviceName : %s\n", props->deviceName);
            printf("  apiVersion : %u.%u.%u\n", props->apiVersion >> 22,
                   (props->apiVersion >> 12) & 0x3FF, props->apiVersion & 0xFFF);
            printf("  vendorID   : 0x%04X%s\n", props->vendorID,
                   props->vendorID == 0x1002 ? "  (AMD)" : "");
            printf("  deviceID   : 0x%04X\n", props->deviceID);

            if (props->vendorID == 0x1002 && amdFound < 0)
            {
                amdFound = (int)i;
                phys = devices[i];
            }
        }

        if (amdFound < 0)
        {
            printf("  [FAIL] 未发现 AMD (0x1002) 物理设备\n");
            goto fail;
        }
        printf("  [ok]   选中 AMD 设备 #%d\n", amdFound);

        // 队列族：找带 COMPUTE 位的族
        pQFP(phys, &nQueues, NULL);
        if (nQueues == 0) { printf("  [FAIL] 无队列族\n"); goto fail; }
        queueProps = (RokVkQueueFamilyProperties *)HeapAlloc(
            GetProcessHeap(), 0, sizeof(RokVkQueueFamilyProperties) * nQueues);
        if (queueProps == NULL) { printf("  [FAIL] OOM\n"); goto fail; }
        pQFP(phys, &nQueues, queueProps);
        for (i = 0; i < nQueues; i++)
        {
            printf("  queueFamily #%u: flags=%#x count=%u\n",
                   i, queueProps[i].queueFlags, queueProps[i].queueCount);
            if ((queueProps[i].queueFlags & ROK_VK_QUEUE_COMPUTE_BIT) &&
                queueProps[i].queueCount > 0)
            {
                computeFamily = i;
                break;
            }
        }
        if (computeFamily == 0xFFFFFFFFu)
        {
            printf("  [FAIL] 没有 compute 队列族\n");
            goto fail;
        }
        printf("  [ok]   compute 队列族 #%u\n", computeFamily);

        // 内存堆/类型诊断
        {
            RokVkPhysicalDeviceMemoryProperties mem;
            memset(&mem, 0, sizeof(mem));
            pMem(phys, &mem);
            printf("  [info] memory heaps=%u types=%u\n",
                   mem.memoryHeapCount, mem.memoryTypeCount);
            for (i = 0; i < mem.memoryHeapCount && i < 4; i++)
                printf("    heap #%u: size=%lluMB flags=%#x\n", i,
                       (unsigned long long)(mem.memoryHeaps[i].size / (1024 * 1024)),
                       mem.memoryHeaps[i].flags);
        }
    }

    // ---- [3] device + compute queue ----
    {
        RokVkDeviceCreateInfo dci;
        RokVkDeviceQueueCreateInfo qci;
        PvkCreateDevice pCreateDevice =
            (PvkCreateDevice)vkGetInstanceProcAddr(instance, "vkCreateDevice");
        PvkGetDeviceQueue pGetQueue =
            (PvkGetDeviceQueue)vkGetDeviceProcAddr(device, "vkGetDeviceQueue");

        memset(&qci, 0, sizeof(qci));
        qci.sType = ROK_VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = computeFamily;
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;

        memset(&dci, 0, sizeof(dci));
        dci.sType = ROK_VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        // 1.0 核心 compute 路径零扩展零 feature。

        ROK_CHECK("vkCreateDevice", pCreateDevice(phys, &dci, NULL, &device));

        pGetQueue = (PvkGetDeviceQueue)vkGetDeviceProcAddr(device, "vkGetDeviceQueue");
        pGetQueue(device, computeFamily, 0, &queue);
        if (queue == NULL) { printf("  [FAIL] vkGetDeviceQueue\n"); goto fail; }
        printf("  [ok]   vkGetDeviceQueue\n");
    }

    // ---- [4] buffer + HOST_VISIBLE|COHERENT memory + map ----
    {
        RokVkBufferCreateInfo bci;
        RokVkMemoryRequirements req;
        RokVkMemoryAllocateInfo mai;
        RokVkPhysicalDeviceMemoryProperties mem;
        PvkGetPhysicalDeviceMemoryProperties pMem =
            (PvkGetPhysicalDeviceMemoryProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceMemoryProperties");
        PvkCreateBuffer pCreateBuffer =
            (PvkCreateBuffer)vkGetDeviceProcAddr(device, "vkCreateBuffer");
        PvkGetBufferMemoryRequirements pReq =
            (PvkGetBufferMemoryRequirements)vkGetDeviceProcAddr(device, "vkGetBufferMemoryRequirements");
        PvkAllocateMemory pAlloc =
            (PvkAllocateMemory)vkGetDeviceProcAddr(device, "vkAllocateMemory");
        PvkBindBufferMemory pBind =
            (PvkBindBufferMemory)vkGetDeviceProcAddr(device, "vkBindBufferMemory");
        PvkMapMemory pMap =
            (PvkMapMemory)vkGetDeviceProcAddr(device, "vkMapMemory");
        uint32_t t;
        int typeIdx = -1;

        memset(&bci, 0, sizeof(bci));
        bci.sType = ROK_VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = sizeof(uint32_t) * ROK_SMOKE_ELEMENTS;
        bci.usage = ROK_VK_BUFFER_USAGE_STORAGE_BUFFER;
        bci.sharingMode = ROK_VK_SHARING_MODE_EXCLUSIVE;
        ROK_CHECK("vkCreateBuffer", pCreateBuffer(device, &bci, NULL, &buffer));

        memset(&req, 0, sizeof(req));
        pReq(device, buffer, &req);
        printf("  [info] buffer req size=%llu align=%llu typeBits=%#x\n",
               (unsigned long long)req.size, (unsigned long long)req.alignment,
               req.memoryTypeBits);

        memset(&mem, 0, sizeof(mem));
        pMem(phys, &mem);
        for (t = 0; t < mem.memoryTypeCount; t++)
        {
            uint32_t flags = mem.memoryTypes[t].propertyFlags;
            if ((req.memoryTypeBits & (1u << t)) &&
                (flags & ROK_VK_MEMORY_PROPERTY_HOST_VISIBLE) &&
                (flags & ROK_VK_MEMORY_PROPERTY_HOST_COHERENT))
            {
                typeIdx = (int)t;
                break;
            }
        }
        if (typeIdx < 0)
        {
            printf("  [FAIL] 没有 HOST_VISIBLE|HOST_COHERENT 内存类型\n");
            goto fail;
        }

        memset(&mai, 0, sizeof(mai));
        mai.sType = ROK_VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = (uint32_t)typeIdx;
        ROK_CHECK("vkAllocateMemory", pAlloc(device, &mai, NULL, &memory));
        ROK_CHECK("vkBindBufferMemory", pBind(device, buffer, memory, 0));

        memset(&req, 0, sizeof(req));
        ROK_CHECK("vkMapMemory", pMap(device, memory, 0, sizeof(uint32_t) * ROK_SMOKE_ELEMENTS, 0, &mapped));
        for (i = 0; i < ROK_SMOKE_ELEMENTS; i++)
            ((uint32_t *)mapped)[i] = i;     // 初始数据：0..7
        printf("  [ok]   SSBO 初始数据 [0..%u]\n", ROK_SMOKE_ELEMENTS - 1u);
    }

    // ---- [5] 描述符 set（binding=0, SSBO）----
    {
        RokVkDescriptorSetLayoutBinding bindings[1];
        RokVkDescriptorSetLayoutCreateInfo lci;
        RokVkDescriptorPoolSize poolSize;
        RokVkDescriptorPoolCreateInfo pci;
        RokVkDescriptorSetAllocateInfo sai;
        RokVkDescriptorBufferInfo bufInfo;
        RokVkWriteDescriptorSet write;
        PvkCreateDescriptorSetLayout pCreateLayout =
            (PvkCreateDescriptorSetLayout)vkGetDeviceProcAddr(device, "vkCreateDescriptorSetLayout");
        PvkCreateDescriptorPool pCreatePool =
            (PvkCreateDescriptorPool)vkGetDeviceProcAddr(device, "vkCreateDescriptorPool");
        PvkAllocateDescriptorSets pAllocSet =
            (PvkAllocateDescriptorSets)vkGetDeviceProcAddr(device, "vkAllocateDescriptorSets");
        PvkUpdateDescriptorSets pUpdate =
            (PvkUpdateDescriptorSets)vkGetDeviceProcAddr(device, "vkUpdateDescriptorSets");

        memset(bindings, 0, sizeof(bindings));
        bindings[0].binding = 0;
        bindings[0].descriptorType = ROK_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = ROK_VK_SHADER_STAGE_COMPUTE_BIT;

        memset(&lci, 0, sizeof(lci));
        lci.sType = ROK_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        lci.bindingCount = 1;
        lci.pBindings = bindings;
        ROK_CHECK("vkCreateDescriptorSetLayout", pCreateLayout(device, &lci, NULL, &setLayout));

        memset(&poolSize, 0, sizeof(poolSize));
        poolSize.type = ROK_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 1;

        memset(&pci, 0, sizeof(pci));
        pci.sType = ROK_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pci.maxSets = 1;
        pci.poolSizeCount = 1;
        pci.pPoolSizes = &poolSize;
        ROK_CHECK("vkCreateDescriptorPool", pCreatePool(device, &pci, NULL, &descPool));

        memset(&sai, 0, sizeof(sai));
        sai.sType = ROK_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        sai.descriptorPool = descPool;
        sai.descriptorSetCount = 1;
        sai.pSetLayouts = &setLayout;
        ROK_CHECK("vkAllocateDescriptorSets", pAllocSet(device, &sai, &descSet));

        memset(&bufInfo, 0, sizeof(bufInfo));
        bufInfo.buffer = buffer;
        bufInfo.offset = 0;
        bufInfo.range = sizeof(uint32_t) * ROK_SMOKE_ELEMENTS;

        memset(&write, 0, sizeof(write));
        write.sType = ROK_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = ROK_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufInfo;
        pUpdate(device, 1, &write, 0, NULL);
        printf("  [ok]   描述符绑定 SSBO\n");
    }

    // ---- [6] 计算 pipeline（内置 SPIR-V）----
    {
        RokVkShaderModuleCreateInfo smci;
        RokVkPipelineLayoutCreateInfo plci;
        RokVkComputePipelineCreateInfo cpci;
        PvkCreateShaderModule pCreateModule =
            (PvkCreateShaderModule)vkGetDeviceProcAddr(device, "vkCreateShaderModule");
        PvkCreatePipelineLayout pCreateLayout2 =
            (PvkCreatePipelineLayout)vkGetDeviceProcAddr(device, "vkCreatePipelineLayout");
        PvkCreateComputePipelines pCreatePipeline =
            (PvkCreateComputePipelines)vkGetDeviceProcAddr(device, "vkCreateComputePipelines");

        memset(&smci, 0, sizeof(smci));
        smci.sType = ROK_VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = sizeof(uint32_t) * ROK_SPIRV_NUM_WORDS;
        smci.pCode = RokSpirvCompound;
        ROK_CHECK("vkCreateShaderModule", pCreateModule(device, &smci, NULL, &shaderModule));

        memset(&plci, 0, sizeof(plci));
        plci.sType = ROK_VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &setLayout;
        ROK_CHECK("vkCreatePipelineLayout", pCreateLayout2(device, &plci, NULL, &pipelineLayout));

        memset(&cpci, 0, sizeof(cpci));
        cpci.sType = ROK_VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType = ROK_VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; // =18
        cpci.stage.stage = ROK_VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = shaderModule;
        cpci.stage.pName = "main";
        cpci.layout = pipelineLayout;
        ROK_CHECK("vkCreateComputePipelines", pCreatePipeline(device, NULL, 1, &cpci, NULL, &pipeline));
        printf("  [ok]   计算 pipeline（SPIR-V %u words）\n", ROK_SPIRV_NUM_WORDS);
    }

    // ---- [7] 命令缓冲 + 提交 + 等待 ----
    {
        RokVkCommandPoolCreateInfo cpi;
        RokVkCommandBufferAllocateInfo cbi;
        RokVkCommandBufferBeginInfo bi;
        RokVkSubmitInfo si;
        PvkCreateCommandPool pCreatePool2 =
            (PvkCreateCommandPool)vkGetDeviceProcAddr(device, "vkCreateCommandPool");
        PvkAllocateCommandBuffers pAllocCb =
            (PvkAllocateCommandBuffers)vkGetDeviceProcAddr(device, "vkAllocateCommandBuffers");
        PvkBeginCommandBuffer pBegin =
            (PvkBeginCommandBuffer)vkGetDeviceProcAddr(device, "vkBeginCommandBuffer");
        PvkEndCommandBuffer pEnd =
            (PvkEndCommandBuffer)vkGetDeviceProcAddr(device, "vkEndCommandBuffer");
        PvkCmdBindPipeline pBindPipe =
            (PvkCmdBindPipeline)vkGetDeviceProcAddr(device, "vkCmdBindPipeline");
        PvkCmdBindDescriptorSets pBindSets =
            (PvkCmdBindDescriptorSets)vkGetDeviceProcAddr(device, "vkCmdBindDescriptorSets");
        PvkCmdDispatch pDispatch =
            (PvkCmdDispatch)vkGetDeviceProcAddr(device, "vkCmdDispatch");
        PvkQueueSubmit pSubmit =
            (PvkQueueSubmit)vkGetDeviceProcAddr(device, "vkQueueSubmit");
        PvkQueueWaitIdle pWait =
            (PvkQueueWaitIdle)vkGetDeviceProcAddr(device, "vkQueueWaitIdle");

        memset(&cpi, 0, sizeof(cpi));
        cpi.sType = ROK_VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpi.queueFamilyIndex = computeFamily;
        ROK_CHECK("vkCreateCommandPool", pCreatePool2(device, &cpi, NULL, &commandPool));

        memset(&cbi, 0, sizeof(cbi));
        cbi.sType = ROK_VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbi.commandPool = commandPool;
        cbi.level = ROK_VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbi.commandBufferCount = 1;
        ROK_CHECK("vkAllocateCommandBuffers", pAllocCb(device, &cbi, &commandBuffer));

        memset(&bi, 0, sizeof(bi));
        bi.sType = ROK_VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;  // =42
        bi.flags = ROK_VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT;
        ROK_CHECK("vkBeginCommandBuffer", pBegin(commandBuffer, &bi));

        pBindPipe(commandBuffer, ROK_VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        pBindSets(commandBuffer, ROK_VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
                  0, 1, &descSet, 0, NULL);
        pDispatch(commandBuffer, ROK_SMOKE_ELEMENTS, 1, 1);
        ROK_CHECK("vkEndCommandBuffer", pEnd(commandBuffer));

        memset(&si, 0, sizeof(si));
        si.sType = ROK_VK_STRUCTURE_TYPE_SUBMIT_INFO;               // =4
        si.commandBufferCount = 1;
        si.pCommandBuffers = &commandBuffer;
        ROK_CHECK("vkQueueSubmit", pSubmit(queue, 1, &si, NULL));
        ROK_CHECK("vkQueueWaitIdle", pWait(queue));
        printf("  [ok]   提交 + 等待完成（KMD 提交链路已打通）\n");
    }

    // ---- [8] 结果校验 ----
    {
        PvkUnmapMemory pUnmap =
            (PvkUnmapMemory)vkGetDeviceProcAddr(device, "vkUnmapMemory");
        uint32_t bad = 0;

        memcpy(verify, mapped, sizeof(verify));
        pUnmap(device, memory);
        mapped = NULL;

        for (i = 0; i < ROK_SMOKE_ELEMENTS; i++)
        {
            uint32_t expect = i * 2u + 1u;
            if (verify[i] != expect)
            {
                printf("  [FAIL] data[%u] = %u, 期望 %u\n", i, verify[i], expect);
                bad++;
            }
        }
        if (bad != 0) goto fail;
        printf("  [ok]   结果校验：data[x] = x*2+1 全部正确\n");
    }

    printf("\n  ===== PASS：Vulkan compute 全链路（loader/ICD/驱动/KMD）=====\n");
    ret = 0;
    goto cleanup;

fail:
    ret = 1;

cleanup:
    if (mapped != NULL && device != NULL)
    {
        PvkUnmapMemory pUnmap = (PvkUnmapMemory)vkGetDeviceProcAddr(device, "vkUnmapMemory");
        if (pUnmap != NULL) pUnmap(device, memory);
    }
    if (queueFamilyIndices != NULL)
        HeapFree(GetProcessHeap(), 0, queueFamilyIndices);
    if (queueProps != NULL)
        HeapFree(GetProcessHeap(), 0, queueProps);
    if (devices != NULL)
        HeapFree(GetProcessHeap(), 0, devices);
    printf("\nvk_smoke 结果: %s\n", ret == 0 ? "PASS" : "FAIL");
    return ret;
}

int main(int argc, char **argv)
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);
    printf("vk_smoke — radeon-windows Vulkan 1.0 端到端冒烟测试\n");
    printf("目标：loader -> ICD -> 驱动 -> KMD 全链路跑通一次 compute\n\n");
    return RunSmoke();
}
