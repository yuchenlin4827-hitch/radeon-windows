// SPDX-License-Identifier: MIT
// kmock2/ntddk.h — E2E harness 的内核环境 mock（基础层）。
//
// 与 kmock1（ccc.c/sim.c 逻辑单测）不同，kmock2 要编译 kmd/src 的
// **全部五个 C 文件**（driver.c/adapter.c/ddi.c/sim.c/ccc.c），因此
// 需要 ntddk/dispmprt/d3dkmddi 的一个足够完整子集：
//   * 结构体只需覆盖 ddi.c/adapter.c/driver.c **实际访问的字段**；
//   * 桩 DDI 用到的类型保持"完整但最小"（RtlZeroMemory 需要 sizeof）；
//   * 函数指针类型与 ddi.h 签名逐一对齐 —— 驱动侧任何签名漂移会在
//     本 mock 上直接编译失败（这正是测试价值）。
//
// 语义约定：
//   * ULONG = 32 位（Windows 语义，非 Linux 的 64 位 long）；
//   * UNICODE_STRING.Length 以字节计（内核语义）；
//   * 单线程 harness：自旋锁为空操作，原子操作用 GCC builtin。
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>
#include <stdarg.h>

// ---- 基础类型（Windows 宽度语义）----
typedef int32_t         INT;
typedef int32_t         INT32;
typedef int64_t         INT64;
typedef uint32_t        UINT32;
typedef uint64_t        UINT64;
typedef uint32_t        UINT;
typedef unsigned int    ULONG;
typedef signed int      LONG;
typedef uint64_t        ULONGLONG;
typedef int64_t         LONGLONG;
typedef unsigned short  USHORT;
typedef short           SHORT;
typedef unsigned char   UCHAR;
typedef signed char     SCHAR;
typedef char            CHAR;
typedef uint8_t         BYTE;
typedef uint16_t        WORD;
typedef uint32_t        DWORD;
typedef wchar_t         WCHAR;
typedef int             BOOLEAN;
typedef void            VOID;
typedef void*           PVOID;
typedef void*           HANDLE;
typedef const WCHAR*    PCWSTR;
typedef WCHAR*          PWSTR;
typedef const CHAR*     PCSTR;
typedef CHAR*           PCHAR;
typedef void*           PDEVICE_OBJECT;     // harness 只传不透明指针
typedef void*           PDRIVER_OBJECT;     // DriverEntry 第一个参数
typedef size_t          SIZE_T;
typedef intptr_t        INT_PTR;
typedef uintptr_t       UINT_PTR;
typedef uintptr_t       ULONG_PTR;
typedef ULONG_PTR       SIZE_T_WDK;
typedef ULONG*          PULONG;
typedef LONG*           PLONG;
typedef UCHAR*          PUCHAR;
typedef USHORT*         PUSHORT;
typedef HANDLE*         PHANDLE;
typedef PVOID*          PPVOID;
typedef SIZE_T*         PSIZE_T;

// ---- GUID（PowerRuntimeControlRequest 的控制码参数）----
typedef struct _GUID {
    ULONG   Data1;
    USHORT  Data2;
    USHORT  Data3;
    UCHAR   Data4[8];
} GUID;
typedef const GUID *LPCGUID;

#ifndef CONST
#define CONST const
#endif
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif
#define MAXULONG   0xFFFFFFFFUL

#ifndef FIELD_OFFSET
#define FIELD_OFFSET(t, f)  offsetof(t, f)
#endif
#define RTL_NUMBER_OF(a)    (sizeof(a) / sizeof((a)[0]))

// C_ASSERT（rok_escape.h 内核分支会用到）
#ifndef C_ASSERT
#define ROK_KMOCK2_PASTE2(x, y)  x##y
#define ROK_KMOCK2_PASTE(x, y)   ROK_KMOCK2_PASTE2(x, y)
#define C_ASSERT(e) \
    typedef char ROK_KMOCK2_PASTE(rok_c2_assert_, __LINE__)[((e) ? 1 : -1)]
#endif

#ifndef __inline
#define __inline inline
#endif

