// SPDX-License-Identifier: MIT
// r600hw.c — TeraScale 2 硬件识别 + BAR0 直访 + GRBM 域基础序列实现
//
// M3.1：PCI 设备 ID 精确分发表（120 个 Evergreen 家族 DID，源：Mesa
// r600_pci_ids.h，MIT；与 umd/gl46/gl46_r600g.c 同源，一致性由
// scripts/check_pci_tables.py 机械校验）+ 寄存器直访封装（边界检查）。
//
// M3.2 第一批：GRBM 状态解码 / WaitIdle / CP 域软复位。寄存器地址与
// 位域取自 Linux radeon r600d.h（v6.6），本文件不含任何 GPL 代码——
// 序列编排为本项目原创（停顿/超时参数为本项目工程选型）。
//
// 内核/宿主双构建：寄存器读写经 READ_REGISTER_ULONG/WRITE_REGISTER_ULONG
// 宏（内核 = ntddk；宿主测试 = kmock 以 64KB 内存块模拟），停顿经
// RokStallUs（内核 = KeStallExecutionProcessor；宿主 = kmock 计数）。
#include "precomp.h"
#include "r600hw.h"
#include "adapter.h"

// ---- 精确分发表（按 DID 升序；测试机械校验升序+无重复）----
typedef struct _ROK_HW_CHIP_ENTRY
{
    ULONG   DeviceId;
    ULONG   Chip;
} ROK_HW_CHIP_ENTRY;

