// SPDX-License-Identifier: MIT
// DXGK DDI 骨架声明 —— 由 scripts/gen_ddi2.py 从 WDK 头文件机械生成（勿手改）。
#pragma once

#include "precomp.h"

NTSTATUS
RokDdiDispatchIoRequest(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_ULONG VidPnSourceId,
    IN_PVIDEO_REQUEST_PACKET VideoRequestPacket
    );

NTSTATUS
RokDdiQueryChildRelations(
    IN_CONST_PVOID MiniportDeviceContext,
    _Inout_updates_bytes_(ChildRelationsSize) PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_ ULONG ChildRelationsSize
    );

NTSTATUS
RokDdiQueryChildStatus(
    IN_CONST_PVOID MiniportDeviceContext,
    INOUT_PDXGK_CHILD_STATUS ChildStatus,
    IN_BOOLEAN NonDestructiveOnly
    );

NTSTATUS
RokDdiQueryDeviceDescriptor(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_ULONG ChildUid,
    INOUT_PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor
    );

NTSTATUS
RokDdiSetPowerState(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_ULONG DeviceUid,
    IN_DEVICE_POWER_STATE DevicePowerState,
    IN_POWER_ACTION ActionType
    );

NTSTATUS
RokDdiNotifyAcpiEvent(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_DXGK_EVENT_TYPE EventType,
    IN_ULONG Event,
    IN_PVOID Argument,
    OUT_PULONG AcpiFlags
    );

NTSTATUS
RokDdiQueryInterface(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_PQUERY_INTERFACE QueryInterface
    );

NTSTATUS
RokDdiGetChildContainerId(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG ChildUid,
    _Inout_ PDXGK_CHILD_CONTAINER_ID ContainerId
    );

NTSTATUS
RokDdiQueryAdapterInfo(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_QUERYADAPTERINFO pQueryAdapterInfo
    );

NTSTATUS
RokDdiCreateDevice(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CREATEDEVICE pCreateDevice
    );

NTSTATUS
RokDdiDestroyDevice(
    IN_CONST_HANDLE hDevice
    );

NTSTATUS
RokDdiCreateAllocation(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CREATEALLOCATION pCreateAllocation
    );

NTSTATUS
RokDdiDestroyAllocation(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_DESTROYALLOCATION pDestroyAllocation
    );

NTSTATUS
RokDdiDescribeAllocation(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_DESCRIBEALLOCATION pDescribeAllocation
    );

NTSTATUS
RokDdiGetStandardAllocationDriverData(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA pGetStandardAllocationDriverData
    );

NTSTATUS
RokDdiPatch(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_PATCH pPatch
    );

NTSTATUS
RokDdiSubmitCommand(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMAND pSubmitCommand
    );

NTSTATUS
RokDdiPreemptCommand(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_PREEMPTCOMMAND pPreemptCommand
    );

NTSTATUS
RokDdiBuildPagingBuffer(
    IN_CONST_HANDLE hAdapter,
    IN_PDXGKARG_BUILDPAGINGBUFFER pBuildPagingBuffer
    );

NTSTATUS
RokDdiResetFromTimeout(
    IN_CONST_HANDLE hAdapter
    );

NTSTATUS
RokDdiEscape(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_ESCAPE pEscape
    );

NTSTATUS
RokDdiCollectDbgInfo(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_COLLECTDBGINFO pCollectDbgInfo
    );

NTSTATUS
RokDdiCreateContext(
    IN_CONST_HANDLE hDevice,
    INOUT_PDXGKARG_CREATECONTEXT pCreateContext
    );

NTSTATUS
RokDdiDestroyContext(
    IN_CONST_HANDLE hContext
    );

NTSTATUS
RokDdiRender(
    IN_CONST_HANDLE hContext,
    INOUT_PDXGKARG_RENDER pRender
    );

