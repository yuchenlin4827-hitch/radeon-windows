// SPDX-License-Identifier: MIT
// ring.h — GFX ring 状态与 PM4 包构造（M3.4a）
//
// 语义对照 Linux radeon r600/evergreen（GPL-2.0 序列参考，实现自写 MIT）：
//   * ring 以 dword 计，容量为 2 的幂：dwords = 2 << BufszLog2
//     （= 8 << BufszLog2 字节；Linux r600_ring_init 的
//     rb_bufsz = order_base_2(ring_size/8) 同一取值域）；
//   * 本地写指针 WptrDwords 回绕于 SizeDwords，写入 CP_RB_WPTR 寄存器；
//   * M3.4a 的 ring 是"初始化 + 观测"通道（ME_INIT + ring test +
//     escape 状态回传），WDDM 提交路径仍走即时完成模型 —— 真实提交
//     在 M3.4b/M3.6 与 VidMm 对接后切换。
#pragma once

#include "precomp.h"
#include "rok_escape.h"
#include "regs_ts2.h"

// 前向声明（避免 adapter.h ⇄ ring.h 循环包含）。
struct _ROK_ADAPTER;

typedef struct _ROK_RING
{
    volatile ROK_U32 *CpuVa;      // ring 内存 CPU 侧地址（VRAM 孔径映射 / 演练池）
    UINT64            GpuAddr;    // 总线地址（CP_RB_BASE 写入 GpuAddr >> 8）
    ULONG             SizeDwords; // 环容量（dword，2 的幂，>= 16）
    ULONG             BufszLog2;  // RB_BUFSZ 字段值（SizeDwords = 2 << BufszLog2）
    ULONG             BlkszLog2;  // RB_BLKSZ 字段值（恒 9 = 4KB 粒度，Linux 同款）
    ULONG             WptrDwords; // 本地写指针（dword 偏移，回绕）
    ULONG             WptrTotal;  // 累计写入 dword（观测用，不回绕）
    ULONG             RptrDwords; // 最近一次观测的 CP_RB_RPTR
    BOOLEAN           Ready;      // ME_INIT 已写入且未停机
    BOOLEAN           HwProgrammed; // 寄存器序列已执行过（演练/真实）
} ROK_RING, *PROK_RING;

// ---- 环容量数学（纯函数，host 测试覆盖）----
// 由字节数取 RB_BUFSZ 值：向上取到 2 的幂（order_base_2(bytes/8)）。
ULONG RokRingBufszFromBytes(_In_ ULONG SizeBytes);
// 由 RB_BUFSZ 值还原容量：字节 = 8 << BufszLog2，dword = 2 << BufszLog2。
ULONG RokRingBytesFromBufsz(_In_ ULONG BufszLog2);
ULONG RokRingDwordsFromBufsz(_In_ ULONG BufszLog2);

// ---- PM4 构造（纯函数，host 测试覆盖）----
// type-3 包头；BodyDwords = 包体 dword 数（不含包头，>= 1）。
ROK_U32 RokPm4Type3(_In_ ROK_U32 Opcode, _In_ ROK_U32 BodyDwords);

// ---- 环写入（处理回绕；不触碰硬件 —— WPTR 寄存器由提交方写）----
VOID RokRingWriteU32(_Inout_ ROK_RING *Ring, _In_ ROK_U32 Value);

// ME_INITIALIZE（evergreen_cp_start 最小化：7 dwords 总长）。
VOID RokRingWriteMeInitialize(_Inout_ ROK_RING *Ring, _In_ ROK_U32 MaxHwContexts);

// SET_CONFIG_REG 寄存器写包（scratch 观测 / ring test 用）。
// RegOffset 必须落在 [SET_CONFIG_REG_OFFSET, SET_CONFIG_REG_OFFSET+0x8000) 窗口。
VOID RokRingWriteSetConfigReg(_Inout_ ROK_RING *Ring,
                              _In_ ROK_U32 RegOffset,
                              _In_ ROK_U32 Value);
