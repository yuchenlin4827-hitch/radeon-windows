// SPDX-License-Identifier: MIT
// DXGK DDI 实现（WDDM 2.0，M2.5"兼容完成"里程碑）。
//
// 签名由 scripts/gen_ddi2.py 从 WDK 26100 头文件生成（勿手改）；
// 函数体为 M2.5 的真实最小实现：
//
//   * QueryAdapterInfo —— DRIVERCAPS（MultiEngineAware/NoDmaPatching/1 节点/
//     WDDMv2）、QUERYSEGMENT3/4（0 段：全系统内存，VidMm 无分页）、GPUMMUCAPS；
//   * 设备/分配/上下文/进程 —— 完整生命周期簿记（0 段模式下 VidMm 以
//     系统内存承载分配，OpenAllocation 经 DxgkCbGetHandleData 映射全局句柄）；
//   * SubmitCommand/Virtual —— "即时完成"模型：M2 无真实 GPU，提交即
//     标记 fence 完成，OS 经 QueryCurrentFence 轮询回收 DMA buffer，
//     不产生假死/TDR；M3 换 PM4 ring 提交 + IH 上报；
//   * BuildPagingBuffer —— 写入 TeraScale PM4 type-3 NOP（0xC0001000）；
//   * Escape —— rok_escape.h 契约：版本/PCI/显存/能力查询，
//     M4 的 terakan ICD 用同一 ABI 与本 KMD 通信。
//
// IRQL 约定（对照 d3dkmddi.h 的 _IRQL_requires_ 注解）：
//   DISPATCH_LEVEL：SubmitCommand、PreemptCommand —— 无 PAGED_CODE、
//                   无池分配，只用自旋锁/原子操作；
//   PASSIVE_LEVEL ：其余 DDI。
#include "precomp.h"
#include "adapter.h"
#include "ddi.h"
#include "rok_escape.h"

// 每次提交随 DMA buffer 传递的私有数据（ContextInfo.DmaBufferPrivateDataSize）。
typedef struct _ROK_SUBMIT_PRIV
{
    UINT64 FenceId;
    UINT32 NodeOrdinal;
    UINT32 Flags;
} ROK_SUBMIT_PRIV;

// TeraScale PM4：type-3 包头 = 0xC0000000 | (count-1)<<16 | opcode<<8；
// IT_NOP（opcode 0x10）无参数体即单 DWORD 0xC0001000。
#define ROK_PM4_NOP_DWORD   0xC0001000UL
#define ROK_PM4_NOP_MAX     256

// ------------------------------------------------------------------
// 辅助
// ------------------------------------------------------------------

static PROK_ADAPTER
RokAdapterFromDevice(
    IN_CONST_HANDLE hDevice
    )
{
    PROK_DEVICE device = (PROK_DEVICE)hDevice;
    return (device != NULL) ? device->Adapter : NULL;
}

static PROK_CONTEXT
RokFindContextLocked(
    _In_ PROK_ADAPTER Adapter,
    _In_ CONST HANDLE hContext
    )
{
    PLIST_ENTRY link;
    PROK_CONTEXT ctx;

    for (link = Adapter->ContextList.Flink;
         link != &Adapter->ContextList;
         link = link->Flink)
    {
        ctx = CONTAINING_RECORD(link, ROK_CONTEXT, Link);
        if ((HANDLE)ctx == hContext)
        {
            return ctx;
        }
    }
    return NULL;
}

static PROK_ALLOCATION
RokFindAllocationLocked(
    _In_ PROK_ADAPTER Adapter,
    _In_ CONST HANDLE hAllocation
    )
{
    PLIST_ENTRY link;
    PROK_ALLOCATION rec;

    for (link = Adapter->AllocationList.Flink;
         link != &Adapter->AllocationList;
         link = link->Flink)
    {
        rec = CONTAINING_RECORD(link, ROK_ALLOCATION, Link);
        if ((HANDLE)rec == hAllocation)
        {
            return rec;
        }
    }
    return NULL;
}

// ------------------------------------------------------------------
// PnP / 电源 / 子设备
// ------------------------------------------------------------------

