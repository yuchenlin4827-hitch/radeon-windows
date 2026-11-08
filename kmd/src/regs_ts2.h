// SPDX-License-Identifier: MIT
// regs_ts2.h — TeraScale 2（Evergreen/NI 家族，含 Seymour 0x6760 / Caicos 0x677x）
// MMIO 寄存器映射与 PM4 常量 —— M3 硬件编程序列的单一事实来源。
//
// 偏移逐条对照 Linux 内核 radeon 驱动头文件（GPL-2.0，寄存器偏移为硬件事实，
// 仅作数值参考；本文件为自写定义，MIT）：
//   * r600d.h          —— CONFIG_*/GRBM/SRBM/CP_RB/SCRATCH/PM4 基础面
//   * evergreend.h     —— evergreen 家族差异位（GRBM_SOFT_RESET 位、CP_SEM_*）
//   * radeon_ucode.h   —— 微码尺寸常量（PFP 1120 / ME 1376 dwords）
//   * evergreen.c      —— 编程序列（cp_load_microcode / cp_resume / cp_start）
//
// 覆盖范围：M3 所需的全部寄存器偏移都 < 0x10000，落在 r600+ 的直接 MMIO
// 窗口内（Linux 对 pre-BONAIRE 用 BAR2 作为寄存器窗口，大小 256KB~512KB），
// 无需 MM_INDEX/MM_DATA 间接寻址（那是 >窗口 的寄存器才需要的路径）。
#pragma once

#include "rok_escape.h"     // ROK_U32（内核/用户态双模式）
#include "r600hw.h"         // GRBM 块真值（M3.1 精确表：ROK_GRBM_*）

// ------------------------------------------------------------------
// 总线/配置块（直接 MMIO）
// ------------------------------------------------------------------
#define ROK_REG_SRBM_STATUS                 0x0E50UL
#define ROK_REG_SRBM_STATUS2                0x0EC4UL  // evergreend.h 1179（r600 为 0x0E54）
#define ROK_REG_SRBM_SOFT_RESET             0x0E60UL
#define ROK_REG_MC_VM_FB_LOCATION           0x2180UL  // FB 定位：低16位=起始>>24，高16位=结束>>24
#define ROK_REG_MC_VM_SYSTEM_APERTURE_LOW   0x2190UL
#define ROK_REG_MC_VM_SYSTEM_APERTURE_HIGH  0x2194UL
#define ROK_REG_MC_VM_SYSTEM_APERTURE_DEFAULT 0x2198UL
#define ROK_REG_HDP_HOST_PATH_CNTL          0x2C00UL

#define ROK_REG_CONFIG_CNTL                 0x5424UL
#define ROK_REG_CONFIG_MEMSIZE              0x5428UL  // VRAM 大小，KB（r600.c: RREG32(CONFIG_MEMSIZE)*1024）

// ------------------------------------------------------------------
// GRBM（图形寄存器总线管理）
// ------------------------------------------------------------------
#define ROK_REG_GRBM_CNTL                   0x8000UL
// 偏移与状态位别名到 r600hw.h 的 M3.1 精确表（单一真值，防漂移）：
#define ROK_REG_GRBM_STATUS                 ROK_GRBM_STATUS
#define ROK_REG_GRBM_STATUS2                ROK_GRBM_STATUS2
#define ROK_REG_GRBM_SOFT_RESET             ROK_GRBM_SOFT_RESET
#define ROK_GRBM_STATUS_GUI_ACTIVE          ROK_GRBM_GUI_ACTIVE
#define ROK_GRBM_STATUS_CMDFIFO_AVAIL_MASK  ROK_GRBM_CMDFIFO_AVAIL_MASK

