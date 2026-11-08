// SPDX-License-Identifier: MIT
// kmock/precomp.h — 主机端（Linux CI）单元测试的内核环境 mock。
//
// 用法：build-and-run.sh 把 kmd/src/{adapter.h,sim.h,ccc.h,ccc.c,sim.c}
// 拷进 scratch 目录（**不含 precomp.h**），再以 -Ikmock 编译 —— 源文件里
// 的 `#include "precomp.h"` 落到本文件。测的是仓库里的真实源码字节
// （脚本前后各做一次 sha256 校验），mock 只提供内核原语的宿主语义。
//
// 覆盖面（仅模拟 ccc.c/sim.c 实际用到的子集，不求完备）：
//   基础类型 / SAL 标注 / NTSTATUS / UNICODE_STRING / 注册表 Zw*（内存表）
//   池分配（malloc）/ KdPrintEx（printf）/ PAGED_CODE（空）/ __cpuid（可注入）
#pragma once

// ---- 基础类型 ----
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>
#include <stdarg.h>

typedef uint32_t        UINT32;
typedef uint64_t        UINT64;
typedef uint32_t        UINT;
typedef int32_t         INT;
typedef unsigned int    ULONG;
typedef signed int      LONG;
typedef unsigned short  USHORT;
typedef unsigned char   UCHAR;
typedef char            CHAR;
typedef wchar_t         WCHAR;
typedef int             BOOLEAN;
typedef void            VOID;
typedef void*           PVOID;
typedef void*           HANDLE;
typedef const WCHAR*    PCWSTR;
typedef WCHAR*          PWSTR;
typedef const CHAR*     PCSTR;
typedef void*           PDEVICE_OBJECT;     // ccc.c/sim.c 不解引用，仅占位
typedef unsigned long   SIZE_T;
typedef intptr_t        INT_PTR;
typedef uintptr_t       UINT_PTR;
typedef ULONG*          PULONG;
typedef LONG*           PLONG;
typedef UCHAR*          PUCHAR;
typedef USHORT*         PUSHORT;
typedef HANDLE*         PHANDLE;
typedef VOID*           PPVOID;

#ifndef CONST
#define CONST const
#endif
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#ifndef FIELD_OFFSET
#define FIELD_OFFSET(t, f)  offsetof(t, f)
#endif
#define RTL_NUMBER_OF(a)    (sizeof(a) / sizeof((a)[0]))

// C_ASSERT（用户态语义同 rok_escape.h 的回退定义）
#ifndef C_ASSERT
#define ROK_KMOCK_PASTE2(x, y)  x##y
#define ROK_KMOCK_PASTE(x, y)   ROK_KMOCK_PASTE2(x, y)
#define C_ASSERT(e) \
    typedef char ROK_KMOCK_PASTE(rok_c_assert_, __LINE__)[((e) ? 1 : -1)]
#endif

// __inline：头文件里以 `__inline ULONG Fn(...)` 形式出现 → inline 即可。
#ifndef __inline
#define __inline inline
#endif

// ---- SAL 标注（宿主编译全为空）----
#define _In_
#define _Out_
#define _Inout_
#define _In_opt_
#define _In_reads_bytes_(x)
#define _Success_(x)
#define _Ret_maybenull_
#define _Post_writable_byte_size_(x)
#define _Out_writes_z_(x)
#define _Out_opt_
#define _Inout_opt_

// adapter.h 生命周期声明用的 WDK 复合宏（仅声明，无实现编译）。
#define IN_CONST_PDEVICE_OBJECT   const PVOID
#define OUT_PPVOID                PVOID*
#define IN_CONST_PVOID            const PVOID
#define IN_PVOID                  PVOID
#define IN_PDXGK_START_INFO       PVOID
#define IN_PDXGKRNL_INTERFACE     PVOID
#define OUT_PULONG                PULONG
#define IN_ULONG                  ULONG

// ---- NTSTATUS ----
typedef LONG NTSTATUS;
#define NT_SUCCESS(s)   (((NTSTATUS)(s)) >= 0)
#define STATUS_SUCCESS                      ((NTSTATUS)0x00000000L)
#define STATUS_OBJECT_NAME_NOT_FOUND        ((NTSTATUS)0xC0000034L)
#define STATUS_INVALID_PARAMETER            ((NTSTATUS)0xC000000DL)
#define STATUS_INSUFFICIENT_RESOURCES       ((NTSTATUS)0xC000009AL)
#define STATUS_REVISION_MISMATCH            ((NTSTATUS)0xC0000059L)

// ---- 结构化异常宏 / 调试输出 / IRQL ----
#define PAGED_CODE()            ((void)0)
#define ASSERT(e)               assert(e)

// KdPrintEx((Component, Level, Fmt, ...)) — 双括号宏 → 转发 vprintf。
// %ls 在 glibc printf 下的语义与 MSVC 一致（宽字符串）。
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