NTSTATUS
RokDdiDispatchIoRequest(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_ULONG VidPnSourceId,
    IN_PVIDEO_REQUEST_PACKET VideoRequestPacket
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(VidPnSourceId);

    // 无显示路径：拒绝一切视频请求包。
    // WDK 26100: VIDEO_REQUEST_PACKET.StatusBlock 为 PSTATUS_BLOCK（指针）。
    if (VideoRequestPacket->StatusBlock != NULL)
    {
        VideoRequestPacket->StatusBlock->Status = STATUS_NOT_SUPPORTED;
        VideoRequestPacket->StatusBlock->Information = 0;
    }
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
RokDdiQueryChildRelations(
    IN_CONST_PVOID MiniportDeviceContext,
    _Inout_updates_bytes_(ChildRelationsSize) PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_ ULONG ChildRelationsSize
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    // render-only：0 个子设备（HPD/监视器），描述符数组整体置零即可。
    if (ChildRelations != NULL && ChildRelationsSize > 0)
    {
        RtlZeroMemory(ChildRelations, ChildRelationsSize);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiQueryChildStatus(
    IN_CONST_PVOID MiniportDeviceContext,
    INOUT_PDXGK_CHILD_STATUS ChildStatus,
    IN_BOOLEAN NonDestructiveOnly
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);

    // 0 子设备：OS 不应调用；防御性置零返回成功。
    if (ChildStatus != NULL)
    {
        RtlZeroMemory(ChildStatus, sizeof(*ChildStatus));
    }
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiQueryDeviceDescriptor(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_ULONG ChildUid,
    INOUT_PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(DeviceDescriptor);

    // 无子设备：ChildUid 无效。
    return STATUS_INVALID_PARAMETER;
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
    // 接受任意电源状态（不做硬件操作）。M3+ 在 D3 时挂起 ring/IH。
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
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(Event);
    UNREFERENCED_PARAMETER(Argument);

    // 未处理，OS 可继续默认链路（M3+ 处理 ATIF/ATCS）。
    if (AcpiFlags != NULL)
    {
        *AcpiFlags = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiQueryInterface(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_PQUERY_INTERFACE QueryInterface
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(QueryInterface);
    // 暂无导出接口（M3 可经此暴露 bus interface 供配置空间直读）。
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
RokDdiGetChildContainerId(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG ChildUid,
    _Inout_ PDXGK_CHILD_CONTAINER_ID ContainerId
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(ContainerId);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiNotifySurpriseRemoval(
    _In_ PVOID MiniportDeviceContext,
    _In_ DXGK_SURPRISE_REMOVAL_TYPE RemovalType
    )
{
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
               "rokmd: NotifySurpriseRemoval ctx=%p type=%d\n",
               MiniportDeviceContext, RemovalType));
    return STATUS_SUCCESS;
}

// ------------------------------------------------------------------
// QueryAdapterInfo
// ------------------------------------------------------------------

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
    {
        DXGK_DRIVERCAPS *caps;
        DXGK_VIDSCHCAPS sch;

        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DRIVERCAPS))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        caps = (DXGK_DRIVERCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(caps, sizeof(DXGK_DRIVERCAPS));

        // DMA buffer 可落在任意物理地址（无硬件限制；M3 按真实 DMA 掩码收紧）。
        caps->HighestAcceptableAddress.QuadPart = (LONGLONG)(~(ULONGLONG)0);

        // 引擎拓扑：单一通用引擎（对应 radeon 的 family ring）。
        caps->GpuEngineTopology.NbAsymetricProcessingNodes = 1;

        // 调度器能力：多引擎感知（Context/SubmitCommand 生命周期）、
        // 免 patch（提交地址即最终地址）、不支持 64 位原子。
        RtlZeroMemory(&sch, sizeof(sch));
        sch.MultiEngineAware    = 1;
        sch.PreemptionAware     = 1;
        sch.NoDmaPatching       = 1;
        sch.No64BitAtomics      = 1;
        caps->SchedulingCaps    = sch;

        // 显存管理能力：0 段模式下 VidMm 把全部分配放在系统内存，
        // 驻留/驱逐分页不会发生 —— MemoryManagementCaps 保持全零。
        // M3 引入 VRAM/GTT 段时在此上报对应位（对照 VidMm 文档）。

        // PreemptionCaps 全零 = 不支持抢占粒度细分（OS 仍会在必要时
        // 调用 PreemptCommand，见 SchedulingCaps.PreemptionAware）。

        // 惊喜移除已实现（NotifySurpriseRemoval 返回成功）：显式声明，
        // 设备热拔时 OS 走标准通知链而非强杀路径。
        caps->SupportSurpriseRemoval = TRUE;

        caps->WDDMVersion = DXGKDDI_WDDMv2;

        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: QueryAdapterInfo DRIVERCAPS (nodes=1, MultiEngineAware)\n"));
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT3:
    {
        DXGK_QUERYSEGMENTOUT3 *out;

        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT3))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        out = (DXGK_QUERYSEGMENTOUT3 *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(out, sizeof(DXGK_QUERYSEGMENTOUT3));
        out->NbSegment = 0;   // M3：上报 VRAM / GTT 两个段

        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: QueryAdapterInfo QUERYSEGMENT3 (0 segments, sysmem-only)\n"));
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT4:
    {
        DXGK_QUERYSEGMENTOUT4 *out;

        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT4))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        out = (DXGK_QUERYSEGMENTOUT4 *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(out, sizeof(DXGK_QUERYSEGMENTOUT4));
        out->NbSegment = 0;   // WDDM 2.0 主路径；M3 填 DXGK_SEGMENTDESCRIPTOR4

        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: QueryAdapterInfo QUERYSEGMENT4 (0 segments, sysmem-only)\n"));
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_GPUMMUCAPS:
    {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_GPUMMUCAPS))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(pQueryAdapterInfo->pOutputData, sizeof(DXGK_GPUMMUCAPS));
        // 全零 = 暂不支持 GPU MMU（M3 接 r600 VM 页表后上报
        // PageTableLevelDesc / ZeroBasedPagingRoot 等）。

        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: QueryAdapterInfo GPUMMUCAPS (none)\n"));
        return STATUS_SUCCESS;
    }

    default:
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: QueryAdapterInfo type=%u (not implemented)\n", pQueryAdapterInfo->Type));
        return STATUS_NOT_IMPLEMENTED;
    }
}

// ------------------------------------------------------------------
// 设备
// ------------------------------------------------------------------

NTSTATUS
RokDdiCreateDevice(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CREATEDEVICE pCreateDevice
    )
{
    PROK_DEVICE device;

    PAGED_CODE();

    if ((PROK_ADAPTER)hAdapter == NULL || pCreateDevice == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    device = (PROK_DEVICE)ExAllocatePoolWithTag(PagedPool, sizeof(ROK_DEVICE), ROK_POOL_TAG);
    if (device == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(device, sizeof(ROK_DEVICE));
    device->Adapter = (PROK_ADAPTER)hAdapter;
    pCreateDevice->hDevice = device;

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
        ExFreePoolWithTag((PVOID)hDevice, ROK_POOL_TAG);
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: DestroyDevice\n"));
    return STATUS_SUCCESS;
}

// ------------------------------------------------------------------
// 分配
// ------------------------------------------------------------------

NTSTATUS
RokDdiCreateAllocation(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CREATEALLOCATION pCreateAllocation
    )
{
    PROK_ADAPTER adapter = (PROK_ADAPTER)hAdapter;
    UINT i;
    KIRQL oldIrql;

    PAGED_CODE();

    if (adapter == NULL || pCreateAllocation->pAllocationInfo == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < pCreateAllocation->NumAllocations; i++)
    {
        DXGK_ALLOCATIONINFO *info = &pCreateAllocation->pAllocationInfo[i];
        PROK_ALLOCATION record;

        record = (PROK_ALLOCATION)ExAllocatePoolWithTag(
            PagedPool, sizeof(ROK_ALLOCATION), ROK_POOL_TAG);
        if (record == NULL)
        {
            // 回滚本批已登记的分配（OS 会随后调用 DestroyAllocation，
            // 这里保持自身状态自洽）。
            UINT j;
            for (j = 0; j < i; j++)
            {
                PROK_ALLOCATION rec = (PROK_ALLOCATION)pCreateAllocation->pAllocationInfo[j].hAllocation;
                if (rec != NULL)
                {
                    KeAcquireSpinLock(&adapter->AllocationLock, &oldIrql);
                    RemoveEntryList(&rec->Link);
                    KeReleaseSpinLock(&adapter->AllocationLock, oldIrql);
                    ExFreePoolWithTag(rec, ROK_POOL_TAG);
                }
            }
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        record->Adapter = adapter;
        record->Seq = ++adapter->AllocationSeq;
        InitializeListHead(&record->Link);

        // 0 段模式：分配由 VidMm 以系统内存承载，驱动不设 Size/Flags/段偏好。
        // hAllocation 返回记录指针，后续 OpenAllocation/提交路径引用。
        info->hAllocation = record;

        KeAcquireSpinLock(&adapter->AllocationLock, &oldIrql);
        InsertTailList(&adapter->AllocationList, &record->Link);
        KeReleaseSpinLock(&adapter->AllocationLock, oldIrql);
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: CreateAllocation count=%u resource=%p resourceFlag=%u\n",
               pCreateAllocation->NumAllocations,
               pCreateAllocation->hResource,
               pCreateAllocation->Flags.Resource));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiDestroyAllocation(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_DESTROYALLOCATION pDestroyAllocation
    )
{
    PROK_ADAPTER adapter = (PROK_ADAPTER)hAdapter;
    UINT i;
    KIRQL oldIrql;

    PAGED_CODE();

    if (adapter == NULL || pDestroyAllocation->pAllocationList == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < pDestroyAllocation->NumAllocations; i++)
    {
        PROK_ALLOCATION record = (PROK_ALLOCATION)pDestroyAllocation->pAllocationList[i];
        BOOLEAN removed = FALSE;

        if (record == NULL)
        {
            continue;
        }

        KeAcquireSpinLock(&adapter->AllocationLock, &oldIrql);
        if (!IsListEmpty(&adapter->AllocationList) &&
            RokFindAllocationLocked(adapter, (HANDLE)record) != NULL)
        {
            RemoveEntryList(&record->Link);
            removed = TRUE;
        }
        KeReleaseSpinLock(&adapter->AllocationLock, oldIrql);

        if (removed)
        {
            ExFreePoolWithTag(record, ROK_POOL_TAG);
        }
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: DestroyAllocation count=%u\n",
               pDestroyAllocation->NumAllocations));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiOpenAllocation(
    IN_CONST_HANDLE hDevice,
    IN_CONST_PDXGKARG_OPENALLOCATION pOpenAllocation
    )
{
    PROK_ADAPTER adapter = RokAdapterFromDevice(hDevice);
    UINT i;

    PAGED_CODE();

    if (adapter == NULL || pOpenAllocation->pOpenAllocation == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // WDK 26100: DXGKARG_OPENALLOCATION 携带 DXGK_OPENALLOCATIONINFO* 数组。
    if (pOpenAllocation->pOpenAllocation == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < pOpenAllocation->NumAllocations; i++)
    {
        DXGKARGCB_GETHANDLEDATA query;
        VOID *data;

        RtlZeroMemory(&query, sizeof(query));
        query.hObject = pOpenAllocation->pOpenAllocation[i].hAllocation;
        query.Type = DXGK_HANDLE_ALLOCATION;
        // DeviceSpecific = 0：取 CreateAllocation 登记的分配级私有数据
        //（= info->hAllocation，即 ROK_ALLOCATION 记录指针）。
        query.Flags.DeviceSpecific = 0;

        data = adapter->DxgkInterface.DxgkCbGetHandleData(&query);
        if (data == NULL)
        {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
                       "rokmd: OpenAllocation — bad allocation handle 0x%p\n",
                       (PVOID)(ULONG_PTR)pOpenAllocation->pOpenAllocation[i].hAllocation));
            return STATUS_INVALID_HANDLE;
        }

        // 设备级打开句柄 = 同一记录指针；提交路径仅做簿记。
        pOpenAllocation->pOpenAllocation[i].hDeviceSpecificAllocation = data;
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: OpenAllocation count=%u\n", pOpenAllocation->NumAllocations));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiCloseAllocation(
    IN_CONST_HANDLE hDevice,
    IN_CONST_PDXGKARG_CLOSEALLOCATION pCloseAllocation
    )
{
    // 设备级打开句柄（hDeviceSpecificAllocation = 记录指针）无需释放，
    // 记录本体由 DestroyAllocation 管理。
    PAGED_CODE();
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: CloseAllocation count=%u\n",
               (pCloseAllocation != NULL) ? pCloseAllocation->NumAllocations : 0));
    UNREFERENCED_PARAMETER(hDevice);
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiDescribeAllocation(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_DESCRIBEALLOCATION pDescribeAllocation
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pDescribeAllocation);
    // 仅 primary/共享分配需要 DescribeAllocation；0 段 render-only
    // 阶段不存在这类分配（GetStandardAllocationDriverData 同样未实现）。
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiGetStandardAllocationDriverData(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA pGetStandardAllocationDriverData
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pGetStandardAllocationDriverData);
    // GDI/主表面标准分配：显示路径（M3+ VidPn/Present）落地前不支持。
    return STATUS_NOT_IMPLEMENTED;
}

// ------------------------------------------------------------------
// 上下文
// ------------------------------------------------------------------

NTSTATUS
RokDdiCreateContext(
    IN_CONST_HANDLE hDevice,
    INOUT_PDXGKARG_CREATECONTEXT pCreateContext
    )
{
    PROK_ADAPTER adapter = RokAdapterFromDevice(hDevice);
    PROK_CONTEXT ctx;
    KIRQL oldIrql;

    PAGED_CODE();

    if (adapter == NULL || pCreateContext == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    ctx = (PROK_CONTEXT)ExAllocatePoolWithTag(PagedPool, sizeof(ROK_CONTEXT), ROK_POOL_TAG);
    if (ctx == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ctx->Adapter = adapter;
    ctx->NodeOrdinal = pCreateContext->NodeOrdinal;
    ctx->EngineAffinity = pCreateContext->EngineAffinity;
    ctx->Seq = ++adapter->ContextSeq;
    InitializeListHead(&ctx->Link);

    // DMA buffer：系统内存承载（SegmentSet=0），64KB/缓冲；
    // 私有数据区携带提交元数据；免 patch、无 patch location。
    RtlZeroMemory(&pCreateContext->ContextInfo, sizeof(pCreateContext->ContextInfo));
    pCreateContext->ContextInfo.DmaBufferSize            = 0x10000;
    pCreateContext->ContextInfo.DmaBufferSegmentSet      = 0;
    pCreateContext->ContextInfo.DmaBufferPrivateDataSize = sizeof(ROK_SUBMIT_PRIV);
    pCreateContext->ContextInfo.AllocationListSize       = 32;
    pCreateContext->ContextInfo.PatchLocationListSize    = 0;
    pCreateContext->ContextInfo.Caps.NoPatchingRequired  = 1;
    pCreateContext->ContextInfo.PagingCompanionNodeId    = 0;

    KeAcquireSpinLock(&adapter->ContextLock, &oldIrql);
    InsertTailList(&adapter->ContextList, &ctx->Link);
    KeReleaseSpinLock(&adapter->ContextLock, oldIrql);

    pCreateContext->hContext = ctx;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: CreateContext node=%u engine=%u seq=%u\n",
               ctx->NodeOrdinal, ctx->EngineAffinity, ctx->Seq));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiDestroyContext(
    IN_CONST_HANDLE hContext
    )
{
    PROK_CONTEXT ctx = (PROK_CONTEXT)hContext;
    KIRQL oldIrql;
    BOOLEAN removed = FALSE;

    PAGED_CODE();

    if (ctx != NULL && ctx->Adapter != NULL)
    {
        KeAcquireSpinLock(&ctx->Adapter->ContextLock, &oldIrql);
        if (RokFindContextLocked(ctx->Adapter, hContext) != NULL)
        {
            RemoveEntryList(&ctx->Link);
            removed = TRUE;
        }
        KeReleaseSpinLock(&ctx->Adapter->ContextLock, oldIrql);

        if (removed)
        {
            ExFreePoolWithTag(ctx, ROK_POOL_TAG);
        }
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: DestroyContext (removed=%u)\n", removed));
    return STATUS_SUCCESS;
}

// ------------------------------------------------------------------
// 命令提交（M3 核心；M2.5 为"即时完成"模型）
// ------------------------------------------------------------------

// DISPATCH_LEVEL：无 PAGED_CODE、无池分配。
NTSTATUS
RokDdiSubmitCommand(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMAND pSubmitCommand
    )
{
    PROK_ADAPTER adapter = (PROK_ADAPTER)hAdapter;

    if (adapter == NULL || pSubmitCommand == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    // 即时完成：无真实 GPU 前，提交即视为执行完毕。
    // OS 经 DxgkDdiQueryCurrentFence 轮询该值回收 DMA buffer。
    // M3：把 DMA buffer 以 PM4 INDIRECT_BUFFER 写入 CP ring（对照
    // Linux radeon 的 radeon_ring_commit / r600_ib_*），fence 由 IH 上报。
    InterlockedExchange(&adapter->LastCompletedFenceId,
                        (LONG)pSubmitCommand->SubmissionFenceId);

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL,
               "rokmd: SubmitCommand fence=%u ctx=%p size=%u\n",
               pSubmitCommand->SubmissionFenceId,
               pSubmitCommand->hContext,
               pSubmitCommand->DmaBufferSize));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiSubmitCommandVirtual(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMANDVIRTUAL pSubmitCommand
    )
{
    PROK_ADAPTER adapter = (PROK_ADAPTER)hAdapter;

    PAGED_CODE();

    if (adapter == NULL || pSubmitCommand == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    // WDDM 2.0 虚拟地址提交路径（UMD/ICD 直填 GPU VA 的 DMA buffer）。
    // M4 的 terakan ICD 命令流将走本通道；当前同样即时完成。
    InterlockedExchange(&adapter->LastCompletedFenceId,
                        (LONG)pSubmitCommand->SubmissionFenceId);

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL,
               "rokmd: SubmitCommandVirtual fence=%u ctx=%p va=%I64x size=%u\n",
               pSubmitCommand->SubmissionFenceId,
               pSubmitCommand->hContext,
               (UINT64)pSubmitCommand->DmaBufferVirtualAddress,
               pSubmitCommand->DmaBufferSize));
    return STATUS_SUCCESS;
}

// DISPATCH_LEVEL。
NTSTATUS
RokDdiPreemptCommand(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_PREEMPTCOMMAND pPreemptCommand
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPreemptCommand);
    // 即时完成模型下提交已被视作完成，抢占无事可做。
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiPatch(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_PATCH pPatch
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPatch);
    // SchedulingCaps.NoDmaPatching=1：OS 不会调用；保险起见仍返回成功。
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiCancelCommand(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_CANCELCOMMAND pCancelCommand
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCancelCommand);
    // SchedulingCaps.CancelCommandAware=0：OS 不会调用。
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiQueryCurrentFence(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_QUERYCURRENTFENCE pCurrentFence
    )
{
    PROK_ADAPTER adapter = (PROK_ADAPTER)hAdapter;

    PAGED_CODE();

    if (adapter == NULL || pCurrentFence == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    // 无中断的完成上报通道：OS 周期性轮询当前已完成的 fence id。
    pCurrentFence->CurrentFence = (UINT)adapter->LastCompletedFenceId;
    return STATUS_SUCCESS;
}

// ------------------------------------------------------------------
// 分页 buffer（VidMm 驻留路径）
// ------------------------------------------------------------------

NTSTATUS
RokDdiBuildPagingBuffer(
    IN_CONST_HANDLE hAdapter,
    IN_PDXGKARG_BUILDPAGINGBUFFER pBuildPagingBuffer
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);

    if (pBuildPagingBuffer == NULL || pBuildPagingBuffer->pDmaBuffer == NULL)
    {
        return STATUS_SUCCESS;   // 无 buffer 可填（0 段模式下不应发生）
    }

    // 填充 PM4 type-3 NOP（0xC0001000）：真实驱动在此生成
    // GART/VM 页表更新（对照 Linux radeon 的 r600_vm 系列包），
    // M3 的 TransferVirtual/UpdatePageTable/NotifyResidency 操作逐个落地。
    {
        UINT count = pBuildPagingBuffer->DmaSize / sizeof(UINT32);
        UINT32 *dw = (UINT32 *)pBuildPagingBuffer->pDmaBuffer;
        UINT i;

        if (count > ROK_PM4_NOP_MAX)
        {
            count = ROK_PM4_NOP_MAX;
        }

        for (i = 0; i < count; i++)
        {
            dw[i] = ROK_PM4_NOP_DWORD;
        }
        pBuildPagingBuffer->DmaBufferWriteOffset = count * sizeof(UINT32);
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: BuildPagingBuffer op=%u dma=%p size=%u\n",
               (UINT)pBuildPagingBuffer->Operation,
               pBuildPagingBuffer->pDmaBuffer,
               pBuildPagingBuffer->DmaSize));
    return STATUS_SUCCESS;
}

// ------------------------------------------------------------------
// 进程（WDDM 2.0）
// ------------------------------------------------------------------

NTSTATUS
RokDdiCreateProcess(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CREATEPROCESS pArgs
    )
{
    PROK_ADAPTER adapter = (PROK_ADAPTER)hAdapter;
    PROK_PROCESS process;

    PAGED_CODE();

    if (adapter == NULL || pArgs == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    process = (PROK_PROCESS)ExAllocatePoolWithTag(PagedPool, sizeof(ROK_PROCESS), ROK_POOL_TAG);
    if (process == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    process->Adapter = adapter;
    process->hDxgkProcess = pArgs->hDxgkProcess;

    pArgs->hKmdProcess = process;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: CreateProcess dxgkProcess=%p\n", pArgs->hDxgkProcess));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiDestroyProcess(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hKmdProcess
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);

    if (hKmdProcess != NULL)
    {
        ExFreePoolWithTag((PVOID)hKmdProcess, ROK_POOL_TAG);
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "rokmd: DestroyProcess\n"));
    return STATUS_SUCCESS;
}

// ------------------------------------------------------------------
// Escape（rok_escape.h 契约 —— M4 terakan ICD 通道）
// ------------------------------------------------------------------

NTSTATUS
RokDdiEscape(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_ESCAPE pEscape
    )
{
    PROK_ADAPTER adapter;
    PROK_DEVICE device;
    PROK_ESCAPE_HEADER header;

    PAGED_CODE();

    if (pEscape == NULL ||
        pEscape->pPrivateDriverData == NULL ||
        pEscape->PrivateDriverDataSize < sizeof(ROK_ESCAPE_HEADER))
    {
        return STATUS_INVALID_PARAMETER;
    }

    // 适配器级支持：hDevice 可为 NULL（D3DKMTEscape 的 hDevice=0 调用形态，
    // gpu_probe / 探针工具用）；此时退回适配器上下文。
    device = (PROK_DEVICE)pEscape->hDevice;
    if (device != NULL && device->Adapter != NULL)
    {
        adapter = device->Adapter;
    }
    else
    {
        adapter = (PROK_ADAPTER)hAdapter;
    }
    if (adapter == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    header = (PROK_ESCAPE_HEADER)pEscape->pPrivateDriverData;
    if (header->Magic != ROK_ESCAPE_MAGIC)
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
                   "rokmd: Escape bad magic 0x%08X\n", header->Magic));
        return STATUS_INVALID_PARAMETER;
    }
    if (header->Version != ROK_ESCAPE_VERSION)
    {
        return STATUS_REVISION_MISMATCH;
    }

    // 输出紧随报文头写入（缓冲区 = [HEADER][OUT]，调用方需预留 16+outSize）。
    switch (header->Type)
    {
    case ROK_ESC_TYPE_QUERY_VERSION:
    {
        PROK_ESC_VERSION_OUT out;
        if (pEscape->PrivateDriverDataSize <
            sizeof(ROK_ESCAPE_HEADER) + sizeof(ROK_ESC_VERSION_OUT))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        out = (PROK_ESC_VERSION_OUT)(header + 1);
        out->Major = 0;
        out->Minor = 1;
        out->Patch = 0;
        out->WddmInterfaceVersion = (UINT32)DXGKDDI_INTERFACE_VERSION;
        return STATUS_SUCCESS;
    }

    case ROK_ESC_TYPE_QUERY_PCI_ID:
    {
        PROK_ESC_PCI_ID_OUT out;
        if (pEscape->PrivateDriverDataSize <
            sizeof(ROK_ESCAPE_HEADER) + sizeof(ROK_ESC_PCI_ID_OUT))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        out = (PROK_ESC_PCI_ID_OUT)(header + 1);
        out->VendorId = adapter->PciVendorId;
        out->DeviceId = adapter->PciDeviceId;
        out->RevisionId = adapter->PciRevisionId;
        out->Family = RokGpuFamilyFromDeviceId(adapter->PciDeviceId);
        return STATUS_SUCCESS;
    }

    case ROK_ESC_TYPE_QUERY_VRAM:
    {
        PROK_ESC_VRAM_OUT out;
        if (pEscape->PrivateDriverDataSize <
            sizeof(ROK_ESCAPE_HEADER) + sizeof(ROK_ESC_VRAM_OUT))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        out = (PROK_ESC_VRAM_OUT)(header + 1);
        // M3 前 0 段：显存/GTT 尺寸未知。ICD 应改用 QUERY_SEGMENT_INFO。
        out->VramSizeBytes = 0;
        out->GartSizeBytes = 0;
        out->VramSegmentId = 0;
        out->Reserved = 0;
        return STATUS_SUCCESS;
    }

    case ROK_ESC_TYPE_QUERY_CAPS:
    {
        PROK_ESC_CAPS_OUT out;
        if (pEscape->PrivateDriverDataSize <
            sizeof(ROK_ESCAPE_HEADER) + sizeof(ROK_ESC_CAPS_OUT))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        out = (PROK_ESC_CAPS_OUT)(header + 1);
        out->CapsMask = ROK_CAP_PCI_ID_VALID | ROK_CAP_SEGMENT_QUERY;
        if (adapter->MmioVirtualAddress != NULL)
        {
            out->CapsMask |= ROK_CAP_MMIO_MAPPED;
        }
        out->Pm4PacketVersion = 3;   // TeraScale type-3 PM4
        out->Reserved[0] = 0;
        out->Reserved[1] = 0;
        return STATUS_SUCCESS;
    }

    case ROK_ESC_TYPE_QUERY_SEGMENT_INFO:
    {
        PROK_ESC_SEGMENT_INFO_OUT out;
        if (pEscape->PrivateDriverDataSize <
            sizeof(ROK_ESCAPE_HEADER) + sizeof(ROK_ESC_SEGMENT_INFO_OUT))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        out = (PROK_ESC_SEGMENT_INFO_OUT)(header + 1);
        // M3 前全 0：VRAM/GTT 段尚未建立；M3 探测后填充真实尺寸/段号并
        // 置 ROK_SEG_FLAG_*_POPULATED（ICD 依此构造 VkMemoryHeap 布局）。
        out->VramSizeBytes = 0;
        out->GartSizeBytes = 0;
        out->VramSegmentId = 0;
        out->GartSegmentId = 0;
        out->Flags = 0;
        out->Reserved = 0;
        return STATUS_SUCCESS;
    }

    default:
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: Escape type=%u (not implemented)\n", header->Type));
        return STATUS_NOT_IMPLEMENTED;
    }
}

