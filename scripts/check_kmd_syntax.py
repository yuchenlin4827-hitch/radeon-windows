#!/usr/bin/env python3
"""KMD M2.8 新增代码块语法检查（Linux gcc，mock WDK 环境）。

从 kmd/src/ddi.c 按 marker 提取本次新增/修改的代码块，拼接 mock 的
WDK 类型/宏后用 gcc -fsyntax-only 编译。被测内容：
  - QueryAdapterInfo 三个新 case（NUMPOWERCOMPONENTS/SEGMENTMEMORYSTATE/
    DISPLAY_DRIVERCAPS_EXTENSION）
  - Escape QUERY_ENGINE_STATUS case
  - QueryEngineStatus 重写函数
  - 5 个新 DDI（LinkDevice/SetPowerComponentFState/PowerRuntimeControlRequest/
    SetPowerPState/SetVidPnSourceAddressWithMultiPlaneOverlay）
结构体形状逐字对照 WDK 26100 d3dkmddi.h/dispmprt.h。
"""
import subprocess, sys, os

REPO = "/home/z/my-project"
DDI_C = os.path.join(REPO, "kmd/src/ddi.c")
src = open(DDI_C, encoding="utf-8").read()

def extract(a, b):
    i = src.index(a)
    j = src.index(b, i)
    return src[i:j]

qa_cases = extract("    case DXGKQAITYPE_NUMPOWERCOMPONENTS:", "    default:\n        KdPrintEx")
esc_case = extract("    case ROK_ESC_TYPE_QUERY_ENGINE_STATUS:", "    default:\n        KdPrintEx")
qes_fn = extract("NTSTATUS\nRokDdiQueryEngineStatus(", "NTSTATUS\nRokDdiResetEngine(")
new_fns = src[src.index("// ------------------------------------------------------------------\n// WDDM 2.0 DDI 表补全收尾"):]

