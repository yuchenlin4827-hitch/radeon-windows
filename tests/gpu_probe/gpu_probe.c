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
    ULONG   Flags;
} ROK_D3DKMT_ADAPTERINFO;

typedef struct _ROK_D3DKMT_ENUMADAPTERS2 {
    ULONG NumAdapters;
    ROK_D3DKMT_ADAPTERINFO *pAdapters;
} ROK_D3DKMT_ENUMADAPTERS2;

typedef struct _ROK_D3D_UMD_INTERFACE_VERSION {
    union {
        struct { USHORT MinorVer; USHORT MajorVer; };
        ULONG Version;
    };
} ROK_D3D_UMD_INTERFACE_VERSION;

#define ROK_MAX_PATH 260
typedef struct _ROK_D3DKMT_UMDFILENAMEINFO {
    ROK_D3D_UMD_INTERFACE_VERSION Version;
    WCHAR UmdFileName[ROK_MAX_PATH];
} ROK_D3DKMT_UMDFILENAMEINFO;

typedef struct _ROK_D3DKMT_QUERYADAPTERINFO {
    ULONG hAdapter;
    ULONG Type;                 // KMTQUERYADAPTERINFOTYPE
    VOID *pPrivateDriverData;
    UINT  PrivateDriverDataSize;
} ROK_D3DKMT_QUERYADAPTERINFO;

#define ROK_KMTQAITYPE_UMDRIVERNAME 7   // KMTQAITYPE_UMDRIVERNAME

typedef ULONG ROK_D3DKMT_HANDLE;
typedef NTSTATUS (WINAPI *PFN_D3DKMTEnumAdapters2)(ROK_D3DKMT_ENUMADAPTERS2 *);
typedef NTSTATUS (WINAPI *PFN_D3DKMTQueryAdapterInfo)(ROK_D3DKMT_QUERYADAPTERINFO *);

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
        printf("  D3DKMT exports missing (需 Windows 8+ / WDDM 1.3+)\n");
        return;
    }

    ZeroMemory(&enumAdapters, sizeof(enumAdapters));
    st = pEnumAdapters2(&enumAdapters);
    if (st != 0)
    {
        printf("  D3DKMTEnumAdapters2 failed, status=0x%08lX\n", (unsigned long)st);
        return;
    }

    printf("  adapter count: %lu\n", enumAdapters.NumAdapters);
    if (enumAdapters.NumAdapters == 0)
        return;

    for (i = 0; i < enumAdapters.NumAdapters; i++)
    {
        ROK_D3DKMT_QUERYADAPTERINFO qi;
        ROK_D3DKMT_UMDFILENAMEINFO umd;

        printf("  ---- adapter #%lu ----\n", i);
        printf("  handle      : 0x%08lX\n", (unsigned long)enumAdapters.pAdapters[i].hAdapter);
        printf("  luid        : %08lX-%08lX\n",
               (unsigned long)enumAdapters.pAdapters[i].AdapterLuid.HighPart,
               (unsigned long)enumAdapters.pAdapters[i].AdapterLuid.LowPart);

        ZeroMemory(&umd, sizeof(umd));
        ZeroMemory(&qi, sizeof(qi));
        qi.hAdapter = enumAdapters.pAdapters[i].hAdapter;
        qi.Type = ROK_KMTQAITYPE_UMDRIVERNAME;
        qi.pPrivateDriverData = &umd;
        qi.PrivateDriverDataSize = sizeof(umd);
        st = pQueryAdapterInfo(&qi);
        if (st == 0)
        {
            printf("  UMD dll     : %ls  (UMD interface v%u.%u)\n",
                   umd.UmdFileName,
                   umd.Version.MajorVer, umd.Version.MinorVer);
            printf("                ^^^ 装 terakan 后预期出现 terakan 相关 DLL\n");
        }
        else
        {
            printf("  UMD query   : failed status=0x%08lX\n", (unsigned long)st);
        }
    }

    HeapFree(GetProcessHeap(), 0, enumAdapters.pAdapters);
}

// ---------------------------------------------------------------------------
// [3] Vulkan（动态加载）
// ---------------------------------------------------------------------------
static void RokProbeVulkan(void)
{
    HMODULE vk;
    PFN_rok_vkCreateInstance pCreateInstance;
    PFN_rok_vkEnumerateInstanceExtensionProperties pEnumInstanceExts;
    PFN_rok_vkEnumeratePhysicalDevices pEnumDevices;
    PFN_rok_vkGetPhysicalDeviceProperties pGetProps;
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
    }

    HeapFree(GetProcessHeap(), 0, devices);
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    printf("gpu_probe — radeon-windows M0 探测工具\n");
    printf("目标：弄清 Windows 图形栈当前状态（PCI / WDDM / Vulkan）\n");

    RokProbePciDisplayDevices();
    RokProbeD3DKMTAdapters();
    RokProbeVulkan();

    printf("\ndone.\n");
    return 0;
}