// ---- SAL 标注（宿主全为空）----
// WDM 遗留 IN/OUT/OPTIONAL 宏（ddi.h M2.8 新成员签名沿用 WDK 拼写）。
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif
#define _In_
#define _Out_
#define _Inout_
#define _In_opt_
#define _In_reads_bytes_(x)
#define _In_reads_bytes_opt_(x)
#define _Inout_updates_bytes_(x)
#define _Inout_updates_bytes_opt_(x)
#define _Out_writes_bytes_(x)
#define _Out_opt_
#define _Inout_opt_
#define _Success_(x)
#define _Ret_maybenull_
#define _Post_writable_byte_size_(x)
#define _IRQL_requires_max_(x)
#define _IRQL_requires_(x)
#define _When_(c, a)
#define _Analysis_assume_(x)

// ---- PnP 参数宏（adapter.h/ddi.h 签名用；展开到 mock 真实类型）----
// 宏惰性展开：adapter.h/ddi.h 使用时 dispmprt/d3dkmddi 已由 precomp.h
// 引入，类型均已定义。
#define IN_CONST_PDEVICE_OBJECT   const PVOID
#define IN_CONST_PVOID            const PVOID
#define IN_PVOID                  PVOID
#define IN_CONST_HANDLE           HANDLE
#define IN_ULONG                  ULONG
#define IN_UCHAR                  UCHAR
#define IN_BOOLEAN                BOOLEAN
#define IN_DEVICE_POWER_STATE     DEVICE_POWER_STATE
#define IN_POWER_ACTION           POWER_ACTION
#define IN_DXGK_EVENT_TYPE        DXGK_EVENT_TYPE
#define OUT_PULONG                PULONG
#define OUT_PPVOID                PVOID*
#define IN_PDXGK_START_INFO       DXGK_START_INFO*
#define IN_PDXGKRNL_INTERFACE     DXGKRNL_INTERFACE*
#define IN_PVIDEO_REQUEST_PACKET  VIDEO_REQUEST_PACKET*
#define IN_PQUERY_INTERFACE       PVOID
#define INOUT_PDXGK_CHILD_STATUS            DXGK_CHILD_STATUS*
#define INOUT_PDXGK_DEVICE_DESCRIPTOR       DXGK_DEVICE_DESCRIPTOR*
#define INOUT_PDXGK_CHILD_CONTAINER_ID      DXGK_CHILD_CONTAINER_ID*
// LDA 链接协商（LinkDevice 第三参数，实为指针类型，WDK 同款拼写）。
#define INOUT_PLINKED_DEVICE                PLINKED_DEVICE

// ---- ntintsafe / WDM 前向类型（SetPowerState/NotifyAcpiEvent 桩参数）----
typedef enum _DEVICE_POWER_STATE { PowerDeviceUnspecified = 0, PowerDeviceD3 = 4 } DEVICE_POWER_STATE;
typedef enum _POWER_ACTION { PowerActionNone = 0 } POWER_ACTION;
typedef enum _DXGK_EVENT_TYPE { DxgkEventAcpiEvent = 1 } DXGK_EVENT_TYPE;

