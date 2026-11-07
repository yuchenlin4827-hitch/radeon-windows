// SPDX-License-Identifier: MIT
// 适配器生命周期实现：AddDevice / StartDevice / StopDevice / RemoveDevice 等。
// 签名来自 scripts/gen_ddi2.py 生成的 adapter.h（与 WDK 26100 一致）。
//
// M2.5 增量：保存 DxgkInterface 回调表、经 DxgkCbReadDeviceSpace 读 PCI
// 标识、经 DxgkCbMapMemory 映射 BAR0 —— 三者均为 M3 硬件编程序列
//（MM_INDEX/MM_DATA 间接寻址、GRBM/CP）的前置条件。
#include "precomp.h"
#include "adapter.h"

// ------------------------------------------------------------------
// PCI 标识与 MMIO
// ------------------------------------------------------------------

// 读 PCI 配置空间 0x00..0x3F，登记 Vendor/Device/Revision。
// M3 的 ASIC 家族分发表以此为键（Seymour 0x6840/0x6841、Caicos 0x6779/0x677B…）。
static NTSTATUS
RokAdapterReadPciIds(
    _Inout_ PROK_ADAPTER Adapter
    )
{
    UCHAR cfg[64];
    ULONG bytesRead = 0;
    NTSTATUS status;

    RtlZeroMemory(cfg, sizeof(cfg));

    status = Adapter->DxgkInterface.DxgkCbReadDeviceSpace(
        Adapter->DxgkInterface.DeviceHandle,
        DXGK_WHICHSPACE_CONFIG,
        cfg,
        0,
        sizeof(cfg),
        &bytesRead);

    if (!NT_SUCCESS(status) || bytesRead < 12)
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
                   "rokmd: PCI config read failed 0x%08X (read %lu)\n",
                   status, bytesRead));
        return status;
    }

    // 按字节拼装，规避配置空间缓冲区的对齐问题。
    Adapter->PciVendorId   = (ULONG)cfg[0] | ((ULONG)cfg[1] << 8);
    Adapter->PciDeviceId   = (ULONG)cfg[2] | ((ULONG)cfg[3] << 8);
    Adapter->PciRevisionId = (ULONG)cfg[8];

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: PCI VID=0x%04X DID=0x%04X REV=0x%02X\n",
               Adapter->PciVendorId, Adapter->PciDeviceId, Adapter->PciRevisionId));

    return STATUS_SUCCESS;
}

// 映射 BAR0（第一个 <=16MB 的 MMIO 内存资源）。
// 更大的窗口（BAR2 VRAM aperture 256MB+）留待 M3 按段策略处理。
static NTSTATUS
RokAdapterMapMmio(
    _Inout_ PROK_ADAPTER      Adapter,
    _In_    PCM_RESOURCE_LIST TranslatedResources
    )
{
    PCM_FULL_RESOURCE_DESCRIPTOR full;
    CM_PARTIAL_RESOURCE_DESCRIPTOR *desc;
    ULONG i, j;
    NTSTATUS status;

    if (TranslatedResources == NULL)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    for (i = 0; i < TranslatedResources->Count; i++)
    {
        full = &TranslatedResources->List[i];
        for (j = 0; j < full->PartialResourceList.Count; j++)
        {
            desc = &full->PartialResourceList.PartialDescriptors[j];

            if (desc->Type != CmResourceTypeMemory)
            {
                continue;
            }

            if (desc->u.Memory.Length > (16 * 1024 * 1024))
            {
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                           "rokmd:   skip large MEM window %#I64x len=%#x (M3 VRAM aperture)\n",
                           (UINT64)desc->u.Memory.Start.QuadPart,
                           desc->u.Memory.Length));
                continue;
            }

            Adapter->MmioBase.QuadPart = desc->u.Memory.Start.QuadPart;
            Adapter->MmioLength        = desc->u.Memory.Length;

            status = Adapter->DxgkInterface.DxgkCbMapMemory(
                Adapter->DxgkInterface.DeviceHandle,
                Adapter->MmioBase,
                Adapter->MmioLength,
                FALSE,          // InIoSpace：内存资源
                FALSE,          // MapToUserMode：仅内核
                MmNonCached,
                &Adapter->MmioVirtualAddress);

            if (!NT_SUCCESS(status))
            {
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                           "rokmd: DxgkCbMapMemory(BAR0) failed 0x%08X\n", status));
                Adapter->MmioVirtualAddress = NULL;
                return status;
            }

            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                       "rokmd: BAR0 MMIO mapped PA=%#I64x len=%#x -> VA=%p\n",
                       (UINT64)Adapter->MmioBase.QuadPart,
                       Adapter->MmioLength,
                       Adapter->MmioVirtualAddress));
            return STATUS_SUCCESS;
        }
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
               "rokmd: no BAR0 memory resource found\n"));
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

