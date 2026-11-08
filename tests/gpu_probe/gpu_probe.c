// SPDX-License-Identifier: MIT
// gpu_probe — Windows 图形栈探测工具（radeon-windows 项目 M0 交付物）
//
// 用途：在安装 terakan / 自研 rokmd 前后，弄清"系统看到了什么"：
//   [1] PCI 显示适配器（Vendor 0x1002 = AMD，含设备 ID 与驱动版本）
//   [2] WDDM 适配器（D3DKMT：LUID + 用户态驱动 UMD 文件名）
//   [3] Vulkan（Loader 是否可用、物理设备、apiVersion / driverVersion）
//
// 说明：
//   - llvm-mingw 无 d3dkmthk.h，D3DKMT 的 ABI（gdi32.dll 导出，Win8+ 稳定）
//     在此手工声明，与 WDK d3dkmthk.h 布局一致。
//   - Vulkan 通过 LoadLibrary("vulkan-1.dll") 动态加载，只读前缀字段，
//     不需要 vulkan.h。
//   - 控制台输出用英文标签：避免 Windows 控制台代码页对 UTF-8 中文乱码。
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <initguid.h>   // 必须在 devguid.h 之前：让 DEFINE_GUID 产生定义
#include <setupapi.h>
#include <devguid.h>

// ---------------------------------------------------------------------------
// D3DKMT 最小 ABI（与 WDK d3dkmthk.h 对齐，gdi32.dll 导出）
// ---------------------------------------------------------------------------
typedef struct _LUID_CUSTOM { ULONG LowPart; LONG HighPart; } ROK_LUID;

typedef struct _ROK_D3DKMT_ADAPTERINFO {
    ULONG   hAdapter;
    ROK_LUID AdapterLuid;
    ULONG   NumOfSources;                    // 视频源数
    BOOL    bPrecisePresentRegionsPreferred; // （与 WDK 26100 对齐）
} ROK_D3DKMT_ADAPTERINFO;

typedef struct _ROK_D3DKMT_ENUMADAPTERS2 {
    ULONG NumAdapters;
    ROK_D3DKMT_ADAPTERINFO *pAdapters;
} ROK_D3DKMT_ENUMADAPTERS2;

// KMTUMDVERSION（d3dkmthk.h 26100）
typedef enum _ROK_KMTUMDVERSION {
    ROK_KMTUMDVERSION_DX9 = 0,
    ROK_KMTUMDVERSION_DX10,
    ROK_KMTUMDVERSION_DX11,
    ROK_KMTUMDVERSION_DX12,
    ROK_KMTUMDVERSION_DX12_WSA32,
    ROK_KMTUMDVERSION_DX12_WSA64,
} ROK_KMTUMDVERSION;

#define ROK_MAX_PATH 260
typedef struct _ROK_D3DKMT_UMDFILENAMEINFO {
    ROK_KMTUMDVERSION Version;                  // In: UMD 版本族
    WCHAR             UmdFileName[ROK_MAX_PATH]; // Out: UMD 文件名
} ROK_D3DKMT_UMDFILENAMEINFO;

typedef struct _ROK_D3DKMT_QUERYADAPTERINFO {
    ULONG hAdapter;
    ULONG Type;                 // KMTQUERYADAPTERINFOTYPE
    VOID *pPrivateDriverData;
    UINT  PrivateDriverDataSize;
} ROK_D3DKMT_QUERYADAPTERINFO;

#define ROK_KMTQAITYPE_UMDRIVERNAME 1   // KMTQAITYPE_UMDRIVERNAME（26100 枚举值）
#define ROK_KMTQAITYPE_UMD_DRIVER_VERSION 2    // out: 8 字节打包版本 a.b.c.d
#define ROK_KMTQAITYPE_ADAPTERREGISTRYINFO 3   // out: DriverName + PnpString

typedef struct _ROK_D3DKMT_UMDDRIVERVERSION {
    DWORDLONG DriverVersion;         // out: a.b.c.d 打包（与 SetupAPI DriverVersion 同源）
} ROK_D3DKMT_UMDDRIVERVERSION;

typedef struct _ROK_D3DKMT_ADAPTERREGISTRYINFO {
    WCHAR DriverName[ROK_MAX_PATH];  // out: 驱动注册表名
    WCHAR PnpString[ROK_MAX_PATH];   // out: 如 PCI\VEN_1002&DEV_6760 —— 适配器↔设备对照
} ROK_D3DKMT_ADAPTERREGISTRYINFO;

typedef ULONG ROK_D3DKMT_HANDLE;
typedef NTSTATUS (WINAPI *PFN_D3DKMTEnumAdapters2)(ROK_D3DKMT_ENUMADAPTERS2 *);
typedef NTSTATUS (WINAPI *PFN_D3DKMTQueryAdapterInfo)(ROK_D3DKMT_QUERYADAPTERINFO *);

// ---- D3DKMT 设备/Escape ABI（rokmd 通道探测用，布局对 26100 核对）----
typedef struct _ROK_D3DKMT_CREATEDEVICE {
    ULONG hAdapter;         // in: 适配器句柄（用户态创建路径）
    ULONG Flags;            // in: D3DKMT_CREATEDEVICEFLAGS（全零）
    ULONG hDevice;          // out: 设备句柄
} ROK_D3DKMT_CREATEDEVICE;

typedef struct _ROK_D3DKMT_DESTROYDEVICE {
    ULONG hDevice;          // in: 设备句柄
} ROK_D3DKMT_DESTROYDEVICE;

typedef struct _ROK_D3DKMT_ESCAPE {
    ULONG hAdapter;         // in: 适配器句柄
    ULONG hDevice;          // in: 设备句柄（0 = 适配器级 escape）
    ULONG Type;             // in: D3DKMT_ESCAPETYPE（D3DKMT_ESCAPE_DRIVERPRIVATE = 0）
    ULONG Flags;            // in: D3DDDI_ESCAPEFLAGS 位域；bit0 = HardwareAccess
    VOID *pPrivateDriverData;      // in/out: 私有数据缓冲
    UINT  PrivateDriverDataSize;   // in: 缓冲大小
    ULONG hContext;         // in: 上下文句柄（可选，0）
} ROK_D3DKMT_ESCAPE;

#define ROK_D3DKMT_ESCAPE_DRIVERPRIVATE 0
#define ROK_D3DDDI_ESCAPEFLAG_HARDWAREACCESS 0x00000001UL

typedef NTSTATUS (WINAPI *PFN_D3DKMTCreateDevice)(ROK_D3DKMT_CREATEDEVICE *);
typedef NTSTATUS (WINAPI *PFN_D3DKMTDestroyDevice)(ROK_D3DKMT_DESTROYDEVICE *);
typedef NTSTATUS (WINAPI *PFN_D3DKMTEscape)(ROK_D3DKMT_ESCAPE *);