// GRBM_SOFT_RESET 位（evergreend.h 829-840；evergreen_cp_resume 使用
// CP|PA|SH|VGT|SPI|SX 的组合；CP 位别名 r600hw 的 ROK_GRBM_SOFT_RESET_CP）。
#define ROK_SOFT_RESET_CP                   ROK_GRBM_SOFT_RESET_CP
#define ROK_SOFT_RESET_CB                   (1UL << 1)
#define ROK_SOFT_RESET_DB                   (1UL << 3)
#define ROK_SOFT_RESET_PA                   (1UL << 5)
#define ROK_SOFT_RESET_SPI                  (1UL << 8)
#define ROK_SOFT_RESET_SH                   (1UL << 9)
#define ROK_SOFT_RESET_SX                   (1UL << 10)
#define ROK_SOFT_RESET_TC                   (1UL << 11)
#define ROK_SOFT_RESET_VC                   (1UL << 13)
#define ROK_SOFT_RESET_VGT                  (1UL << 14)
// evergreen_cp_resume 的复位掩码：CP+PA+SH+VGT+SPI+SX = 0x00004721。
#define ROK_SOFT_RESET_EVERGREEN_CP_MASK    (ROK_SOFT_RESET_CP | ROK_SOFT_RESET_PA | \
                                             ROK_SOFT_RESET_SH | ROK_SOFT_RESET_VGT | \
                                             ROK_SOFT_RESET_SPI | ROK_SOFT_RESET_SX)

// ------------------------------------------------------------------
// SRBM_SOFT_RESET 位（r600d.h 1840-1850，按需子集）
// ------------------------------------------------------------------
#define ROK_SRBM_SOFT_RESET_GRBM            (1UL << 8)
#define ROK_SRBM_SOFT_RESET_HDP             (1UL << 9)
#define ROK_SRBM_SOFT_RESET_IH              (1UL << 10)
#define ROK_SRBM_SOFT_RESET_MC              (1UL << 11)
#define ROK_SRBM_SOFT_RESET_RLC             (1UL << 13)

// ------------------------------------------------------------------
// Scratch（fence 观测用；CP 经 SET_CONFIG_REG 写入）
// ------------------------------------------------------------------
#define ROK_REG_SCRATCH_REG0                0x8500UL
#define ROK_REG_SCRATCH_REG6                0x8518UL
#define ROK_REG_SCRATCH_REG7                0x851CUL
#define ROK_REG_SCRATCH_UMSK                0x8540UL
#define ROK_REG_SCRATCH_ADDR                0x8544UL

// ------------------------------------------------------------------
// CP 状态/控制
// ------------------------------------------------------------------
#define ROK_REG_CP_STALLED_STAT1            0x8674UL
#define ROK_REG_CP_STALLED_STAT2            0x8678UL
#define ROK_REG_CP_BUSY_STAT                0x867CUL
#define ROK_REG_CP_STAT                     0x8680UL
#define ROK_REG_CP_SEM_WAIT_TIMER           0x85BCUL
#define ROK_REG_CP_SEM_INCOMPLETE_TIMER_CNTL 0x85C8UL // evergreend.h 492
#define ROK_REG_CP_ME_CNTL                  0x86D8UL

// CP_ME_CNTL（r600d.h R_0086D8）：停机位 26=PFP_HALT，28=ME_HALT。
// 解除停机 = 写 0xFF（r600.c r600_cp_start / evergreen_cp_start 同款）。
#define ROK_CP_ME_PFP_HALT                  (1UL << 26)
#define ROK_CP_ME_ME_HALT                   (1UL << 28)
#define ROK_CP_ME_CNTL_HALT                 (ROK_CP_ME_PFP_HALT | ROK_CP_ME_ME_HALT)
#define ROK_CP_ME_CNTL_UNHALT               0xFFUL