// ------------------------------------------------------------------
// 生命周期 DDI
// ------------------------------------------------------------------

NTSTATUS
RokDdiAddDevice(
    _In_  PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PVOID         *MiniportDeviceContext
    )
{
    PROK_ADAPTER adapter;

    PAGED_CODE();

    adapter = (PROK_ADAPTER)ExAllocatePoolWithTag(
        PagedPool, sizeof(ROK_ADAPTER), ROK_POOL_TAG);
    if (adapter == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(adapter, sizeof(ROK_ADAPTER));
    adapter->PhysicalDeviceObject = PhysicalDeviceObject;
    InitializeListHead(&adapter->AllocationList);
    InitializeListHead(&adapter->ContextList);
    KeInitializeSpinLock(&adapter->AllocationLock);
    KeInitializeSpinLock(&adapter->ContextLock);

    *MiniportDeviceContext = adapter;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: AddDevice PDO %p\n", PhysicalDeviceObject));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiStartDevice(
    IN_CONST_PVOID          MiniportDeviceContext,
    IN_PDXGK_START_INFO     DxgkStartInfo,
    IN_PDXGKRNL_INTERFACE   DxgkInterface,
    OUT_PULONG              NumberOfVideoPresentSources,
    OUT_PULONG              NumberOfChildren
    )
{
    PROK_ADAPTER adapter = (PROK_ADAPTER)MiniportDeviceContext;
    DXGK_DEVICE_INFO deviceInfo;
    NTSTATUS status;

    PAGED_CODE();

    if (adapter == NULL || DxgkInterface == NULL ||
        DxgkInterface->Size != sizeof(DXGKRNL_INTERFACE))
    {
        return STATUS_INVALID_PARAMETER;
    }

    // 骨架为 render-only：0 路显示 source、0 个子设备（监视器）。
    // 显示路径（VidPn/Present）留到 M3+，先保证适配器加载稳定。
    *NumberOfVideoPresentSources = 0;
    *NumberOfChildren = 0;

    // 保存 dxgkrnl 回调表（含 DeviceHandle），后续所有 DxgkCb* 由此发起。
    adapter->DxgkInterface = *DxgkInterface;

    // LUID 是用户态（D3DKMT / M4 的 Vulkan ICD）定位适配器的钥匙。
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: StartDevice LUID=%08X-%08X\n",
               DxgkStartInfo->AdapterLuid.HighPart,
               DxgkStartInfo->AdapterLuid.LowPart));

    // 通过 dxgkrnl 回调取设备信息（PDO + 翻译后的资源清单）。
    // TeraScale 期望形态（对照 Linux radeon 探测结果）：
    //   BAR0: MMIO 寄存器窗口（64KB~16MB 视代际）
    //   BAR2: VRAM aperture（可写映射显存）
    //   IRQ:  一条 MSI/Line-based 中断（IH ring）
    RtlZeroMemory(&deviceInfo, sizeof(deviceInfo));
    status = DxgkInterface->DxgkCbGetDeviceInformation(DxgkInterface->DeviceHandle,
                                                       &deviceInfo);
    if (NT_SUCCESS(status))
    {
        adapter->PhysicalDeviceObject = deviceInfo.PhysicalDeviceObject;
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: StartDevice PDO %p, registry %wZ\n",
                   deviceInfo.PhysicalDeviceObject,
                   &deviceInfo.DeviceRegistryPath));
        RokLogTranslatedResources(deviceInfo.TranslatedResourceList);
    }
    else
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
                   "rokmd: DxgkCbGetDeviceInformation failed 0x%08X\n", status));
    }

    // PCI 标识 + BAR0 映射。M2 不因 BAR0 缺失而拒绝启动
    //（虚拟机/桥接设备形态不同），M3 引入寄存器访问时再收紧。
    RokAdapterReadPciIds(adapter);
    if (!NT_SUCCESS(RokAdapterMapMmio(adapter, deviceInfo.TranslatedResourceList)))
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
                   "rokmd: BAR0 unmapped — continue in pure render-only mode\n"));
    }

    adapter->Started = TRUE;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: StartDevice OK (render-only, WDDM 2.0 DDI)\n"));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiStopDevice(
    IN_CONST_PVOID MiniportDeviceContext
    )
{
    PROK_ADAPTER adapter = (PROK_ADAPTER)MiniportDeviceContext;

    PAGED_CODE();

    if (adapter != NULL)
    {
        if (adapter->MmioVirtualAddress != NULL)
        {
            adapter->DxgkInterface.DxgkCbUnmapMemory(
                adapter->DxgkInterface.DeviceHandle,
                adapter->MmioVirtualAddress);
            adapter->MmioVirtualAddress = NULL;
        }
        adapter->Started = FALSE;
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: StopDevice\n"));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiRemoveDevice(
    IN_CONST_PVOID MiniportDeviceContext
    )
{
    PROK_ADAPTER adapter = (PROK_ADAPTER)MiniportDeviceContext;

    PAGED_CODE();

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RemoveDevice\n"));
    if (adapter != NULL)
    {
        // 分配/上下文记录应由 VidMm 先经 DestroyAllocation/DestroyContext
        // 回收；此处防御性断言链表为空。
        ASSERT(IsListEmpty(&adapter->AllocationList));
        ASSERT(IsListEmpty(&adapter->ContextList));
        ExFreePoolWithTag(adapter, ROK_POOL_TAG);
    }
    return STATUS_SUCCESS;
}

VOID
RokDdiResetDevice(
    IN_PVOID MiniportDeviceContext
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    // 设备意外复位（D3 cold 等）。M3 起接硬件复位序列（GRBM 软复位等）。
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "rokmd: ResetDevice\n"));
}