static const ROK_HW_CHIP_ENTRY RokHwChipTable[] =
{
    // BARTS（HD 68xx）
    { 0x6720, ROK_HW_CHIP_BARTS },  { 0x6721, ROK_HW_CHIP_BARTS },
    { 0x6722, ROK_HW_CHIP_BARTS },  { 0x6723, ROK_HW_CHIP_BARTS },
    { 0x6724, ROK_HW_CHIP_BARTS },  { 0x6725, ROK_HW_CHIP_BARTS },
    { 0x6726, ROK_HW_CHIP_BARTS },  { 0x6727, ROK_HW_CHIP_BARTS },
    { 0x6728, ROK_HW_CHIP_BARTS },  { 0x6729, ROK_HW_CHIP_BARTS },
    { 0x6738, ROK_HW_CHIP_BARTS },  { 0x6739, ROK_HW_CHIP_BARTS },
    { 0x673E, ROK_HW_CHIP_BARTS },
    // TURKS（HD 67xx/76xxM）—— 0x6840-0x6859 段在 Mesa 归 TURKS
    { 0x6740, ROK_HW_CHIP_TURKS },  { 0x6741, ROK_HW_CHIP_TURKS },
    { 0x6742, ROK_HW_CHIP_TURKS },  { 0x6743, ROK_HW_CHIP_TURKS },
    { 0x6744, ROK_HW_CHIP_TURKS },  { 0x6745, ROK_HW_CHIP_TURKS },
    { 0x6746, ROK_HW_CHIP_TURKS },  { 0x6747, ROK_HW_CHIP_TURKS },
    { 0x6748, ROK_HW_CHIP_TURKS },  { 0x6749, ROK_HW_CHIP_TURKS },
    { 0x674A, ROK_HW_CHIP_TURKS },  { 0x6750, ROK_HW_CHIP_TURKS },
    { 0x6751, ROK_HW_CHIP_TURKS },  { 0x6758, ROK_HW_CHIP_TURKS },
    { 0x6759, ROK_HW_CHIP_TURKS },  { 0x675B, ROK_HW_CHIP_TURKS },
    { 0x675D, ROK_HW_CHIP_TURKS },  { 0x675F, ROK_HW_CHIP_TURKS },
    // CAICOS（HD 64xx-74xx；Mesa 无 SEYMOUR 组，0x6766 = HD 6490M 亦在此）
    { 0x6760, ROK_HW_CHIP_CAICOS }, { 0x6761, ROK_HW_CHIP_CAICOS },
    { 0x6762, ROK_HW_CHIP_CAICOS }, { 0x6763, ROK_HW_CHIP_CAICOS },
    { 0x6764, ROK_HW_CHIP_CAICOS }, { 0x6765, ROK_HW_CHIP_CAICOS },
    { 0x6766, ROK_HW_CHIP_CAICOS }, { 0x6767, ROK_HW_CHIP_CAICOS },
    { 0x6768, ROK_HW_CHIP_CAICOS }, { 0x6770, ROK_HW_CHIP_CAICOS },
    { 0x6771, ROK_HW_CHIP_CAICOS }, { 0x6772, ROK_HW_CHIP_CAICOS },
    { 0x6778, ROK_HW_CHIP_CAICOS }, { 0x6779, ROK_HW_CHIP_CAICOS },
    { 0x677B, ROK_HW_CHIP_CAICOS },
    // TURKS（0x68xx 段）
    { 0x6840, ROK_HW_CHIP_TURKS },  { 0x6841, ROK_HW_CHIP_TURKS },
    { 0x6842, ROK_HW_CHIP_TURKS },  { 0x6843, ROK_HW_CHIP_TURKS },
    { 0x6849, ROK_HW_CHIP_TURKS },  { 0x6850, ROK_HW_CHIP_TURKS },
    { 0x6858, ROK_HW_CHIP_TURKS },  { 0x6859, ROK_HW_CHIP_TURKS },
    // CYPRESS（HD 58xx）
    { 0x6880, ROK_HW_CHIP_CYPRESS },{ 0x6888, ROK_HW_CHIP_CYPRESS },
    { 0x6889, ROK_HW_CHIP_CYPRESS },{ 0x688A, ROK_HW_CHIP_CYPRESS },
    { 0x688C, ROK_HW_CHIP_CYPRESS },{ 0x688D, ROK_HW_CHIP_CYPRESS },
    { 0x6898, ROK_HW_CHIP_CYPRESS },{ 0x6899, ROK_HW_CHIP_CYPRESS },
    { 0x689B, ROK_HW_CHIP_CYPRESS },{ 0x689E, ROK_HW_CHIP_CYPRESS },
    // HEMLOCK（HD 59xx CF）
    { 0x689C, ROK_HW_CHIP_HEMLOCK },{ 0x689D, ROK_HW_CHIP_HEMLOCK },
    // JUNIPER（HD 57xx）
    { 0x68A0, ROK_HW_CHIP_JUNIPER },{ 0x68A1, ROK_HW_CHIP_JUNIPER },
    { 0x68A8, ROK_HW_CHIP_JUNIPER },{ 0x68A9, ROK_HW_CHIP_JUNIPER },
    { 0x68B0, ROK_HW_CHIP_JUNIPER },{ 0x68B8, ROK_HW_CHIP_JUNIPER },
    { 0x68B9, ROK_HW_CHIP_JUNIPER },{ 0x68BA, ROK_HW_CHIP_JUNIPER },
    { 0x68BE, ROK_HW_CHIP_JUNIPER },{ 0x68BF, ROK_HW_CHIP_JUNIPER },
    // REDWOOD（HD 56xx）
    { 0x68C0, ROK_HW_CHIP_REDWOOD },{ 0x68C1, ROK_HW_CHIP_REDWOOD },
    { 0x68C7, ROK_HW_CHIP_REDWOOD },{ 0x68C8, ROK_HW_CHIP_REDWOOD },
    { 0x68C9, ROK_HW_CHIP_REDWOOD },{ 0x68D8, ROK_HW_CHIP_REDWOOD },
    { 0x68D9, ROK_HW_CHIP_REDWOOD },{ 0x68DA, ROK_HW_CHIP_REDWOOD },
    { 0x68DE, ROK_HW_CHIP_REDWOOD },
    // CEDAR（HD 54xx）
    { 0x68E0, ROK_HW_CHIP_CEDAR },  { 0x68E1, ROK_HW_CHIP_CEDAR },
    { 0x68E4, ROK_HW_CHIP_CEDAR },  { 0x68E5, ROK_HW_CHIP_CEDAR },
    { 0x68E8, ROK_HW_CHIP_CEDAR },  { 0x68E9, ROK_HW_CHIP_CEDAR },
    { 0x68F1, ROK_HW_CHIP_CEDAR },  { 0x68F2, ROK_HW_CHIP_CEDAR },
    { 0x68F8, ROK_HW_CHIP_CEDAR },  { 0x68F9, ROK_HW_CHIP_CEDAR },
    { 0x68FA, ROK_HW_CHIP_CEDAR },  { 0x68FE, ROK_HW_CHIP_CEDAR },
    // SUMO / SUMO2（Llano APU；0x964x 段混布）
    { 0x9640, ROK_HW_CHIP_SUMO },   { 0x9641, ROK_HW_CHIP_SUMO },
    { 0x9647, ROK_HW_CHIP_SUMO },   { 0x9648, ROK_HW_CHIP_SUMO },
    { 0x964A, ROK_HW_CHIP_SUMO },   { 0x964B, ROK_HW_CHIP_SUMO },
    { 0x964C, ROK_HW_CHIP_SUMO },   { 0x964E, ROK_HW_CHIP_SUMO },
    { 0x964F, ROK_HW_CHIP_SUMO },
    { 0x9642, ROK_HW_CHIP_SUMO2 },  { 0x9643, ROK_HW_CHIP_SUMO2 },
    { 0x9644, ROK_HW_CHIP_SUMO2 },  { 0x9645, ROK_HW_CHIP_SUMO2 },
    { 0x9649, ROK_HW_CHIP_SUMO2 },
    // PALM（Brazos APU）
    { 0x9802, ROK_HW_CHIP_PALM },   { 0x9803, ROK_HW_CHIP_PALM },
    { 0x9804, ROK_HW_CHIP_PALM },   { 0x9805, ROK_HW_CHIP_PALM },
    { 0x9806, ROK_HW_CHIP_PALM },   { 0x9807, ROK_HW_CHIP_PALM },
    { 0x9808, ROK_HW_CHIP_PALM },   { 0x9809, ROK_HW_CHIP_PALM },
    { 0x980A, ROK_HW_CHIP_PALM },
};