// ------------------------------------------------------------------
// CP ring 寄存器（evergreen_cp_resume 序列）
// ------------------------------------------------------------------
#define ROK_REG_CP_RB_BASE                  0xC100UL  // 环总线地址 >> 8
#define ROK_REG_CP_RB_CNTL                  0xC104UL
#define ROK_REG_CP_RB_RPTR_WR               0xC108UL  // RPTR_WR_ENA 期间的软件写指针
#define ROK_REG_CP_RB_RPTR                  0x8700UL  // GRBM 域读指针（NO_UPDATE 时轮询它）
#define ROK_REG_CP_RB_RPTR_ADDR             0xC10CUL
#define ROK_REG_CP_RB_RPTR_ADDR_HI          0xC110UL
#define ROK_REG_CP_RB_WPTR                  0xC114UL
#define ROK_REG_CP_RB_WPTR_DELAY            0x8704UL
#define ROK_REG_CP_PFP_UCODE_ADDR           0xC150UL
#define ROK_REG_CP_PFP_UCODE_DATA           0xC154UL
#define ROK_REG_CP_ME_RAM_RADDR             0xC158UL
#define ROK_REG_CP_ME_RAM_WADDR             0xC15CUL
#define ROK_REG_CP_ME_RAM_DATA              0xC160UL
#define ROK_REG_CP_INT_CNTL                 0xC124UL
#define ROK_REG_CP_DEBUG                    0xC1FCUL

// CP_RB_CNTL 字段（r600d.h 194-199）。
#define ROK_RB_BUFSZ(x)                     (((ROK_U32)(x)) << 0)
#define ROK_RB_BLKSZ(x)                     (((ROK_U32)(x)) << 8)
#define ROK_RB_BUF_SWAP_32BIT               (2UL << 16)
#define ROK_RB_NO_UPDATE                    (1UL << 27)
#define ROK_RB_RPTR_WR_ENA                  (1UL << 31)

// ------------------------------------------------------------------
// PM4 type-3 常量（r600d.h 1584-1690 / evergreend.h 1548-1665）
// ------------------------------------------------------------------
// 包头：type3(位31:30=3) | (count<<16) | (opcode<<8)。
// count 字段 = 包体 dword 数 - 1（Linux 内核 r600_ring_test 的
// SET_CONFIG_REG count=1 + 2 个包体、ME_INITIALIZE count=5 + 6 个包体、
// INDIRECT_BUFFER count=2 + 3 个包体、EVENT_WRITE_EOP count=4 + 5 个包体，
// 四处独立证据一致；M3.4a 同时修正了 sim.c 解码器的同类 off-by-one）。
#define ROK_PM4_TYPE3(op, body_dwords)      ((0x3UL << 30) | \
                                             ((((ROK_U32)(body_dwords)) - 1UL) & 0x3FFFUL) << 16 | \
                                             ((((ROK_U32)(op)) & 0xFFUL) << 8))
#define ROK_PM4_TYPE3_OPCODE(word)          (((word) >> 8) & 0xFFUL)
#define ROK_PM4_TYPE3_COUNT(word)           ((((word) >> 16) & 0x3FFFUL) + 1UL)  // 包体 dword 数

#define ROK_PM4_NOP                         0x10UL
#define ROK_PM4_INDIRECT_BUFFER             0x32UL
#define ROK_PM4_SURFACE_SYNC                0x43UL
#define ROK_PM4_ME_INITIALIZE               0x44UL
#define ROK_PM4_EVENT_WRITE                 0x46UL
#define ROK_PM4_EVENT_WRITE_EOP             0x47UL
#define ROK_PM4_SET_CONFIG_REG              0x68UL

#define ROK_PM4_ME_INITIALIZE_DEVICE_ID(x)  (((ROK_U32)(x)) << 16)
#define ROK_PM4_SET_CONFIG_REG_OFFSET       0x00008000UL

// ------------------------------------------------------------------
// 微码尺寸（radeon_ucode.h 31-32；CAICOS_pfp/me.bin 实测一致）
// ------------------------------------------------------------------
#define ROK_EVERGREEN_PFP_UCODE_DWORDS      1120UL
#define ROK_EVERGREEN_PM4_UCODE_DWORDS      1376UL
