// SPDX-License-Identifier: MIT
// gfxinit.h — 硬件初始化前哨（M3.4a）：寄存器访问、状态快照、CP ring bring-up
#pragma once

#include "precomp.h"
#include "rok_escape.h"

// 前向声明（避免 adapter.h ⇄ gfxinit.h 循环包含）。
struct _ROK_ADAPTER;

// ---- 寄存器读写（sim 感知）----
// 真实模式：经 StartDevice 映射的 BAR0 直接 MMIO（READ/WRITE_REGISTER_ULONG）。
// 模拟模式：落到 sim.c 的 64KB SimRegisterSpace（host 测试即测试真实代码路径）。
// 两者都不可用：返回 0 / 丢弃写入（调用方用状态快照的 MmioSource 区分）。
ROK_U32
RokDevRegRead32(
    _In_opt_ struct _ROK_ADAPTER *Adapter,
    _In_ ROK_U32 Offset
    );

VOID
RokDevRegWrite32(
    _In_opt_ struct _ROK_ADAPTER *Adapter,
    _In_ ROK_U32 Offset,
    _In_ ROK_U32 Value
    );

// ---- 只读硬件状态快照（escape type 10：ROK_ESC_TYPE_QUERY_HW_STATUS）----
// 真实/模拟/无 MMIO 三态皆可用；对非 rokmd 适配器不可达（escape 由 KMD 接）。
NTSTATUS
RokGfxProbeStatus(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _Out_ ROK_ESC_HW_STATUS_OUT *Out
    );

// ---- CP ring bring-up（M3.4a）----
// 真实模式（Rehearse=FALSE）执行条件（任一不满足即记日志返回 NOT_SUPPORTED）：
//   * <DriverKey>\Parameters\EnableRing = 1（默认关闭 —— render-only
//     即时完成模型保持不变，零 TDR 风险）；
//   * 非模拟模式、BAR0 已映射、设备 ID 属 TS2 家族（微码为 CAICOS 版）；
//   * 资源清单含 VRAM 孔径（>16MB MEM 窗口）且 MC_VM_FB_LOCATION 已配置
//     （热替换：Catalyst 遗留配置；冷启动：VBIOS 配置）。
// 演练模式（Rehearse=TRUE，仅 host 测试）：寄存器写入全部落入
// SimRegisterSpace，ring 内存取自池分配 —— 用真实序列 + 真实数据流验证代码。
NTSTATUS
RokRingHardwareInit(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _In_ BOOLEAN Rehearse
    );

// StopDevice 阶段调用：停机 CP（若我们启动过）+ 解除 ring 窗口映射。
VOID
RokRingHardwareStop(
    _Inout_ struct _ROK_ADAPTER *Adapter
    );

// StartDevice 阶段调用（adapter.c）：读 EnableRing 开关并按需初始化。
// RegistryPath 为 DXGK_DEVICE_INFO.DeviceRegistryPath（驱动键），可为 NULL。
VOID
RokGfxStartDeviceRing(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _In_opt_ PUNICODE_STRING RegistryPath
    );

// 微码（evergreen_ucode.c，scripts/gen_ucode.py 生成）。
extern const UINT32 RokEvergreenPfpUcode[1120];
extern const UINT32 RokEvergreenMeUcode[1376];