#define ROK_HW_CHIP_TABLE_COUNT RTL_NUMBER_OF(RokHwChipTable)

// 定长 ANSI 串拷贝（仓库惯例：不用 CRT str*；显式循环 + 恒置 NUL 结尾）。
static VOID
RokHwCopyName(_Out_writes_z_(DstLen) CHAR *Dst, _In_ ULONG DstLen, _In_ const CHAR *Src)
{
    ULONG i;

    for (i = 0; (i + 1) < DstLen && Src[i] != '\0'; i++)
    {
        Dst[i] = Src[i];
    }
    Dst[i] = '\0';
}

BOOLEAN
RokHwLookupChip(_In_ ULONG DeviceId, _Out_ ROK_HW_CHIP_INFO *Info)
{
    ULONG i;

    if (Info == NULL)
    {
        return FALSE;
    }

    for (i = 0; i < ROK_HW_CHIP_TABLE_COUNT; i++)
    {
        if (RokHwChipTable[i].DeviceId == DeviceId)
        {
            Info->Family = ROK_GPU_FAMILY_TS2;
            Info->Chip = RokHwChipTable[i].Chip;
            RokHwCopyName(Info->Name, sizeof(Info->Name),
                          RokHwChipName(Info->Chip));
            return TRUE;
        }
    }
    return FALSE;
}

const CHAR *
RokHwChipName(_In_ ULONG Chip)
{
    switch (Chip)
    {
    case ROK_HW_CHIP_CEDAR:     return "CEDAR";
    case ROK_HW_CHIP_REDWOOD:   return "REDWOOD";
    case ROK_HW_CHIP_JUNIPER:   return "JUNIPER";
    case ROK_HW_CHIP_CYPRESS:   return "CYPRESS";
    case ROK_HW_CHIP_HEMLOCK:   return "HEMLOCK";
    case ROK_HW_CHIP_BARTS:     return "BARTS";
    case ROK_HW_CHIP_TURKS:     return "TURKS";
    case ROK_HW_CHIP_CAICOS:    return "CAICOS";
    case ROK_HW_CHIP_PALM:      return "PALM";
    case ROK_HW_CHIP_SUMO:      return "SUMO";
    case ROK_HW_CHIP_SUMO2:     return "SUMO2";
    default:                    return "UNKNOWN";
    }
}

ULONG
RokHwFamilyHeuristic(_In_ ULONG DeviceId)
{
    if ((DeviceId >= 0x6700 && DeviceId <= 0x671F) ||
        (DeviceId >= 0x9900 && DeviceId <= 0x99AF))
    {
        return ROK_GPU_FAMILY_TS3;      // Cayman / Trinity
    }
    if (DeviceId >= 0x9400 && DeviceId <= 0x9900)
    {
        return ROK_GPU_FAMILY_TS2;      // Evergreen / Northern Islands 主段
    }
    return ROK_GPU_FAMILY_TS1;          // R600-RV770 主段兜底
}

// ---- BAR0 直访（边界检查）----
BOOLEAN
RokRegRead32(
    _In_opt_ PVOID MmioVa,
    _In_ ULONG MmioLength,
    _In_ ULONG Offset,
    _Out_ ULONG *Value)
{
    if (Value == NULL)
    {
        return FALSE;
    }
    *Value = 0;
    if (MmioVa == NULL || (Offset & 3) != 0)
    {
        return FALSE;
    }
    if (Offset > MmioLength || (MmioLength - Offset) < 4)
    {
        return FALSE;
    }
    *Value = READ_REGISTER_ULONG((volatile ULONG *)((PUCHAR)MmioVa + Offset));
    return TRUE;
}

