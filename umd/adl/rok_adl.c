// SPDX-License-Identifier: MIT
// rok_adl.c — ADL 兼容 shim 实现（rokmd 专用，替代 atiadlxx.dll 的核心面）
//
// 工作原理：
//   1. Main_Control_Create 时 D3DKMTEnumAdapters2 枚举 WDDM 适配器；
//   2. 对每个适配器发 rokmd escape（ROK_ESC_TYPE_QUERY_VERSION），
//      应答成功者为 rokmd 适配器，进入 ADL 索引表（其余忽略）；
//   3. AdapterInfo / Overdrive 设置全部经 escape 读回/写入，
//      与 KMD 侧 ccc.c 的注册表导入共用同一存储。
//
// 构建见 .github/workflows/build-adl.yml（MSVC /LD 与 mingw -shared 双验证）。
#define ROK_ADL_BUILD_DLL 1
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "rok_adl.h"

// ------------------------------------------------------------------
// rok_escape.h 契约的用户态镜像（与 kmd/src/rok_escape.h 一致）
// ------------------------------------------------------------------
#define ROK_ESCAPE_MAGIC        0x4B4F5241u
#define ROK_ESCAPE_VERSION      1u

typedef enum
{
    ROK_ESC_TYPE_QUERY_VERSION    = 1,
    ROK_ESC_TYPE_QUERY_PCI_ID     = 2,
    ROK_ESC_TYPE_QUERY_VRAM       = 3,
    ROK_ESC_TYPE_QUERY_CAPS       = 4,
    ROK_ESC_TYPE_QUERY_SIM_STATUS = 5,
    ROK_ESC_TYPE_CCC_GET_SETTINGS = 6,
    ROK_ESC_TYPE_CCC_SET_SETTINGS = 7,
} ROK_ESCAPE_TYPE_UM;

#define ROK_ESC_CCC_VALID_ENGINE_CLOCK 0x00000001u
#define ROK_ESC_CCC_VALID_MEMORY_CLOCK 0x00000002u
#define ROK_ESC_CCC_VALID_OD_ENABLE    0x00000004u
#define ROK_ESC_CCC_VALID_FAN_SPEED    0x00000008u

typedef struct RokEscHeader
{
    unsigned int Magic;
    unsigned int Version;
    unsigned int Type;
    unsigned int Reserved;
} RokEscHeader;

typedef struct RokEscVersionOut
{
    unsigned int Major;
    unsigned int Minor;
    unsigned int Patch;
    unsigned int WddmInterfaceVersion;
} RokEscVersionOut;

typedef struct RokEscPciIdOut
{
    unsigned int VendorId;
    unsigned int DeviceId;
    unsigned int RevisionId;
    unsigned int Family;
} RokEscPciIdOut;

typedef struct RokEscCccSettings
{
    unsigned int Version;
    unsigned int ValidMask;
    unsigned int EngineClockKHz;
    unsigned int MemoryClockKHz;
    unsigned int OverdriveEnable;
    unsigned int FanSpeedPercent;
    unsigned int Source;
    unsigned int Reserved;
} RokEscCccSettings;

// D3DKMT_ESCAPE 镜像（x64 32 字节，与 WDK 布局一致）。
typedef struct RokD3DKMTEscape
{
    ULONG hAdapter;
    ULONG hDevice;
    ULONG Type;                 // 0 = D3DKMT_ESCAPE_DRIVERPRIVATE
    VOID *pPrivateDriverData;
    UINT  PrivateDriverDataSize;
    ULONG FlagsValue;           // D3DDDI_ESCAPEFLAGS = 0
} RokD3DKMTEscape;

typedef struct RokD3DKMTAdapterInfo
{
    ULONG hAdapter;
    ULONG AdapterLuidLow;
    LONG  AdapterLuidHigh;
    ULONG Flags;
} RokD3DKMTAdapterInfo;

typedef struct RokD3DKMTEnumAdapters2
{
    ULONG NumAdapters;
    RokD3DKMTAdapterInfo *pAdapters;
} RokD3DKMTEnumAdapters2;

typedef NTSTATUS (WINAPI *PFN_D3DKMTEnumAdapters2)(RokD3DKMTEnumAdapters2 *);
typedef NTSTATUS (WINAPI *PFN_D3DKMTEscape)(RokD3DKMTEscape *);

// ------------------------------------------------------------------
// 上下文
// ------------------------------------------------------------------
#define ROK_ADL_MAX_ADAPTERS 8