NTSTATUS
RokDdiPresent(
    IN_CONST_HANDLE hContext,
    INOUT_PDXGKARG_PRESENT pPresent
    );

NTSTATUS
RokDdiIsSupportedVidPn(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_ISSUPPORTEDVIDPN pIsSupportedVidPn
    );

NTSTATUS
RokDdiRecommendFunctionalVidPn(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_RECOMMENDFUNCTIONALVIDPN_CONST pRecommendFunctionalVidPn
    );

NTSTATUS
RokDdiCommitVidPn(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_COMMITVIDPN_CONST pCommitVidPn
    );

NTSTATUS
RokDdiUpdateActiveVidPnPresentPath(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_UPDATEACTIVEVIDPNPRESENTPATH_CONST pUpdateActiveVidPnPresentPath
    );

NTSTATUS
RokDdiRecommendMonitorModes(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_RECOMMENDMONITORMODES_CONST pRecommendMonitorModes
    );

NTSTATUS
RokDdiSetVidPnSourceAddress(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESS pSetVidPnSourceAddress
    );

NTSTATUS
RokDdiSetVidPnSourceVisibility(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEVISIBILITY pSetVidPnSourceVisibility
    );

NTSTATUS
RokDdiControlInterrupt(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_DXGK_INTERRUPT_TYPE InterruptType,
    IN_BOOLEAN EnableInterrupt
    );

NTSTATUS
RokDdiQueryDependentEngineGroup(
    IN_CONST_HANDLE hAdapter,
    INOUT_DXGKARG_QUERYDEPENDENTENGINEGROUP pQueryDependentEngineGroup
    );

NTSTATUS
RokDdiQueryEngineStatus(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_QUERYENGINESTATUS pQueryEngineStatus
    );

NTSTATUS
RokDdiResetEngine(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_RESETENGINE pResetEngine
    );

NTSTATUS
RokDdiSubmitCommandVirtual(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SUBMITCOMMANDVIRTUAL pSubmitCommand
    );

// ---- M2.5 追加（签名同上对照 d3dkmddi.h/dispmprt.h typedef）----

NTSTATUS
RokDdiOpenAllocation(
    IN_CONST_HANDLE hDevice,
    IN_CONST_PDXGKARG_OPENALLOCATION pOpenAllocation
    );

NTSTATUS
RokDdiCloseAllocation(
    IN_CONST_HANDLE hDevice,
    IN_CONST_PDXGKARG_CLOSEALLOCATION pCloseAllocation
    );

NTSTATUS
RokDdiQueryCurrentFence(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_QUERYCURRENTFENCE pCurrentFence
    );

NTSTATUS
RokDdiCancelCommand(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_CANCELCOMMAND pCancelCommand
    );

NTSTATUS
RokDdiCreateProcess(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CREATEPROCESS pArgs
    );

NTSTATUS
RokDdiDestroyProcess(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_HANDLE hKmdProcess
    );

VOID
RokDdiControlEtwLogging(
    IN_BOOLEAN Enable,
    IN_ULONG Flags,
    IN_UCHAR Level
    );

NTSTATUS
RokDdiNotifySurpriseRemoval(
    _In_ PVOID MiniportDeviceContext,
    _In_ DXGK_SURPRISE_REMOVAL_TYPE RemovalType
    );


// ---- M2.6 追加：WDDM 2.0 DDI 表补全（签名对照 WDK 26100 d3dkmddi.h/dispmprt.h）----

NTSTATUS
RokDdiAcquireSwizzlingRange(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_ACQUIRESWIZZLINGRANGE pAcquireSwizzlingRange
    );

NTSTATUS
RokDdiReleaseSwizzlingRange(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_RELEASESWIZZLINGRANGE pReleaseSwizzlingRange
    );

NTSTATUS
RokDdiSetPalette(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETPALETTE pSetPalette
    );

NTSTATUS
RokDdiSetPointerPosition(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETPOINTERPOSITION pSetPointerPosition
    );