// ---- UNICODE_STRING（语义对齐内核：Length/MaximumLength 以字节计）----
typedef struct _UNICODE_STRING {
    USHORT  Length;             // 字节数（不含 NUL）
    USHORT  MaximumLength;      // 字节数（含 NUL）
    PWSTR   Buffer;
} UNICODE_STRING, *PUNICODE_STRING, *PCUNICODE_STRING;

void    RtlInitUnicodeString(PUNICODE_STRING s, PCWSTR buf);
void    RtlCopyUnicodeString(PUNICODE_STRING dst, PCUNICODE_STRING src);
BOOLEAN RtlAppendUnicodeToString(PUNICODE_STRING dst, PCWSTR suffix);
BOOLEAN RtlAppendUnicodeStringToString(PUNICODE_STRING dst, PCUNICODE_STRING src);

// ---- 对象属性 / 注册表（内存 mock，实现在 mock_registry.c）----
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

// mock 注册表数据表（测试用例填充；路径/值名大小写不敏感）。
typedef struct _ROK_MOCK_REG_VALUE {
    PCWSTR  Path;       // 键全路径（宽字符）
    PCWSTR  ValueName;  // 值名
    ULONG   Dword;      // REG_DWORD 数据
} ROK_MOCK_REG_VALUE;
extern ROK_MOCK_REG_VALUE rok_mock_registry[];
extern const int          rok_mock_registry_count;
void rok_mock_registry_reset(void);

// ---- 池分配 ----
typedef enum _POOL_TYPE { NonPagedPool = 0, PagedPool = 1 } POOL_TYPE;
PVOID ExAllocatePoolWithTag(POOL_TYPE PoolType, SIZE_T Size, ULONG Tag);
VOID  ExFreePoolWithTag(PVOID Ptr, ULONG Tag);

// ---- 自旋锁 / 链表 / 其他内核类型（adapter.h 声明面需要；结构体需完整
// 定义 —— adapter.h 以值字段使用 DXGKRNL_INTERFACE / PHYSICAL_ADDRESS）----
typedef struct _LIST_ENTRY {
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;
typedef CHAR KSPIN_LOCK;
typedef UINT64 PHYSICAL_ADDRESS;
typedef struct _DXGKRNL_INTERFACE_MOCK {    // 值字段占位（仅声明面被编译）
    ULONG Dummy;
} DXGKRNL_INTERFACE;
typedef struct _PCM_RESOURCE_LIST_MOCK {    // 以指针传递，仅需前向形态
    ULONG Dummy;
} PCM_RESOURCE_LIST, *PCM_RESOURCE_LIST_PTR;
typedef struct _DXGK_START_INFO_MOCK {
    ULONG Dummy;
} DXGK_START_INFO;

// ---- CPUID（可注入：测试用例替换钩子）----
// 默认实现 = 真执行 x86 cpuid；测试可指向注入版（恒定 leaf1 ECX bit31 等）。
extern void (*rok_mock_cpuid)(INT regs[4], INT leaf);
void __cpuid(INT regs[4], INT leaf);

// ---- MMIO（BAR0）mock：64KB 寄存器空间 + 读钩子 + 停顿计数 ----
// r600hw.c 的 READ/WRITE_REGISTER_ULONG 落到这里：真实内核是设备内存，
// 宿主是测试可控的普通内存。读钩子可在返回前改写值（模拟硬件状态
// 迁移，如 GRBM GUI_ACTIVE 轮询后清零）；停顿只计数不实际等待。
#define ROK_KMOCK_MMIO_SIZE 65536
extern unsigned char rok_mock_mmio[ROK_KMOCK_MMIO_SIZE];
typedef ULONG (*rok_mock_reg_hook_fn)(ULONG Offset, ULONG Value);
extern rok_mock_reg_hook_fn rok_mock_reg_read_hook;   // NULL = 直读
extern long rok_mock_stall_count;                     // RokStallUs 次数
extern long rok_mock_stall_us_total;                  // 累计微秒
void rok_mock_mmio_reset(void);
ULONG rok_mock_read_reg(volatile ULONG *p);
void  rok_mock_write_reg(volatile ULONG *p, ULONG v);
#define READ_REGISTER_ULONG(p)      rok_mock_read_reg((p))
#define WRITE_REGISTER_ULONG(p, v)  rok_mock_write_reg((p), (v))
void KeStallExecutionProcessor(ULONG us);

// SIZE_T 最大值（ExAllocatePoolWithTag 失败语义测试用）
#ifndef ROK_KMOCK_SIZE_MAX
#define ROK_KMOCK_SIZE_MAX ((SIZE_T)-1)
#endif

// ---- precomp.h 原有定义（保真）----
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x5023
#endif
#define ROK_POOL_TAG     'DkoR'
#define ROK_KERNEL_BUILD 1