// ---------------------------------------------------------------------------
// Vulkan 最小 ABI（动态加载 vulkan-1.dll，无需 vulkan.h）
// ---------------------------------------------------------------------------
#define ROK_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define ROK_VK_MAX_PHYSICAL_DEVICE_NAME_SIZE 256
#define ROK_VK_UUID_SIZE 16

typedef void *ROK_VkInstance;
typedef void *ROK_VkPhysicalDevice;

typedef struct _ROK_VkInstanceCreateInfo {
    unsigned int sType;             // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
    const void  *pNext;
    unsigned int flags;
    const void  *pApplicationInfo;
    unsigned int enabledLayerCount;
    const char * const *ppEnabledLayerNames;
    unsigned int enabledExtensionCount;
    const char * const *ppEnabledExtensionNames;
} ROK_VkInstanceCreateInfo;

typedef struct _ROK_VkPhysicalDevicePropsPrefix {
    unsigned int apiVersion;
    unsigned int driverVersion;
    unsigned int vendorID;
    unsigned int deviceID;
    int          deviceType;
    char         deviceName[ROK_VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    unsigned char pipelineCacheUUID[ROK_VK_UUID_SIZE];
    // 之后是 VkPhysicalDeviceLimits / sparseProperties —— 用大缓冲区承载，
    // 只读前缀字段。
} ROK_VkPhysicalDevicePropsPrefix;

typedef int (*PFN_rok_vkCreateInstance)(const ROK_VkInstanceCreateInfo *,
                                        const void *, ROK_VkInstance *);
typedef int (*PFN_rok_vkEnumerateInstanceExtensionProperties)(
    const char *, unsigned int *, void *);
typedef int (*PFN_rok_vkEnumeratePhysicalDevices)(ROK_VkInstance,
                                                  unsigned int *, ROK_VkPhysicalDevice *);
typedef void (*PFN_rok_vkGetPhysicalDeviceProperties)(ROK_VkPhysicalDevice, void *);

typedef struct _ROK_VkExtensionProperties {
    char         extensionName[256];
    unsigned int specVersion;
} RokVkExtensionProperties;

typedef struct _ROK_VkExtent3D { unsigned int width, height, depth; } RokVkExtent3D;

typedef struct _ROK_VkQueueFamilyProperties {
    unsigned int  queueFlags;
    unsigned int  queueCount;
    unsigned int  timestampValidBits;
    RokVkExtent3D minImageTransferGranularity;
} RokVkQueueFamilyProperties;

typedef struct _ROK_VkMemoryType {
    unsigned int propertyFlags;
    unsigned int heapIndex;
} RokVkMemoryType;

typedef struct _ROK_VkMemoryHeap {
    unsigned long long size;
    unsigned int       flags;
} RokVkMemoryHeap;

typedef struct _ROK_VkPhysicalDeviceMemoryProperties {
    unsigned int    memoryTypeCount;
    RokVkMemoryType memoryTypes[32];
    unsigned int    memoryHeapCount;
    RokVkMemoryHeap memoryHeaps[16];
} RokVkPhysicalDeviceMemoryProperties;

typedef int (*PFN_rok_vkEnumerateDeviceExtensionProperties)(
    ROK_VkPhysicalDevice, const char *, unsigned int *, void *);
typedef void (*PFN_rok_vkGetPhysicalDeviceQueueFamilyProperties)(
    ROK_VkPhysicalDevice, unsigned int *, RokVkQueueFamilyProperties *);
typedef void (*PFN_rok_vkGetPhysicalDeviceMemoryProperties)(
    ROK_VkPhysicalDevice, RokVkPhysicalDeviceMemoryProperties *);

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------
static const char *RokVkDeviceType(int t)
{
    switch (t)
    {
    case 0: return "other";
    case 1: return "integrated";
    case 2: return "discrete";
    case 3: return "virtual";
    case 4: return "cpu";
    default: return "unknown";
    }
}

static void RokPrintGuidSectionHeader(const char *title)
{
    printf("\n================================================================\n");
    printf(" %s\n", title);
    printf("================================================================\n");
}

// ---------------------------------------------------------------------------
// [1] PCI 显示设备（SetupAPI，SPDRP）
// ---------------------------------------------------------------------------
static void RokProbePciDisplayDevices(void)
{
    HDEVINFO devInfo;
    SP_DEVINFO_DATA devData;
    DWORD i;
    WCHAR descW[512];
    WCHAR hwidsW[1024];
    WCHAR driverKeyW[512];

    RokPrintGuidSectionHeader("[1] PCI display adapters (SetupAPI, class DISPLAY)");

    devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, NULL, NULL,
                                   DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE)
    {
        printf("  SetupDiGetClassDevs failed, GLE=%lu\n", GetLastError());
        return;
    }

    devData.cbSize = sizeof(devData);
    for (i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); i++)
    {
        DWORD type = 0, needed = 0;

        printf("  ---- device #%lu ----\n", i);

        if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_DEVICEDESC,
                                              &type, (PBYTE)descW, sizeof(descW), &needed))
            printf("  description : %ls\n", descW);

        if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_HARDWAREID,
                                              &type, (PBYTE)hwidsW, sizeof(hwidsW), &needed))
        {
            // 多串 REG_MULTI_SZ：第一串即 PCI\VEN_xxxx&DEV_xxxx
            printf("  hardware id : %ls\n", hwidsW);
            if (wcsstr(hwidsW, L"VEN_1002") != NULL)
                printf("  vendor      : AMD (0x1002) — TeraScale/GCN 目标设备\n");
        }

        // 驱动注册键 → DriverVersion / DriverDate
        if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_DRIVER,
                                              &type, (PBYTE)driverKeyW, sizeof(driverKeyW), &needed))
        {
            WCHAR pathW[1024];
            HKEY hKey;
            wsprintfW(pathW, L"SYSTEM\\CurrentControlSet\\Control\\Class\\%ls", driverKeyW);
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, pathW, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                WCHAR verW[128] = L"";
                WCHAR dateW[128] = L"";
                WCHAR provW[128] = L"";
                DWORD sz = sizeof(verW);
                DWORD vtype = 0;
                if (RegQueryValueExW(hKey, L"DriverVersion", NULL, &vtype,
                                     (LPBYTE)verW, &sz) == ERROR_SUCCESS && sz)
                    printf("  driver ver  : %ls\n", verW);
                sz = sizeof(dateW);
                if (RegQueryValueExW(hKey, L"DriverDate", NULL, &vtype,
                                     (LPBYTE)dateW, &sz) == ERROR_SUCCESS && sz)
                    printf("  driver date : %ls\n", dateW);
                sz = sizeof(provW);
                if (RegQueryValueExW(hKey, L"ProviderName", NULL, &vtype,
                                     (LPBYTE)provW, &sz) == ERROR_SUCCESS && sz)
                    printf("  provider    : %ls\n", provW);
                RegCloseKey(hKey);
            }
        }
    }

    if (GetLastError() != ERROR_NO_MORE_ITEMS)
        printf("  SetupDiEnumDeviceInfo stopped early, GLE=%lu\n", GetLastError());

    SetupDiDestroyDeviceInfoList(devInfo);
}