typedef struct RokAdlAdapterEntry
{
    ULONG        hAdapter;              // D3DKMT_HANDLE
    unsigned int VendorId;
    unsigned int DeviceId;
    unsigned int RevisionId;
    unsigned int Major, Minor, Patch;
} RokAdlAdapterEntry;

typedef struct RokAdlContext
{
    int               AdapterCount;
    RokAdlAdapterEntry Adapters[ROK_ADL_MAX_ADAPTERS];
} RokAdlContext;

// ------------------------------------------------------------------
// escape 帮助函数
// ------------------------------------------------------------------

// 发送 escape；buf 布局 = [HEADER][OUT]，调用方需保证大小。
static int
RokAdlDoEscape(ULONG hAdapter, unsigned int type,
               void *buf, unsigned int bufSize)
{
    static PFN_D3DKMTEscape pEscape = NULL;
    static int resolved = 0;
    RokD3DKMTEscape esc;
    RokEscHeader *hdr = (RokEscHeader *)buf;
    NTSTATUS st;

    if (!resolved)
    {
        HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
        if (gdi32 == NULL)
            gdi32 = LoadLibraryW(L"gdi32.dll");
        if (gdi32 != NULL)
            pEscape = (PFN_D3DKMTEscape)(void *)GetProcAddress(gdi32, "D3DKMTEscape");
        resolved = 1;
    }
    if (pEscape == NULL)
        return ROK_ADL_ERR;

    ZeroMemory(&esc, sizeof(esc));
    esc.hAdapter = hAdapter;
    esc.Type = 0;
    esc.pPrivateDriverData = buf;
    esc.PrivateDriverDataSize = bufSize;
    esc.FlagsValue = 0;

    ZeroMemory(hdr, sizeof(*hdr));
    hdr->Magic = ROK_ESCAPE_MAGIC;
    hdr->Version = ROK_ESCAPE_VERSION;
    hdr->Type = type;

    st = pEscape(&esc);
    return (st == 0) ? ROK_ADL_OK : ROK_ADL_ERR;
}

// ------------------------------------------------------------------
// ADL 枚举接口
// ------------------------------------------------------------------

