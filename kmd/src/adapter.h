// SPDX-License-Identifier: MIT
// 适配器/设备上下文与生命周期 DDI —— 生命周期签名由 scripts/gen_ddi2.py
// 从 WDK 头文件机械生成（勿手改签名）。
//
// M2.5：ROK_ADAPTER 扩展为完整的 VidMm 簿记 + 硬件信息载体：
//   * DxgkInterface 整体拷贝（StartDevice 交付的回调表 + DeviceHandle）；
//   * PCI Vendor/Device/Revision（DxgkCbReadDeviceSpace 读取，M3 分发表键值）；
//   * BAR0 MMIO 映射（DxgkCbMapMemory，M3 寄存器访问前置条件）；
//   * 分配/上下文记录链表（CreateAllocation/CreateContext 生命周期）；
//   * LastCompletedFenceId —— "即时完成"模型的进度上报
//     （OS 经 DxgkDdiQueryCurrentFence 轮询；M3 换 IH ring 真实 fence）。
#pragma once

#include "precomp.h"

typedef struct _ROK_ADAPTER {
    // ---- VidMm / 上下文簿记 ----
    LIST_ENTRY      AllocationList;         // ROK_ALLOCATION 链表
    LIST_ENTRY      ContextList;            // ROK_CONTEXT 链表
    KSPIN_LOCK      AllocationLock;
    KSPIN_LOCK      ContextLock;
    ULONG           AllocationSeq;
    ULONG           ContextSeq;

    // ---- OS 交付的静态信息 ----
    PDEVICE_OBJECT  PhysicalDeviceObject;   // PDO（来自 DXGK_DEVICE_INFO）
    DXGKRNL_INTERFACE DxgkInterface;        // StartDevice 回调表整体拷贝
    BOOLEAN         Started;

    // ---- PCI 标识 ----
    ULONG           PciVendorId;
    ULONG           PciDeviceId;
    ULONG           PciRevisionId;

    // ---- MMIO（BAR0）----
    PHYSICAL_ADDRESS MmioBase;
    ULONG           MmioLength;
    PVOID           MmioVirtualAddress;     // StopDevice 时 DxgkCbUnmapMemory

    // ---- 命令进度 ----
    volatile LONG   LastCompletedFenceId;
} ROK_ADAPTER, *PROK_ADAPTER;

// 逻辑设备上下文（DxgkDdiCreateDevice 分配）。
typedef struct _ROK_DEVICE {
    PROK_ADAPTER    Adapter;
} ROK_DEVICE, *PROK_DEVICE;

// 渲染上下文（DxgkDdiCreateContext 分配）。M3 扩展为 PM4 ring 提交实体。
typedef struct _ROK_CONTEXT {
    PROK_ADAPTER    Adapter;
    UINT            NodeOrdinal;
    UINT            EngineAffinity;
    ULONG           Seq;
    LIST_ENTRY      Link;
} ROK_CONTEXT, *PROK_CONTEXT;

// 分配记录（DxgkDdiCreateAllocation 分配，作为 hAllocation 返回 OS）。
// 0 段模式下分配由 VidMm 以系统内存承载，此处仅做簿记。
typedef struct _ROK_ALLOCATION {
    PROK_ADAPTER    Adapter;
    ULONG           Seq;
    LIST_ENTRY      Link;
} ROK_ALLOCATION, *PROK_ALLOCATION;

// 进程记录（WDDM 2.0 DxgkDdiCreateProcess/DestroyProcess）。
typedef struct _ROK_PROCESS {
    PROK_ADAPTER    Adapter;
    HANDLE          hDxgkProcess;
} ROK_PROCESS, *PROK_PROCESS;

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