VOID
RokDdiUnload(
    VOID
    )
{
    PAGED_CODE();
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: Unload\n"));
}

VOID
RokLogTranslatedResources(
    _In_ PCM_RESOURCE_LIST TranslatedResources
    )
{
    PCM_FULL_RESOURCE_DESCRIPTOR full;
    CM_PARTIAL_RESOURCE_DESCRIPTOR *desc;
    ULONG i, j;

    if (TranslatedResources == NULL)
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
                   "rokmd: StartDevice — no translated resources\n"));
        return;
    }

    for (i = 0; i < TranslatedResources->Count; i++)
    {
        full = &TranslatedResources->List[i];
        for (j = 0; j < full->PartialResourceList.Count; j++)
        {
            desc = &full->PartialResourceList.PartialDescriptors[j];
            switch (desc->Type)
            {
            case CmResourceTypeMemory:
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                           "rokmd:   MEM  %#I64x len=%#x flags=%#x\n",
                           (UINT64)desc->u.Memory.Start.QuadPart,
                           desc->u.Memory.Length,
                           desc->Flags));
                break;
            case CmResourceTypePort:
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                           "rokmd:   PORT %#I64x len=%#x\n",
                           (UINT64)desc->u.Port.Start.QuadPart,
                           desc->u.Port.Length));
                break;
            case CmResourceTypeInterrupt:
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                           "rokmd:   IRQ  level=%u vector=%u\n",
                           desc->u.Interrupt.Level,
                           desc->u.Interrupt.Vector));
                break;
            default:
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                           "rokmd:   res type=%u\n", desc->Type));
                break;
            }
        }
    }
}

BOOLEAN
RokDdiInterruptRoutine(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_ULONG       MessageNumber
    )
{
    // 未启用硬件中断前不会进入；M3 接 IH ring 后改为 TRUE + DPC 排队。
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(MessageNumber);
    return FALSE;
}

VOID
RokDdiDpcRoutine(
    IN_CONST_PVOID MiniportDeviceContext
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
}