PRELUDE = r"""
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* ---- 基本类型 ---- */
typedef void VOID; typedef int INT; typedef unsigned int UINT;
typedef unsigned char UCHAR; typedef unsigned char BOOLEAN;
typedef unsigned int UINT32; typedef unsigned long long UINT64;
typedef unsigned short WORD, USHORT, WCHAR_; typedef wchar_t WCHAR;
typedef unsigned long ULONG; typedef long LONG;
typedef unsigned long long ULONG64, DWORD64; typedef long long LONGLONG;
typedef size_t SIZE_T, *PSIZE_T; typedef unsigned char BYTE;
typedef void *PVOID; typedef const void *LPCVOID;
typedef void *HANDLE, *HMODULE; typedef LONG NTSTATUS;
typedef struct _GUID { unsigned long a; unsigned short b, c; unsigned char d[8]; } GUID;
typedef const GUID *LPCGUID;
typedef union _LARGE_INTEGER { LONGLONG QuadPart; } LARGE_INTEGER;
typedef union _PHYSICAL_ADDRESS { LONGLONG QuadPart; } PHYSICAL_ADDRESS;
typedef struct _UNICODE_STRING { unsigned short Length, MaximumLength; PVOID Buffer; } UNICODE_STRING, *PUNICODE_STRING;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY;
typedef unsigned long KIRQL; typedef struct { PVOID p; } KSPIN_LOCK;
typedef struct _DEVICE_OBJECT *PDEVICE_OBJECT;
typedef UINT D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DDDIFORMAT, D3DGPU_VIRTUAL_ADDRESS;

/* ---- 宏 ---- */
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define APIENTRY
#define PAGED_CODE() ((void)0)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define NT_SUCCESS(x) ((x) >= 0)
#define ASSERT(x) ((void)(x))
#define KdPrintEx(args) ((void)0)
#define DPFLTR_IHVDRIVER_ID 0
#define DPFLTR_ERROR_LEVEL 0
#define DPFLTR_WARNING_LEVEL 1
#define DPFLTR_INFO_LEVEL 2
#define DPFLTR_TRACE_LEVEL 3
#define PAGEDPOOL 1
#define ROK_POOL_TAG 'DkoR'
static inline void RtlZeroMemory(void *d, size_t n) { memset(d, 0, n); }
static inline void InitializeListHead(LIST_ENTRY *h) { h->Flink = h->Blink = h; }
static inline void InsertTailList(LIST_ENTRY *h, LIST_ENTRY *e) { (void)h; (void)e; }
static inline void RemoveEntryList(LIST_ENTRY *e) { (void)e; }
static inline int IsListEmpty(const LIST_ENTRY *h) { return h->Flink == h; }
#define CONTAINING_RECORD(addr, type, field) ((type *)((char *)(addr) - offsetof(type, field)))
static inline void KeAcquireSpinLock(KSPIN_LOCK *l, KIRQL *o) { (void)l; *o = 0; }
static inline void KeReleaseSpinLock(KSPIN_LOCK *l, KIRQL o) { (void)l; (void)o; }
static inline LONG InterlockedExchange(volatile LONG *t, LONG v) { LONG o = *t; *t = v; return o; }
static inline LONG InterlockedIncrement(volatile LONG *t) { return ++(*t); }
static inline LONG InterlockedCompareExchange(volatile LONG *t, LONG ex, LONG cmp) { LONG o = *t; if (o == cmp) *t = ex; return o; }

#define STATUS_SUCCESS                ((NTSTATUS)0x00000000L)
#define STATUS_INVALID_PARAMETER      ((NTSTATUS)0xC000000DL)
#define STATUS_INVALID_HANDLE         ((NTSTATUS)0xC0000008L)
#define STATUS_NOT_IMPLEMENTED        ((NTSTATUS)0xC0000002L)
#define STATUS_NOT_SUPPORTED          ((NTSTATUS)0xC00000BBL)
#define STATUS_BUFFER_TOO_SMALL       ((NTSTATUS)0xC0000023L)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009AL)
#define STATUS_REVISION_MISMATCH      ((NTSTATUS)0xC0000059L)

/* 版本常量（SDK d3dukmdt.h）*/
#define DXGKDDI_INTERFACE_VERSION_VISTA   0x1052
#define DXGKDDI_INTERFACE_VERSION_WIN7    0x2005
#define DXGKDDI_INTERFACE_VERSION_WIN8    0x300E
#define DXGKDDI_INTERFACE_VERSION_WDDM1_3 0x4002
#define DXGKDDI_INTERFACE_VERSION_WDDM2_0 0x5023
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x5023
#endif

/* ---- WDK 注解 typedef（签名用）---- */
typedef CONST HANDLE IN_CONST_HANDLE;
typedef CONST PVOID IN_CONST_PVOID;
typedef CONST PDEVICE_OBJECT IN_CONST_PDEVICE_OBJECT;
typedef ULONG *OUT_PULONG;
typedef struct _LINKED_DEVICE { int x; } LINKED_DEVICE, *PLINKED_DEVICE;
typedef PLINKED_DEVICE INOUT_PLINKED_DEVICE;
typedef BOOLEAN IN_BOOLEAN;
typedef ULONG IN_ULONG;

/* ---- opaque 结构（仅签名用）---- */
typedef struct _DXGK_CHILD_DESCRIPTOR DXGK_CHILD_DESCRIPTOR, *PDXGK_CHILD_DESCRIPTOR;
typedef struct _DXGK_CHILD_STATUS DXGK_CHILD_STATUS, *PDXGK_CHILD_STATUS;
typedef struct _DXGK_DEVICE_DESCRIPTOR DXGK_DEVICE_DESCRIPTOR, *PDXGK_DEVICE_DESCRIPTOR;
typedef struct _DXGK_CHILD_CONTAINER_ID DXGK_CHILD_CONTAINER_ID, *PDXGK_CHILD_CONTAINER_ID;
typedef struct _QUERY_INTERFACE QUERY_INTERFACE, *PQUERY_INTERFACE;
typedef struct _VIDEO_REQUEST_PACKET VIDEO_REQUEST_PACKET, *PVIDEO_REQUEST_PACKET;
typedef struct _DXGK_START_INFO DXGK_START_INFO, *PDXGK_START_INFO;
typedef struct _DXGKRNL_INTERFACE DXGKRNL_INTERFACE, *PDXGKRNL_INTERFACE;
typedef struct _DXGK_DEVICE_INFO DXGK_DEVICE_INFO, *PDXGK_DEVICE_INFO;
typedef struct _CM_RESOURCE_LIST CM_RESOURCE_LIST, *PCM_RESOURCE_LIST;
typedef struct _DXGK_DISPLAY_INFORMATION DXGK_DISPLAY_INFORMATION, *PDXGK_DISPLAY_INFORMATION;
typedef struct _DXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS DXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS, *PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS;

/* ---- 枚举 ---- */
typedef enum _DXGK_EVENT_TYPE { DxgkEventAcpi = 0 } DXGK_EVENT_TYPE;
typedef enum _DXGK_SURPRISE_REMOVAL_TYPE { DxgkRemovalPsuedo = 0 } DXGK_SURPRISE_REMOVAL_TYPE;
typedef enum _DXGK_INTERRUPT_TYPE { DXGK_INTERRUPT_DMA_COMPLETED = 0 } DXGK_INTERRUPT_TYPE;
typedef enum _DXGK_ENGINE_TYPE { DXGK_ENGINE_TYPE_OTHER = 0, DXGK_ENGINE_TYPE_3D = 3 } DXGK_ENGINE_TYPE;
typedef enum _DXGK_WDDMVERSION { DXGKDDI_WDDMv1 = 1, DXGKDDI_WDDMv2 = 2 } DXGK_WDDMVERSION;
typedef enum _DXGK_QUERYADAPTERINFOTYPE {
    DXGKQAITYPE_UMDRIVERPRIVATE = 0, DXGKQAITYPE_DRIVERCAPS = 1, DXGKQAITYPE_QUERYSEGMENT = 2,
    DXGKQAITYPE_QUERYSEGMENT3 = 5, DXGKQAITYPE_NUMPOWERCOMPONENTS = 6,
    DXGKQAITYPE_QUERYSEGMENT4 = 11, DXGKQAITYPE_SEGMENTMEMORYSTATE = 12,
    DXGKQAITYPE_GPUMMUCAPS = 13, DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION = 16,
} DXGK_QUERYADAPTERINFOTYPE;

/* ---- DDI 参数结构（成员访问面按 ddi.c 实际使用；形状对照 WDK 26100）---- */
typedef struct _DXGK_ENGINESTATUS {
    union { struct { UINT Responsive : 1; UINT Reserved : 31; }; UINT Value; };
} DXGK_ENGINESTATUS;
typedef struct _DXGKARG_QUERYENGINESTATUS {
    UINT NodeOrdinal; UINT EngineOrdinal; DXGK_ENGINESTATUS EngineStatus;
} DXGKARG_QUERYENGINESTATUS, *INOUT_PDXGKARG_QUERYENGINESTATUS;

typedef struct _DXGKARG_ESCAPE {
    HANDLE hDevice; UINT Type; VOID *pPrivateDriverData; UINT PrivateDriverDataSize;
} DXGKARG_ESCAPE;

typedef struct _DXGKARG_QUERYADAPTERINFO {
    DXGK_QUERYADAPTERINFOTYPE Type; CONST VOID *pInputData; UINT InputDataSize;
    VOID *pOutputData; UINT OutputDataSize;
} DXGKARG_QUERYADAPTERINFO;

typedef struct _DXGK_MEMORYRANGE { UINT64 SegmentOffset; UINT64 SizeInBytes; } DXGK_MEMORYRANGE;
typedef struct _DXGK_QUERYSEGMENTMEMORYSTATE {
    WORD DriverSegmentId; WORD PhysicalAdapterIndex;
    UINT NumInvalidMemoryRanges;
    DXGK_MEMORYRANGE *pMemoryRanges;
} DXGK_QUERYSEGMENTMEMORYSTATE;

typedef struct _DXGK_DISPLAY_DRIVERCAPS_EXTENSION {
    union { struct {
        UINT SecureDisplaySupport : 1; UINT VirtualModeSupport : 1;
        UINT Reserved : 29; UINT NonSpecificPrimarySupport : 1;
    }; UINT Value; };
} DXGK_DISPLAY_DRIVERCAPS_EXTENSION;

typedef union _DXGK_VIDSCHCAPS {
    struct { UINT MultiEngineAware : 1; UINT CancelCommandAware : 1;
             UINT No64BitAtomics : 1; UINT PreemptionAware : 1;
             UINT NoDmaPatching : 1; UINT Reserved : 27; };
    UINT Value;
} DXGK_VIDSCHCAPS;
typedef struct _DXGK_GPUMMUCAPS { int x; } DXGK_GPUMMUCAPS;
typedef struct _DXGK_QUERYSEGMENTOUT3 { UINT NbSegment; } DXGK_QUERYSEGMENTOUT3;
typedef struct _DXGK_QUERYSEGMENTOUT4 { UINT NbSegment; } DXGK_QUERYSEGMENTOUT4;
typedef struct _DXGK_DRIVERCAPS {
    LARGE_INTEGER HighestAcceptableAddress;
    struct { UINT NbAsymetricProcessingNodes; } GpuEngineTopology;
    DXGK_VIDSCHCAPS SchedulingCaps;
    BOOLEAN SupportSurpriseRemoval;
    DXGK_WDDMVERSION WDDMVersion;
} DXGK_DRIVERCAPS;

typedef struct _DXGKARG_CREATEDEVICE { void *hDevice; } DXGKARG_CREATEDEVICE, *INOUT_PDXGKARG_CREATEDEVICE;
typedef struct _DXGK_ALLOCATIONINFO { void *hAllocation; } DXGK_ALLOCATIONINFO;
typedef struct _DXGKARG_CREATEALLOCATION {
    UINT NumAllocations; DXGK_ALLOCATIONINFO *pAllocationInfo; void *hResource;
    union { struct { UINT Resource : 1; }; UINT Value; } Flags;
} DXGKARG_CREATEALLOCATION, *INOUT_PDXGKARG_CREATEALLOCATION;
typedef struct _DXGKARG_DESTROYALLOCATION { UINT NumAllocations; void **pAllocationList; } DXGKARG_DESTROYALLOCATION;
typedef const struct _DXGKARG_DESTROYALLOCATION *IN_CONST_PDXGKARG_DESTROYALLOCATION;
typedef struct _DXGK_OPENALLOCATIONINFO { void *hAllocation; void *hDeviceSpecificAllocation; } DXGK_OPENALLOCATIONINFO;
typedef struct _DXGKARG_OPENALLOCATION { UINT NumAllocations; DXGK_OPENALLOCATIONINFO *pOpenAllocation; } DXGKARG_OPENALLOCATION;
typedef const struct _DXGKARG_OPENALLOCATION *IN_CONST_PDXGKARG_OPENALLOCATION;
typedef struct _DXGKARG_CLOSEALLOCATION { UINT NumAllocations; } DXGKARG_CLOSEALLOCATION;
typedef const struct _DXGKARG_CLOSEALLOCATION *IN_CONST_PDXGKARG_CLOSEALLOCATION;
typedef struct _DXGKARGCB_GETHANDLEDATA {
    HANDLE hObject; UINT Type; union { struct { UINT DeviceSpecific : 1; }; UINT Value; } Flags;
} DXGKARGCB_GETHANDLEDATA;
typedef struct _DXGK_CONTEXTINFO {
    UINT DmaBufferSize, DmaBufferSegmentSet, DmaBufferPrivateDataSize;
    UINT AllocationListSize, PatchLocationListSize;
    union { struct { UINT NoPatchingRequired : 1; }; UINT Value; } Caps;
    UINT PagingCompanionNodeId;
} DXGK_CONTEXTINFO;
typedef struct _DXGKARG_CREATECONTEXT {
    UINT NodeOrdinal, EngineAffinity; DXGK_CONTEXTINFO ContextInfo; void *hContext;
} DXGKARG_CREATECONTEXT, *INOUT_PDXGKARG_CREATECONTEXT;
typedef struct _DXGKARG_BUILDPAGINGBUFFER {
    UINT Operation; VOID *pDmaBuffer; UINT DmaSize; UINT DmaBufferWriteOffset;
} DXGKARG_BUILDPAGINGBUFFER;
typedef struct _DXGKARG_SUBMITCOMMAND {
    HANDLE hContext; UINT DmaBufferSize, SubmissionFenceId;
} DXGKARG_SUBMITCOMMAND;
typedef const struct _DXGKARG_SUBMITCOMMAND *IN_CONST_PDXGKARG_SUBMITCOMMAND;
typedef struct _DXGKARG_SUBMITCOMMANDVIRTUAL {
    HANDLE hContext; VOID *DmaBufferVirtualAddress; UINT DmaBufferSize, SubmissionFenceId;
} DXGKARG_SUBMITCOMMANDVIRTUAL;
typedef const struct _DXGKARG_SUBMITCOMMANDVIRTUAL *IN_CONST_PDXGKARG_SUBMITCOMMANDVIRTUAL;
typedef struct _DXGKARG_RESETENGINE { UINT LastAbortedFenceId; } DXGKARG_RESETENGINE, *INOUT_PDXGKARG_RESETENGINE;
typedef struct _DXGKARG_GETNODEMETADATA { DXGK_ENGINE_TYPE EngineType; WCHAR FriendlyName[32]; } DXGKARG_GETNODEMETADATA, *OUT_PDXGKARG_GETNODEMETADATA;
#define DXGK_MAX_METADATA_NAME_LENGTH 32
#define DXGKNODEMETADATA_GETNODEORDINAL(x) ((x) & 0xFFFF)
#define DXGKNODEMETADATA_GETPHYSICALADAPTERINDEX(x) ((UINT)(x) >> 16)
typedef struct _DXGKARG_QUERYCURRENTFENCE { UINT CurrentFence; } DXGKARG_QUERYCURRENTFENCE, *INOUT_PDXGKARG_QUERYCURRENTFENCE;
typedef struct _DXGKARG_QUERYDEPENDENTENGINEGROUP { UINT NodeOrdinal; ULONG64 DependentNodeOrdinalMask; } DXGKARG_QUERYDEPENDENTENGINEGROUP, *INOUT_PDXGKARG_QUERYDEPENDENTENGINEGROUP;
typedef struct _DXGKARG_CREATEPROCESS { HANDLE hDxgkProcess; void *hKmdProcess; } DXGKARG_CREATEPROCESS, *INOUT_PDXGKARG_CREATEPROCESS;

/* opaque 参数结构（仅签名引用）*/
#define OPQ(n) typedef struct _##n n, *P##n
#define OPQC(n) typedef struct _##n n; typedef const n *IN_CONST_P##n
OPQ(DXGKARG_ISSUPPORTEDVIDPN); typedef DXGKARG_ISSUPPORTEDVIDPN *INOUT_PDXGKARG_ISSUPPORTEDVIDPN;
OPQC(DXGKARG_RECOMMENDFUNCTIONALVIDPN_CONST);
OPQC(DXGKARG_COMMITVIDPN_CONST);
OPQC(DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH_CONST);
OPQC(DXGKARG_RECOMMENDMONITORMODES_CONST);
OPQC(DXGKARG_RECOMMENDVIDPNTOPOLOGY_CONST);
OPQC(DXGKARG_ENUMVIDPNCOFUNCMODALITY_CONST);
OPQC(DXGKARG_SETPALETTE);
OPQC(DXGKARG_SETPOINTERPOSITION);
OPQC(DXGKARG_SETPOINTERSHAPE);
OPQ(DXGKARG_GETSCANLINE); typedef DXGKARG_GETSCANLINE *INOUT_PDXGKARG_GETSCANLINE;
OPQC(DXGKARG_STOPCAPTURE);
OPQC(DXGKARG_SETDISPLAYPRIVATEDRIVERFORMAT);
OPQ(DXGKARG_CREATEOVERLAY); typedef DXGKARG_CREATEOVERLAY *INOUT_PDXGKARG_CREATEOVERLAY;
OPQC(DXGKARG_UPDATEOVERLAY);
OPQC(DXGKARG_FLIPOVERLAY);
typedef struct _DXGKARG_RENDER DXGKARG_RENDER, *INOUT_PDXGKARG_RENDER;
typedef struct _DXGKARG_PRESENT DXGKARG_PRESENT, *INOUT_PDXGKARG_PRESENT;
typedef struct _DXGKARG_RENDERGDI DXGKARG_RENDERGDI, *INOUT_PDXGKARG_RENDERGDI;
OPQC(DXGKARG_SETROOTPAGETABLE);
OPQ(DXGKARG_GETROOTPAGETABLESIZE); typedef DXGKARG_GETROOTPAGETABLESIZE *INOUT_PDXGKARG_GETROOTPAGETABLESIZE;
OPQC(DXGKARG_MAPCPUHOSTAPERTURE);
OPQC(DXGKARG_UNMAPCPUHOSTAPERTURE);
OPQ(DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT); typedef DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT *IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT;
OPQ(DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT2); typedef DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT2 *IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT2;
OPQC(DXGKARG_SETVIDPNSOURCEADDRESS);
OPQC(DXGKARG_SETVIDPNSOURCEVISIBILITY);
OPQC(DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2);
OPQC(DXGKARG_SETSTABLEPOWERSTATE);
OPQC(DXGKARG_SETVIDEOPROTECTEDREGION);
OPQ(DXGKARG_CALIBRATEGPUCLOCK); typedef DXGKARG_CALIBRATEGPUCLOCK *OUT_PDXGKARG_CALIBRATEGPUCLOCK;
OPQ(DXGKARG_FORMATHISTORYBUFFER);
OPQC(DXGKARG_CONTROLINTERRUPT2);
OPQC(DXGKARG_ACQUIRESWIZZLINGRANGE); typedef DXGKARG_ACQUIRESWIZZLINGRANGE *INOUT_PDXGKARG_ACQUIRESWIZZLINGRANGE;
OPQC(DXGKARG_RELEASESWIZZLINGRANGE);
OPQC(DXGKARG_PREEMPTCOMMAND);
OPQC(DXGKARG_CANCELCOMMAND);
OPQC(DXGKARG_PATCH);
OPQC(DXGKARG_COLLECTDBGINFO);
typedef struct _DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY;
typedef const DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY *IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY;

/* ---- repo 内部类型 ---- */
typedef struct _ROK_SIM_STATS {
    ULONG PacketCount, Type2Count, Type0Count, UnknownOpcodes;
    UINT32 LastIndirectBaseLo, LastIndirectSizeDwords, LastFenceSubmitted;
    ULONG OpcodeCounts[64];
} ROK_SIM_STATS;
typedef struct _ROK_CCC_SETTINGS { UINT Version, ValidMask, EngineClockKHz, MemoryClockKHz, OverdriveEnable, FanSpeedPercent, Source, Reserved; } ROK_CCC_SETTINGS;
typedef struct _ROK_DEVICE { struct _ROK_ADAPTER *Adapter; } ROK_DEVICE, *PROK_DEVICE;
typedef struct _ROK_CONTEXT { struct _ROK_ADAPTER *Adapter; UINT NodeOrdinal, EngineAffinity; ULONG Seq; LIST_ENTRY Link; } ROK_CONTEXT, *PROK_CONTEXT;
typedef struct _ROK_ALLOCATION { struct _ROK_ADAPTER *Adapter; ULONG Seq; LIST_ENTRY Link; } ROK_ALLOCATION, *PROK_ALLOCATION;
typedef struct _ROK_PROCESS { struct _ROK_ADAPTER *Adapter; HANDLE hDxgkProcess; } ROK_PROCESS, *PROK_PROCESS;
typedef struct _ROK_ADAPTER {
    LIST_ENTRY AllocationList, ContextList;
    KSPIN_LOCK AllocationLock, ContextLock;
    ULONG AllocationSeq, ContextSeq;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PVOID DxgkIfUnused;
    BOOLEAN Started;
    ULONG PciVendorId, PciDeviceId, PciRevisionId;
    PHYSICAL_ADDRESS MmioBase; ULONG MmioLength; PVOID MmioVirtualAddress;
    volatile LONG LastCompletedFenceId, LastSubmittedFenceId, SubmitCount, PagingOpsCount;
    BOOLEAN SimulationMode, HypervisorPresent;
    PVOID SimRegisterSpace; ULONG SimRegisterSpaceSize;
    ROK_SIM_STATS SimStats;
    ROK_CCC_SETTINGS CccSettings;
} ROK_ADAPTER, *PROK_ADAPTER;

/* rok_escape.h 真实头（用户态分支；RokGpuFamilyFromDeviceId 为其自带 static inline）*/
#include "rok_escape.h"
NTSTATUS RokCccGetSettings(PROK_ADAPTER a, ROK_CCC_SETTINGS *s);
NTSTATUS RokCccSetSettings(PROK_ADAPTER a, const ROK_CCC_SETTINGS *s);
BOOLEAN RokSimEnabled(PROK_ADAPTER a);
VOID RokSimObserveDma(PROK_ADAPTER a, CONST VOID *buf, UINT n);
/* gfxinit.c（M3.4a）—— 提取块引用；完整类型面见 gfxinit.h */
NTSTATUS RokGfxProbeStatus(PROK_ADAPTER a, PROK_ESC_HW_STATUS_OUT out);
"""

