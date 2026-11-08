// SPDX-License-Identifier: MIT
// gfxinit.c — 硬件初始化前哨实现（M3.4a）
//
// 编程序列逐条对照 Linux 内核 drivers/gpu/drm/radeon/evergreen.c（GPL-2.0，
// 序列参考；实现自写，MIT）：
//   * evergreen_cp_load_microcode（2968-3002）—— PFP 1120 + ME 1376 dwords
//   * evergreen_cp_resume（3067-3130）—— GRBM 软复位 + CP_RB_* 编程
//   * evergreen_cp_start（3004-3017 的 ME_INIT 前半）—— 最小启动包
//   * r600_ring_test（r600.c 2823-2861）—— scratch 回环测试
//
// M3.4a 边界：ring 是"初始化 + 观测"通道（escape 回传状态），WDDM 提交
// 路径保持即时完成模型；默认关闭，注册表 EnableRing=1 显式开启。
#include "precomp.h"
#include "adapter.h"
#include "gfxinit.h"
#include "ring.h"
#include "sim.h"

// ring 布点：VRAM 孔径内 8MB 偏移（避开 VBIOS/原驱动 scanout 的低区），
// 32KB 容量（8192 dwords，BufszLog2=12）。Linux 默认 ring 1MB —— M3.4a
// 用小环足够（观测 + 回环测试），M3.4b 切真实提交时再扩。
#define ROK_RING_VRAM_OFFSET        (8UL * 1024 * 1024)
#define ROK_RING_SIZE_BYTES         (32UL * 1024)
#define ROK_RING_BLKSZ_LOG2         9UL      // order_base_2(4096/8)（Linux 同款）
#define ROK_ME_INIT_MAX_HW_CONTEXTS 8UL      // radeon evergreen config.max_hw_contexts

// ring test（r600_ring_test 语义）。
#define ROK_RING_TEST_SEED          0xCAFEDEADUL
#define ROK_RING_TEST_SENTINEL      0xDEADBEEFUL
#define ROK_RING_TEST_MAX_POLLS     200UL    // × 500µs = 100ms 上限
#define ROK_RING_TEST_STALL_US      500UL

// 演练模式的假总线基址（host 测试；真实模式用 MC_VM_FB_LOCATION 推导）。
#define ROK_REHEARSE_FB_BASE        0x80000000ULL

// ------------------------------------------------------------------
// 延时原语（PASSIVE_LEVEL；StartDevice 上下文）
// ------------------------------------------------------------------

static VOID
RokGfxDelayMs(
    _In_ ULONG Milliseconds
    )
{
    LARGE_INTEGER due;

    due.QuadPart = -(LONGLONG)Milliseconds * 10000;   // 100ns 单位，负值=相对
    KeDelayExecutionThread(KernelMode, FALSE, &due);
}

// ------------------------------------------------------------------
// 寄存器读写（sim 感知）
// ------------------------------------------------------------------

ROK_U32
RokDevRegRead32(
    _In_opt_ struct _ROK_ADAPTER *Adapter,
    _In_ ROK_U32 Offset
    )
{
    ULONG value = 0;

    if (Adapter == NULL)
    {
        return 0;
    }

    if (Adapter->SimulationMode)
    {
        if (Adapter->SimRegisterSpace != NULL &&
            Offset + sizeof(ROK_U32) <= Adapter->SimRegisterSpaceSize)
        {
            return *(volatile ROK_U32 *)((PUCHAR)Adapter->SimRegisterSpace + Offset);
        }
        return 0;
    }

    // 真实路径委托 r600hw 的边界检查直访（kmock 宿主构建落 mock MMIO）。
    if (Adapter->MmioVirtualAddress != NULL)
    {
        if (RokRegRead32(Adapter->MmioVirtualAddress, Adapter->MmioLength,
                         Offset, &value))
        {
            return (ROK_U32)value;
        }
    }
    return 0;
}

VOID
RokDevRegWrite32(
    _In_opt_ struct _ROK_ADAPTER *Adapter,
    _In_ ROK_U32 Offset,
    _In_ ROK_U32 Value
    )
{
    if (Adapter == NULL)
    {
        return;
    }

    if (Adapter->SimulationMode)
    {
        if (Adapter->SimRegisterSpace != NULL &&
            Offset + sizeof(ROK_U32) <= Adapter->SimRegisterSpaceSize)
        {
            *(volatile ROK_U32 *)((PUCHAR)Adapter->SimRegisterSpace + Offset) = Value;
        }
        return;
    }

    // 真实路径委托 r600hw 的边界检查直访。
    if (Adapter->MmioVirtualAddress != NULL)
    {
        (VOID)RokRegWrite32(Adapter->MmioVirtualAddress, Adapter->MmioLength,
                            Offset, Value);
    }
}

