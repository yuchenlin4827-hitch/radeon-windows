// SPDX-License-Identifier: MIT
// ccc.h — rokmd 与 AMD 官方 CCC（Catalyst Control Center / Radeon Settings
// 时代之前的 Overdrive 体系）设置的兼容层
//
// 设计边界（详见 docs/ccc.md）：
//   * 官方 CCC 本体绑定 Catalyst KMD（atikmdag），无法直接驱动 rokmd；
//   * 兼容从两条路实现：
//     (1) 注册表导入 —— Catalyst 体系把 Overdrive/时钟覆盖等设置写在
//         驱动键（HKLM\SYSTEM\...\Control\Class\{4D36E968...}\00XX），rokmd
//         启动时按同名值导入（本文件 RokCccImport）；
//     (2) escape 通道 —— rok_escape.h 的 CCC get/set 报文，配合
//         umd/adl（ADL 兼容 shim）让"调取 Overdrive 设置"的工具链路
//         保持与官方 ADL 相同的调用习惯。
//   * 时钟/风扇的真实生效点在 M3（寄存器编程可用后）；当前阶段做
//     校验、存储、回读 —— 语义与官方 Overdrive 的"设置-回读"一致。
#pragma once

#include "precomp.h"

struct _ROK_ADAPTER;

// 设置版本与有效位（escape 报文与注册表导入共用）。
#define ROK_CCC_VERSION                 1

#define ROK_CCC_VALID_ENGINE_CLOCK      0x00000001UL    // EngineClockKHz 有效
#define ROK_CCC_VALID_MEMORY_CLOCK      0x00000002UL    // MemoryClockKHz 有效
#define ROK_CCC_VALID_OD_ENABLE         0x00000004UL    // OverdriveEnable 有效
#define ROK_CCC_VALID_FAN_SPEED         0x00000008UL    // FanSpeedPercent 有效

// 设置来源（Source 字段）。
#define ROK_CCC_SOURCE_NONE             0               // 默认值
#define ROK_CCC_SOURCE_REGISTRY         1               // RokCccImport 导入
#define ROK_CCC_SOURCE_ESCAPE           2               // ADL shim / 工具写入

// 合法范围（超出被钳制并回写 —— 与官方 Overdrive "range-clamp" 行为一致）。
#define ROK_CCC_ENGINE_CLOCK_MIN_KHZ    50000UL         // 50 MHz
#define ROK_CCC_ENGINE_CLOCK_MAX_KHZ    2000000UL       // 2 GHz
#define ROK_CCC_MEMORY_CLOCK_MIN_KHZ    50000UL
#define ROK_CCC_MEMORY_CLOCK_MAX_KHZ    2000000UL
#define ROK_CCC_FAN_MIN_PERCENT         0UL
#define ROK_CCC_FAN_MAX_PERCENT         100UL

// 钳制宏（内核头 min/max 可用性随 WDK 版本不同，自备）。
#define ROK_CCC_CLAMP(v, lo, hi)    (((v) < (lo)) ? (lo) : (((v) > (hi)) ? (hi) : (v)))

// CCC 设置块（escape 报文体；也是适配器内的当前设置存储）。
typedef struct _ROK_CCC_SETTINGS
{
    UINT32 Version;             // ROK_CCC_VERSION
    UINT32 ValidMask;           // ROK_CCC_VALID_* 组合
    UINT32 EngineClockKHz;      // 引擎时钟覆盖（0 = 未覆盖）
    UINT32 MemoryClockKHz;      // 显存时钟覆盖（0 = 未覆盖）
    UINT32 OverdriveEnable;     // 1 = 启用 Overdrive 手动档
    UINT32 FanSpeedPercent;     // 固定风扇转速（0..100）
    UINT32 Source;              // ROK_CCC_SOURCE_*
    UINT32 Reserved;            // 0
} ROK_CCC_SETTINGS, *PROK_CCC_SETTINGS;

#if defined(_WIN32)
C_ASSERT(sizeof(ROK_CCC_SETTINGS) == 32);
#endif

// StartDevice 阶段调用：从驱动键导入 Catalyst 体系设置（存在才导入）。
NTSTATUS RokCccImport(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _In_opt_ PUNICODE_STRING DeviceRegistryPath);

// escape ROK_ESC_TYPE_CCC_GET_SETTINGS：回传当前设置。
NTSTATUS RokCccGetSettings(
    _In_ struct _ROK_ADAPTER *Adapter,
    _Out_ PROK_CCC_SETTINGS Out);

// escape ROK_ESC_TYPE_CCC_SET_SETTINGS：校验/钳制后应用（M3 起落硬件）。
NTSTATUS RokCccSetSettings(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _In_ CONST PROK_CCC_SETTINGS In);