// ------------------------------------------------------------------
// 杂项
// ------------------------------------------------------------------

NTSTATUS
RokDdiCollectDbgInfo(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_COLLECTDBGINFO pCollectDbgInfo
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCollectDbgInfo);
    // 可在此附加上下文（M3：ring 读写指针、fence 进度）供内核转储分析。
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiResetFromTimeout(
    IN_CONST_HANDLE hAdapter
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    // TDR：OS 判定 GPU 挂死。即时完成模型不应触发；M3 实现
    // GRBM 软复位 + ring 重置（对照 r600_gpu_reset）。返回成功让
    // OS 继续标准恢复流程。
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "rokmd: ResetFromTimeout\n"));
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiRender(
    IN_CONST_HANDLE hContext,
    INOUT_PDXGKARG_RENDER pRender
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pRender);
    // Render DDI 服务于旧式系统内存命令缓冲路径（DX9 时代）；WDDM 2.0
    // 的 ICD/UMD 走 SubmitCommand/Virtual，不经过这里。
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiPresent(
    IN_CONST_HANDLE hContext,
    INOUT_PDXGKARG_PRESENT pPresent
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pPresent);
    // render-only 骨架不会走到这里（StartDevice 报告 0 路 source）。
    return STATUS_NOT_IMPLEMENTED;
}

