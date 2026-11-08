// SPDX-License-Identifier: MIT
// rokmd — Radeon Open Kernel Mode Driver
// DriverEntry：向 dxgkrnl 注册 DDI 表（WDDM 2.0 基线）。
//
// 字段名与 d3dkmddi.h/dispmprt.h 的 DRIVER_INITIALIZATION_DATA 逐一对齐；
// DDI 原型见 adapter.h（生命周期）与 ddi.h（其余，由 scripts/gen_ddi2.py 生成）。
// M2.8：0x5023（WDDM 2.0）范围内成员全量注册，无 NULL 项 —— 对照
// WDK 26100 dispmprt.h 的 DRIVER_INITIALIZATION_DATA 逐成员核对。
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
               "rokmd: DriverEntry — Radeon Open KMD, WDDM 2.0 (M2.5)\n"));

    RtlZeroMemory(&g_DdiData, sizeof(g_DdiData));
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

    // LDA 链接协商（基础成员）：单适配器驱动明确拒绝，避免 NULL 成员。
    g_DdiData.DxgkDdiLinkDevice = RokDdiLinkDevice;

    // ---- 适配器 / 分配 / 提交 ----
    g_DdiData.DxgkDdiQueryAdapterInfo                  = RokDdiQueryAdapterInfo;
    g_DdiData.DxgkDdiCreateDevice                      = RokDdiCreateDevice;
    g_DdiData.DxgkDdiDestroyDevice                     = RokDdiDestroyDevice;
    g_DdiData.DxgkDdiCreateAllocation                  = RokDdiCreateAllocation;
    g_DdiData.DxgkDdiDestroyAllocation                 = RokDdiDestroyAllocation;
    g_DdiData.DxgkDdiDescribeAllocation                = RokDdiDescribeAllocation;
    g_DdiData.DxgkDdiGetStandardAllocationDriverData   = RokDdiGetStandardAllocationDriverData;
    g_DdiData.DxgkDdiOpenAllocation                    = RokDdiOpenAllocation;
    g_DdiData.DxgkDdiCloseAllocation                   = RokDdiCloseAllocation;
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
    g_DdiData.DxgkDdiQueryCurrentFence                 = RokDdiQueryCurrentFence;
    g_DdiData.DxgkDdiCancelCommand                     = RokDdiCancelCommand;
    g_DdiData.DxgkDdiControlEtwLogging                 = RokDdiControlEtwLogging;

    // ---- VidPn / 显示（render-only 骨架：0 路 source，通常不会被调用）----
    g_DdiData.DxgkDdiIsSupportedVidPn             = RokDdiIsSupportedVidPn;
    g_DdiData.DxgkDdiRecommendFunctionalVidPn     = RokDdiRecommendFunctionalVidPn;
    g_DdiData.DxgkDdiCommitVidPn                  = RokDdiCommitVidPn;
    g_DdiData.DxgkDdiUpdateActiveVidPnPresentPath = RokDdiUpdateActiveVidPnPresentPath;
    g_DdiData.DxgkDdiRecommendMonitorModes        = RokDdiRecommendMonitorModes;
    g_DdiData.DxgkDdiSetVidPnSourceAddress        = RokDdiSetVidPnSourceAddress;
    g_DdiData.DxgkDdiSetVidPnSourceVisibility     = RokDdiSetVidPnSourceVisibility;
    g_DdiData.DxgkDdiEnumVidPnCofuncModality      = RokDdiEnumVidPnCofuncModality;
    g_DdiData.DxgkDdiRecommendVidPnTopology       = RokDdiRecommendVidPnTopology;
    g_DdiData.DxgkDdiGetScanLine                  = RokDdiGetScanLine;
    g_DdiData.DxgkDdiStopCapture                  = RokDdiStopCapture;
    g_DdiData.DxgkDdiCreateOverlay                = RokDdiCreateOverlay;
    g_DdiData.DxgkDdiUpdateOverlay                = RokDdiUpdateOverlay;
    g_DdiData.DxgkDdiFlipOverlay                  = RokDdiFlipOverlay;
    g_DdiData.DxgkDdiDestroyOverlay               = RokDdiDestroyOverlay;
    g_DdiData.DxgkDdiSetDisplayPrivateDriverFormat = RokDdiSetDisplayPrivateDriverFormat;

    // ---- 遗留成员（0 段 render-only 下不可达；保签名 + NOT_IMPLEMENTED）----
    g_DdiData.DxgkDdiAcquireSwizzlingRange = RokDdiAcquireSwizzlingRange;
    g_DdiData.DxgkDdiReleaseSwizzlingRange = RokDdiReleaseSwizzlingRange;
    g_DdiData.DxgkDdiSetPalette            = RokDdiSetPalette;
    g_DdiData.DxgkDdiSetPointerPosition    = RokDdiSetPointerPosition;
    g_DdiData.DxgkDdiSetPointerShape       = RokDdiSetPointerShape;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
    g_DdiData.DxgkDdiRenderKm               = RokDdiRenderKm;
    g_DdiData.DxgkDdiQueryVidPnHWCapability = RokDdiQueryVidPnHWCapability;
