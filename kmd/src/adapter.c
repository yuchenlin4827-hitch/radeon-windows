// SPDX-License-Identifier: MIT
// 适配器生命周期实现：AddDevice / StartDevice / StopDevice / RemoveDevice 等。
// 签名来自 scripts/gen_ddi2.py 生成的 adapter.h（与 WDK 26100 一致）。
#include "precomp.h"
#include "adapter.h"

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

    UNREFERENCED_PARAMETER(DxgkStartInfo);
    PAGED_CODE();

    if (adapter == NULL || DxgkInterface == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // 骨架为 render-only：0 路显示 source、0 个子设备（监视器）。
    // 显示路径（VidPn/Present）留到 M3+，先保证适配器加载稳定。
    *NumberOfVideoPresentSources = 0;
    *NumberOfChildren = 0;

    adapter->PhysicalDeviceObject = NULL;
    adapter->Started = FALSE;

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

    adapter->Started = TRUE;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: StartDevice OK (render-only skeleton)\n"));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiStopDevice(
    IN_CONST_PVOID MiniportDeviceContext
    )
{
    PROK_ADAPTER adapter = (PROK_ADAPTER)MiniportDeviceContext;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(adapter);

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
                           "rokmd:   MEM  %#I64x len=%#x\n",
                           (UINT64)desc->u.Memory.Start.QuadPart,
                           desc->u.Memory.Length));
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