// ---- 物理地址 / LUID / 大整数 ----
typedef union _LARGE_INTEGER {
    struct { ULONG LowPart; LONG HighPart; } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _PHYSICAL_ADDRESS {
    struct { ULONG LowPart; LONG HighPart; } u;
    LONGLONG QuadPart;
} PHYSICAL_ADDRESS, *PPHYSICAL_ADDRESS;

typedef struct _LUID {
    ULONG LowPart;
    LONG  HighPart;
} LUID, *PLUID;

// ---- UNICODE_STRING（Length 以字节计 —— 内核语义）----
typedef struct _UNICODE_STRING {
    USHORT  Length;
    USHORT  MaximumLength;
    PWSTR   Buffer;
} UNICODE_STRING, *PUNICODE_STRING, *PCUNICODE_STRING;

void RtlInitUnicodeString(PUNICODE_STRING s, PCWSTR buf);
void RtlCopyUnicodeString(PUNICODE_STRING dst, PCUNICODE_STRING src);
BOOLEAN RtlAppendUnicodeToString(PUNICODE_STRING dst, PCWSTR suffix);
BOOLEAN RtlAppendUnicodeStringToString(PUNICODE_STRING dst, PCUNICODE_STRING src);

// ---- 双向链表 / 自旋锁 / 原子操作 ----
typedef struct _LIST_ENTRY {
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

// LINKED_DEVICE（DxgkDdiLinkDevice 参数；驱动明确拒绝 LDA，仅保签名）。
typedef struct _LINKED_DEVICE {
    ULONG Dummy;
} LINKED_DEVICE, *PLINKED_DEVICE;

#define CONTAINING_RECORD(addr, type, field) \
    ((type *)((CHAR *)(addr) - offsetof(type, field)))

void InitializeListHead(PLIST_ENTRY head);
void InsertTailList(PLIST_ENTRY head, PLIST_ENTRY entry);
void InsertHeadList(PLIST_ENTRY head, PLIST_ENTRY entry);
int  IsListEmpty(CONST PLIST_ENTRY head);
int  RemoveEntryList(PLIST_ENTRY entry);

typedef UCHAR   KIRQL;
typedef CHAR    KSPIN_LOCK;
typedef KSPIN_LOCK* PKSPIN_LOCK;
void KeInitializeSpinLock(PKSPIN_LOCK lock);
void KeAcquireSpinLock(PKSPIN_LOCK lock, KIRQL *oldIrql);
void KeReleaseSpinLock(PKSPIN_LOCK lock, KIRQL oldIrql);

static __inline LONG RokM2InterlockedExchange(volatile LONG *Target, LONG Value)
{
    return (LONG)__atomic_exchange_n(Target, Value, __ATOMIC_SEQ_CST);
}
#define InterlockedExchange(Target, Value) \
    RokM2InterlockedExchange((volatile LONG *)(Target), (LONG)(Value))

static __inline LONG RokM2InterlockedIncrement(volatile LONG *Addend)
{
    return __atomic_add_fetch(Addend, 1, __ATOMIC_SEQ_CST);
}
#define InterlockedIncrement(Addend) \
    RokM2InterlockedIncrement((volatile LONG *)(Addend))

// WDK 语义：始终返回 Destination 的原值（交换前）。
// GCC builtin 无论成败都会把 Destination 原值写回 expected（此处 Comperand）。
static __inline LONG RokM2InterlockedCompareExchange(volatile LONG *Destination,
                                                     LONG Exchange, LONG Comperand)
{
    __atomic_compare_exchange_n(Destination, &Comperand, Exchange, 0,
                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return Comperand;
}
#define InterlockedCompareExchange(Destination, Exchange, Comperand) \
    RokM2InterlockedCompareExchange((volatile LONG *)(Destination), \
                                    (LONG)(Exchange), (LONG)(Comperand))

// ---- NTSTATUS ----
typedef LONG NTSTATUS;
#define NT_SUCCESS(s)   (((NTSTATUS)(s)) >= 0)
#define STATUS_SUCCESS                      ((NTSTATUS)0x00000000L)
#define STATUS_UNSUCCESSFUL                 ((NTSTATUS)0xC0000001L)
#define STATUS_BUFFER_TOO_SMALL             ((NTSTATUS)0xC0000023L)
#define STATUS_INVALID_PARAMETER            ((NTSTATUS)0xC000000DL)
#define STATUS_INVALID_PARAMETER_1          ((NTSTATUS)0xC00000EFL)
#define STATUS_INVALID_HANDLE               ((NTSTATUS)0xC0000008L)
#define STATUS_INSUFFICIENT_RESOURCES       ((NTSTATUS)0xC000009AL)
#define STATUS_NOT_IMPLEMENTED              ((NTSTATUS)0xC0000002L)
#define STATUS_NOT_SUPPORTED                ((NTSTATUS)0xC00000BBL)
#define STATUS_REVISION_MISMATCH            ((NTSTATUS)0xC0000059L)
#define STATUS_OBJECT_NAME_NOT_FOUND        ((NTSTATUS)0xC0000034L)
#define STATUS_DEVICE_CONFIGURATION_ERROR   ((NTSTATUS)0xC0000182L)

// ---- 调试输出 / IRQL ----
#define PAGED_CODE()            ((void)0)
#define ASSERT(e)               assert(e)
#define UNREFERENCED_PARAMETER(x)   ((void)(x))
#define DISPATCH_LEVEL          2
#define PASSIVE_LEVEL           0

void rok_kdprintf(ULONG Component, ULONG Level, PCSTR Fmt, ...);
#define DPFLTR_IHVDRIVER_ID    0x53
#define DPFLTR_ERROR_LEVEL     0x0
#define DPFLTR_WARNING_LEVEL   0x2
#define DPFLTR_TRACE_LEVEL     0x4
#define DPFLTR_INFO_LEVEL      0x8
#define KdPrintEx(args)  rok_kdprintf args

// ---- 内存原语 ----
#define RtlZeroMemory(d, l)     memset((d), 0, (l))
#define RtlCopyMemory(d, s, l)  memcpy((d), (s), (l))
#define RtlFillMemory(d, l, f)  memset((d), (f), (l))
#define RtlEqualMemory(a, b, l) (memcmp((a), (b), (l)) == 0)

// ---- 寄存器访问原语（宿主 = volatile 解引用；真机 MMIO 语义由硬件保证）----
#define READ_REGISTER_ULONG(reg)    (*(volatile ULONG *)(reg))
#define WRITE_REGISTER_ULONG(reg,v) ((void)(*(volatile ULONG *)(reg) = (ULONG)(v)))
#define READ_REGISTER_USHORT(reg)   (*(volatile USHORT *)(reg))
#define WRITE_REGISTER_USHORT(reg,v)((void)(*(volatile USHORT *)(reg) = (USHORT)(v)))
#define READ_REGISTER_UCHAR(reg)    (*(volatile UCHAR *)(reg))
#define WRITE_REGISTER_UCHAR(reg,v) ((void)(*(volatile UCHAR *)(reg) = (UCHAR)(v)))

// ---- 池分配（malloc 底座 + tag 记账，E2E 退出前核对平衡）----
typedef enum _POOL_TYPE { NonPagedPool = 0, PagedPool = 1 } POOL_TYPE;
PVOID ExAllocatePoolWithTag(POOL_TYPE PoolType, SIZE_T Size, ULONG Tag);
VOID  ExFreePoolWithTag(PVOID Ptr, ULONG Tag);
ULONG RokMockPoolOutstanding(VOID);

// ---- CPUID（可注入钩子：真实执行 / 模拟 hypervisor / 模拟裸机）----
extern void (*rok_mock_cpuid)(INT regs[4], INT leaf);
void __cpuid(INT regs[4], INT leaf);

// ---- 对象属性 / 注册表（内存 mock）----
#define OBJ_CASE_INSENSITIVE    0x00000040L
#define OBJ_KERNEL_HANDLE       0x00000200L

typedef struct _OBJECT_ATTRIBUTES {
    ULONG           Length;
    PUNICODE_STRING ObjectName;
    ULONG           Attributes;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

#define InitializeObjectAttributes(oa, n, a, r, s)  \
    do {                                            \
        (oa)->Length     = sizeof(OBJECT_ATTRIBUTES); \
        (oa)->ObjectName = (n);                     \
        (oa)->Attributes = (a);                     \
        (void)(r); (void)(s);                       \
    } while (0)

#define KEY_QUERY_VALUE             0x0001
#define REG_DWORD                   4UL

typedef enum _KEY_INFORMATION_CLASS {
    KeyValuePartialInformation = 1
} KEY_INFORMATION_CLASS;

typedef struct _KEY_VALUE_PARTIAL_INFORMATION {
    ULONG   TitleIndex;
    ULONG   Type;
    ULONG   DataLength;
    UCHAR   Data[1];
} KEY_VALUE_PARTIAL_INFORMATION, *PKEY_VALUE_PARTIAL_INFORMATION;

NTSTATUS ZwOpenKey(PHANDLE KeyHandle, ULONG DesiredAccess, POBJECT_ATTRIBUTES oa);
NTSTATUS ZwQueryValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName,
                         KEY_INFORMATION_CLASS Class,
                         PVOID KeyValueInformation, ULONG Length,
                         PULONG ResultLength);
NTSTATUS ZwClose(HANDLE KeyHandle);

// mock 注册表数据表（路径/值名大小写不敏感）。
typedef struct _ROK_MOCK_REG_VALUE {
    PCWSTR  Path;
    PCWSTR  ValueName;
    ULONG   Dword;
} ROK_MOCK_REG_VALUE;
extern ROK_MOCK_REG_VALUE rok_mock_registry[];
extern const int          rok_mock_registry_count;
void rok_mock_registry_reset(void);
