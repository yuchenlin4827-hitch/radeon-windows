// SPDX-License-Identifier: MIT
// DXGK DDI 骨架实现（stub）。
//
// 现阶段除三处外全部返回 STATUS_NOT_IMPLEMENTED：
//   1) QueryAdapterInfo(DRIVERCAPS)：交回全零能力表，保证 OS 能完成适配器初始化；
//   2) CreateDevice/DestroyDevice：分配/释放最小设备上下文；
//   3) 中断回调：FALSE/VOID（未启用硬件中断前不会触发）。
// M3 起按 roadmap 逐个落地（寄存器访问 → VRAM → ring → IH）。
#include "precomp.h"
#include "adapter.h"
#include "ddi.h"

// ---------------- 适配器级 ----------------

NTSTATUS
RokDdiQueryAdapterInfo(
    _In_ CONST HANDLE                    hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    PAGED_CODE();

    switch (pQueryAdapterInfo->Type)
    {
    case DXGKQAITYPE_DRIVERCAPS:
        // 全零 DRIVER_CAPS = 声明"最朴素的驱动能力"：
        // 无 2D 加速、无覆盖、无日志段——骨架阶段足够让适配器进入工作状态。
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DRIVER_CAPS))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(pQueryAdapterInfo->pOutputData, sizeof(DRIVER_CAPS));
        return STATUS_SUCCESS;

    default:
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: QueryAdapterInfo type=%u (stub)\n", pQueryAdapterInfo->Type));
        return STATUS_NOT_IMPLEMENTED;
    }
}

NTSTATUS
RokDdiCreateDevice(
    _In_ HANDLE                  hAdapter,
    _Inout_ DXGKARG_CREATEDEVICE* pCreateDevice
    )
{
    // 设备上下文：骨架阶段只存回链适配器指针。
    PROK_DEVICE device;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);

    device = (PROK_DEVICE)ExAllocatePoolWithTag(PagedPool, sizeof(ROK_DEVICE), ROK_POOL_TAG);
    if (device == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(device, sizeof(ROK_DEVICE));
    device->hAdapter = hAdapter;

    pCreateDevice->hDevice = device;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: CreateDevice\n"));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiCloseAdapter(
    _In_ CONST DXGKARG_CLOSEADAPTER* pCloseAdapter
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(pCloseAdapter);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: CloseAdapter\n"));
    return STATUS_SUCCESS;
}

VOID
RokDdiDestroyDevice(
    _In_ HANDLE hDevice
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
    _In_ CONST HANDLE                 hAdapter,
    _Inout_ DXGKARG_CREATEALLOCATION* pCreateAllocation
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    PAGED_CODE();
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: CreateAllocation count=%u (stub)\n",
               pCreateAllocation->AllocationCount));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiDestroyAllocation(
    _In_ CONST HANDLE                    hAdapter,
    _In_ CONST DXGKARG_DESTROYALLOCATION* pDestroyAllocation
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pDestroyAllocation);
    PAGED_CODE();
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: DestroyAllocation (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiDescribeAllocation(
    _In_ CONST HANDLE                   hAdapter,
    _Inout_ DXGKARG_DESCRIBEALLOCATION* pDescribeAllocation
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pDescribeAllocation);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiGetStandardAllocationDriverData(
    _In_ CONST HANDLE                                  hAdapter,
    _Inout_ DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA*    pGetStandardAllocationDriverData
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pGetStandardAllocationDriverData);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

// ---------------- 上下文 / 命令 ----------------

NTSTATUS
RokDdiCreateContext(
    _In_ HANDLE                  hDevice,
    _Inout_ DXGKARG_CREATECONTEXT* pCreateContext
    )
{
    UNREFERENCED_PARAMETER(hDevice);
    PAGED_CODE();
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: CreateContext engine=%u (stub)\n",
               pCreateContext->EngineAffinity));
    return STATUS_NOT_IMPLEMENTED;
}

VOID
RokDdiDestroyContext(
    _In_ HANDLE hDevice,
    _In_ HANDLE hContext
    )
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(hContext);
    PAGED_CODE();
}

VOID
RokDdiSubmitCommand(
    _In_ CONST HANDLE             hContext,
    _In_ PDXGKARG_SUBMITCOMMAND   pSubmitCommand
    )
{
    // M3 核心：把 OS 递交的 DMA buffer 以 PM4 INDIRECT_BUFFER 形式
    // 写入 CP ring（对照 Linux radeon 的 radeon_ring_commit / r600_ib_*）。
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSubmitCommand);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "rokmd: SubmitCommand (stub!)\n"));
}