NTSTATUS
RokDdiSetPointerShape(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETPOINTERSHAPE pSetPointerShape
    );

NTSTATUS
RokDdiEnumVidPnCofuncModality(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_ENUMVIDPNCOFUNCMODALITY_CONST pEnumCofuncModality
    );

NTSTATUS
RokDdiRecommendVidPnTopology(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_RECOMMENDVIDPNTOPOLOGY_CONST pRecommendVidPnTopology
    );

NTSTATUS
RokDdiGetScanLine(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_GETSCANLINE pGetScanLine
    );

NTSTATUS
RokDdiStopCapture(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_STOPCAPTURE pStopCapture
    );

NTSTATUS
RokDdiSetDisplayPrivateDriverFormat(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETDISPLAYPRIVATEDRIVERFORMAT pSetDisplayPrivateDriverFormat
    );

NTSTATUS
RokDdiCreateOverlay(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_CREATEOVERLAY pCreateOverlay
    );

NTSTATUS
RokDdiUpdateOverlay(
    IN_CONST_HANDLE hOverlay,
    IN_CONST_PDXGKARG_UPDATEOVERLAY pUpdateOverlay
    );

NTSTATUS
RokDdiFlipOverlay(
    IN_CONST_HANDLE hOverlay,
    IN_CONST_PDXGKARG_FLIPOVERLAY pFlipOverlay
    );

NTSTATUS
RokDdiDestroyOverlay(
    IN_CONST_HANDLE hOverlay
    );

NTSTATUS
RokDdiRenderKm(
    IN_CONST_HANDLE hContext,
    INOUT_PDXGKARG_RENDER pRenderKmArgs
    );

NTSTATUS
RokDdiQueryVidPnHWCapability(
    IN_CONST_HANDLE i_hAdapter,
    INOUT_PDXGKARG_QUERYVIDPNHWCAPABILITY io_pVidPnHWCaps
    );

NTSTATUS
RokDdiStopDeviceAndReleasePostDisplayOwnership(
    _In_ PVOID MiniportDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo
    );

NTSTATUS
RokDdiSystemDisplayEnable(
    _In_ PVOID MiniportDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT* Width,
    _Out_ UINT* Height,
    _Out_ D3DDDIFORMAT* ColorFormat
    );

VOID
RokDdiSystemDisplayWrite(
    _In_ PVOID MiniportDeviceContext,
    _In_reads_bytes_(SourceHeight * SourceStride) PVOID Source,
    _In_ UINT SourceWidth,
    _In_ UINT SourceHeight,
    _In_ UINT SourceStride,
    _In_ UINT PositionX,
    _In_ UINT PositionY
    );

NTSTATUS
RokDdiRestartFromTimeout(
    IN_CONST_HANDLE hAdapter
    );

NTSTATUS
RokDdiGetNodeMetadata(
    IN_CONST_HANDLE hAdapter,
    UINT NodeOrdinalAndAdapterIndex,
    OUT_PDXGKARG_GETNODEMETADATA pGetNodeMetadata
    );

NTSTATUS
RokDdiControlInterrupt2(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_DXGKARG_CONTROLINTERRUPT2 InterruptControl
    );

NTSTATUS
RokDdiCheckMultiPlaneOverlaySupport(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT pCheckMultiPlaneOverlaySupport
    );

NTSTATUS
RokDdiCalibrateGpuClock(
    IN_CONST_HANDLE hAdapter,
    UINT32 NodeOrdinal,
    UINT32 EngineOrdinal,
    OUT_PDXGKARG_CALIBRATEGPUCLOCK pClockCalibration
    );

NTSTATUS
RokDdiFormatHistoryBuffer(
    IN_CONST_HANDLE hContext,
    DXGKARG_FORMATHISTORYBUFFER* pFormatData
    );

NTSTATUS
RokDdiRenderGdi(
    IN_CONST_HANDLE hContext,
    INOUT_PDXGKARG_RENDERGDI pRenderGdi
    );

