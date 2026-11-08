// SPDX-License-Identifier: MIT
// ring.c — GFX ring 数学与 PM4 包构造实现（M3.4a）
//
// 全部为纯逻辑（无内核服务依赖），host_tests/test_m3.c 直接测试本文件字节。
#include "precomp.h"
#include "ring.h"

// order_base_2：向上取 log2（Linux 内核同名语义）。
static ULONG
RokOrderBase2(
    _In_ ULONG Value
    )
{
    ULONG log2 = 0;

    while ((1UL << log2) < Value)
    {
        log2++;
    }
    return log2;
}

ULONG
RokRingBufszFromBytes(
    _In_ ULONG SizeBytes
    )
{
    // Linux r600_ring_init：rb_bufsz = order_base_2(ring_size / 8)，ring_size
    // 以字节计（1MB → 17）。下限钳到 8 字节（BUFSZ=0 → 2 dwords）。
    ULONG dwords = SizeBytes / 8;

    if (dwords < 1)
    {
        dwords = 1;
    }
    return RokOrderBase2(dwords);
}

ULONG
RokRingBytesFromBufsz(
    _In_ ULONG BufszLog2
    )
{
    return (8UL << BufszLog2);
}

ULONG
RokRingDwordsFromBufsz(
    _In_ ULONG BufszLog2
    )
{
    return (2UL << BufszLog2);
}

ROK_U32
RokPm4Type3(
    _In_ ROK_U32 Opcode,
    _In_ ROK_U32 BodyDwords
    )
{
    // count 字段 = 包体 dword 数 - 1（详见 regs_ts2.h 头注的四点证据）。
    return ROK_PM4_TYPE3(Opcode, BodyDwords);
}

VOID
RokRingWriteU32(
    _Inout_ ROK_RING *Ring,
    _In_ ROK_U32 Value
    )
{
    if (Ring == NULL || Ring->CpuVa == NULL || Ring->SizeDwords == 0)
    {
        return;
    }

    Ring->CpuVa[Ring->WptrDwords] = Value;
    Ring->WptrDwords = (Ring->WptrDwords + 1) & (Ring->SizeDwords - 1);
    Ring->WptrTotal++;
}

VOID
RokRingWriteMeInitialize(
    _Inout_ ROK_RING *Ring,
    _In_ ROK_U32 MaxHwContexts
    )
{
    // evergreen_cp_start（evergreen.c 2989-2998）：
    //   PACKET3(ME_INITIALIZE, 5), 0x1, 0x0, max_hw_contexts-1,
    //   DEVICE_ID(1), 0, 0
    if (MaxHwContexts == 0)
    {
        MaxHwContexts = 1;
    }

    RokRingWriteU32(Ring, RokPm4Type3(ROK_PM4_ME_INITIALIZE, 6));
    RokRingWriteU32(Ring, 0x1);
    RokRingWriteU32(Ring, 0x0);
    RokRingWriteU32(Ring, MaxHwContexts - 1);
    RokRingWriteU32(Ring, ROK_PM4_ME_INITIALIZE_DEVICE_ID(1));
    RokRingWriteU32(Ring, 0);
    RokRingWriteU32(Ring, 0);
}

VOID
RokRingWriteSetConfigReg(
    _Inout_ ROK_RING *Ring,
    _In_ ROK_U32 RegOffset,
    _In_ ROK_U32 Value
    )
{
    // r600_ring_test（r600.c 2842-2844）：
    //   PACKET3(SET_CONFIG_REG, 1), (reg - 0x8000) >> 2, value
    if (RegOffset < ROK_PM4_SET_CONFIG_REG_OFFSET ||
        RegOffset >= ROK_PM4_SET_CONFIG_REG_OFFSET + 0x8000UL)
    {
        // 窗口外寄存器不属于 config 空间，拒绝编码（调用方日志）。
        return;
    }

    RokRingWriteU32(Ring, RokPm4Type3(ROK_PM4_SET_CONFIG_REG, 2));
    RokRingWriteU32(Ring, (RegOffset - ROK_PM4_SET_CONFIG_REG_OFFSET) >> 2);
    RokRingWriteU32(Ring, Value);
}