#endif

    // ---- 引擎管理 / 中断控制（WDDM 1.3+ 成员）----
    g_DdiData.DxgkDdiControlInterrupt          = RokDdiControlInterrupt;
    g_DdiData.DxgkDdiQueryDependentEngineGroup = RokDdiQueryDependentEngineGroup;
    g_DdiData.DxgkDdiQueryEngineStatus         = RokDdiQueryEngineStatus;
    g_DdiData.DxgkDdiResetEngine               = RokDdiResetEngine;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    g_DdiData.DxgkDdiNotifySurpriseRemoval     = RokDdiNotifySurpriseRemoval;

    // 电源组件 / PEP 通道（WIN8 成员）：未注册电源组件时不可达，
    // 但成员必须非空以保证 0x5023 版本校验全表无 NULL（M2.8）。
    g_DdiData.DxgkDdiSetPowerComponentFState  = RokDdiSetPowerComponentFState;
    g_DdiData.DxgkDdiPowerRuntimeControlRequest = RokDdiPowerRuntimeControlRequest;

    // MPO v1 翻转（WIN8 成员）：0 路 source 下不可达，与 MPO2 同款保签名。
    g_DdiData.DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay =
        RokDdiSetVidPnSourceAddressWithMultiPlaneOverlay;

    // POST 所有权释放 / 系统显示（蓝屏渲染路径；本驱动不持有 POST，
    // 显式拒绝即可，但成员必须非空以保证 DDI 表完整）。
    g_DdiData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership =
        RokDdiStopDeviceAndReleasePostDisplayOwnership;
    g_DdiData.DxgkDdiSystemDisplayEnable = RokDdiSystemDisplayEnable;
    g_DdiData.DxgkDdiSystemDisplayWrite  = RokDdiSystemDisplayWrite;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    // 引擎元数据 / 中断 v2 / MPO / 时钟校准 / 历史缓冲（WDDM 1.3 面）。
    g_DdiData.DxgkDdiGetNodeMetadata          = RokDdiGetNodeMetadata;
    g_DdiData.DxgkDdiControlInterrupt2        = RokDdiControlInterrupt2;
    g_DdiData.DxgkDdiCheckMultiPlaneOverlaySupport = RokDdiCheckMultiPlaneOverlaySupport;
    g_DdiData.DxgkDdiCalibrateGpuClock        = RokDdiCalibrateGpuClock;
    g_DdiData.DxgkDdiFormatHistoryBuffer      = RokDdiFormatHistoryBuffer;

    // P-state 切换（WDDM 1.3 面电源管理；无时钟控制路径，确定性拒绝）。
    g_DdiData.DxgkDdiSetPowerPState = RokDdiSetPowerPState;
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    // ---- WDDM 2.0：虚拟地址提交 + 进程上下文 ----
    g_DdiData.DxgkDdiSubmitCommandVirtual = RokDdiSubmitCommandVirtual;
    g_DdiData.DxgkDdiCreateProcess        = RokDdiCreateProcess;
    g_DdiData.DxgkDdiDestroyProcess       = RokDdiDestroyProcess;

    // ---- WDDM 2.0 其余成员（GDI 表面 / GPU MMU / aperture / MPO2 / 电源）----
    g_DdiData.DxgkDdiRenderGdi                       = RokDdiRenderGdi;
    g_DdiData.DxgkDdiSetRootPageTable                = RokDdiSetRootPageTable;
    g_DdiData.DxgkDdiGetRootPageTableSize            = RokDdiGetRootPageTableSize;
    g_DdiData.DxgkDdiMapCpuHostAperture              = RokDdiMapCpuHostAperture;
    g_DdiData.DxgkDdiUnmapCpuHostAperture            = RokDdiUnmapCpuHostAperture;
    g_DdiData.DxgkDdiCheckMultiPlaneOverlaySupport2  = RokDdiCheckMultiPlaneOverlaySupport2;
    g_DdiData.DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay2 =
        RokDdiSetVidPnSourceAddressWithMultiPlaneOverlay2;
    g_DdiData.DxgkDdiPowerRuntimeSetDeviceHandle = RokDdiPowerRuntimeSetDeviceHandle;
    g_DdiData.DxgkDdiSetStablePowerState         = RokDdiSetStablePowerState;
    g_DdiData.DxgkDdiSetVideoProtectedRegion     = RokDdiSetVideoProtectedRegion;
#endif

    // ---- TDR 恢复闭环（ResetFromTimeout 之后的续跑通知）----
    g_DdiData.DxgkDdiRestartFromTimeout = RokDdiRestartFromTimeout;

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