// ------------------------------------------------------------------
// 只读状态快照（escape type 12）
// ------------------------------------------------------------------

NTSTATUS
RokGfxProbeStatus(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _Out_ ROK_ESC_HW_STATUS_OUT *Out
    )
{
    ROK_U32 fbLoc;

    if (Adapter == NULL || Out == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Out, sizeof(*Out));

    if (Adapter->SimulationMode)
    {
        Out->MmioSource = ROK_HW_SRC_SIM;
    }
    else if (Adapter->MmioVirtualAddress != NULL)
    {
        Out->MmioSource = ROK_HW_SRC_REAL_MMIO;
    }
    else
    {
        Out->MmioSource = ROK_HW_SRC_NONE;
    }
    Out->MmioLength = Adapter->SimulationMode
                          ? Adapter->SimRegisterSpaceSize
                          : Adapter->MmioLength;

    Out->ConfigMemsize    = RokDevRegRead32(Adapter, ROK_REG_CONFIG_MEMSIZE);
    fbLoc                 = RokDevRegRead32(Adapter, ROK_REG_MC_VM_FB_LOCATION);
    Out->McVmFbLocation   = fbLoc;
    Out->FbBaseBytes      = (fbLoc & 0xFFFFUL) << 24;
    Out->McApertureLow    = RokDevRegRead32(Adapter, ROK_REG_MC_VM_SYSTEM_APERTURE_LOW);
    Out->McApertureHigh   = RokDevRegRead32(Adapter, ROK_REG_MC_VM_SYSTEM_APERTURE_HIGH);

    Out->SrbmStatus       = RokDevRegRead32(Adapter, ROK_REG_SRBM_STATUS);
    Out->SrbmStatus2      = RokDevRegRead32(Adapter, ROK_REG_SRBM_STATUS2);
    Out->GrbmStatus       = RokDevRegRead32(Adapter, ROK_REG_GRBM_STATUS);
    Out->GrbmStatus2      = RokDevRegRead32(Adapter, ROK_REG_GRBM_STATUS2);

    Out->CpStalledStat1   = RokDevRegRead32(Adapter, ROK_REG_CP_STALLED_STAT1);
    Out->CpStalledStat2   = RokDevRegRead32(Adapter, ROK_REG_CP_STALLED_STAT2);
    Out->CpBusyStat       = RokDevRegRead32(Adapter, ROK_REG_CP_BUSY_STAT);
    Out->CpStat           = RokDevRegRead32(Adapter, ROK_REG_CP_STAT);
    Out->CpMeCntl         = RokDevRegRead32(Adapter, ROK_REG_CP_ME_CNTL);

    Out->CpRbBase         = RokDevRegRead32(Adapter, ROK_REG_CP_RB_BASE);
    Out->CpRbCntl         = RokDevRegRead32(Adapter, ROK_REG_CP_RB_CNTL);
    Out->CpRbRptr         = RokDevRegRead32(Adapter, ROK_REG_CP_RB_RPTR);
    Out->CpRbWptr         = RokDevRegRead32(Adapter, ROK_REG_CP_RB_WPTR);

    Out->ScratchUmsk      = RokDevRegRead32(Adapter, ROK_REG_SCRATCH_UMSK);
    Out->ScratchReg6      = RokDevRegRead32(Adapter, ROK_REG_SCRATCH_REG6);
    Out->ScratchReg7      = RokDevRegRead32(Adapter, ROK_REG_SCRATCH_REG7);

    // rokmd 自身 ring 状态（区别于寄存器里看到的 ring 配置）。
    Out->RingReady        = Adapter->Ring.Ready ? 1 : 0;
    Out->RingGpuAddrLo    = (ROK_U32)(Adapter->Ring.GpuAddr & 0xFFFFFFFFULL);
    Out->RingSizeDwords   = Adapter->Ring.SizeDwords;
    Out->RingTestRetries  = Adapter->RingTestRetries;
    Out->RingTestValue    = Adapter->RingTestValue;
    Out->UcodeLoaded      = Adapter->UcodeLoaded ? 1 : 0;
    Out->Reserved[0]      = 0;
    Out->Reserved[1]      = 0;

    return STATUS_SUCCESS;
}