VOID
RokDdiControlEtwLogging(
    IN_BOOLEAN Enable,
    IN_ULONG Flags,
    IN_UCHAR Level
    )
{
    UNREFERENCED_PARAMETER(Enable);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Level);
}

// ------------------------------------------------------------------
// VidPn（render-only 阶段不会被调用；保持正确签名 + NOT_IMPLEMENTED）
// ------------------------------------------------------------------

NTSTATUS
RokDdiIsSupportedVidPn(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_ISSUPPORTEDVIDPN pIsSupportedVidPn
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pIsSupportedVidPn);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiRecommendFunctionalVidPn(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_RECOMMENDFUNCTIONALVIDPN_CONST pRecommendFunctionalVidPn
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendFunctionalVidPn);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiCommitVidPn(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_COMMITVIDPN_CONST pCommitVidPn
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCommitVidPn);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiUpdateActiveVidPnPresentPath(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_UPDATEACTIVEVIDPNPRESENTPATH_CONST pUpdateActiveVidPnPresentPath
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pUpdateActiveVidPnPresentPath);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiRecommendMonitorModes(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_RECOMMENDMONITORMODES_CONST pRecommendMonitorModes
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendMonitorModes);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiSetVidPnSourceAddress(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESS pSetVidPnSourceAddress
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetVidPnSourceAddress);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiSetVidPnSourceVisibility(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEVISIBILITY pSetVidPnSourceVisibility
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetVidPnSourceVisibility);
    return STATUS_NOT_IMPLEMENTED;
}