VOID
RokDdiSetRootPageTable(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETROOTPAGETABLE pSetPageTable
    );

SIZE_T
RokDdiGetRootPageTableSize(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_GETROOTPAGETABLESIZE pArgs
    );

NTSTATUS
RokDdiMapCpuHostAperture(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_MAPCPUHOSTAPERTURE pArgs
    );

NTSTATUS
RokDdiUnmapCpuHostAperture(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_UNMAPCPUHOSTAPERTURE pArgs
    );

NTSTATUS
RokDdiCheckMultiPlaneOverlaySupport2(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT2 pCheckMultiPlaneOverlaySupport
    );

NTSTATUS
RokDdiSetVidPnSourceAddressWithMultiPlaneOverlay2(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2 pSetVidPnSourceAddressWithMultiPlaneOverlay
    );

NTSTATUS
RokDdiPowerRuntimeSetDeviceHandle(
    IN_CONST_HANDLE DriverContext,
    HANDLE PoDeviceHandle
    );

VOID
RokDdiSetStablePowerState(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETSTABLEPOWERSTATE pArgs
    );

NTSTATUS
RokDdiSetVideoProtectedRegion(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDEOPROTECTEDREGION pArgs
    );

// ---- M2.8 追加：0x5023 版本范围内最后一批 NULL 成员补齐
// （签名逐字对照 WDK 26100 dispmprt.h / d3dkmddi.h）----

// DxgkDdiLinkDevice（DRIVER_INITIALIZATION_DATA 基础成员，LDA 链接协商）。
// PASSIVE_LEVEL。
NTSTATUS
RokDdiLinkDevice(
    IN_CONST_PDEVICE_OBJECT PhysicalDeviceObject,
    IN_CONST_PVOID          MiniportDeviceContext,
    INOUT_PLINKED_DEVICE    LinkedDevice
    );

// DxgkDdiSetPowerComponentFState（WIN8 成员；未注册电源组件时不可达）。
// _IRQL_requires_max_(DISPATCH_LEVEL)：禁止 PAGED_CODE。
NTSTATUS
RokDdiSetPowerComponentFState(
    IN_CONST_HANDLE DriverContext,
    UINT            ComponentIndex,
    UINT            FState
    );

// DxgkDdiPowerRuntimeControlRequest（WIN8 成员；SupportRuntimePowerManagement=0
// 时不可达）。_IRQL_requires_max_(DISPATCH_LEVEL)：禁止 PAGED_CODE。
// 参数拼写逐字对齐 WDK 26100 d3dkmddi.h 的 DXGKDDIPOWERRUNTIMECONTROLREQUEST。
NTSTATUS
RokDdiPowerRuntimeControlRequest(
    IN_CONST_HANDLE DriverContext,
    IN LPCGUID      PowerControlCode,
    IN OPTIONAL PVOID InBuffer,
    IN SIZE_T       InBufferSize,
    OUT OPTIONAL PVOID OutBuffer,
    IN SIZE_T       OutBufferSize,
    OUT OPTIONAL PSIZE_T BytesReturned
    );

// DxgkDdiSetPowerPState（WDDM 1.3 成员；无 P-state 控制能力）。
// PASSIVE_LEVEL。
NTSTATUS
RokDdiSetPowerPState(
    IN_CONST_HANDLE DriverContext,
    UINT            ComponentIndex,
    UINT            RequestedComponentPState
    );

// DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay（WIN8 成员，MPO v1 翻转；
// 0 路 source 不可达，与 MPO2 同样保签名）。
// _IRQL_requires_max_(PROFILE_LEVEL - 1)：禁止 PAGED_CODE。
NTSTATUS
RokDdiSetVidPnSourceAddressWithMultiPlaneOverlay(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY
    pSetVidPnSourceAddressWithMultiPlaneOverlay
    );