// ------------------------------------------------------------------
// CP 微码加载（evergreen_cp_load_microcode 移植）
// ------------------------------------------------------------------

static VOID
RokCpLoadUcode(
    _Inout_ struct _ROK_ADAPTER *Adapter
    )
{
    ULONG i;

    // r700_cp_stop（evergreen.c 2976）：ME/PFP 停机 + 关 scratch 掩码。
    RokDevRegWrite32(Adapter, ROK_REG_CP_ME_CNTL, ROK_CP_ME_CNTL_HALT);
    RokDevRegWrite32(Adapter, ROK_REG_SCRATCH_UMSK, 0);

    // 微码写入期间用最小哑环（evergreen.c 2977-2981）。
    RokDevRegWrite32(Adapter, ROK_REG_CP_RB_CNTL,
                  ROK_RB_NO_UPDATE | ROK_RB_BLKSZ(15) | ROK_RB_BUFSZ(3));

    // PFP：地址计数器清零后逐 dword 写（evergreen.c 2983-2987）。
    RokDevRegWrite32(Adapter, ROK_REG_CP_PFP_UCODE_ADDR, 0);
    for (i = 0; i < ROK_EVERGREEN_PFP_UCODE_DWORDS; i++)
    {
        RokDevRegWrite32(Adapter, ROK_REG_CP_PFP_UCODE_DATA, RokEvergreenPfpUcode[i]);
    }
    RokDevRegWrite32(Adapter, ROK_REG_CP_PFP_UCODE_ADDR, 0);

    // ME：WADDR 清零后逐 dword 写（evergreen.c 2989-2994）。
    RokDevRegWrite32(Adapter, ROK_REG_CP_ME_RAM_WADDR, 0);
    for (i = 0; i < ROK_EVERGREEN_PM4_UCODE_DWORDS; i++)
    {
        RokDevRegWrite32(Adapter, ROK_REG_CP_ME_RAM_DATA, RokEvergreenMeUcode[i]);
    }

    // 收尾（evergreen.c 2996-2999）。
    RokDevRegWrite32(Adapter, ROK_REG_CP_PFP_UCODE_ADDR, 0);
    RokDevRegWrite32(Adapter, ROK_REG_CP_ME_RAM_WADDR, 0);
    RokDevRegWrite32(Adapter, ROK_REG_CP_ME_RAM_RADDR, 0);

    Adapter->UcodeLoaded = TRUE;
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: CP ucode loaded (CAICOS PFP %u + ME %u dwords)\n",
               (unsigned)ROK_EVERGREEN_PFP_UCODE_DWORDS,
               (unsigned)ROK_EVERGREEN_PM4_UCODE_DWORDS));
}

// ------------------------------------------------------------------
// CP ring 寄存器编程（evergreen_cp_resume 移植，去掉 writeback 路径）
// ------------------------------------------------------------------