// ------------------------------------------------------------------
// 引擎管理（WDDM 1.3+）
// ------------------------------------------------------------------

NTSTATUS
RokDdiControlInterrupt(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_DXGK_INTERRUPT_TYPE InterruptType,
    IN_BOOLEAN EnableInterrupt
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(InterruptType);
    UNREFERENCED_PARAMETER(EnableInterrupt);
    // 拒绝使能：IH ring 未接线（M3），OS 按中断不可用处理。
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

    // 单节点拓扑：重置目标节点本身即全部受影响范围。
    pQueryDependentEngineGroup->DependentNodeOrdinalMask =
        (ULONGLONG)1 << pQueryDependentEngineGroup->NodeOrdinal;
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

    // 即时完成模型下引擎永远"有进度"；M3 换真实 GRBM 状态探测。
    pQueryEngineStatus->EngineStatus.Responsive = 1;
    return STATUS_SUCCESS;
}

NTSTATUS
RokDdiResetEngine(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_RESETENGINE pResetEngine
    )
{
    PROK_ADAPTER adapter = (PROK_ADAPTER)hAdapter;

    PAGED_CODE();

    pResetEngine->LastAbortedFenceId =
        (adapter != NULL) ? (UINT)adapter->LastCompletedFenceId : 0;
    return STATUS_SUCCESS;
}