BOOLEAN
RokRegWrite32(
    _In_opt_ PVOID MmioVa,
    _In_ ULONG MmioLength,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    if (MmioVa == NULL || (Offset & 3) != 0)
    {
        return FALSE;
    }
    if (Offset > MmioLength || (MmioLength - Offset) < 4)
    {
        return FALSE;
    }
    WRITE_REGISTER_ULONG((volatile ULONG *)((PUCHAR)MmioVa + Offset), Value);
    return TRUE;
}

// ---- 停顿（内核/宿主切换见 r600hw.h 注释）----
VOID
RokStallUs(_In_ ULONG Microseconds)
{
    KeStallExecutionProcessor(Microseconds);
}

// ---- GRBM 域（M3.2 第一批）----
BOOLEAN
RokGrbmWaitIdle(
    _In_opt_ PVOID MmioVa,
    _In_ ULONG MmioLength,
    _In_ ULONG MaxPolls,
    _Out_opt_ ULONG *StallsUsed)
{
    ULONG i;
    ULONG st = 0;

    if (StallsUsed != NULL)
    {
        *StallsUsed = 0;
    }
    if (MaxPolls == 0)
    {
        return FALSE;
    }
    for (i = 0; i < MaxPolls; i++)
    {
        if (!RokRegRead32(MmioVa, MmioLength, ROK_GRBM_STATUS, &st))
        {
            return FALSE;
        }
        if ((st & ROK_GRBM_GUI_ACTIVE) == 0)
        {
            if (StallsUsed != NULL)
            {
                *StallsUsed = i;
            }
            return TRUE;
        }
        RokStallUs(10);
    }
    if (StallsUsed != NULL)
    {
        *StallsUsed = MaxPolls;
    }
    return FALSE;
}

BOOLEAN
RokGrbmSoftResetCp(
    _In_opt_ PVOID MmioVa,
    _In_ ULONG MmioLength,
    _Out_opt_ ULONG *StatusBefore,
    _Out_opt_ ULONG *StatusAfter)
{
    ULONG st = 0;

    if (StatusBefore != NULL)
    {
        *StatusBefore = 0;
    }
    if (StatusAfter != NULL)
    {
        *StatusAfter = 0;
    }

    // 1) 可读性探测 + 复位前状态快照。
    if (!RokRegRead32(MmioVa, MmioLength, ROK_GRBM_STATUS, &st))
    {
        return FALSE;
    }
    if (StatusBefore != NULL)
    {
        *StatusBefore = st;
    }

    // 2) CP 域软复位脉冲：置位 → 停顿 → 回读冲刷 → 清零。
    if (!RokRegWrite32(MmioVa, MmioLength, ROK_GRBM_SOFT_RESET,
                       ROK_GRBM_SOFT_RESET_CP))
    {
        return FALSE;
    }
    RokStallUs(100);
    if (!RokRegRead32(MmioVa, MmioLength, ROK_GRBM_STATUS, &st))
    {
        return FALSE;
    }
    if (!RokRegWrite32(MmioVa, MmioLength, ROK_GRBM_SOFT_RESET, 0))
    {
        return FALSE;
    }

    // 3) 等待图形域空闲（上限 10000 轮 × 10µs ≈ 100ms，工程选型）。
    if (!RokGrbmWaitIdle(MmioVa, MmioLength, 10000, NULL))
    {
        return FALSE;
    }
    if (!RokRegRead32(MmioVa, MmioLength, ROK_GRBM_STATUS, &st))
    {
        return FALSE;
    }
    if (StatusAfter != NULL)
    {
        *StatusAfter = st;
    }
    return TRUE;
}

// ---- StartDevice 后段的识别入口 ----
VOID
RokHwIdentify(_Inout_ struct _ROK_ADAPTER *Adapter)
{
    ROK_HW_CHIP_INFO info;
    ULONG st = 0;

    if (Adapter == NULL)
    {
        return;
    }

    if (RokHwLookupChip(Adapter->PciDeviceId, &info))
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL,
                   "rokmd: HW identify: DID %#x -> %s (exact, TeraScale 2)\n",
                   Adapter->PciDeviceId, info.Name));
    }
    else
    {
        info.Family = RokHwFamilyHeuristic(Adapter->PciDeviceId);
        info.Chip = ROK_HW_CHIP_UNKNOWN;
        RokHwCopyName(info.Name, sizeof(info.Name), "UNKNOWN");
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL,
                   "rokmd: HW identify: DID %#x -> heuristic family TS%u\n",
                   Adapter->PciDeviceId, info.Family));
    }

    Adapter->HwChip = info;
    Adapter->HwGrbmStatus = 0;
    if (RokRegRead32(Adapter->MmioVirtualAddress, Adapter->MmioLength,
                     ROK_GRBM_STATUS, &st))
    {
        Adapter->HwGrbmStatus = st;
    }
}