// ---------------------------------------------------------------------------
// [1.5] Hypervisor 探测（CPUID leaf 1 ECX bit31 + leaf 0x40000000 厂商串）
//       VM 环境装 rokmd 后 KMD 会自动进入模拟模式（kmd/src/sim.c）。
// ---------------------------------------------------------------------------
#if defined(_MSC_VER)
#include <intrin.h>
static void RokCpuid(int leaf, int regs[4]) { __cpuid(regs, leaf); }
#else
static void RokCpuid(int leaf, int regs[4])
{
    __asm__ __volatile__ ("cpuid"
        : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
        : "a"(leaf), "c"(0));
}
#endif

static void RokProbeHypervisor(void)
{
    static const char *sigs[] = {
        "KVMKVMKVM\0\0\0", "Microsoft Hv", "VMwareVMware",
        "VBoxVBoxVBox", "XenVMMXenVMM", "TCGTCGTCGTCG", "prl hyperv",
    };
    int regs[4];
    char vendor[13];
    unsigned int i, j, bit31;
    const char *found = NULL;

    printf("\n================================================================\n");
    printf(" [1.5] Hypervisor detection (CPUID)\n");
    printf("================================================================\n");

    RokCpuid(1, regs);
    bit31 = (unsigned int)((regs[2] >> 31) & 1u);
    printf("  leaf1.ECX.bit31 = %u%s\n", bit31,
           bit31 ? "  (hypervisor present)" : "  (bare metal)");
    if (!bit31)
        return;

    RokCpuid(0x40000000, regs);
    memcpy(vendor + 0, &regs[1], 4);
    memcpy(vendor + 4, &regs[2], 4);
    memcpy(vendor + 8, &regs[3], 4);
    vendor[12] = '\0';
    printf("  hypervisor vendor: \"%s\"\n", vendor);

    for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]) && found == NULL; i++)
    {
        for (j = 0; j < 12; j++)
            if (vendor[j] != sigs[i][j]) break;
        if (j == 12)
            found = sigs[i];
    }
    printf("  matched: %s\n", found ? found : "(unknown hypervisor)");
}

// ---------------------------------------------------------------------------
// [2] WDDM 适配器（D3DKMT）
// ---------------------------------------------------------------------------
static void RokProbeD3DKMTAdapters(void)
{
    HMODULE gdi32;
    PFN_D3DKMTEnumAdapters2 pEnumAdapters2;
    PFN_D3DKMTQueryAdapterInfo pQueryAdapterInfo;
    ROK_D3DKMT_ENUMADAPTERS2 enumAdapters;
    ULONG i;
    NTSTATUS st;

    RokPrintGuidSectionHeader("[2] WDDM adapters (D3DKMT via gdi32.dll)");

    gdi32 = GetModuleHandleW(L"gdi32.dll");
    if (gdi32 == NULL)
        gdi32 = LoadLibraryW(L"gdi32.dll");
    if (gdi32 == NULL)
    {
        printf("  gdi32.dll not loadable\n");
        return;
    }

    pEnumAdapters2 = (PFN_D3DKMTEnumAdapters2)(void *)GetProcAddress(gdi32, "D3DKMTEnumAdapters2");
    pQueryAdapterInfo = (PFN_D3DKMTQueryAdapterInfo)(void *)GetProcAddress(gdi32, "D3DKMTQueryAdapterInfo");
    if (pEnumAdapters2 == NULL || pQueryAdapterInfo == NULL)
    {
        printf("  D3DKMT exports missing (need Windows 8+ / WDDM 1.3+)\n");
        return;
    }

    // 两段式枚举（d3dkmthk.h 契约）：第一次 pAdapters=NULL 取数量，
    // 分配数组后第二次取数据。旧版只调一次即解引用 NULL 数组（已修）。
    ZeroMemory(&enumAdapters, sizeof(enumAdapters));
    st = pEnumAdapters2(&enumAdapters);
    if (st != 0)
    {
        printf("  D3DKMTEnumAdapters2 (count) failed, status=0x%08lX\n", (unsigned long)st);
        return;
    }
    if (enumAdapters.NumAdapters == 0)
    {
        printf("  adapter count: 0\n");
        return;
    }

    enumAdapters.pAdapters = (ROK_D3DKMT_ADAPTERINFO *)
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                  sizeof(ROK_D3DKMT_ADAPTERINFO) * enumAdapters.NumAdapters);
    if (enumAdapters.pAdapters == NULL)
    {
        printf("  out of memory for %lu adapters\n", enumAdapters.NumAdapters);
        return;
    }
    st = pEnumAdapters2(&enumAdapters);
    if (st != 0)
    {
        printf("  D3DKMTEnumAdapters2 (data) failed, status=0x%08lX\n", (unsigned long)st);
        HeapFree(GetProcessHeap(), 0, enumAdapters.pAdapters);
        return;
    }

    printf("  adapter count: %lu\n", enumAdapters.NumAdapters);
    for (i = 0; i < enumAdapters.NumAdapters; i++)
    {
        ROK_D3DKMT_QUERYADAPTERINFO qi;
        ROK_D3DKMT_UMDFILENAMEINFO umd;
        ROK_D3DKMT_UMDDRIVERVERSION umdver;
        ROK_D3DKMT_ADAPTERREGISTRYINFO reginfo;
        /* 逐版本族查询：传统驱动（如 TeraScale 的 aticfx）只有 DX9/DX10/DX11 UMD，
         * 没有 DX12 —— 硬编码单个族会在真实硬件上 0xC000000D（HD 7470M 实测教训） */
        static const struct { ROK_KMTUMDVERSION v; const char *name; } fams[] = {
            { ROK_KMTUMDVERSION_DX9,        "DX9"        },
            { ROK_KMTUMDVERSION_DX10,       "DX10"       },
            { ROK_KMTUMDVERSION_DX11,       "DX11"       },
            { ROK_KMTUMDVERSION_DX12,       "DX12"       },
            { ROK_KMTUMDVERSION_DX12_WSA32, "DX12-WSA32" },
            { ROK_KMTUMDVERSION_DX12_WSA64, "DX12-WSA64" },
        };
        ULONG f, okFamilies = 0, hi, lo;

        printf("  ---- adapter #%lu ----\n", i);
        printf("  handle      : 0x%08lX\n", (unsigned long)enumAdapters.pAdapters[i].hAdapter);
        printf("  luid        : %08lX-%08lX\n",
               (unsigned long)enumAdapters.pAdapters[i].AdapterLuid.HighPart,
               (unsigned long)enumAdapters.pAdapters[i].AdapterLuid.LowPart);
        printf("  sources     : %lu\n", enumAdapters.pAdapters[i].NumOfSources);

        /* 注册表信息：PnpString 直接给出 PCI ID，可与 [1] 段设备一一对上 */
        ZeroMemory(&reginfo, sizeof(reginfo));
        ZeroMemory(&qi, sizeof(qi));
        qi.hAdapter = enumAdapters.pAdapters[i].hAdapter;
        qi.Type = ROK_KMTQAITYPE_ADAPTERREGISTRYINFO;
        qi.pPrivateDriverData = &reginfo;
        qi.PrivateDriverDataSize = sizeof(reginfo);
        if (pQueryAdapterInfo(&qi) == 0)
            printf("  registry    : driver=%ls  pnp=%ls\n",
                   reginfo.DriverName, reginfo.PnpString);

        for (f = 0; f < sizeof(fams) / sizeof(fams[0]); f++)
        {
            ZeroMemory(&umd, sizeof(umd));
            umd.Version = fams[f].v;
            ZeroMemory(&qi, sizeof(qi));
            qi.hAdapter = enumAdapters.pAdapters[i].hAdapter;
            qi.Type = ROK_KMTQAITYPE_UMDRIVERNAME;
            qi.pPrivateDriverData = &umd;
            qi.PrivateDriverDataSize = sizeof(umd);
            st = pQueryAdapterInfo(&qi);
            if (st == 0)
            {
                printf("  UMD dll     : %ls  (family: %s)\n",
                       umd.UmdFileName, fams[f].name);
                okFamilies++;
            }
        }
        if (okFamilies > 0)
            printf("                ^^^ terakan/rokmd 集成后此处应出现对应 DLL\n");
        else
            printf("  UMD query   : failed for all families (last status=0x%08lX)\n",
                   (unsigned long)st);

        /* UMD 驱动版本号（预期与 [1] 段 SetupAPI 的 DriverVersion 一致） */
        ZeroMemory(&umdver, sizeof(umdver));
        ZeroMemory(&qi, sizeof(qi));
        qi.hAdapter = enumAdapters.pAdapters[i].hAdapter;
        qi.Type = ROK_KMTQAITYPE_UMD_DRIVER_VERSION;
        qi.pPrivateDriverData = &umdver;
        qi.PrivateDriverDataSize = sizeof(umdver);
        if (pQueryAdapterInfo(&qi) == 0)
        {
            hi = (ULONG)(umdver.DriverVersion >> 32);
            lo = (ULONG)(umdver.DriverVersion & 0xFFFFFFFFULL);
            printf("  UMD version : %lu.%lu.%lu.%lu\n",
                   (unsigned long)(hi >> 16), (unsigned long)(hi & 0xFFFF),
                   (unsigned long)(lo >> 16), (unsigned long)(lo & 0xFFFF));
        }
    }

    HeapFree(GetProcessHeap(), 0, enumAdapters.pAdapters);
}

