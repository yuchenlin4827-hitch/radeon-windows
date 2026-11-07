// SPDX-License-Identifier: MIT
// 其余 DXGK DDI 的骨架实现（stub）。
//
// 命名约定：RokDdi<函数名>，签名与 d3dkmddi.h / dispmprt.h 中的
// PDXGKDDI_* 指针类型一一对应（driver.c 按名挂表）。
// 骨架阶段"可观测"比"功能"优先：实机上 OS 的每次调用都能在
// WinDbg/DbgView 里看到留痕，为 M3 的实现顺序提供依据。
#pragma once

#include "precomp.h"

// ---- 适配器级 ----
NTSTATUS RokDdiQueryAdapterInfo(
    _In_ CONST HANDLE hAdapter, _In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo);
NTSTATUS RokDdiCreateDevice(
    _In_ HANDLE hAdapter, _Inout_ DXGKARG_CREATEDEVICE* pCreateDevice);
NTSTATUS RokDdiCloseAdapter(
    _In_ CONST DXGKARG_CLOSEADAPTER* pCloseAdapter);
VOID RokDdiDestroyDevice(
    _In_ HANDLE hDevice);
NTSTATUS RokDdiCreateAllocation(
    _In_ CONST HANDLE hAdapter, _Inout_ DXGKARG_CREATEALLOCATION* pCreateAllocation);
NTSTATUS RokDdiDestroyAllocation(
    _In_ CONST HANDLE hAdapter, _In_ CONST DXGKARG_DESTROYALLOCATION* pDestroyAllocation);
NTSTATUS RokDdiDescribeAllocation(
    _In_ CONST HANDLE hAdapter, _Inout_ DXGKARG_DESCRIBEALLOCATION* pDescribeAllocation);
NTSTATUS RokDdiGetStandardAllocationDriverData(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA* pGetStandardAllocationDriverData);
NTSTATUS RokDdiRender(
    _In_ CONST HANDLE hContext, _Inout_ DXGKARG_RENDER* pRender);
VOID RokDdiPresent(
    _In_ CONST HANDLE hContext, _Inout_ DXGKARG_PRESENT* pPresent);
NTSTATUS RokDdiEscape(
    _In_ CONST HANDLE hAdapter, _In_ CONST DXGKARG_ESCAPE* pEscape);
VOID RokDdiCollectDbgInfo(
    _In_ CONST PVOID MiniportDeviceContext, _In_ PDXGKARG_COLLECTDBGINFO pCollectDbgInfo);
NTSTATUS RokDdiQueryChildStatus(
    _In_ CONST PVOID MiniportDeviceContext,
    _Inout_ DXGKARG_QUERYCHILDSTATUS* pChildStatus,
    _In_ BOOLEAN NonDestructiveOnly);
NTSTATUS RokDdiQueryDeviceRelations(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG ChildIndex,
    _Inout_ PDXGK_DEVICE_RELATIONS_TYPE DeviceRelationsType);
NTSTATUS RokDdiQueryDescriptor(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG ChildIndex,
    _In_ DEVICE_DESCRIPTOR_TYPE DescriptorType,
    _Inout_ PVOID DescriptorBuffer,
    _Inout_ ULONG DescriptorBufferSize);
NTSTATUS RokDdiSetPowerState(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION ActionType);
NTSTATUS RokDdiNotifyAcpiEvent(
    _In_ VOID* MiniportDeviceContext,
    _In_ ULONG DeviceUid,
    _In_ DXGK_ACPI_EVENT_TYPE EventType,
    _Inout_ PACPI_NOTIFICATION_DATA NotificationData,
    _Out_ BOOLEAN* ReturnStatus);
NTSTATUS RokDdiQueryInterface(
    _In_ PVOID MiniportDeviceContext, _In_ PQUERY_INTERFACE QueryInterface);
NTSTATUS RokDdiGetChildContainerId(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG ChildUid,
    _Inout_ PCONTAINER_ID ContainerId);
