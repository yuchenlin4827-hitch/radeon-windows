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