// ---------------------------------------------------------------------------
// [2.5] Vulkan ICD 清单（loader 发现机制：注册表 Khronos\Vulkan\Drivers）
// ---------------------------------------------------------------------------
static void RokProbeVulkanIcdRegistry(void)
{
    static const struct { HKEY root; const wchar_t *label; } roots[] = {
        { HKEY_LOCAL_MACHINE, L"HKLM" },
        { HKEY_CURRENT_USER,  L"HKCU" },
    };
    DWORD r;

    printf("\n================================================================\n");
    printf(" [2.5] Vulkan ICD manifests (registry Khronos\\Vulkan\\Drivers)\n");
    printf("================================================================\n");

    for (r = 0; r < 2; r++)
    {
        HKEY hKey;
        DWORD index = 0;
        wchar_t name[512];
        DWORD nameLen = 512;
        BYTE data[64];
        DWORD dataSize = sizeof(data);
        DWORD type = 0;

        if (RegOpenKeyExW(roots[r].root, L"SOFTWARE\\Khronos\\Vulkan\\Drivers",
                          0, KEY_READ, &hKey) != ERROR_SUCCESS)
        {
            printf("  %ls: (no key)\n", roots[r].label);
            continue;
        }

        printf("  %ls:\\SOFTWARE\\Khronos\\Vulkan\\Drivers:\n", roots[r].label);
        while (RegEnumValueW(hKey, index++, name, &nameLen, NULL,
                             &type, data, &dataSize) == ERROR_SUCCESS)
        {
            printf("    %ls\n", name);
            nameLen = 512;
            dataSize = sizeof(data);
        }
        if (index == 1)
            printf("    (empty)\n");
        RegCloseKey(hKey);
    }

    {
        char buf[1024];
        /* GetEnvironmentVariableA 第三参为 DWORD 值（非指针）；
         * 优先 VK_ICD_FILENAMES，回退 VK_DRIVER_FILES（新版 loader 名）。 */
        DWORD n = GetEnvironmentVariableA("VK_ICD_FILENAMES", buf,
                                          (DWORD)sizeof(buf));
        if (n == 0)
            n = GetEnvironmentVariableA("VK_DRIVER_FILES", buf,
                                        (DWORD)sizeof(buf));
        if (n > 0 && n < (DWORD)sizeof(buf))
            printf("  env override: %s\n", buf);
    }
}

// ---------------------------------------------------------------------------
// [3] Vulkan（动态加载）
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ABI 守卫：构建系统加了 -I../../kmd/src（共享契约头）即启用 [4] 探测。
// __has_include 在 MSVC 19.0+ / GCC 5+ / clang 全部可用；无头文件时整段
// 编译排除，保持老构建路径兼容。
// ---------------------------------------------------------------------------
#if !defined(ROK_PROBE_HAS_ESCAPE_ABI)
#  if defined(__has_include)
#    if __has_include("rok_escape.h")
#      define ROK_PROBE_HAS_ESCAPE_ABI 1
#    endif
#  endif
#endif
#if !defined(ROK_PROBE_HAS_ESCAPE_ABI)
#define ROK_PROBE_HAS_ESCAPE_ABI 0
#endif