static VOID
RokCpProgramRing(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _Inout_ ROK_RING *Ring
    )
{
    ROK_U32 tmp;

    // 1) GRBM 软复位 CP+PA+SH+VGT+SPI+SX（evergreen.c 3076-3083）。
    RokDevRegWrite32(Adapter, ROK_REG_GRBM_SOFT_RESET,
                  ROK_SOFT_RESET_EVERGREEN_CP_MASK);
    (VOID)RokDevRegRead32(Adapter, ROK_REG_GRBM_SOFT_RESET);
    RokGfxDelayMs(15);
    RokDevRegWrite32(Adapter, ROK_REG_GRBM_SOFT_RESET, 0);
    (VOID)RokDevRegRead32(Adapter, ROK_REG_GRBM_SOFT_RESET);

    // 2) 环容量字段（evergreen.c 3086-3091）：
    //    BLKSZ = order_base_2(4096/8) = 9；BUFSZ = log2 容量。
    tmp = ROK_RB_BLKSZ(ROK_RING_BLKSZ_LOG2) | ROK_RB_BUFSZ(Ring->BufszLog2);

    RokDevRegWrite32(Adapter, ROK_REG_CP_RB_CNTL, tmp);
    RokDevRegWrite32(Adapter, ROK_REG_CP_SEM_WAIT_TIMER, 0);
    RokDevRegWrite32(Adapter, ROK_REG_CP_SEM_INCOMPLETE_TIMER_CNTL, 0);
    RokDevRegWrite32(Adapter, ROK_REG_CP_RB_WPTR_DELAY, 0);

    // 3) 软件接管读指针清零（evergreen.c 3094-3097）。
    RokDevRegWrite32(Adapter, ROK_REG_CP_RB_CNTL, tmp | ROK_RB_RPTR_WR_ENA);
    RokDevRegWrite32(Adapter, ROK_REG_CP_RB_RPTR_WR, 0);
    Ring->WptrDwords = 0;
    RokDevRegWrite32(Adapter, ROK_REG_CP_RB_WPTR, Ring->WptrDwords);

    // 4) 无 writeback：NO_UPDATE + 关 scratch 掩码，rptr 从 MMIO(0x8700) 轮询
    //    （evergreen.c 3100-3110 的 wb.enabled=FALSE 分支；SCRATCH_ADDR 不写）。
    tmp |= ROK_RB_NO_UPDATE;
    RokDevRegWrite32(Adapter, ROK_REG_SCRATCH_UMSK, 0);

    RokGfxDelayMs(1);
    RokDevRegWrite32(Adapter, ROK_REG_CP_RB_CNTL, tmp);

    // 5) 环基址（>>8 定位）+ CP_DEBUG（evergreen.c 3119-3121）。
    RokDevRegWrite32(Adapter, ROK_REG_CP_RB_BASE,
                  (ROK_U32)(Ring->GpuAddr >> 8));
    RokDevRegWrite32(Adapter, ROK_REG_CP_DEBUG, (1UL << 27) | (1UL << 28));

    Ring->HwProgrammed = TRUE;
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: CP ring programmed base=%I64x size=%lu dwords "
               "(BUFSZ=%lu BLKSZ=%lu, NO_UPDATE)\n",
               Ring->GpuAddr, Ring->SizeDwords, Ring->BufszLog2,
               (unsigned long)ROK_RING_BLKSZ_LOG2));
}

// ------------------------------------------------------------------
// ME_INIT + 解除停机（evergreen_cp_start 前半 + r600.c cp_start 收尾）
// ------------------------------------------------------------------

static VOID
RokCpMeInitAndUnhalt(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _Inout_ ROK_RING *Ring
    )
{
    RokRingWriteMeInitialize(Ring, ROK_ME_INIT_MAX_HW_CONTEXTS);

    // ring 提交：把本地 WPTR 写入寄存器并读回 flush（r600_gfx_set_wptr 同款）。
    RokDevRegWrite32(Adapter, ROK_REG_CP_RB_WPTR, Ring->WptrDwords);
    (VOID)RokDevRegRead32(Adapter, ROK_REG_CP_RB_WPTR);

    // 解除停机（evergreen.c 3020-3023：cp_me = 0xff）。
    RokDevRegWrite32(Adapter, ROK_REG_CP_ME_CNTL, ROK_CP_ME_CNTL_UNHALT);

    Ring->Ready = TRUE;
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: ME_INIT written, CP unhalted (ME_CNTL=0xFF)\n"));
}

// ------------------------------------------------------------------
// ring 回环测试（r600_ring_test 移植）
// ------------------------------------------------------------------