NTSTATUS RokDdiDispatchIoRequest(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG VidPnSourceId,
    _In_ PVIDEO_REQUEST_PACKET VideoRequestPacket);

// ---- 上下文 / 命令提交（M3 核心区）----
NTSTATUS RokDdiCreateContext(
    _In_ HANDLE hDevice, _Inout_ DXGKARG_CREATECONTEXT* pCreateContext);
VOID RokDdiDestroyContext(
    _In_ HANDLE hDevice, _In_ HANDLE hContext);
VOID RokDdiSubmitCommand(
    _In_ CONST HANDLE hContext, _In_ PDXGKARG_SUBMITCOMMAND pSubmitCommand);
NTSTATUS RokDdiPreemptCommand(
    _In_ CONST HANDLE hContext, _In_ CONST DXGKARG_PREEMPTCOMMAND* pPreemptCommand);
VOID RokDdiPatch(
    _In_ CONST HANDLE hAdapter, _In_ CONST DXGKARG_PATCH* pPatch);
VOID RokDdiBuildPagingBuffer(
    _In_ CONST HANDLE hAdapter, _Inout_ DXGKARG_BUILDPAGINGBUFFER* pBuildPagingBuffer);
VOID RokDdiResetFromTimeout(
    _In_ CONST PVOID MiniportDeviceContext);

// ---- VidPn / 显示路径（M2 骨架为 render-only：0 路 source，不会被调用）----
NTSTATUS RokDdiIsSupportedVidPn(
    _In_ CONST PVOID MiniportDeviceContext,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN* pIsSupportedVidPn);
NTSTATUS RokDdiRecommendFunctionalVidPn(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* pRecommendFunctionalVidPn);
NTSTATUS RokDdiRecommendMonitorModes(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES* pRecommendMonitorModes);
NTSTATUS RokDdiCommitVidPn(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_COMMITVIDPN* pCommitVidPn);
NTSTATUS RokDdiUpdateActiveVidPnPresentPath(
    _In_ CONST VOID* MiniportDeviceContext,
    _Inout_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* pUpdateActiveVidPnPresentPath);
NTSTATUS RokDdiSetVidPnSourceAddress(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS* pSetVidPnSourceAddress);
VOID RokDdiSetVidPnSourceVisibility(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility);

// ---- 中断 / 引擎 ----
BOOLEAN RokDdiInterruptRoutine(
    _In_ CONST PVOID MiniportDeviceContext, _In_ ULONG MessageNumber);
VOID RokDdiDpcRoutine(
    _In_ CONST PVOID MiniportDeviceContext);
NTSTATUS RokDdiControlInterrupt(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGK_INTERRUPT_TYPE InterruptType,
    _In_ BOOLEAN Enable);
NTSTATUS RokDdiQueryDependentEngineGroup(
    _In_ CONST HANDLE hAdapter, _Out_ UINT* pNodeOrdinal, _Out_ UINT* pEngineOrdinal);
NTSTATUS RokDdiQueryEngineStatus(
    _In_ CONST HANDLE hAdapter,
    _In_ UINT NodeOrdinal,
    _In_ UINT EngineOrdinal,
    _Out_ DXGK_ENGINE_STATUS* pEngineStatus);
NTSTATUS RokDdiResetEngine(
    _In_ CONST HANDLE hAdapter, _In_ UINT NodeOrdinal, _In_ UINT EngineOrdinal);

// ---- WDDM 2.0（WIN10）驻留模型 ----
NTSTATUS RokDdiMakeResident(
    _In_ HANDLE hAdapter, _Inout_ DXGKARG_MAKERESIDENT* pMakeResident);
NTSTATUS RokDdiEvict(
    _In_ CONST HANDLE hAdapter, _Inout_ DXGKARG_EVICT* pEvict);
NTSTATUS RokDdiSubmitCommandVirtual(
    _In_ CONST HANDLE hContext,
    _In_ CONST DXGKARG_SUBMITCOMMANDVIRTUAL* pSubmitCommandVirtual);
