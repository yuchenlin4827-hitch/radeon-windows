// SPDX-License-Identifier: MIT
// rok_adl.h — rokmd 的 ADL（AMD Display Library）兼容 shim 头文件
//
// 目标：让"官方 CCC 设置生态"的工具以 ADL 的调用习惯访问 rokmd：
//   * 枚举接口与官方 atiadlxx.dll 同名同签名（ADL2_* 系列）；
//   * Overdrive 设置经 rok_escape.h 的 CCC 报文落到 KMD（M3 起生效到硬件）；
//   * 与官方 ADL 的差异全部在本文档与 docs/ccc.md 标注，不冒充未实现的部分。
//
// 与官方 ADL SDK 结构体的对应：
//   AdapterInfo —— 按 ADL SDK 公开头文件布局（ADL_MAX_PATH=512，核心 11 字段），
//   本 shim 只填充保证兼容的前缀字段；调用方按 iSize 决定布局。
#pragma once

#include <windows.h>

#if defined(ROK_ADL_BUILD_DLL)
#define ROK_ADL_API __declspec(dllexport)
#else
#define ROK_ADL_API
#endif

// 调用约定：官方 ADL 头使用默认约定（x64 只有一种）；x86 下为 __cdecl。
#ifndef ROK_ADL_CALL
#define ROK_ADL_CALL
#endif

// ADL 返回码（官方 adl_defines.h 对齐的常用值）。
#define ROK_ADL_OK                    0
#define ROK_ADL_ERR                   -1
#define ROK_ADL_ERR_NOT_SUPPORTED     -8
#define ROK_ADL_ERR_BAD_PARAM         -20
#define ROK_ADL_ERR_NO_ADAPTERS       -1001

#define ROK_ADL_MAX_PATH 512
#define ROK_ADL_TRUE  1
#define ROK_ADL_FALSE 0

// ---- ADL 兼容结构（官方 adlstructures.h 核心布局）----
typedef struct ROKAdapterInfo
{
    int  iSize;                                 // 结构体大小（调用方填）
    int  iAdapterIndex;                         // ADL 索引
    char strUDID[ROK_ADL_MAX_PATH];             // PCI_VEN_..&DEV_.. 形态
    char strDriverPath[ROK_ADL_MAX_PATH];       // 驱动服务名路径
    char strDriverPathExt[ROK_ADL_MAX_PATH];
    char strPNPString[ROK_ADL_MAX_PATH];
    char strDisplayName[ROK_ADL_MAX_PATH];
    int  iVendorID;                             // 0x1002 = AMD
    char strAdapterName[ROK_ADL_MAX_PATH];      // "Radeon Open KMD (roKmd)"
    char strDisplayDriverVersion[ROK_ADL_MAX_PATH];
    char strDisplayDriverDate[ROK_ADL_MAX_PATH];
} ROKAdapterInfo, *ROKLPAdapterInfo;

// ---- ADL 兼容函数指针/上下文 ----
typedef void *ROKADLContext;
typedef void *(*ROKADL_MAIN_MALLOC_CALLBACK)(int iSize);

#ifdef __cplusplus
extern "C" {
#endif

// ---- 枚举（与官方 ADL2_* 同名同签名）----
ROK_ADL_API int ROK_ADL_CALL
ADL2_Main_Control_Create(ROKADL_MAIN_MALLOC_CALLBACK callback,
                         int iEnumConnectedAdapters,
                         ROKADLContext *context);

ROK_ADL_API int ROK_ADL_CALL
ADL2_Main_Control_Destroy(ROKADLContext context);

ROK_ADL_API int ROK_ADL_CALL
ADL2_Adapter_NumberOfAdapters_Get(ROKADLContext context, int *lpNumAdapters);

ROK_ADL_API int ROK_ADL_CALL
ADL2_Adapter_AdapterInfo_Get(ROKADLContext context,
                             ROKLPAdapterInfo lpInfo, int iInputSize);

ROK_ADL_API int ROK_ADL_CALL
ADL2_Adapter_Active_Get(ROKADLContext context,
                        int iAdapterIndex, int *lpStatus);

ROK_ADL_API int ROK_ADL_CALL
ADL2_Adapter_ID_Get(ROKADLContext context, int iAdapterIndex, int *lpAdapterID);

// ---- Overdrive 桥（rokmd 专有扩展；语义对应官方 Overdrive 的
//      "时钟/风扇设置读取与写入"，走 rok_escape.h CCC 报文）----
typedef struct ROKOD5Settings
{
    unsigned int ValidMask;        // ROK_ESC_CCC_VALID_* 组合
    unsigned int EngineClockMin;   // KHz（官方 OD5 风格 min/max/default）
    unsigned int EngineClockMax;
    unsigned int EngineClockDefault;
    unsigned int MemoryClockMin;
    unsigned int MemoryClockMax;
    unsigned int MemoryClockDefault;
    unsigned int EngineClock;      // 当前设置（0 = 未覆盖）
    unsigned int MemoryClock;      // 当前设置（0 = 未覆盖）
    unsigned int OverdriveEnable;  // 1 = 手动档
    unsigned int FanSpeedPercent;  // 0..100
    unsigned int Source;           // 0=none 1=registry 2=escape
} ROKOD5Settings;

ROK_ADL_API int ROK_ADL_CALL
ADL2_Overdrive5_Settings_Get(ROKADLContext context,
                             int iAdapterIndex, ROKOD5Settings *lpSettings);

ROK_ADL_API int ROK_ADL_CALL
ADL2_Overdrive5_Settings_Set(ROKADLContext context,
                             int iAdapterIndex, ROKOD5Settings *lpSettings);

#ifdef __cplusplus
}
#endif
