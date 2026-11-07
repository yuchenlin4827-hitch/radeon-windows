// SPDX-License-Identifier: MIT
// DXGK DDI 骨架实现（stub）—— 由 scripts/gen_ddi2.py 生成，签名勿手改。
// M3 起按 roadmap 逐个落地真实实现。
#include "precomp.h"
#include "adapter.h"
#include "ddi.h"

NTSTATUS
RokDdiDispatchIoRequest(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_ULONG VidPnSourceId,
    IN_PVIDEO_REQUEST_PACKET VideoRequestPacket
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiDispatchIoRequest (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiQueryChildRelations(
    IN_CONST_PVOID MiniportDeviceContext,
    _Inout_updates_bytes_(ChildRelationsSize) PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_ ULONG ChildRelationsSize
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiQueryChildRelations (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiQueryChildStatus(
    IN_CONST_PVOID MiniportDeviceContext,
    INOUT_PDXGK_CHILD_STATUS ChildStatus,
    IN_BOOLEAN NonDestructiveOnly
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiQueryChildStatus (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiQueryDeviceDescriptor(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_ULONG ChildUid,
    INOUT_PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiQueryDeviceDescriptor (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiSetPowerState(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_ULONG DeviceUid,
    IN_DEVICE_POWER_STATE DevicePowerState,
    IN_POWER_ACTION ActionType
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(DeviceUid);
    UNREFERENCED_PARAMETER(ActionType);
    // 骨架阶段接受任何电源状态请求（不做硬件操作）。
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: SetPowerState state=%d\n", DevicePowerState));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiNotifyAcpiEvent(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_DXGK_EVENT_TYPE EventType,
    IN_ULONG Event,
    IN_PVOID Argument,
    OUT_PULONG AcpiFlags
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiNotifyAcpiEvent (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiQueryInterface(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_PQUERY_INTERFACE QueryInterface
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiQueryInterface (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiGetChildContainerId(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG ChildUid,
    _Inout_ PDXGK_CHILD_CONTAINER_ID ContainerId
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiGetChildContainerId (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiQueryAdapterInfo(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_QUERYADAPTERINFO pQueryAdapterInfo
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);

    switch (pQueryAdapterInfo->Type)
    {
    case DXGKQAITYPE_DRIVERCAPS:
        // 全零 DXGK_DRIVERCAPS = 声明"最朴素的驱动能力"，足够让适配器进入工作状态。
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DRIVERCAPS))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(pQueryAdapterInfo->pOutputData, sizeof(DXGK_DRIVERCAPS));
        return STATUS_SUCCESS;

    default:
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: QueryAdapterInfo type=%u (stub)\n", pQueryAdapterInfo->Type));
        return STATUS_NOT_IMPLEMENTED;
    }
}

NTSTATUS
RokDdiCreateDevice(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CREATEDEVICE pCreateDevice
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);

    {
        PROK_DEVICE device = (PROK_DEVICE)ExAllocatePoolWithTag(PagedPool, sizeof(ROK_DEVICE), ROK_POOL_TAG);
        if (device == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(device, sizeof(ROK_DEVICE));
        device->hAdapter = hAdapter;
        pCreateDevice->hDevice = device;
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: CreateDevice\n"));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiDestroyDevice(
    IN_CONST_HANDLE hDevice
    )
{
    PAGED_CODE();
    if (hDevice != NULL)
    {
        ExFreePoolWithTag(hDevice, ROK_POOL_TAG);
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: DestroyDevice\n"));
}

NTSTATUS
RokDdiCreateAllocation(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CREATEALLOCATION pCreateAllocation
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiCreateAllocation (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiDestroyAllocation(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_DESTROYALLOCATION pDestroyAllocation
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiDestroyAllocation (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiDescribeAllocation(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_DESCRIBEALLOCATION pDescribeAllocation
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiDescribeAllocation (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiGetStandardAllocationDriverData(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA pGetStandardAllocationDriverData
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiGetStandardAllocationDriverData (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiPatch(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_PATCH pPatch
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiPatch (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiSubmitCommand(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMAND pSubmitCommand
    )
{
    // M3 核心：把 OS 递交的 DMA buffer 以 PM4 INDIRECT_BUFFER 形式
    // 写入 CP ring（对照 Linux radeon 的 radeon_ring_commit / r600_ib_*）。
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSubmitCommand);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "rokmd: SubmitCommand (stub!)\n"));
}

NTSTATUS
RokDdiPreemptCommand(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_PREEMPTCOMMAND pPreemptCommand
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiPreemptCommand (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiBuildPagingBuffer(
    IN_CONST_HANDLE hAdapter,
    IN_PDXGKARG_BUILDPAGINGBUFFER pBuildPagingBuffer
    )
{
    // VidMm 请求生成页表迁移命令（对应 radeon 的 GART/VM 更新包）。
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pBuildPagingBuffer);
    PAGED_CODE();
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "rokmd: BuildPagingBuffer (stub!)\n"));
}

NTSTATUS
RokDdiResetFromTimeout(
    IN_CONST_HANDLE hAdapter
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiResetFromTimeout (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiEscape(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_ESCAPE pEscape
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiEscape (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiCollectDbgInfo(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_COLLECTDBGINFO pCollectDbgInfo
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiCollectDbgInfo (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiCreateContext(
    IN_CONST_HANDLE hDevice,
    INOUT_PDXGKARG_CREATECONTEXT pCreateContext
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiCreateContext (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiDestroyContext(
    IN_CONST_HANDLE hContext
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiDestroyContext (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiRender(
    IN_CONST_HANDLE hContext,
    INOUT_PDXGKARG_RENDER pRender
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiRender (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiPresent(
    IN_CONST_HANDLE hContext,
    INOUT_PDXGKARG_PRESENT pPresent
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiPresent (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiIsSupportedVidPn(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_ISSUPPORTEDVIDPN pIsSupportedVidPn
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiIsSupportedVidPn (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiRecommendFunctionalVidPn(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_RECOMMENDFUNCTIONALVIDPN_CONST pRecommendFunctionalVidPn
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiRecommendFunctionalVidPn (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiCommitVidPn(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_COMMITVIDPN_CONST pCommitVidPn
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiCommitVidPn (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiUpdateActiveVidPnPresentPath(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_UPDATEACTIVEVIDPNPRESENTPATH_CONST pUpdateActiveVidPnPresentPath
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiUpdateActiveVidPnPresentPath (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiRecommendMonitorModes(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_RECOMMENDMONITORMODES_CONST pRecommendMonitorModes
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiRecommendMonitorModes (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiSetVidPnSourceAddress(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESS pSetVidPnSourceAddress
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiSetVidPnSourceAddress (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiSetVidPnSourceVisibility(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEVISIBILITY pSetVidPnSourceVisibility
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiSetVidPnSourceVisibility (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiControlInterrupt(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_DXGK_INTERRUPT_TYPE InterruptType,
    IN_BOOLEAN EnableInterrupt
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiControlInterrupt (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiQueryDependentEngineGroup(
    IN_CONST_HANDLE hAdapter,
    INOUT_DXGKARG_QUERYDEPENDENTENGINEGROUP pQueryDependentEngineGroup
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    // 骨架：声明无其他节点依赖该引擎。
    pQueryDependentEngineGroup->DependentNodeOrdinalMask = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiQueryEngineStatus(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_QUERYENGINESTATUS pQueryEngineStatus
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    // 骨架：报告引擎正常推进，避免调度器进入恢复流程。
    pQueryEngineStatus->EngineStatus.Responsive = 1;
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiResetEngine(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_RESETENGINE pResetEngine
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiResetEngine (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiSubmitCommandVirtual(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMANDVIRTUAL pSubmitCommand
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(0);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: RokDdiSubmitCommandVirtual (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