NTSTATUS
RokDdiPreemptCommand(
    _In_ CONST HANDLE                    hContext,
    _In_ CONST DXGKARG_PREEMPTCOMMAND*   pPreemptCommand
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pPreemptCommand);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
RokDdiPatch(
    _In_ CONST HANDLE         hAdapter,
    _In_ CONST DXGKARG_PATCH* pPatch
    )
{
    // 重定位补丁：分配地址落定后回填 DMA buffer 中的地址槽。
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPatch);
    PAGED_CODE();
}

VOID
RokDdiBuildPagingBuffer(
    _In_ CONST HANDLE                  hAdapter,
    _Inout_ DXGKARG_BUILDPAGINGBUFFER* pBuildPagingBuffer
    )
{
    // VidMm 请求生成页表迁移命令（对应 radeon 的 GART/VM 更新包）。
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pBuildPagingBuffer);
    PAGED_CODE();
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "rokmd: BuildPagingBuffer (stub!)\n"));
}

VOID
RokDdiResetFromTimeout(
    _In_ CONST PVOID MiniportDeviceContext
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    // TDR：OS 判定 GPU 挂死后调用。M3 起实现 GRBM 软复位 + ring 重置。
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "rokmd: ResetFromTimeout (stub)\n"));
}

// ---------------- 渲染 / 显示 ----------------

NTSTATUS
RokDdiRender(
    _In_ CONST HANDLE       hContext,
    _Inout_ DXGKARG_RENDER* pRender
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pRender);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
RokDdiPresent(
    _In_ CONST HANDLE       hContext,
    _Inout_ DXGKARG_PRESENT* pPresent
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pPresent);
    // render-only 骨架不会走到这里（StartDevice 报告 0 路 source）。
}

NTSTATUS
RokDdiEscape(
    _In_ CONST HANDLE        hAdapter,
    _In_ CONST DXGKARG_ESCAPE* pEscape
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pEscape);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
RokDdiCollectDbgInfo(
    _In_ CONST PVOID                MiniportDeviceContext,
    _In_ PDXGKARG_COLLECTDBGINFO    pCollectDbgInfo
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pCollectDbgInfo);
}

// ---------------- PnP / 电源 / 子设备 ----------------