// --cp-reset：显式触发一次 CP 域软复位（escape 类型 11，写硬件）。
// 默认关：[4] 段保持纯只读观测。定义在 ABI 守卫之外（main 解析参数，
// [4.10] 仅在 ABI 可用时消费）。
static int g_opt_cp_reset = 0;

// ---------------------------------------------------------------------------
// [4] rokmd Escape 通道（D3DKMTEscape → DxgkDdiEscape）
//     直接编译共享契约头 kmd/src/rok_escape.h（ROK_PROBE_HAS_ESCAPE_ABI）。
//     在装了 rokmd 的实机上，本段验证 M4 ICD 将使用的同一条 ABI 通路；
//     对非 rokmd 适配器，KMD 会因魔数不符拒绝 —— 属预期结果。
// ---------------------------------------------------------------------------
#if ROK_PROBE_HAS_ESCAPE_ABI

#include "rok_escape.h"    /* 共享契约头（KMD/ICD/gpu_probe 同源 ABI） */

typedef union _ROK_ESC_OUT_BUF {
    ROK_ESC_VERSION_OUT       version;
    ROK_ESC_PCI_ID_OUT        pci;
    ROK_ESC_VRAM_OUT          vram;
    ROK_ESC_CAPS_OUT          caps;
    ROK_ESC_SEGMENT_INFO_OUT  seg;
    ROK_ESC_SIM_STATUS_OUT    sim;
    ROK_ESC_CCC_SETTINGS      ccc;
    ROK_ESC_ENGINE_STATUS_OUT engine;
    ROK_ESC_HW_INFO_OUT       hw;
    ROK_ESC_HW_RESET_OUT      rst;
    unsigned char             raw[sizeof(ROK_ESCAPE_HEADER) + 296];  // 最大 = SIM_STATUS(296)
} ROK_ESC_OUT_BUF;

static const char *RokEscFamilyName(unsigned int family)
{
    switch (family)
    {
    case ROK_GPU_FAMILY_TS1: return "TeraScale1";
    case ROK_GPU_FAMILY_TS2: return "TeraScale2";
    case ROK_GPU_FAMILY_TS3: return "TeraScale3";
    default: return "unknown";
    }
}

static void
RokEscSend(PFN_D3DKMTEscape pEscape, ULONG hAdapter, ULONG hDevice,
           ROK_U32 escType, ROK_ESC_OUT_BUF *buf, unsigned int outSize,
           NTSTATUS *status)
{
    ROK_D3DKMT_ESCAPE esc;
    ROK_ESCAPE_HEADER *h = (ROK_ESCAPE_HEADER *)buf->raw;

    ZeroMemory(buf, sizeof(*buf));
    h->Magic = ROK_ESCAPE_MAGIC;
    h->Version = ROK_ESCAPE_VERSION;
    h->Type = (ROK_U32)escType;
    h->Reserved = 0;

    ZeroMemory(&esc, sizeof(esc));
    esc.hAdapter = hAdapter;
    esc.hDevice = hDevice;
    esc.Type = ROK_D3DKMT_ESCAPE_DRIVERPRIVATE;
    esc.Flags = ROK_D3DDDI_ESCAPEFLAG_HARDWAREACCESS;
    esc.pPrivateDriverData = buf->raw;
    esc.PrivateDriverDataSize = sizeof(ROK_ESCAPE_HEADER) + outSize;
    esc.hContext = 0;

    *status = pEscape(&esc);
}