// ------------------------------------------------------------------
// WDDM 2.0 DDI 表补全（M2.6）
//
// 目标：DRIVER_INITIALIZATION_DATA 在 DXGKDDI_INTERFACE_VERSION 0x5023
// 范围内不再有 NULL 成员 —— dxgkrnl 对各版本必填成员做校验，缺项会在
// 加载期被拒绝。分工：
//   * 可达路径给最小正确行为（RestartFromTimeout / GetNodeMetadata /
//     CheckMultiPlaneOverlaySupport* / QueryVidPnHWCapability / 电源类）；
//   * 渲染专用形态不可达的路径（显示 / 覆盖 / 指针 / 调色板 / 交换带 /
//     GDI 表面）返回 STATUS_NOT_IMPLEMENTED，保签名、留观测点。
// IRQL 一律对照 d3dkmddi.h / dispmprt.h 的 _IRQL_ 注解（签名经
// WDK 26100 头文件核对）。
// ------------------------------------------------------------------

// ---- TDR 恢复闭环 ----
// ResetFromTimeout 返回成功后，OS 会调用本 DDI 继续调度。
// PASSIVE_LEVEL。
NTSTATUS
RokDdiRestartFromTimeout(
    IN_CONST_HANDLE hAdapter
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    // 即时完成模型无硬件状态可恢复；M3 在此重置 ring 写指针/ fence 状态。
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: RestartFromTimeout\n"));
    return STATUS_SUCCESS;
}

