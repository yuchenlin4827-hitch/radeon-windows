// SPDX-License-Identifier: MIT
// r600hw.h — TeraScale 2 硬件识别 + BAR0 直访 + GRBM 域基础序列
//            （M3.1 寄存器访问封装 / M3.2 时钟复位第一批）
//
// 事实来源（只取地址与位域，不复制实现）：
//   * GRBM 寄存器地址/位域：Linux radeon r600d.h（v6.6）——GRBM_STATUS
//     0x8010、GRBM_STATUS2 0x8014、GRBM_SOFT_RESET 0x8020、SOFT_RESET_CP
//     1<<0、GUI_ACTIVE 1<<31、CMDFIFO_AVAIL_MASK 0x1F；
//   * PCI 设备 ID 表：Mesa include/pci_ids/r600_pci_ids.h（MIT），与
//     umd/gl46/gl46_r600g.c 同源；一致性由 scripts/check_pci_tables.py
//     机械校验。
//
// 范围说明：
//   * 本批只做 BAR0 直访（GRBM 域全部可直访）。MM_INDEX/MM_DATA 间接
//     寻址推迟到 3.3（VRAM/GTT）批次——其总线空间语义尚待权威出处核实，
//     不把未经核实的常量写进内核；
//   * 所有寄存器访问带边界检查：MmioVa 为 NULL 或 Offset+4 越界时返回
//     FALSE 且绝不触碰总线（M2"不蓝屏"不变量的延伸）。
#pragma once

#include "precomp.h"
#include "rok_escape.h"   // ROK_GPU_FAMILY_*（ABI 值，查询回传共用）

// 前向声明（避免 adapter.h ⇄ r600hw.h 循环包含）。
struct _ROK_ADAPTER;

// ---- 芯片枚举（Evergreen 家族 = TeraScale 2 精确表）----
// Mesa r600_pci_ids.h 无 SEYMOUR 组：0x6760-0x677B 全归 CAICOS
// （HD 6490M = 0x6766 亦在其中）；0x6840-0x6859 为 TURKS。
#define ROK_HW_CHIP_UNKNOWN     0
#define ROK_HW_CHIP_CEDAR       1   // HD 54xx
#define ROK_HW_CHIP_REDWOOD     2   // HD 56xx
#define ROK_HW_CHIP_JUNIPER     3   // HD 57xx
#define ROK_HW_CHIP_CYPRESS     4   // HD 58xx
#define ROK_HW_CHIP_HEMLOCK     5   // HD 59xx (CF)
#define ROK_HW_CHIP_BARTS       6   // HD 68xx
#define ROK_HW_CHIP_TURKS       7   // HD 67xx/76xxM
#define ROK_HW_CHIP_CAICOS      8   // HD 64xx-74xx（含 0x6779 主验证卡）
#define ROK_HW_CHIP_PALM        9   // Brazos APU
#define ROK_HW_CHIP_SUMO        10  // Llano APU
#define ROK_HW_CHIP_SUMO2       11  // Llano APU (增强)

// 芯片识别结果（StartDevice 快照进 ROK_ADAPTER；escape 回传给探针/ICD）。
typedef struct _ROK_HW_CHIP_INFO
{
    ULONG Family;               // ROK_GPU_FAMILY_*（与 rok_escape.h ABI 值一致）
    ULONG Chip;                 // ROK_HW_CHIP_*（未收录 = ROK_HW_CHIP_UNKNOWN）
    CHAR  Name[12];             // "CAICOS" 等，NUL 结尾；未知 = "UNKNOWN"
} ROK_HW_CHIP_INFO, *PROK_HW_CHIP_INFO;

// 精确查表（120 个 TeraScale 2 DID，源：Mesa r600_pci_ids.h）。
// 命中返回 TRUE 并填充 Info（Family 恒为 ROK_GPU_FAMILY_TS2）；
// 未命中返回 FALSE（调用方可回退 RokHwFamilyHeuristic）。
BOOLEAN RokHwLookupChip(_In_ ULONG DeviceId, _Out_ ROK_HW_CHIP_INFO *Info);

// 芯片枚举 → 短名（"CAICOS"/"TURKS"/…；UNKNOWN → "UNKNOWN"）。日志与
// escape 回传用；返回静态只读串，无分配。
const CHAR *RokHwChipName(_In_ ULONG Chip);

// 兜底启发式（r600_pci_ids.h 未收录或超表的 DID）：按 ID 段推断家族。
// 返回 ROK_GPU_FAMILY_TS1/2/3。与 rok_escape.h 中原启发式同规则。
ULONG RokHwFamilyHeuristic(_In_ ULONG DeviceId);

// StartDevice 后段调用：查表 + 日志 + （BAR0 已映射时）快照 GRBM_STATUS。
// 结果存 Adapter->HwChip 与 Adapter->HwGrbmStatus；无硬件副作用。
VOID RokHwIdentify(_Inout_ struct _ROK_ADAPTER *Adapter);

// ---- BAR0 直访（边界检查）----
// MmioVa 为 NULL、Offset 未按 4 对齐或 Offset+4 > MmioLength → FALSE。
BOOLEAN RokRegRead32(
    _In_opt_ PVOID MmioVa,
    _In_ ULONG MmioLength,
    _In_ ULONG Offset,
    _Out_ ULONG *Value);

BOOLEAN RokRegWrite32(
    _In_opt_ PVOID MmioVa,
    _In_ ULONG MmioLength,
    _In_ ULONG Offset,
    _In_ ULONG Value);

// ---- GRBM 域常量（地址/位域事实，见文件头出处）----
#define ROK_GRBM_STATUS              0x00008010UL
#define ROK_GRBM_STATUS2             0x00008014UL
#define ROK_GRBM_SOFT_RESET          0x00008020UL
#define ROK_GRBM_GUI_ACTIVE          0x80000000UL  // 1 = 图形域忙
#define ROK_GRBM_CMDFIFO_AVAIL_MASK  0x0000001FUL  // 命令 FIFO 空槽数
#define ROK_GRBM_SOFT_RESET_CP       0x00000001UL  // 软复位位：CP

// 微秒停顿。内核 = KeStallExecutionProcessor；宿主测试由 kmock 注入
// （计数不实际等待，便于断言轮询行为）。
VOID RokStallUs(_In_ ULONG Microseconds);

// 轮询 GRBM_STATUS 直到 GUI_ACTIVE 清零或达到 MaxPolls。
// 每轮间隔 RokStallUs(10)。空闲返回 TRUE；*StallsUsed 记录实际轮询数
// （即时空闲 = 0）。MaxPolls == 0 视为参数错误返回 FALSE。
BOOLEAN RokGrbmWaitIdle(
    _In_opt_ PVOID MmioVa,
    _In_ ULONG MmioLength,
    _In_ ULONG MaxPolls,
    _Out_opt_ ULONG *StallsUsed);

// CP 域软复位（对照内核 r600_gpu_reset 的 CP 段）：
//   读状态（可读性探测）→ 写 SOFT_RESET_CP → 停顿 100µs → 读回（写冲刷）
//   → 清零 → WaitIdle(10000 轮 × 10µs 上限)。
// 返回 FALSE 的情况：BAR0 不可用 / 序列中任一步失败 / 超时未空闲。
BOOLEAN RokGrbmSoftResetCp(
    _In_opt_ PVOID MmioVa,
    _In_ ULONG MmioLength,
    _Out_opt_ ULONG *StatusBefore,
    _Out_opt_ ULONG *StatusAfter);