static NTSTATUS
RokRingTest(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _Inout_ ROK_RING *Ring,
    _In_ BOOLEAN Rehearse
    )
{
    ROK_U32 value = 0;
    ULONG i;

    // 种子写 scratch（MMIO 侧），CP 经 SET_CONFIG_REG 覆写为哨兵值。
    RokDevRegWrite32(Adapter, ROK_REG_SCRATCH_REG6, ROK_RING_TEST_SEED);
    RokRingWriteSetConfigReg(Ring, ROK_REG_SCRATCH_REG6, ROK_RING_TEST_SENTINEL);

    RokDevRegWrite32(Adapter, ROK_REG_CP_RB_WPTR, Ring->WptrDwords);
    (VOID)RokDevRegRead32(Adapter, ROK_REG_CP_RB_WPTR);

    if (Rehearse)
    {
        // 演练：没有真实 CP 取指 —— sim 后端代执行该包（写哨兵），
        // 轮询逻辑本身仍走完整路径（首轮命中）。
        RokDevRegWrite32(Adapter, ROK_REG_SCRATCH_REG6, ROK_RING_TEST_SENTINEL);
    }

    for (i = 0; i < ROK_RING_TEST_MAX_POLLS; i++)
    {
        value = RokDevRegRead32(Adapter, ROK_REG_SCRATCH_REG6);
        if (value == ROK_RING_TEST_SENTINEL)
        {
            break;
        }
        KeStallExecutionProcessor(ROK_RING_TEST_STALL_US);
    }

    Adapter->RingTestRetries = i;
    Adapter->RingTestValue = value;
    Adapter->Ring.RptrDwords = RokDevRegRead32(Adapter, ROK_REG_CP_RB_RPTR);

    if (value == ROK_RING_TEST_SENTINEL)
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: ring test PASSED in %lu polls (rptr=%lu)\n",
                   i, Adapter->Ring.RptrDwords));
        return STATUS_SUCCESS;
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
               "rokmd: ring test FAILED (scratch=0x%08X after %lu polls) "
               "—— CP 未取指？检查 ucode/MC 状态\n",
               value, i));
    Ring->Ready = FALSE;
    return STATUS_DEVICE_HARDWARE_ERROR;
}

// ------------------------------------------------------------------
// StartDevice / StopDevice 挂接
// ------------------------------------------------------------------

VOID
RokGfxStartDeviceRing(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _In_opt_ PUNICODE_STRING RegistryPath
    )
{
    ULONG flag = 0;

    PAGED_CODE();

    if (Adapter == NULL)
    {
        return;
    }

    Adapter->RingEnableRequested = FALSE;
    if (RegistryPath != NULL &&
        RokRegReadUlongValue(RegistryPath, L"Parameters", L"EnableRing", &flag) &&
        flag == 1)
    {
        Adapter->RingEnableRequested = TRUE;
    }

    if (!Adapter->RingEnableRequested)
    {
        // 默认路径：纯 render-only，不写任何硬件寄存器。
        return;
    }

    (VOID)RokRingHardwareInit(Adapter, FALSE);
}

