// SPDX-License-Identifier: MIT
// rokmd — Radeon Open Kernel Mode Driver
// DriverEntry：向 dxgkrnl 注册 DDI 表（WDDM 2.0 基线）。
//
// 字段名与 d3dkmddi.h/dispmprt.h 的 DRIVER_INITIALIZATION_DATA 逐一对齐；
// DDI 原型见 adapter.h（生命周期）与 ddi.h（其余，由 scripts/gen_ddi2.py 生成）。
#include "precomp.h"
#include "adapter.h"
#include "ddi.h"

static DRIVER_INITIALIZATION_DATA g_DdiData;

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS status;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: DriverEntry — Radeon Open KMD, WDDM 2.0 skeleton (M2)\n"));

    RtlZeroMemory(&g_DdiData, sizeof(g_DdiData));
    g_DdiData.Size    = sizeof(g_DdiData);
    g_DdiData.Version = DXGKDDI_INTERFACE_VERSION;

    // ---- 生命周期（adapter.h）----
    g_DdiData.DxgkDdiAddDevice       = RokDdiAddDevice;
    g_DdiData.DxgkDdiStartDevice     = RokDdiStartDevice;
    g_DdiData.DxgkDdiStopDevice      = RokDdiStopDevice;
    g_DdiData.DxgkDdiRemoveDevice    = RokDdiRemoveDevice;
    g_DdiData.DxgkDdiResetDevice     = RokDdiResetDevice;
    g_DdiData.DxgkDdiUnload          = RokDdiUnload;

    // ---- 中断 ----
    g_DdiData.DxgkDdiInterruptRoutine = RokDdiInterruptRoutine;
    g_DdiData.DxgkDdiDpcRoutine       = RokDdiDpcRoutine;

    // ---- PnP / 电源 / 子设备 ----
    g_DdiData.DxgkDdiDispatchIoRequest     = RokDdiDispatchIoRequest;
    g_DdiData.DxgkDdiQueryChildRelations   = RokDdiQueryChildRelations;
    g_DdiData.DxgkDdiQueryChildStatus      = RokDdiQueryChildStatus;
    g_DdiData.DxgkDdiQueryDeviceDescriptor = RokDdiQueryDeviceDescriptor;
    g_DdiData.DxgkDdiSetPowerState         = RokDdiSetPowerState;
    g_DdiData.DxgkDdiNotifyAcpiEvent       = RokDdiNotifyAcpiEvent;
    g_DdiData.DxgkDdiQueryInterface        = RokDdiQueryInterface;
    g_DdiData.DxgkDdiGetChildContainerId   = RokDdiGetChildContainerId;

    // ---- 适配器 / 分配 / 提交 ----
    g_DdiData.DxgkDdiQueryAdapterInfo                  = RokDdiQueryAdapterInfo;
    g_DdiData.DxgkDdiCreateDevice                      = RokDdiCreateDevice;
    g_DdiData.DxgkDdiDestroyDevice                     = RokDdiDestroyDevice;
    g_DdiData.DxgkDdiCreateAllocation                  = RokDdiCreateAllocation;
    g_DdiData.DxgkDdiDestroyAllocation                 = RokDdiDestroyAllocation;
    g_DdiData.DxgkDdiDescribeAllocation                = RokDdiDescribeAllocation;
    g_DdiData.DxgkDdiGetStandardAllocationDriverData   = RokDdiGetStandardAllocationDriverData;
    g_DdiData.DxgkDdiPatch                             = RokDdiPatch;
    g_DdiData.DxgkDdiSubmitCommand                     = RokDdiSubmitCommand;
    g_DdiData.DxgkDdiPreemptCommand                    = RokDdiPreemptCommand;
    g_DdiData.DxgkDdiBuildPagingBuffer                 = RokDdiBuildPagingBuffer;
    g_DdiData.DxgkDdiCreateContext                     = RokDdiCreateContext;
    g_DdiData.DxgkDdiDestroyContext                    = RokDdiDestroyContext;
    g_DdiData.DxgkDdiRender                            = RokDdiRender;
    g_DdiData.DxgkDdiPresent                           = RokDdiPresent;
    g_DdiData.DxgkDdiEscape                            = RokDdiEscape;
    g_DdiData.DxgkDdiCollectDbgInfo                    = RokDdiCollectDbgInfo;
    g_DdiData.DxgkDdiResetFromTimeout                  = RokDdiResetFromTimeout;

    // ---- VidPn / 显示（render-only 骨架：0 路 source，通常不会被调用）----
    g_DdiData.DxgkDdiIsSupportedVidPn             = RokDdiIsSupportedVidPn;
    g_DdiData.DxgkDdiRecommendFunctionalVidPn     = RokDdiRecommendFunctionalVidPn;
    g_DdiData.DxgkDdiCommitVidPn                  = RokDdiCommitVidPn;
    g_DdiData.DxgkDdiUpdateActiveVidPnPresentPath = RokDdiUpdateActiveVidPnPresentPath;
    g_DdiData.DxgkDdiRecommendMonitorModes        = RokDdiRecommendMonitorModes;
    g_DdiData.DxgkDdiSetVidPnSourceAddress        = RokDdiSetVidPnSourceAddress;
    g_DdiData.DxgkDdiSetVidPnSourceVisibility     = RokDdiSetVidPnSourceVisibility;

    // ---- 引擎管理 / 中断控制（WDDM 1.3+ 成员）----
    g_DdiData.DxgkDdiControlInterrupt          = RokDdiControlInterrupt;
    g_DdiData.DxgkDdiQueryDependentEngineGroup = RokDdiQueryDependentEngineGroup;
    g_DdiData.DxgkDdiQueryEngineStatus         = RokDdiQueryEngineStatus;
    g_DdiData.DxgkDdiResetEngine               = RokDdiResetEngine;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    // ---- WDDM 2.0 虚拟地址提交 ----
    g_DdiData.DxgkDdiSubmitCommandVirtual = RokDdiSubmitCommandVirtual;
#endif

    status = DxgkInitialize(DriverObject, RegistryPath, &g_DdiData);
    if (!NT_SUCCESS(status))
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                   "rokmd: DxgkInitialize failed 0x%08X\n", status));
        return status;
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: DxgkInitialize OK, DDI version 0x%X\n", g_DdiData.Version));
    return STATUS_SUCCESS;
}
