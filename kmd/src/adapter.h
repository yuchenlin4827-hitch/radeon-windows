// SPDX-License-Identifier: MIT
// 适配器/设备上下文与生命周期 DDI —— 生命周期签名由 scripts/gen_ddi2.py
// 从 WDK 头文件机械生成（勿手改签名）。
#pragma once

#include "precomp.h"

typedef struct _ROK_ADAPTER {
    PDEVICE_OBJECT PhysicalDeviceObject;    // PDO（来自 DXGK_DEVICE_INFO）
    BOOLEAN        Started;                 // DxgkDdiStartDevice 成功标记
} ROK_ADAPTER, *PROK_ADAPTER;

typedef struct _ROK_DEVICE {
    HANDLE hAdapter;
} ROK_DEVICE, *PROK_DEVICE;

NTSTATUS
RokDdiAddDevice(
    IN_CONST_PDEVICE_OBJECT PhysicalDeviceObject,
    OUT_PPVOID MiniportDeviceContext
    );

NTSTATUS
RokDdiStartDevice(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_PDXGK_START_INFO DxgkStartInfo,
    IN_PDXGKRNL_INTERFACE DxgkInterface,
    OUT_PULONG NumberOfVideoPresentSources,
    OUT_PULONG NumberOfChildren
    );

NTSTATUS
RokDdiStopDevice(
    IN_CONST_PVOID MiniportDeviceContext
    );

NTSTATUS
RokDdiRemoveDevice(
    IN_CONST_PVOID MiniportDeviceContext
    );

VOID
RokDdiResetDevice(
    IN_CONST_PVOID MiniportDeviceContext
    );

VOID
RokDdiUnload(
    VOID
    );

BOOLEAN
RokDdiInterruptRoutine(
    IN_CONST_PVOID MiniportDeviceContext,
    IN_ULONG MessageNumber
    );

VOID
RokDdiDpcRoutine(
    IN_CONST_PVOID MiniportDeviceContext
    );

// 资源清单日志（StartDevice 内部使用）
VOID RokLogTranslatedResources(_In_ PCM_RESOURCE_LIST TranslatedResources);