NTSTATUS
RokDdiQueryChildStatus(
    _In_  CONST PVOID               MiniportDeviceContext,
    _Inout_ DXGKARG_QUERYCHILDSTATUS* pChildStatus,
    _In_  BOOLEAN                   NonDestructiveOnly
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    UNREFERENCED_PARAMETER(pChildStatus);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiQueryDeviceRelations(
    _In_    CONST PVOID                    MiniportDeviceContext,
    _In_    ULONG                          ChildIndex,
    _Inout_ PDXGK_DEVICE_RELATIONS_TYPE    DeviceRelationsType
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildIndex);
    UNREFERENCED_PARAMETER(DeviceRelationsType);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiQueryDescriptor(
    _In_    CONST PVOID                MiniportDeviceContext,
    _In_    ULONG                      ChildIndex,
    _In_    DEVICE_DESCRIPTOR_TYPE     DescriptorType,
    _Inout_ PVOID                      DescriptorBuffer,
    _Inout_ ULONG                      DescriptorBufferSize
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildIndex);
    UNREFERENCED_PARAMETER(DescriptorType);
    UNREFERENCED_PARAMETER(DescriptorBuffer);
    UNREFERENCED_PARAMETER(DescriptorBufferSize);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiSetPowerState(
    _In_ PVOID                 MiniportDeviceContext,
    _In_ ULONG                 DeviceUid,
    _In_ DEVICE_POWER_STATE    DevicePowerState,
    _In_ POWER_ACTION          ActionType
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(DeviceUid);
    UNREFERENCED_PARAMETER(ActionType);
    PAGED_CODE();
    // 先接受任何电源状态请求（不做硬件操作，避免骨架阶段状态机出错）。
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: SetPowerState state=%d\n", DevicePowerState));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiNotifyAcpiEvent(
    _In_    VOID                     *MiniportDeviceContext,
    _In_    ULONG                    DeviceUid,
    _In_    DXGK_ACPI_EVENT_TYPE     EventType,
    _Inout_ PACPI_NOTIFICATION_DATA  NotificationData,
    _Out_   BOOLEAN                  *ReturnStatus
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(DeviceUid);
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(NotificationData);
    PAGED_CODE();
    if (ReturnStatus) { *ReturnStatus = FALSE; }
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiQueryInterface(
    _In_ PVOID             MiniportDeviceContext,
    _In_ PQUERY_INTERFACE  QueryInterface
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(QueryInterface);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiGetChildContainerId(
    _In_    CONST PVOID       MiniportDeviceContext,
    _In_    ULONG             ChildUid,
    _Inout_ PCONTAINER_ID     ContainerId
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(ContainerId);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiDispatchIoRequest(
    _In_  CONST PVOID            MiniportDeviceContext,
    _In_  ULONG                  VidPnSourceId,
    _In_  PVIDEO_REQUEST_PACKET  VideoRequestPacket
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(VideoRequestPacket);
    PAGED_CODE();
    VideoRequestPacket->StatusBlock.Status = STATUS_NOT_IMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

// ---------------- VidPn（render-only 阶段不会被调用） ----------------

NTSTATUS
RokDdiIsSupportedVidPn(
    _In_    CONST VOID                 *MiniportDeviceContext,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN   *pIsSupportedVidPn
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pIsSupportedVidPn);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiRecommendFunctionalVidPn(
    _In_ CONST PVOID                              MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN*  pRecommendFunctionalVidPn
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pRecommendFunctionalVidPn);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiRecommendMonitorModes(
    _In_ CONST PVOID                            MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES*   pRecommendMonitorModes
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pRecommendMonitorModes);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiCommitVidPn(
    _In_ CONST PVOID                    MiniportDeviceContext,
    _In_ CONST DXGKARG_COMMITVIDPN*     pCommitVidPn
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pCommitVidPn);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiUpdateActiveVidPnPresentPath(
    _In_    CONST VOID                                       *MiniportDeviceContext,
    _Inout_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH*      pUpdateActiveVidPnPresentPath
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pUpdateActiveVidPnPresentPath);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiSetVidPnSourceAddress(
    _In_ CONST HANDLE                          hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS*  pSetVidPnSourceAddress
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetVidPnSourceAddress);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
RokDdiSetVidPnSourceVisibility(
    _In_ CONST HANDLE                              hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY*   pSetVidPnSourceVisibility
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetVidPnSourceVisibility);
    PAGED_CODE();
}

// ---------------- 中断 ----------------

BOOLEAN
RokDdiInterruptRoutine(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG       MessageNumber
    )
{
    // 未启用硬件中断前不会进入；M3 接 IH ring 后改为 TRUE + DPC 排队。
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(MessageNumber);
    return FALSE;
}

VOID
RokDdiDpcRoutine(
    _In_ CONST PVOID MiniportDeviceContext
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
}

// ---------------- WDDM 1.3（WIN8）引擎管理 ----------------

NTSTATUS
RokDdiControlInterrupt(
    _In_ CONST HANDLE                 hAdapter,
    _In_ CONST DXGK_INTERRUPT_TYPE    InterruptType,
    _In_ BOOLEAN                      Enable
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(InterruptType);
    PAGED_CODE();
    // 拒绝启用：骨架未接 IH，OS 会按"中断不可用"处理。
    UNREFERENCED_PARAMETER(Enable);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiQueryDependentEngineGroup(
    _In_  CONST HANDLE  hAdapter,
    _Out_ UINT          *pNodeOrdinal,
    _Out_ UINT          *pEngineOrdinal
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    PAGED_CODE();
    *pNodeOrdinal = 0;
    *pEngineOrdinal = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiQueryEngineStatus(
    _In_  CONST HANDLE          hAdapter,
    _In_  UINT                  NodeOrdinal,
    _In_  UINT                  EngineOrdinal,
    _Out_ DXGK_ENGINE_STATUS    *pEngineStatus
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    PAGED_CODE();
    pEngineStatus->ChangeFlags = DXGK_ENGINE_STATUS_CHANGE_TYPE_TRANSITION;
    pEngineStatus->Order = NodeOrdinal;
    pEngineStatus->Ordinal = EngineOrdinal;
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiResetEngine(
    _In_ CONST HANDLE  hAdapter,
    _In_ UINT          NodeOrdinal,
    _In_ UINT          EngineOrdinal
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(NodeOrdinal);
    UNREFERENCED_PARAMETER(EngineOrdinal);
    PAGED_CODE();
    return STATUS_NOT_IMPLEMENTED;
}

// ---------------- WDDM 2.0（WIN10）驻留 / 虚拟提交 ----------------

NTSTATUS
RokDdiMakeResident(
    _In_ HANDLE                  hAdapter,
    _Inout_ DXGKARG_MAKERESIDENT* pMakeResident
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    PAGED_CODE();
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "rokmd: MakeResident (stub!)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiEvict(
    _In_ CONST HANDLE           hAdapter,
    _Inout_ DXGKARG_EVICT*      pEvict
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    PAGED_CODE();
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: Evict (stub)\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiSubmitCommandVirtual(
    _In_ CONST HANDLE                          hContext,
    _In_ CONST DXGKARG_SUBMITCOMMANDVIRTUAL*   pSubmitCommandVirtual
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSubmitCommandVirtual);
    PAGED_CODE();
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "rokmd: SubmitCommandVirtual (stub!)\n"));
    return STATUS_NOT_IMPLEMENTED;
}