// ---- 交换带（swizzling range；NumberOfSwizzlingRanges=0，不会被调用）----
// PASSIVE_LEVEL。
NTSTATUS
RokDdiAcquireSwizzlingRange(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_ACQUIRESWIZZLINGRANGE pAcquireSwizzlingRange
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pAcquireSwizzlingRange);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiReleaseSwizzlingRange(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_RELEASESWIZZLINGRANGE pReleaseSwizzlingRange
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pReleaseSwizzlingRange);
    return STATUS_NOT_IMPLEMENTED;
}

// ---- 指针 / 调色板（PointerCaps 全零、无显示路径，不会被调用）----
// PASSIVE_LEVEL。
NTSTATUS
RokDdiSetPalette(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETPALETTE pSetPalette
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetPalette);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiSetPointerPosition(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETPOINTERPOSITION pSetPointerPosition
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetPointerPosition);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiSetPointerShape(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETPOINTERSHAPE pSetPointerShape
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetPointerShape);
    return STATUS_NOT_IMPLEMENTED;
}

// ---- VidPn 其余成员（0 路 source / 0 子设备，不会被调用）----
// PASSIVE_LEVEL。
NTSTATUS
RokDdiEnumVidPnCofuncModality(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_ENUMVIDPNCOFUNCMODALITY_CONST pEnumCofuncModality
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pEnumCofuncModality);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiRecommendVidPnTopology(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_RECOMMENDVIDPNTOPOLOGY_CONST pRecommendVidPnTopology
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendVidPnTopology);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiGetScanLine(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_GETSCANLINE pGetScanLine
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pGetScanLine);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiStopCapture(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_STOPCAPTURE pStopCapture
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pStopCapture);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiSetDisplayPrivateDriverFormat(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETDISPLAYPRIVATEDRIVERFORMAT pSetDisplayPrivateDriverFormat
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetDisplayPrivateDriverFormat);
    return STATUS_NOT_IMPLEMENTED;
}

// ---- 覆盖（MaxOverlays=0，不会被调用）----
// PASSIVE_LEVEL。
NTSTATUS
RokDdiCreateOverlay(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CREATEOVERLAY pCreateOverlay
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCreateOverlay);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiUpdateOverlay(
    IN_CONST_HANDLE hOverlay,
    IN_CONST_PDXGKARG_UPDATEOVERLAY pUpdateOverlay
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hOverlay);
    UNREFERENCED_PARAMETER(pUpdateOverlay);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiFlipOverlay(
    IN_CONST_HANDLE hOverlay,
    IN_CONST_PDXGKARG_FLIPOVERLAY pFlipOverlay
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hOverlay);
    UNREFERENCED_PARAMETER(pFlipOverlay);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiDestroyOverlay(
    IN_CONST_HANDLE hOverlay
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hOverlay);
    return STATUS_NOT_IMPLEMENTED;
}

// ---- Win7 追加成员 ----
// 内核侧系统内存命令缓冲（DX9 时代遗留）：与 DxgkDdiRender 一致地拒绝。
// PASSIVE_LEVEL。
NTSTATUS
RokDdiRenderKm(
    IN_CONST_HANDLE hContext,
    INOUT_PDXGKARG_RENDER pRenderKmArgs
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pRenderKmArgs);
    return STATUS_NOT_IMPLEMENTED;
}

// VidPn 硬件能力探测：渲染专用形态，全部能力位为 0（不支持）。
// PASSIVE_LEVEL。
NTSTATUS
RokDdiQueryVidPnHWCapability(
    IN_CONST_HANDLE i_hAdapter,
    INOUT_PDXGKARG_QUERYVIDPNHWCAPABILITY io_pVidPnHWCaps
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(i_hAdapter);

    if (io_pVidPnHWCaps != NULL)
    {
        RtlZeroMemory(io_pVidPnHWCaps, sizeof(*io_pVidPnHWCaps));
    }
    return STATUS_SUCCESS;
}

// ---- Win8 追加成员 ----
// POST 设备所有权释放（bugcheck 显示路径）：本驱动从未持有 POST，
// 按文档以 STATUS_NOT_SUPPORTED 拒绝。
// PASSIVE_LEVEL。
NTSTATUS
RokDdiStopDeviceAndReleasePostDisplayOwnership(
    _In_ PVOID MiniportDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(DisplayInfo);
    return STATUS_NOT_SUPPORTED;
}

// 系统显示（蓝屏文字渲染）：无显示路径，显式拒绝。
// 无 IRQL 注解（可在 bugcheck 路径调用）：禁止 PAGED_CODE / 池分配。
NTSTATUS
RokDdiSystemDisplayEnable(
    _In_ PVOID MiniportDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT* Width,
    _Out_ UINT* Height,
    _Out_ D3DDDIFORMAT* ColorFormat
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
    UNREFERENCED_PARAMETER(ColorFormat);
    return STATUS_NOT_SUPPORTED;
}

VOID
RokDdiSystemDisplayWrite(
    _In_ PVOID MiniportDeviceContext,
    _In_reads_bytes_(SourceHeight * SourceStride) PVOID Source,
    _In_ UINT SourceWidth,
    _In_ UINT SourceHeight,
    _In_ UINT SourceStride,
    _In_ UINT PositionX,
    _In_ UINT PositionY
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(Source);
    UNREFERENCED_PARAMETER(SourceWidth);
    UNREFERENCED_PARAMETER(SourceHeight);
    UNREFERENCED_PARAMETER(SourceStride);
    UNREFERENCED_PARAMETER(PositionX);
    UNREFERENCED_PARAMETER(PositionY);
}

