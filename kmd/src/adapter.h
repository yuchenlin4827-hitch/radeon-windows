// SPDX-License-Identifier: MIT
// 适配器（miniport device context）生命周期管理。
#pragma once

#include "precomp.h"

// 每块物理 GPU 对应一个适配器上下文（DxgkDdiAddDevice 分配）。
// M2 骨架只保存 OS 交给我们的最小信息；M3 起在此扩展
// MMIO 映射、PCI ID、ring/IH 状态等（对应 Linux radeon_device 的角色）。
typedef struct _ROK_ADAPTER {
    PDEVICE_OBJECT PhysicalDeviceObject;    // PDO（来自 DXGK_DEVICE_INFO）
    BOOLEAN        Started;                 // DxgkDdiStartDevice 成功标记
} ROK_ADAPTER, *PROK_ADAPTER;

// 逻辑设备上下文（DxgkDdiCreateDevice 分配）。
typedef struct _ROK_DEVICE {
    HANDLE hAdapter;
} ROK_DEVICE, *PROK_DEVICE;

// ---- 生命周期 DDI（签名与 dispmprt.h 的 PDXGKDDI_* 一致）----
NTSTATUS
RokDdiAddDevice(
    _In_  PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PVOID         *MiniportDeviceContext
    );

NTSTATUS
RokDdiStartDevice(
    _In_  CONST PVOID      MiniportDeviceContext,
    _In_  PDXGK_START_INFO DxgkStartInfo,
    _In_  PDXGK_DEVICE_INFO DxgkDeviceInfo,
    _Out_ PULONG           NumberOfVideoPresentSources,
    _Out_ PULONG           NumberOfChildren
    );

NTSTATUS
RokDdiStopDevice(
    _In_ CONST PVOID MiniportDeviceContext
    );

NTSTATUS
RokDdiRemoveDevice(
    _In_ CONST PVOID MiniportDeviceContext
    );

VOID
RokDdiResetDevice(
    _In_ PVOID MiniportDeviceContext
    );

VOID
RokDdiUnload(
    VOID
    );

// 资源清单日志（StartDevice 内部使用）
VOID RokLogTranslatedResources(_In_ PCM_RESOURCE_LIST TranslatedResources);
