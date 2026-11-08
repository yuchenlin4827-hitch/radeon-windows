// SPDX-License-Identifier: MIT
// sim.h — rokmd 模拟设备（simulated device）后端
//
// 目标：在没有 TeraScale 真实硬件的环境（QEMU/Hyper-V/VirtualBox/VMware
// 虚拟机）里跑通 KMD 的完整 WDDM 2.0 管线（AddDevice → StartDevice →
// 分配 → 提交 → fence 回收 → escape 查询），并在提交路径上预演 M3 的
// PM4 命令流解码。
//
// 三种进入方式（按优先级）：
//   1. 设备 ID 命中模拟设备表（INF 的 SIM 模型行，如 QEMU ati-vga）；
//   2. 驱动键 <DeviceRegistryPath>\Parameters\SimulatedDevice = 1
//      （或驱动键根下同名值）强制开启；
//   3. 自动判定：检测到 hypervisor 且适配器不是 AMD（VID != 0x1002）。
//
// 开启后（M2.6 阶段的实际效果）：
//   * BAR0 缺失不再告警 —— 分配 64KB NonPagedPool 作为"模拟寄存器空间"，
//     M3 寄存器访问代码可先指向该缓冲（读写皆内存，无硬件副作用）；
//   * 每次提交的 DMA buffer 做 PM4 包解码（type-0/2/3 遍历 + opcode 直方图），
//     INDIRECT_BUFFER 等关键字打印 —— 这是 M3 ring 提交前的解码器前哨；
//   * escape 通道新增 ROK_ESC_TYPE_QUERY_SIM_STATUS，gpu_probe 可核对。
#pragma once

#include "precomp.h"

// 前向声明（避免 adapter.h ⇄ sim.h 循环包含）。
struct _ROK_ADAPTER;

// ---- 模拟设备 PCI 设备 ID 表 ----
// QEMU `-device ati-vga` 呈现 VID 0x1002 / DID 0x5046（Rage 128 PF 形态，
// 非 TeraScale —— 正因如此不会与真实 TeraScale 卡冲突，是首选模拟目标）。
// 其余为常见虚拟机 VGA 设备（选装 rokmd 时注意：会接管 VM 显示设备，
// render-only 驱动无显示输出 —— 请用 RDP/串口控制 VM，见 docs/simulated-device.md）。
#define ROK_SIM_QEMU_ATI_VGA    0x5046  // 1002:5046 — QEMU ati-vga（首选）
#define ROK_SIM_QEMU_BOCHS_VGA  0x1111  // 1234:1111 — QEMU/Bochs std VGA
#define ROK_SIM_VBOX_VGA        0xBEEF  // 80EE:BEEF — VirtualBox VGA
#define ROK_SIM_VMWARE_SVGA     0x0405  // 15AD:0405 — VMware SVGA II
#define ROK_SIM_HYPERV_VIDEO    0x5353  // 1414:5353 — Hyper-V Video（Gen1）

BOOLEAN RokSimIsSimulatedDeviceId(_In_ ULONG DeviceId);

// 共享注册表 DWORD 读取（sim 判定与 M3.4a EnableRing 开关共用）：
// 读 <RootPath>[\<SubKey>]\ValueName；host 测试经 mock 注册表注入。
BOOLEAN RokRegReadUlongValue(
    _In_ PUNICODE_STRING RootPath,
    _In_opt_ PCWSTR SubKey,
    _In_ PCWSTR ValueName,
    _Out_ PULONG OutValue);

// CPUID 探测 hypervisor（leaf 1 ECX bit 31 + leaf 0x40000000 厂商串）。
BOOLEAN RokSimHypervisorPresent(VOID);

// StartDevice 阶段调用：决定是否进入模拟模式并初始化模拟寄存器空间。
// DeviceRegistryPath 为 DXGK_DEVICE_INFO.DeviceRegistryPath（驱动键）。
NTSTATUS RokSimInitialize(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _In_opt_ PUNICODE_STRING DeviceRegistryPath);

// StopDevice/RemoveDevice 阶段调用：释放模拟寄存器空间。
VOID RokSimShutdown(_Inout_ struct _ROK_ADAPTER *Adapter);

// 提交路径（DISPATCH_LEVEL）：解析 PM4 包并累计直方图。无池分配。
VOID RokSimObserveDma(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _In_reads_bytes_(SizeBytes) CONST VOID *DmaBuffer,
    _In_ UINT SizeBytes);

BOOLEAN RokSimEnabled(_In_ struct _ROK_ADAPTER *Adapter);

// ---- PM4 解码统计（escape 查询回传给 gpu_probe / ICD）----
typedef struct _ROK_SIM_STATS
{
    ULONG   PacketCount;            // type-3 包总数
    ULONG   Type2Count;             // type-2 filler（0x80000000）个数
    ULONG   Type0Count;             // type-0 寄存器写包头总数
    ULONG   UnknownOpcodes;         // 未识别的 type-3 opcode 个数
    UINT32  LastIndirectBaseLo;     // 最近 INDIRECT_BUFFER 基址低 32 位（原始 dword）
    UINT32  LastIndirectSizeDwords; // 最近 INDIRECT_BUFFER 大小（dword，原始 dword）
    UINT32  LastFenceSubmitted;     // 最近提交的 fence id（SubmitCommand 侧观察）
    ULONG   OpcodeCounts[64];       // type-3 opcode 直方图（opcode & 0x3F 槽位）
} ROK_SIM_STATS, *PROK_SIM_STATS;
