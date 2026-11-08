// SPDX-License-Identifier: MIT
// mock_registry.c — kmock 内核原语的宿主实现（注册表内存表 / 池 / Rtl 字符串
// / KdPrint / cpuid 钩子）。
//
// Zw* 语义对齐内核：
//   * ZwOpenKey   —— 按路径（大小写不敏感）查表；不存在 → STATUS_OBJECT_NAME_NOT_FOUND；
//   * ZwQueryValueKey —— 只支持 REG_DWORD + KeyValuePartialInformation；
//   * 句柄 = 表内命中索引+1（非 0 即有效），ZwClose 仅校验范围。
#include "precomp.h"

#include <stdlib.h>
#include <stdarg.h>

// ------------------------------------------------------------------
// KdPrint / 池 / cpuid
// ------------------------------------------------------------------

void rok_kdprintf(ULONG Component, ULONG Level, PCSTR Fmt, ...)
{
    va_list ap;
    (void)Component; (void)Level;
    va_start(ap, Fmt);
    vprintf(Fmt, ap);
    va_end(ap);
}

PVOID ExAllocatePoolWithTag(POOL_TYPE PoolType, SIZE_T Size, ULONG Tag)
{
    (void)PoolType; (void)Tag;
    return malloc(Size);
}

VOID ExFreePoolWithTag(PVOID Ptr, ULONG Tag)
{
    (void)Tag;
    free(Ptr);
}

// 默认 cpuid：真执行（x86/x86_64 主机，内联汇编避免与 gcc cpuid.h 的
// __cpuid 宏冲突）；测试用例可整体替换钩子。
static void rok_native_cpuid(INT regs[4], INT leaf)
{
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"((unsigned int)leaf), "c"(0u));
    regs[0] = (INT)eax; regs[1] = (INT)ebx;
    regs[2] = (INT)ecx; regs[3] = (INT)edx;
}

void (*rok_mock_cpuid)(INT regs[4], INT leaf) = rok_native_cpuid;

void __cpuid(INT regs[4], INT leaf)
{
    rok_mock_cpuid(regs, leaf);
}

// ------------------------------------------------------------------
// UNICODE_STRING（字节语义对齐内核）
// ------------------------------------------------------------------

void RtlInitUnicodeString(PUNICODE_STRING s, PCWSTR buf)
{
    if (s == NULL)
    {
        return;
    }
    // mock 不经 Buffer 写入，const 去除安全（内核 WDK 签名同为 PCWSTR 兼容）。
    s->Buffer = (PWSTR)(uintptr_t)buf;
    if (buf != NULL)
    {
        size_t chars = wcslen(buf);
        // 内核对超长（> MAXUSHORT/2）断言；宿主测试不会触到，保底钳制。
        if (chars > 32766)
        {
            chars = 32766;
        }
        s->Length = (USHORT)(chars * sizeof(WCHAR));
        s->MaximumLength = (USHORT)((chars + 1) * sizeof(WCHAR));
    }
    else
    {
        s->Length = 0;
        s->MaximumLength = 0;
    }
}

void RtlCopyUnicodeString(PUNICODE_STRING dst, PCUNICODE_STRING src)
{
    USHORT copyChars;

    if (dst == NULL)
    {
        return;
    }
    // 内核语义：按 dst->MaximumLength 截断；不追加 NUL。
    copyChars = (USHORT)(src != NULL ? src->Length / sizeof(WCHAR) : 0);
    {
        USHORT maxChars = (USHORT)(dst->MaximumLength / sizeof(WCHAR));
        if (maxChars > 0)
        {
            maxChars -= 1;      // 留 1 个 NUL（宿主实现总是 NUL 结尾，便于 mock）
        }
        if (copyChars > maxChars)
        {
            copyChars = maxChars;
        }
    }
    if (copyChars > 0 && src->Buffer != NULL)
    {
        memmove(dst->Buffer, src->Buffer, copyChars * sizeof(WCHAR));
    }
    dst->Buffer[copyChars] = L'\0';
    dst->Length = (USHORT)(copyChars * sizeof(WCHAR));
}