BODY = r"""
/* ===== 以下为从 ddi.c 机械提取的被测代码块 ===== */

static NTSTATUS TestQueryAdapterInfo(DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo)
{
    PAGED_CODE();
    switch (pQueryAdapterInfo->Type)
    {
QA_CASES
    default:
        return STATUS_NOT_IMPLEMENTED;
    }
}

static NTSTATUS TestEscape(PROK_ADAPTER adapter, DXGKARG_ESCAPE *pEscape)
{
    PROK_ESCAPE_HEADER header;
    PAGED_CODE();
    if (pEscape == NULL || pEscape->pPrivateDriverData == NULL ||
        pEscape->PrivateDriverDataSize < sizeof(ROK_ESCAPE_HEADER))
    {
        return STATUS_INVALID_PARAMETER;
    }
    header = (PROK_ESCAPE_HEADER)pEscape->pPrivateDriverData;
    switch (header->Type)
    {
ESC_CASE
    default:
        return STATUS_NOT_IMPLEMENTED;
    }
}
"""

final = (PRELUDE + "\n" + BODY.replace("QA_CASES", qa_cases).replace("ESC_CASE", esc_case)
         + "\n" + qes_fn + "\n" + new_fns)

os.makedirs("/tmp/kmdcheck", exist_ok=True)
test_c = "/tmp/kmdcheck/test_new.c"
open(test_c, "w", encoding="utf-8").write(final)

cmd = ["gcc", "-std=c11", "-fsyntax-only", "-Wall", "-Wextra",
       "-Wno-unused-parameter", "-Wno-unused-function", "-I", os.path.join(REPO, "kmd/src"), test_c]
r = subprocess.run(cmd, capture_output=True, text=True)
print("gcc exit:", r.returncode)
if r.returncode != 0:
    print(r.stderr[:8000])
    sys.exit(1)
print("OK: M2.8 新增/修改代码块在 mock WDK 环境下语法与类型检查通过")