// ---- WDDM 1.3 追加成员 ----
// 引擎元数据：单一通用节点（NodeOrdinal 0）= 3D 引擎。
// PASSIVE_LEVEL。
NTSTATUS
RokDdiGetNodeMetadata(
    IN_CONST_HANDLE hAdapter,
    UINT NodeOrdinalAndAdapterIndex,
    OUT_PDXGKARG_GETNODEMETADATA pGetNodeMetadata
    )
{
    static const WCHAR engineName[] = L"rokmd-3D";
    UINT node;
    UINT i;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);

    if (pGetNodeMetadata == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // WDDMv2：低 16 位 = node ordinal，高 16 位 = 物理适配器索引。
    node = DXGKNODEMETADATA_GETNODEORDINAL(NodeOrdinalAndAdapterIndex);
    if (DXGKNODEMETADATA_GETPHYSICALADAPTERINDEX(NodeOrdinalAndAdapterIndex) != 0 ||
        node != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(pGetNodeMetadata, sizeof(*pGetNodeMetadata));
    pGetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
    for (i = 0; i < DXGK_MAX_METADATA_NAME_LENGTH &&
                engineName[i] != L'\0' && (i + 1) < DXGK_MAX_METADATA_NAME_LENGTH; i++)
    {
        pGetNodeMetadata->FriendlyName[i] = engineName[i];
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: GetNodeMetadata node=%u -> 3D\n", node));
    return STATUS_SUCCESS;
}

// 中断控制 v2（同 ControlInterrupt：IH 未接线前一律拒绝）。
// PASSIVE_LEVEL。
NTSTATUS
RokDdiControlInterrupt2(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_DXGKARG_CONTROLINTERRUPT2 InterruptControl
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(InterruptControl);
    return STATUS_NOT_IMPLEMENTED;
}

// MPO 支持（v1）：显式回答"不支持"。
// PASSIVE_LEVEL。
NTSTATUS
RokDdiCheckMultiPlaneOverlaySupport(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT pCheckMultiPlaneOverlaySupport
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);

    if (pCheckMultiPlaneOverlaySupport != NULL)
    {
        pCheckMultiPlaneOverlaySupport->Supported = FALSE;
        RtlZeroMemory(&pCheckMultiPlaneOverlaySupport->ReturnInfo,
                      sizeof(pCheckMultiPlaneOverlaySupport->ReturnInfo));
    }
    return STATUS_SUCCESS;
}

// GPU 时钟校准（TDR 历史用；SupportPerEngineTDR=0，不会被调用）。
// _IRQL_requires_max_(DISPATCH_LEVEL)：禁止 PAGED_CODE。
NTSTATUS
RokDdiCalibrateGpuClock(
    IN_CONST_HANDLE hAdapter,
    UINT32 NodeOrdinal,
    UINT32 EngineOrdinal,
    OUT_PDXGKARG_CALIBRATEGPUCLOCK pClockCalibration
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(NodeOrdinal);
    UNREFERENCED_PARAMETER(EngineOrdinal);

    if (pClockCalibration != NULL)
    {
        RtlZeroMemory(pClockCalibration, sizeof(*pClockCalibration));
    }
    return STATUS_NOT_IMPLEMENTED;
}

// 调度历史缓冲格式化（HistoryBuffer 精细调度未启用，不会被调用）。
// DISPATCH_LEVEL：禁止 PAGED_CODE / 池分配。
NTSTATUS
RokDdiFormatHistoryBuffer(
    IN_CONST_HANDLE hContext,
    DXGKARG_FORMATHISTORYBUFFER* pFormatData
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pFormatData);
    return STATUS_NOT_IMPLEMENTED;
}

// ---- WDDM 2.0 追加成员 ----
// GDI 内核命令缓冲（仅 GDI 表面存在时被调用；本驱动不支持
// GetStandardAllocationDriverData，不会有 GDI 表面）。
// PASSIVE_LEVEL。
NTSTATUS
RokDdiRenderGdi(
    IN_CONST_HANDLE hContext,
    INOUT_PDXGKARG_RENDERGDI pRenderGdi
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pRenderGdi);
    return STATUS_NOT_IMPLEMENTED;
}

// GPU MMU 页表（GPUMMUCAPS 全零：VidMm 不经此管理 GPU VA，不会被调用）。
// PASSIVE_LEVEL。
VOID
RokDdiSetRootPageTable(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETROOTPAGETABLE pSetPageTable
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetPageTable);
}

SIZE_T
RokDdiGetRootPageTableSize(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_GETROOTPAGETABLESIZE pArgs
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    // 0 = 无根页表（与 GPUMMUCAPS 全零一致）。
    return 0;
}

// CPU host aperture 映射（仅 aperture 段存在时被调用；0 段模式不适用）。
// PASSIVE_LEVEL。
NTSTATUS
RokDdiMapCpuHostAperture(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_MAPCPUHOSTAPERTURE pArgs
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
RokDdiUnmapCpuHostAperture(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_UNMAPCPUHOSTAPERTURE pArgs
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    return STATUS_NOT_IMPLEMENTED;
}

// MPO 支持（v2）：显式回答"不支持"。
// PASSIVE_LEVEL。
NTSTATUS
RokDdiCheckMultiPlaneOverlaySupport2(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT2 pCheckMultiPlaneOverlaySupport
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);

    if (pCheckMultiPlaneOverlaySupport != NULL)
    {
        pCheckMultiPlaneOverlaySupport->Supported = FALSE;
        RtlZeroMemory(&pCheckMultiPlaneOverlaySupport->ReturnInfo,
                      sizeof(pCheckMultiPlaneOverlaySupport->ReturnInfo));
    }
    return STATUS_SUCCESS;
}

// MPO 翻转地址提交（0 路 source，不会被调用）。
// _IRQL_requires_max_(PROFILE_LEVEL - 1)：按上限处理，禁止 PAGED_CODE。
NTSTATUS
RokDdiSetVidPnSourceAddressWithMultiPlaneOverlay2(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2 pSetVidPnSourceAddressWithMultiPlaneOverlay
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetVidPnSourceAddressWithMultiPlaneOverlay);
    return STATUS_NOT_IMPLEMENTED;
}

// 运行时电源（SupportRuntimePowerManagement=0 不会被调用；
// 接受句柄并忽略，保持 OS 链路畅通）。
// _IRQL_requires_max_(PASSIVE_LEVEL)。
NTSTATUS
RokDdiPowerRuntimeSetDeviceHandle(
    IN_CONST_HANDLE DriverContext,
    HANDLE PoDeviceHandle
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(PoDeviceHandle);

    if (DriverContext == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }
    return STATUS_SUCCESS;
}

// 稳定电源状态（VOID：无法报错，保持无操作）。
// PASSIVE_LEVEL。
VOID
RokDdiSetStablePowerState(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETSTABLEPOWERSTATE pArgs
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
}

// 视频保护区域（内容保护；不支持即拒绝）。
// PASSIVE_LEVEL。
NTSTATUS
RokDdiSetVideoProtectedRegion(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDEOPROTECTEDREGION pArgs
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
    return STATUS_NOT_IMPLEMENTED;
}