BOOLEAN RtlAppendUnicodeToString(PUNICODE_STRING dst, PCWSTR suffix)
{
    size_t addChars, dstChars, maxChars;

    if (dst == NULL || suffix == NULL)
    {
        return TRUE;    // 内核对 NULL suffix 是无操作成功
    }
    addChars = wcslen(suffix);
    dstChars = dst->Length / sizeof(WCHAR);
    maxChars = dst->MaximumLength / sizeof(WCHAR);
    if (maxChars > 0)
    {
        maxChars -= 1;
    }
    if (dstChars + addChars > maxChars)
    {
        return FALSE;   // STATUS_BUFFER_TOO_SMALL 语义
    }
    memcpy(dst->Buffer + dstChars, suffix, addChars * sizeof(WCHAR));
    dst->Buffer[dstChars + addChars] = L'\0';
    dst->Length = (USHORT)((dstChars + addChars) * sizeof(WCHAR));
    return TRUE;
}

BOOLEAN RtlAppendUnicodeStringToString(PUNICODE_STRING dst, PCUNICODE_STRING src)
{
    size_t addChars, dstChars, maxChars;

    if (dst == NULL || src == NULL || src->Buffer == NULL)
    {
        return TRUE;
    }
    addChars = src->Length / sizeof(WCHAR);
    dstChars = dst->Length / sizeof(WCHAR);
    maxChars = dst->MaximumLength / sizeof(WCHAR);
    if (maxChars > 0)
    {
        maxChars -= 1;
    }
    if (dstChars + addChars > maxChars)
    {
        return FALSE;
    }
    memcpy(dst->Buffer + dstChars, src->Buffer, addChars * sizeof(WCHAR));
    dst->Buffer[dstChars + addChars] = L'\0';
    dst->Length = (USHORT)((dstChars + addChars) * sizeof(WCHAR));
    return TRUE;
}

// ------------------------------------------------------------------
// 注册表内存表
// ------------------------------------------------------------------

ROK_MOCK_REG_VALUE rok_mock_registry[64];
const int          rok_mock_registry_count = (int)(sizeof(rok_mock_registry) /
                                                   sizeof(rok_mock_registry[0]));

void rok_mock_registry_reset(void)
{
    memset(rok_mock_registry, 0, sizeof(rok_mock_registry));
}

static int path_matches(PCWSTR a, PCWSTR b)
{
    return wcscasecmp(a, b) == 0;
}

NTSTATUS ZwOpenKey(PHANDLE KeyHandle, ULONG DesiredAccess, POBJECT_ATTRIBUTES oa)
{
    int i;

    (void)DesiredAccess;
    if (KeyHandle == NULL || oa == NULL || oa->ObjectName == NULL ||
        oa->ObjectName->Buffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < rok_mock_registry_count; i++)
    {
        if (rok_mock_registry[i].Path != NULL &&
            path_matches(rok_mock_registry[i].Path, oa->ObjectName->Buffer))
        {
            *KeyHandle = (HANDLE)(INT_PTR)(i + 1);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS ZwQueryValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName,
                         KEY_INFORMATION_CLASS Class,
                         PVOID KeyValueInformation, ULONG Length,
                         PULONG ResultLength)
{
    int idx, i;
    PCWSTR keyPath;
    KEY_VALUE_PARTIAL_INFORMATION *info;

    (void)Class;
    idx = (int)(INT_PTR)KeyHandle - 1;
    if (idx < 0 || idx >= rok_mock_registry_count ||
        rok_mock_registry[idx].Path == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (ValueName == NULL || ValueName->Buffer == NULL ||
        KeyValueInformation == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // 同一“键路径”可对应表中多条记录（多个值名）；按路径分组全表扫描。
    keyPath = rok_mock_registry[idx].Path;
    for (i = 0; i < rok_mock_registry_count; i++)
    {
        if (rok_mock_registry[i].Path == NULL ||
            !path_matches(rok_mock_registry[i].Path, keyPath) ||
            !path_matches(rok_mock_registry[i].ValueName, ValueName->Buffer))
        {
            continue;
        }

        info = (KEY_VALUE_PARTIAL_INFORMATION *)KeyValueInformation;
        if (Length < FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) +
                         sizeof(ULONG))
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        info->TitleIndex = 0;
        info->Type = REG_DWORD;
        info->DataLength = sizeof(ULONG);
        memcpy(info->Data, &rok_mock_registry[i].Dword, sizeof(ULONG));
        if (ResultLength != NULL)
        {
            *ResultLength = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) +
                            sizeof(ULONG);
        }
        return STATUS_SUCCESS;
    }
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS ZwClose(HANDLE KeyHandle)
{
    INT_PTR idx = (INT_PTR)KeyHandle - 1;
    if (KeyHandle == NULL || idx < 0 || idx >= rok_mock_registry_count)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}