static void RokProbeRokmdEscapeChannel(void)
{
    HMODULE gdi32;
    PFN_D3DKMTEnumAdapters2 pEnumAdapters2;
    PFN_D3DKMTCreateDevice pCreateDevice;
    PFN_D3DKMTDestroyDevice pDestroyDevice;
    PFN_D3DKMTEscape pEscape;
    ROK_D3DKMT_ENUMADAPTERS2 enumAdapters;
    ULONG i;
    NTSTATUS st;

    RokPrintGuidSectionHeader("[4] rokmd escape channel (D3DKMTEscape, AROK magic)");
    printf("  contract    : rok_escape.h v%u (shared KMD/ICD ABI)\n",
           (unsigned)ROK_ESCAPE_VERSION);

    gdi32 = GetModuleHandleW(L"gdi32.dll");
    if (gdi32 == NULL)
        gdi32 = LoadLibraryW(L"gdi32.dll");
    if (gdi32 == NULL)
    {
        printf("  gdi32.dll not loadable\n");
        return;
    }

    pEnumAdapters2 = (PFN_D3DKMTEnumAdapters2)(void *)GetProcAddress(gdi32, "D3DKMTEnumAdapters2");
    pCreateDevice = (PFN_D3DKMTCreateDevice)(void *)GetProcAddress(gdi32, "D3DKMTCreateDevice");
    pDestroyDevice = (PFN_D3DKMTDestroyDevice)(void *)GetProcAddress(gdi32, "D3DKMTDestroyDevice");
    pEscape = (PFN_D3DKMTEscape)(void *)GetProcAddress(gdi32, "D3DKMTEscape");
    if (pEnumAdapters2 == NULL || pEscape == NULL)
    {
        printf("  D3DKMT escape exports missing\n");
        return;
    }

    ZeroMemory(&enumAdapters, sizeof(enumAdapters));
    if (pEnumAdapters2(&enumAdapters) != 0 || enumAdapters.NumAdapters == 0)
    {
        printf("  no adapters to probe\n");
        return;
    }
    enumAdapters.pAdapters = (ROK_D3DKMT_ADAPTERINFO *)
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                  sizeof(ROK_D3DKMT_ADAPTERINFO) * enumAdapters.NumAdapters);
    if (enumAdapters.pAdapters == NULL)
        return;
    if (pEnumAdapters2(&enumAdapters) != 0)
    {
        HeapFree(GetProcessHeap(), 0, enumAdapters.pAdapters);
        return;
    }

    for (i = 0; i < enumAdapters.NumAdapters; i++)
    {
        ULONG hAdapter = enumAdapters.pAdapters[i].hAdapter;
        ROK_D3DKMT_CREATEDEVICE cd;
        ULONG hDevice = 0;
        ROK_ESC_OUT_BUF out;
        ROK_ESCAPE_HEADER *h;

        printf("  ---- adapter #%lu (hAdapter=0x%08lX) ----\n", i, (unsigned long)hAdapter);

        // ICD 同款路径：CreateDevice → Escape(hDevice) → DestroyDevice。
        ZeroMemory(&cd, sizeof(cd));
        cd.hAdapter = hAdapter;
        cd.Flags = 0;
        st = pCreateDevice(&cd);
        if (st == 0)
        {
            hDevice = cd.hDevice;
        }
        else
        {
            printf("  CreateDevice : status=0x%08lX (fallback: adapter-level hDevice=0)\n",
                   (unsigned long)st);
        }

        // [4.1] QUERY_VERSION
        RokEscSend(pEscape, hAdapter, hDevice, ROK_ESC_TYPE_QUERY_VERSION,
                   &out, sizeof(out.version), &st);
        h = (ROK_ESCAPE_HEADER *)out.raw;
        if (st == 0 && h->Magic == ROK_ESCAPE_MAGIC)
        {
            printf("  QUERY_VERSION: OK  driver v%u.%u.%u  WDDM DDI=0x%08X\n",
                   (unsigned)out.version.Major, (unsigned)out.version.Minor,
                   (unsigned)out.version.Patch,
                   (unsigned)out.version.WddmInterfaceVersion);
            if (out.version.WddmInterfaceVersion == 0x5023)
                printf("               -> WDDM 2.0 interface confirmed\n");
        }
        else
        {
            printf("  QUERY_VERSION: status=0x%08lX (non-rokmd adapter: expected)\n",
                   (unsigned long)st);
            if (hDevice != 0 && pDestroyDevice != NULL)
            {
                ROK_D3DKMT_DESTROYDEVICE dd;
                ZeroMemory(&dd, sizeof(dd));
                dd.hDevice = hDevice;
                pDestroyDevice(&dd);
            }
            continue;
        }

        // [4.2] QUERY_PCI_ID
        RokEscSend(pEscape, hAdapter, hDevice, ROK_ESC_TYPE_QUERY_PCI_ID,
                   &out, sizeof(out.pci), &st);
        if (st == 0 && ((ROK_ESCAPE_HEADER *)out.raw)->Magic == ROK_ESCAPE_MAGIC)
        {
            printf("  QUERY_PCI_ID : OK  VID=0x%04X DID=0x%04X REV=0x%02X family=%s\n",
                   (unsigned)out.pci.VendorId, (unsigned)out.pci.DeviceId,
                   (unsigned)out.pci.RevisionId, RokEscFamilyName(out.pci.Family));
        }

        // [4.3] QUERY_VRAM
        RokEscSend(pEscape, hAdapter, hDevice, ROK_ESC_TYPE_QUERY_VRAM,
                   &out, sizeof(out.vram), &st);
        if (st == 0 && ((ROK_ESCAPE_HEADER *)out.raw)->Magic == ROK_ESCAPE_MAGIC)
        {
            printf("  QUERY_VRAM   : OK  vram=%llu B gart=%llu B seg=%u\n",
                   (unsigned long long)out.vram.VramSizeBytes,
                   (unsigned long long)out.vram.GartSizeBytes,
                   (unsigned)out.vram.VramSegmentId);
        }

        // [4.4] QUERY_CAPS
        RokEscSend(pEscape, hAdapter, hDevice, ROK_ESC_TYPE_QUERY_CAPS,
                   &out, sizeof(out.caps), &st);
        if (st == 0 && ((ROK_ESCAPE_HEADER *)out.raw)->Magic == ROK_ESCAPE_MAGIC)
        {
            printf("  QUERY_CAPS   : OK  caps=0x%08X pm4=%u%s%s%s%s%s\n",
                   (unsigned)out.caps.CapsMask, (unsigned)out.caps.Pm4PacketVersion,
                   (out.caps.CapsMask & ROK_CAP_MMIO_MAPPED)   ? " [MMIO]" : "",
                   (out.caps.CapsMask & ROK_CAP_PCI_ID_VALID)  ? " [PCI]" : "",
                   (out.caps.CapsMask & ROK_CAP_SEGMENT_QUERY) ? " [SEGQ]" : "",
                   (out.caps.CapsMask & ROK_CAP_VRAM_SEGMENT)  ? " [VRAM-SEG]" : "",
                   (out.caps.CapsMask & ROK_CAP_ENGINE_STATUS_QUERY) ? " [ENGQ]" : "");
        }

        // [4.5] QUERY_SEGMENT_INFO（v1.1 追加；M3 前全 0）
        RokEscSend(pEscape, hAdapter, hDevice, ROK_ESC_TYPE_QUERY_SEGMENT_INFO,
                   &out, sizeof(out.seg), &st);
        if (st == 0 && ((ROK_ESCAPE_HEADER *)out.raw)->Magic == ROK_ESCAPE_MAGIC)
        {
            printf("  QUERY_SEGINFO: OK  vram=%llu B gart=%llu B seg=%u/%u flags=0x%X\n",
                   (unsigned long long)out.seg.VramSizeBytes,
                   (unsigned long long)out.seg.GartSizeBytes,
                   (unsigned)out.seg.VramSegmentId, (unsigned)out.seg.GartSegmentId,
                   (unsigned)out.seg.Flags);
        }

        // [4.6] QUERY_SIM_STATUS（M2.6：模拟设备 + PM4 解码统计）
        RokEscSend(pEscape, hAdapter, hDevice, ROK_ESC_TYPE_QUERY_SIM_STATUS,
                   &out, sizeof(out.sim), &st);
        if (st == 0 && ((ROK_ESCAPE_HEADER *)out.raw)->Magic == ROK_ESCAPE_MAGIC)
        {
            ROK_U32 e;
            printf("  QUERY_SIM   : OK  enabled=%u hv=%u regspace=%u\n",
                   (unsigned)out.sim.Enabled,
                   (unsigned)out.sim.HypervisorPresent,
                   (unsigned)out.sim.RegisterSpaceSize);
            printf("                pm4: t3=%u t0=%u t2=%u unknown=%u "
                   "lastIB(base=0x%X,%u dw) fence=%u\n",
                   (unsigned)out.sim.PacketCount,
                   (unsigned)out.sim.Type0Count,
                   (unsigned)out.sim.Type2Count,
                   (unsigned)out.sim.UnknownOpcodes,
                   (unsigned)out.sim.LastIndirectBaseLo,
                   (unsigned)out.sim.LastIndirectSizeDwords,
                   (unsigned)out.sim.LastFenceSubmitted);
            for (e = 0; e < 64; e++)
            {
                if (out.sim.OpcodeCounts[e] != 0)
                    printf("                op %#04x: %u pkts\n",
                           (unsigned)e, (unsigned)out.sim.OpcodeCounts[e]);
            }
        }

        // [4.7] CCC_GET_SETTINGS（M2.6：CCC 设置兼容，见 docs/ccc.md）
        RokEscSend(pEscape, hAdapter, hDevice, ROK_ESC_TYPE_CCC_GET_SETTINGS,
                   &out, sizeof(out.ccc), &st);
        if (st == 0 && ((ROK_ESCAPE_HEADER *)out.raw)->Magic == ROK_ESCAPE_MAGIC)
        {
            printf("  QUERY_CCC   : OK  mask=0x%X engine=%uKHz mem=%uKHz "
                   "od=%u fan=%u%% (source=%u)\n",
                   (unsigned)out.ccc.ValidMask,
                   (unsigned)out.ccc.EngineClockKHz,
                   (unsigned)out.ccc.MemoryClockKHz,
                   (unsigned)out.ccc.OverdriveEnable,
                   (unsigned)out.ccc.FanSpeedPercent,
                   (unsigned)out.ccc.Source);
        }

        // [4.8] QUERY_ENGINE_STATUS（M2.8：提交管线活性/fence 进度，
        //       M4 terakan ICD 用同一通道轮询 fence）
        RokEscSend(pEscape, hAdapter, hDevice, ROK_ESC_TYPE_QUERY_ENGINE_STATUS,
                   &out, sizeof(out.engine), &st);
        if (st == 0 && ((ROK_ESCAPE_HEADER *)out.raw)->Magic == ROK_ESCAPE_MAGIC)
        {
            printf("  QUERY_ENGINE: OK  submitted=%u completed=%u inflight=%u "
                   "submits=%u paging=%u responsive=%u\n",
                   (unsigned)out.engine.LastSubmittedFenceId,
                   (unsigned)out.engine.LastCompletedFenceId,
                   (unsigned)out.engine.InFlightFences,
                   (unsigned)out.engine.SubmitCount,
                   (unsigned)out.engine.PagingOpsCount,
                   (unsigned)out.engine.Responsive);
        }

        // [4.9] QUERY_HW_INFO（M3.1：芯片识别快照 + GRBM_STATUS 只读采样）
        RokEscSend(pEscape, hAdapter, hDevice, ROK_ESC_TYPE_QUERY_HW_INFO,
                   &out, sizeof(out.hw), &st);
        if (st == 0 && ((ROK_ESCAPE_HEADER *)out.raw)->Magic == ROK_ESCAPE_MAGIC)
        {
            char nameBuf[13];
            memcpy(nameBuf, out.hw.ChipName, sizeof(out.hw.ChipName));
            nameBuf[12] = '\0';
            printf("  QUERY_HW    : OK  family=%s chip=%s(%u) rev=%u mmio=%u\n",
                   RokEscFamilyName((unsigned)out.hw.Family),
                   nameBuf, (unsigned)out.hw.Chip,
                   (unsigned)out.hw.RevisionId, (unsigned)out.hw.MmioMapped);
            printf("                GRBM_STATUS=0x%08X gui_active=%u cmdfifo_avail=%u\n",
                   (unsigned)out.hw.GrbmStatus,
                   (unsigned)out.hw.GuiActive,
                   (unsigned)out.hw.CmdFifoAvail);
        }

        // [4.10] HW_CP_RESET（M3.2：写硬件，仅 --cp-reset 显式触发）
        if (g_opt_cp_reset)
        {
            RokEscSend(pEscape, hAdapter, hDevice, ROK_ESC_TYPE_HW_CP_RESET,
                       &out, sizeof(out.rst), &st);
            if (st == 0 && ((ROK_ESCAPE_HEADER *)out.raw)->Magic == ROK_ESCAPE_MAGIC)
            {
                printf("  CP_RESET    : %s  before=0x%08X after=0x%08X wait_idle=%u\n",
                       out.rst.Status ? "OK" : "REFUSED/FAIL",
                       (unsigned)out.rst.GrbmStatusBefore,
                       (unsigned)out.rst.GrbmStatusAfter,
                       (unsigned)out.rst.WaitIdleOk);
            }
            else
            {
                printf("  CP_RESET    : escape failed (st=0x%08X)\n", (unsigned)st);
            }
        }

        if (hDevice != 0 && pDestroyDevice != NULL)
        {
            ROK_D3DKMT_DESTROYDEVICE dd;
            ZeroMemory(&dd, sizeof(dd));
            dd.hDevice = hDevice;
            pDestroyDevice(&dd);
        }
    }

    HeapFree(GetProcessHeap(), 0, enumAdapters.pAdapters);
}