ROK_ADL_API int ROK_ADL_CALL
ADL2_Main_Control_Create(ROKADL_MAIN_MALLOC_CALLBACK callback,
                         int iEnumConnectedAdapters,
                         ROKADLContext *context)
{
    static PFN_D3DKMTEnumAdapters2 pEnumAdapters2 = NULL;
    static int resolved = 0;
    HMODULE gdi32;
    RokD3DKMTEnumAdapters2 ea;
    RokAdlContext *ctx;
    ULONG i;

    (void)iEnumConnectedAdapters;

    if (callback == NULL || context == NULL)
        return ROK_ADL_ERR_BAD_PARAM;

    if (!resolved)
    {
        gdi32 = GetModuleHandleW(L"gdi32.dll");
        if (gdi32 == NULL)
            gdi32 = LoadLibraryW(L"gdi32.dll");
        if (gdi32 != NULL)
            pEnumAdapters2 = (PFN_D3DKMTEnumAdapters2)(void *)
                GetProcAddress(gdi32, "D3DKMTEnumAdapters2");
        resolved = 1;
    }
    if (pEnumAdapters2 == NULL)
        return ROK_ADL_ERR;

    // 与官方 ADL 的偏差（已文档化）：官方用调用方注入的 malloc 回调，
    // 本 shim 统一用进程堆自管上下文内存（callback 仅保留签名兼容）。
    (void)callback;
    ctx = (RokAdlContext *)HeapAlloc(GetProcessHeap(), 0, sizeof(RokAdlContext));
    if (ctx == NULL)
        return ROK_ADL_ERR;
    ZeroMemory(ctx, sizeof(*ctx));

    ZeroMemory(&ea, sizeof(ea));
    if (pEnumAdapters2(&ea) != 0 || ea.pAdapters == NULL)
    {
        HeapFree(GetProcessHeap(), 0, ctx);
        return ROK_ADL_ERR_NO_ADAPTERS;
    }

    for (i = 0; i < ea.NumAdapters && ctx->AdapterCount < ROK_ADL_MAX_ADAPTERS; i++)
    {
        unsigned char buf[16 + sizeof(RokEscVersionOut)];
        RokEscVersionOut *ver = (RokEscVersionOut *)(buf + sizeof(RokEscHeader));
        RokAdlAdapterEntry *entry;

        if (RokAdlDoEscape(ea.pAdapters[i].hAdapter, ROK_ESC_TYPE_QUERY_VERSION,
                           buf, sizeof(buf)) != ROK_ADL_OK)
        {
            continue;                       // 非 rokmd：跳过
        }

        entry = &ctx->Adapters[ctx->AdapterCount++];
        entry->hAdapter = ea.pAdapters[i].hAdapter;
        entry->Major = ver->Major;
        entry->Minor = ver->Minor;
        entry->Patch = ver->Patch;

        {
            unsigned char pbuf[16 + sizeof(RokEscPciIdOut)];
            RokEscPciIdOut *pci = (RokEscPciIdOut *)(pbuf + sizeof(RokEscHeader));
            if (RokAdlDoEscape(ea.pAdapters[i].hAdapter, ROK_ESC_TYPE_QUERY_PCI_ID,
                               pbuf, sizeof(pbuf)) == ROK_ADL_OK)
            {
                entry->VendorId = pci->VendorId;
                entry->DeviceId = pci->DeviceId;
                entry->RevisionId = pci->RevisionId;
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, ea.pAdapters);

    if (ctx->AdapterCount == 0)
    {
        HeapFree(GetProcessHeap(), 0, ctx);
        return ROK_ADL_ERR_NO_ADAPTERS;
    }

    *context = (ROKADLContext)ctx;
    return ROK_ADL_OK;
}

ROK_ADL_API int ROK_ADL_CALL
ADL2_Main_Control_Destroy(ROKADLContext context)
{
    if (context == NULL)
        return ROK_ADL_ERR_BAD_PARAM;
    // 上下文由本 DLL 的 HeapAlloc 分配（见 Main_Control_Create），此处归还。
    HeapFree(GetProcessHeap(), 0, context);
    return ROK_ADL_OK;
}

static RokAdlContext *
RokAdlCtxFromHandle(ROKADLContext context)
{
    return (RokAdlContext *)context;
}

ROK_ADL_API int ROK_ADL_CALL
ADL2_Adapter_NumberOfAdapters_Get(ROKADLContext context, int *lpNumAdapters)
{
    RokAdlContext *ctx = RokAdlCtxFromHandle(context);
    if (ctx == NULL || lpNumAdapters == NULL)
        return ROK_ADL_ERR_BAD_PARAM;
    *lpNumAdapters = ctx->AdapterCount;
    return ROK_ADL_OK;
}

ROK_ADL_API int ROK_ADL_CALL
ADL2_Adapter_AdapterInfo_Get(ROKADLContext context,
                             ROKLPAdapterInfo lpInfo, int iInputSize)
{
    RokAdlContext *ctx = RokAdlCtxFromHandle(context);
    int i, count, elemSize;

    if (ctx == NULL || lpInfo == NULL)
        return ROK_ADL_ERR_BAD_PARAM;

    count = ctx->AdapterCount;
    elemSize = (int)sizeof(ROKAdapterInfo);

    if (iInputSize < count * elemSize)
        return ROK_ADL_ERR_BAD_PARAM;

    for (i = 0; i < count; i++)
    {
        ROKLPAdapterInfo info = (ROKLPAdapterInfo)((char *)lpInfo + i * elemSize);
        RokAdlAdapterEntry *entry = &ctx->Adapters[i];

        if (info->iSize != 0 && info->iSize < elemSize)
            continue;                       // 调用方用短布局：跳过防溢出

        ZeroMemory(info, elemSize);
        info->iSize = elemSize;
        info->iAdapterIndex = i;
        info->iVendorID = (int)entry->VendorId;
        sprintf(info->strUDID,
                "PCI_VEN_%04X&DEV_%04X&SUBSYS_00000000&REV_%02X",
                entry->VendorId, entry->DeviceId, entry->RevisionId);
        strcpy(info->strDriverPath, "rokmd");
        strcpy(info->strAdapterName, "Radeon Open KMD (roKmd)");
        sprintf(info->strDisplayDriverVersion, "%u.%u.%u (roKmd WDDM 2.0)",
                entry->Major, entry->Minor, entry->Patch);
        strcpy(info->strDisplayDriverDate, "radeon-windows project");
        strcpy(info->strDisplayName, "roKmd");
    }
    return ROK_ADL_OK;
}

ROK_ADL_API int ROK_ADL_CALL
ADL2_Adapter_Active_Get(ROKADLContext context,
                        int iAdapterIndex, int *lpStatus)
{
    RokAdlContext *ctx = RokAdlCtxFromHandle(context);
    if (ctx == NULL || lpStatus == NULL)
        return ROK_ADL_ERR_BAD_PARAM;
    if (iAdapterIndex < 0 || iAdapterIndex >= ctx->AdapterCount)
        return ROK_ADL_ERR_BAD_PARAM;
    // escape 探测通过即视为 active（render-only 适配器同样可用）。
    *lpStatus = ROK_ADL_TRUE;
    return ROK_ADL_OK;
}

ROK_ADL_API int ROK_ADL_CALL
ADL2_Adapter_ID_Get(ROKADLContext context, int iAdapterIndex, int *lpAdapterID)
{
    RokAdlContext *ctx = RokAdlCtxFromHandle(context);
    if (ctx == NULL || lpAdapterID == NULL)
        return ROK_ADL_ERR_BAD_PARAM;
    if (iAdapterIndex < 0 || iAdapterIndex >= ctx->AdapterCount)
        return ROK_ADL_ERR_BAD_PARAM;
    // 官方语义为全局唯一适配器 ID；用 DID+索引稳定生成。
    *lpAdapterID = (int)((ctx->Adapters[iAdapterIndex].DeviceId << 16) | iAdapterIndex);
    return ROK_ADL_OK;
}

// ------------------------------------------------------------------
// Overdrive 桥
// ------------------------------------------------------------------

ROK_ADL_API int ROK_ADL_CALL
ADL2_Overdrive5_Settings_Get(ROKADLContext context,
                             int iAdapterIndex, ROKOD5Settings *lpSettings)
{
    RokAdlContext *ctx = RokAdlCtxFromHandle(context);
    unsigned char buf[16 + sizeof(RokEscCccSettings)];
    RokEscCccSettings *out = (RokEscCccSettings *)(buf + sizeof(RokEscHeader));

    if (ctx == NULL || lpSettings == NULL)
        return ROK_ADL_ERR_BAD_PARAM;
    if (iAdapterIndex < 0 || iAdapterIndex >= ctx->AdapterCount)
        return ROK_ADL_ERR_BAD_PARAM;

    if (RokAdlDoEscape(ctx->Adapters[iAdapterIndex].hAdapter,
                       ROK_ESC_TYPE_CCC_GET_SETTINGS, buf, sizeof(buf)) != ROK_ADL_OK)
        return ROK_ADL_ERR;

    ZeroMemory(lpSettings, sizeof(*lpSettings));
    lpSettings->ValidMask = out->ValidMask;
    lpSettings->EngineClock = out->EngineClockKHz;
    lpSettings->MemoryClock = out->MemoryClockKHz;
    lpSettings->OverdriveEnable = out->OverdriveEnable;
    lpSettings->FanSpeedPercent = out->FanSpeedPercent;
    lpSettings->Source = out->Source;

    // 范围表（官方 OD5 风格；KMD 侧 ccc.h 同值）。
    lpSettings->EngineClockMin    = 50000;
    lpSettings->EngineClockMax    = 2000000;
    lpSettings->EngineClockDefault = 0;
    lpSettings->MemoryClockMin    = 50000;
    lpSettings->MemoryClockMax    = 2000000;
    lpSettings->MemoryClockDefault = 0;
    return ROK_ADL_OK;
}

ROK_ADL_API int ROK_ADL_CALL
ADL2_Overdrive5_Settings_Set(ROKADLContext context,
                             int iAdapterIndex, ROKOD5Settings *lpSettings)
{
    RokAdlContext *ctx = RokAdlCtxFromHandle(context);
    unsigned char buf[16 + sizeof(RokEscCccSettings)];
    RokEscCccSettings *in = (RokEscCccSettings *)(buf + sizeof(RokEscHeader));

    if (ctx == NULL || lpSettings == NULL)
        return ROK_ADL_ERR_BAD_PARAM;
    if (iAdapterIndex < 0 || iAdapterIndex >= ctx->AdapterCount)
        return ROK_ADL_ERR_BAD_PARAM;

    ZeroMemory(buf, sizeof(buf));
    in->Version = 1;
    in->ValidMask = lpSettings->ValidMask;
    in->EngineClockKHz = lpSettings->EngineClock;
    in->MemoryClockKHz = lpSettings->MemoryClock;
    in->OverdriveEnable = lpSettings->OverdriveEnable;
    in->FanSpeedPercent = lpSettings->FanSpeedPercent;
    in->Source = 2;     // ROK_ESC_CCC_SOURCE_ESCAPE

    return RokAdlDoEscape(ctx->Adapters[iAdapterIndex].hAdapter,
                          ROK_ESC_TYPE_CCC_SET_SETTINGS, buf, sizeof(buf));
}