NTSTATUS
RokRingHardwareInit(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _In_ BOOLEAN Rehearse
    )
{
    ROK_RING *ring;
    volatile ROK_U32 *ringCpuVa = NULL;
    UINT64 gpuAddr = 0;
    BOOLEAN mapped = FALSE;
    BOOLEAN poolAllocated = FALSE;
    NTSTATUS status;

    PAGED_CODE();

    if (Adapter == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ring = &Adapter->Ring;

    if (Rehearse)
    {
        // 演练：ring 内存取池，假总线地址；寄存器写入落 sim 空间。
        ringCpuVa = (volatile ROK_U32 *)ExAllocatePoolWithTag(
            NonPagedPool, ROK_RING_SIZE_BYTES, ROK_POOL_TAG);
        if (ringCpuVa == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        poolAllocated = TRUE;
        gpuAddr = ROK_REHEARSE_FB_BASE + ROK_RING_VRAM_OFFSET;
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: ring REHEARSE mode (sim register space)\n"));
    }
    else
    {
        // 真实模式执行条件逐项校验。
        if (Adapter->SimulationMode)
        {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
                       "rokmd: ring init skipped (sim device — use Rehearse)\n"));
            return STATUS_NOT_SUPPORTED;
        }
        if (Adapter->MmioVirtualAddress == NULL)
        {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
                       "rokmd: ring init skipped (no BAR0 MMIO)\n"));
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        // 家族门槛：优先用 RokHwIdentify（M3.1 精确表）的快照；
        // 快照未填充（如演练/单测直调）时退回 r600hw 启发式。
        {
            ULONG family = (Adapter->HwChip.Family != 0)
                               ? Adapter->HwChip.Family
                               : RokHwFamilyHeuristic(Adapter->PciDeviceId);
            if (family != ROK_GPU_FAMILY_TS2)
            {
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
                           "rokmd: ring init skipped (DID 0x%04X not TS2; "
                           "ucode is CAICOS-family)\n",
                           Adapter->PciDeviceId));
                return STATUS_NOT_SUPPORTED;
            }
        }
        if (Adapter->ApertureLength < ROK_RING_VRAM_OFFSET + ROK_RING_SIZE_BYTES)
        {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
                       "rokmd: ring init skipped (aperture %lu bytes too small)\n",
                       Adapter->ApertureLength));
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        {
            ROK_U32 fbLoc = RokDevRegRead32(Adapter, ROK_REG_MC_VM_FB_LOCATION);
            UINT64 fbBase = (UINT64)(fbLoc & 0xFFFFUL) << 24;

            if (fbBase == 0)
            {
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
                           "rokmd: ring init skipped (MC_VM_FB_LOCATION=0 — "
                           "MC 未配置，需 M3.2 MC 初始化)\n"));
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }

            // 映射环窗口：孔径基址 + 8MB 偏移起 1MB（页粒度）。
            {
                PHYSICAL_ADDRESS windowBase;
                NTSTATUS mapStatus;

                windowBase.QuadPart =
                    Adapter->ApertureBase.QuadPart + (LONGLONG)ROK_RING_VRAM_OFFSET;
                mapStatus = Adapter->DxgkInterface.DxgkCbMapMemory(
                    Adapter->DxgkInterface.DeviceHandle,
                    windowBase,
                    1024 * 1024,
                    FALSE,      // InIoSpace
                    FALSE,      // MapToUserMode
                    MmCached,
                    (PVOID *)&ringCpuVa);
                if (!NT_SUCCESS(mapStatus))
                {
                    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                               "rokmd: aperture ring window map failed 0x%08X\n",
                               mapStatus));
                    return mapStatus;
                }
                mapped = TRUE;
            }

            gpuAddr = fbBase + ROK_RING_VRAM_OFFSET;
        }
    }

    RtlZeroMemory((PVOID)ringCpuVa, ROK_RING_SIZE_BYTES);

    ring->CpuVa = ringCpuVa;
    ring->GpuAddr = gpuAddr;
    ring->BufszLog2 = RokRingBufszFromBytes(ROK_RING_SIZE_BYTES);
    ring->BlkszLog2 = (ULONG)ROK_RING_BLKSZ_LOG2;
    ring->SizeDwords = RokRingDwordsFromBufsz(ring->BufszLog2);
    ring->WptrDwords = 0;
    ring->WptrTotal = 0;
    ring->RptrDwords = 0;
    ring->Ready = FALSE;
    ring->HwProgrammed = FALSE;
    Adapter->RingWindowMapped = mapped;
    Adapter->RingWindowPool = poolAllocated;

    // ---- evergreen 启动序列 ----
    RokCpLoadUcode(Adapter);
    RokCpProgramRing(Adapter, ring);
    RokCpMeInitAndUnhalt(Adapter, ring);

    status = RokRingTest(Adapter, ring, Rehearse);
    if (!NT_SUCCESS(status))
    {
        // 回环失败：保持已编程状态（escape 可观测诊断），停机 ME 防
        // 后续异常取指。
        RokDevRegWrite32(Adapter, ROK_REG_CP_ME_CNTL, ROK_CP_ME_CNTL_HALT);
        ring->Ready = FALSE;
        return status;
    }

    return STATUS_SUCCESS;
}

VOID
RokRingHardwareStop(
    _Inout_ struct _ROK_ADAPTER *Adapter
    )
{
    BOOLEAN mapped;
    BOOLEAN poolAllocated;
    PVOID ringVa;

    PAGED_CODE();

    if (Adapter == NULL)
    {
        return;
    }

    // 停机 CP（仅当我们编程过；未开启 ring 时对硬件零写入）。
    if (Adapter->Ring.HwProgrammed)
    {
        RokDevRegWrite32(Adapter, ROK_REG_CP_ME_CNTL, ROK_CP_ME_CNTL_HALT);
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: ring stopped (CP halted)\n"));
    }

    mapped = Adapter->RingWindowMapped;
    poolAllocated = Adapter->RingWindowPool;
    ringVa = (PVOID)Adapter->Ring.CpuVa;

    RtlZeroMemory(&Adapter->Ring, sizeof(Adapter->Ring));
    Adapter->RingWindowMapped = FALSE;
    Adapter->RingWindowPool = FALSE;

    if (ringVa == NULL)
    {
        return;
    }

    if (mapped)
    {
        Adapter->DxgkInterface.DxgkCbUnmapMemory(
            Adapter->DxgkInterface.DeviceHandle, ringVa);
    }
    else if (poolAllocated)
    {
        ExFreePoolWithTag(ringVa, ROK_POOL_TAG);
    }
}