#endif /* ROK_PROBE_HAS_ESCAPE_ABI */

static void RokProbeVulkan(void)
{
    HMODULE vk;
    PFN_rok_vkCreateInstance pCreateInstance;
    PFN_rok_vkEnumerateInstanceExtensionProperties pEnumInstanceExts;
    PFN_rok_vkEnumeratePhysicalDevices pEnumDevices;
    PFN_rok_vkGetPhysicalDeviceProperties pGetProps;
    PFN_rok_vkEnumerateDeviceExtensionProperties pEnumDeviceExts;
    PFN_rok_vkGetPhysicalDeviceQueueFamilyProperties pQueueFamilies;
    PFN_rok_vkGetPhysicalDeviceMemoryProperties pMemProps;
    ROK_VkInstance instance = NULL;
    ROK_VkInstanceCreateInfo ci;
    unsigned int n = 0, i;
    int vr;
    ROK_VkPhysicalDevice *devices = NULL;
    unsigned char propsBuf[1024];
    ROK_VkPhysicalDevicePropsPrefix *props;

    RokPrintGuidSectionHeader("[3] Vulkan (vulkan-1.dll via loader)");

    vk = LoadLibraryW(L"vulkan-1.dll");
    if (vk == NULL)
    {
        printf("  vulkan-1.dll NOT found — Vulkan Loader 未安装\n");
        printf("  （安装 Vulkan Runtime，或 terakan ICD + Loader 后重试）\n");
        return;
    }

    pEnumInstanceExts = (PFN_rok_vkEnumerateInstanceExtensionProperties)
        (void *)GetProcAddress(vk, "vkEnumerateInstanceExtensionProperties");
    pEnumDeviceExts = (PFN_rok_vkEnumerateDeviceExtensionProperties)
        (void *)GetProcAddress(vk, "vkEnumerateDeviceExtensionProperties");
    pQueueFamilies = (PFN_rok_vkGetPhysicalDeviceQueueFamilyProperties)
        (void *)GetProcAddress(vk, "vkGetPhysicalDeviceQueueFamilyProperties");
    pMemProps = (PFN_rok_vkGetPhysicalDeviceMemoryProperties)
        (void *)GetProcAddress(vk, "vkGetPhysicalDeviceMemoryProperties");
    pCreateInstance = (PFN_rok_vkCreateInstance)(void *)GetProcAddress(vk, "vkCreateInstance");
    pEnumDevices = (PFN_rok_vkEnumeratePhysicalDevices)(void *)GetProcAddress(vk, "vkEnumeratePhysicalDevices");
    pGetProps = (PFN_rok_vkGetPhysicalDeviceProperties)(void *)GetProcAddress(vk, "vkGetPhysicalDeviceProperties");

    if (!pCreateInstance || !pEnumDevices || !pGetProps)
    {
        printf("  Vulkan loader 导出不完整（版本过旧？）\n");
        return;
    }

    if (pEnumInstanceExts)
    {
        unsigned int extCount = 0;
        if (pEnumInstanceExts(NULL, &extCount, NULL) == 0 && extCount > 0)
            printf("  instance extensions: %u available\n", extCount);
    }

    ZeroMemory(&ci, sizeof(ci));
    ci.sType = ROK_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    vr = pCreateInstance(&ci, NULL, &instance);
    if (vr != 0)
    {
        printf("  vkCreateInstance failed, VkResult=%d\n", vr);
        return;
    }

    n = 0;
    vr = pEnumDevices(instance, &n, NULL);
    if (vr != 0 || n == 0)
    {
        printf("  vkEnumeratePhysicalDevices: count=0 (result=%d)\n", vr);
        printf("  —— 没有任何物理设备：检查 ICD 是否安装（VK_ICD_FILENAMES）\n");
        return;
    }

    devices = (ROK_VkPhysicalDevice *)HeapAlloc(GetProcessHeap(), 0,
                                                sizeof(ROK_VkPhysicalDevice) * n);
    if (devices == NULL)
        return;

    vr = pEnumDevices(instance, &n, devices);
    if (vr != 0)
    {
        printf("  vkEnumeratePhysicalDevices failed, VkResult=%d\n", vr);
        HeapFree(GetProcessHeap(), 0, devices);
        return;
    }

    printf("  physical devices: %u\n", n);
    for (i = 0; i < n; i++)
    {
        props = (ROK_VkPhysicalDevicePropsPrefix *)propsBuf;
        ZeroMemory(propsBuf, sizeof(propsBuf));
        pGetProps(devices[i], propsBuf);
        printf("  ---- VkPhysicalDevice #%u ----\n", i);
        printf("  deviceName  : %s\n", props->deviceName);
        printf("  apiVersion  : %u.%u.%u  (raw 0x%08X)\n",
               props->apiVersion >> 22,
               (props->apiVersion >> 12) & 0x3FF,
               props->apiVersion & 0xFFF,
               props->apiVersion);
        printf("  driverVer   : raw 0x%08X\n", props->driverVersion);
        printf("  vendorID    : 0x%04X%s\n", props->vendorID,
               props->vendorID == 0x1002 ? "  (AMD)" : "");
        printf("  deviceID    : 0x%04X\n", props->deviceID);
        printf("  deviceType  : %s\n", RokVkDeviceType(props->deviceType));

        if (pEnumDeviceExts != NULL)
        {
            unsigned int extCount = 0;
            if (pEnumDeviceExts(devices[i], NULL, &extCount, NULL) == 0 && extCount > 0)
            {
                RokVkExtensionProperties *exts = (RokVkExtensionProperties *)
                    HeapAlloc(GetProcessHeap(), 0, sizeof(RokVkExtensionProperties) * extCount);
                if (exts != NULL &&
                    pEnumDeviceExts(devices[i], NULL, &extCount, exts) == 0)
                {
                    unsigned int e;
                    printf("  deviceExts  : %u\n", extCount);
                    for (e = 0; e < extCount; e++)
                        printf("                %s (spec %u.%u.%u)\n", exts[e].extensionName,
                               exts[e].specVersion >> 22,
                               (exts[e].specVersion >> 12) & 0x3FF,
                               exts[e].specVersion & 0xFFF);
                }
                if (exts != NULL)
                    HeapFree(GetProcessHeap(), 0, exts);
            }
        }

        if (pQueueFamilies != NULL)
        {
            unsigned int famCount = 0, f;
            pQueueFamilies(devices[i], &famCount, NULL);
            if (famCount > 0 && famCount <= 32)
            {
                RokVkQueueFamilyProperties *fams = (RokVkQueueFamilyProperties *)
                    HeapAlloc(GetProcessHeap(), 0, sizeof(RokVkQueueFamilyProperties) * famCount);
                if (fams != NULL)
                {
                    pQueueFamilies(devices[i], &famCount, fams);
                    printf("  queueFamily : %u\n", famCount);
                    for (f = 0; f < famCount; f++)
                        printf("                #%u flags=%#x count=%u%s%s\n", f,
                               fams[f].queueFlags, fams[f].queueCount,
                               (fams[f].queueFlags & 1) ? " graphics" : "",
                               (fams[f].queueFlags & 2) ? " compute" : "");
                }
                HeapFree(GetProcessHeap(), 0, fams);
            }
        }

        if (pMemProps != NULL)
        {
            RokVkPhysicalDeviceMemoryProperties mem;
            unsigned int h;
            ZeroMemory(&mem, sizeof(mem));
            pMemProps(devices[i], &mem);
            printf("  memory      : %u heaps, %u types\n",
                   mem.memoryHeapCount, mem.memoryTypeCount);
            for (h = 0; h < mem.memoryHeapCount && h < 8; h++)
                printf("                heap #%u: %lluMB%s\n", h,
                       (unsigned long long)(mem.memoryHeaps[h].size / (1024 * 1024)),
                       (mem.memoryHeaps[h].flags & 1) ? " (device-local)" : "");
        }
    }

    HeapFree(GetProcessHeap(), 0, devices);
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
    int i;

    /* --cp-reset：在 [4] 段末尾显式触发一次 CP 软复位（M3.2 实机演练）。
     * 默认全程只读。 */
    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--cp-reset") == 0)
        {
            g_opt_cp_reset = 1;
        }
    }

    /* 源码字符串是 UTF-8 编码；不切控制台代码页时，中文 Windows
     * （cp936/cp950）与传统 cmd 下会显示乱码。65001 即 UTF-8，
     * Win10 1809+ 的 conhost / Windows Terminal 均支持。 */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    printf("gpu_probe — radeon-windows 图形栈探测工具\n");
    printf("目标：弄清 Windows 图形栈当前状态（PCI / WDDM / Vulkan / rokmd escape）\n");

    RokProbePciDisplayDevices();
    RokProbeHypervisor();
    RokProbeD3DKMTAdapters();
    RokProbeVulkanIcdRegistry();
    RokProbeVulkan();
#if ROK_PROBE_HAS_ESCAPE_ABI
    RokProbeRokmdEscapeChannel();
#endif

    printf("\ndone.\n");
    return 0;
}
