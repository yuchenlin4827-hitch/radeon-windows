// SPDX-License-Identifier: MIT
// kmock2/dispmprt.h — 显示微型端口 OS 交付面 mock：
//   DXGKRNL_INTERFACE（DxgkCb* 回调表）、DXGK_DEVICE_INFO、
//   翻译后资源清单（CM_RESOURCE_LIST）、DRIVER_INITIALIZATION_DATA 壳
//   与 DxgkInitialize 声明。
//
// 字段覆盖原则见 ntddk.h 头注释：只 mock 驱动实际访问的字段，
// 但 DxgkCb* 的函数指针签名与 driver.c/adapter.c 的调用点严格对齐。
#pragma once

#include "ntddk.h"

// ---- PCI 配置空间选择（DxgkCbReadDeviceSpace 第二参）----
typedef enum _DXGK_WHICHSPACE {
    DXGK_WHICHSPACE_CONFIG = 1,
    DXGK_WHICHSPACE_ROM = 2,
    DXGK_WHICHSPACE_RAM = 3
} DXGK_WHICHSPACE;

// ---- 内存缓存类型（DxgkCbMapMemory）----
typedef enum _MEMORY_CACHING_TYPE {
    MmFrameBufferCached = 2,
    MmNonCached = 3,
    MmCached = 4
} MEMORY_CACHING_TYPE;

// ---- 翻译后资源清单（CM_RESOURCE_LIST 家族，字段按 WDK 布局）----
#define CmResourceTypeMemory        3
#define CmResourceTypePort          1
#define CmResourceTypeInterrupt     2

typedef struct _CM_PARTIAL_RESOURCE_DESCRIPTOR {
    UCHAR   Type;
    UCHAR   ShareDisposition;
    USHORT  Flags;
    union {
        struct {
            PHYSICAL_ADDRESS Start;
            ULONG            Length;
        } Memory;
        struct {
            PHYSICAL_ADDRESS Start;
            ULONG            Length;
        } Port;
        struct {
            ULONG Level;
            ULONG Vector;
            ULONG Affinity;
        } Interrupt;
        ULONGLONG Generic;
    } u;
} CM_PARTIAL_RESOURCE_DESCRIPTOR, *PCM_PARTIAL_RESOURCE_DESCRIPTOR;

typedef struct _CM_PARTIAL_RESOURCE_LIST {
    USHORT  Version;
    USHORT  Revision;
    ULONG   Count;
    CM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptors[1];
} CM_PARTIAL_RESOURCE_LIST, *PCM_PARTIAL_RESOURCE_LIST;

typedef struct _CM_FULL_RESOURCE_DESCRIPTOR {
    ULONG                   InterfaceType;      // 内部等 eBUTTONT bus 类型
    ULONG                   BusNumber;
    CM_PARTIAL_RESOURCE_LIST PartialResourceList;
} CM_FULL_RESOURCE_DESCRIPTOR, *PCM_FULL_RESOURCE_DESCRIPTOR;

typedef struct _CM_RESOURCE_LIST {
    ULONG                       Count;
    CM_FULL_RESOURCE_DESCRIPTOR List[1];
} CM_RESOURCE_LIST, *PCM_RESOURCE_LIST;

// ---- VIDEO_REQUEST_PACKET（DispatchIoRequest 桩解引用 StatusBlock）----
typedef struct _STATUS_BLOCK {
    NTSTATUS Status;
    ULONG    Information;
} STATUS_BLOCK, *PSTATUS_BLOCK;

typedef struct _VIDEO_REQUEST_PACKET {
    ULONG         IoControlCode;
    PSTATUS_BLOCK StatusBlock;
    PVOID         InputBuffer;
    ULONG         InputBufferLength;
    PVOID         OutputBuffer;
    ULONG         OutputBufferLength;
} VIDEO_REQUEST_PACKET, *PVIDEO_REQUEST_PACKET;

// ---- StartDevice 参数 ----
typedef struct _DXGK_START_INFO {
    LUID    AdapterLuid;                    // 用户态定位适配器的钥匙
    BOOLEAN AllowRuntimePowerManagement;
} DXGK_START_INFO, *PDXGK_START_INFO;

// ---- DxgkCb* 回调原型（签名与 adapter.c/ddi.c 调用点一致）----
struct _DXGK_DEVICE_INFO;   // 文件尾定义

typedef NTSTATUS (*DXGKCB_READ_DEVICE_SPACE)(
    HANDLE              DeviceHandle,
    DXGK_WHICHSPACE     WhichSpace,
    PVOID               Buffer,
    ULONG               Offset,
    ULONG               Length,
    PULONG              BytesRead);

typedef NTSTATUS (*DXGKCB_GET_DEVICE_INFORMATION)(
    HANDLE              DeviceHandle,
    struct _DXGK_DEVICE_INFO *DeviceInfo);

typedef NTSTATUS (*DXGKCB_MAP_MEMORY)(
    HANDLE              DeviceHandle,
    PHYSICAL_ADDRESS    TranslatedAddress,
    ULONG               NumberOfBytes,
    BOOLEAN             InIoSpace,
    BOOLEAN             MapToUserMode,
    MEMORY_CACHING_TYPE CacheType,
    PVOID               *VirtualAddress);

typedef VOID (*DXGKCB_UNMAP_MEMORY)(
    HANDLE              DeviceHandle,
    PVOID               VirtualAddress);

// d3dkmddi.h 里的回调（OpenAllocation 路径）——前向声明类型。
typedef struct _DXGKARGCB_GETHANDLEDATA DXGKARGCB_GETHANDLEDATA;
typedef PVOID (*DXGKCB_GET_HANDLE_DATA)(DXGKARGCB_GETHANDLEDATA *hObject);

// ---- dxgkrnl 交付的回调表 ----
typedef struct _DXGKRNL_INTERFACE {
    ULONG               Size;           // sizeof(DXGKRNL_INTERFACE)
    ULONG               Version;
    HANDLE              DeviceHandle;
    PDEVICE_OBJECT      PhysicalDeviceObject;

    DXGKCB_READ_DEVICE_SPACE    DxgkCbReadDeviceSpace;
    DXGKCB_GET_DEVICE_INFORMATION DxgkCbGetDeviceInformation;
    DXGKCB_MAP_MEMORY           DxgkCbMapMemory;
    DXGKCB_UNMAP_MEMORY         DxgkCbUnmapMemory;
    DXGKCB_GET_HANDLE_DATA      DxgkCbGetHandleData;
} DXGKRNL_INTERFACE, *PDXGKRNL_INTERFACE;

// ---- 设备信息（StartDevice 经 DxgkCbGetDeviceInformation 取回）----
typedef struct _DXGK_DEVICE_INFO {
    PDEVICE_OBJECT      PhysicalDeviceObject;
    UNICODE_STRING      DeviceRegistryPath;     // 驱动键（CCC/sim 开关读取处）
    PCM_RESOURCE_LIST   TranslatedResourceList; // 翻译后资源（BAR0/IRQ）
} DXGK_DEVICE_INFO, *PDXGK_DEVICE_INFO;
