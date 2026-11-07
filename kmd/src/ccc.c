// SPDX-License-Identifier: MIT
// ccc.c — CCC 设置兼容层实现（注册表导入 + escape get/set）
//
// 注册表导入的值名（驱动键 = HKLM\SYSTEM\CurrentControlSet\Control\Class\
// {4D36E968-E325-11CE-BFC1-08002BE10318}\00XX；详见 docs/ccc.md 映射表）：
//
//   rokmd 专有（推荐工具链写入）：
//     Overdrive                  DWORD 0/1            → OverdriveEnable
//     OverdriveEngineClockKHz    DWORD                → EngineClockKHz
//     OverdriveMemoryClockKHz    DWORD                → MemoryClockKHz
//     OverdriveFanSpeedPercent   DWORD                → FanSpeedPercent
//
//   Catalyst 遗留名（老 Catalyst 写在驱动键的同语义值，尽力识别）：
//     EnableOverdrive            DWORD 0/1            → OverdriveEnable
//     EngineClockOverride        DWORD kHz            → EngineClockKHz
//     MemoryClockOverride        DWORD kHz            → MemoryClockKHz
//
// 识别到遗留名且 rokmd 专有名不存在时才采用（专有名优先）。
#include "precomp.h"
#include "adapter.h"
#include "ccc.h"

// ------------------------------------------------------------------
// 注册表读取
// ------------------------------------------------------------------

// 读 <RootPath>\ValueName 的 DWORD。
static BOOLEAN
RokCccRegReadUlong(
    _In_ PUNICODE_STRING RootPath,
    _In_ PCWSTR ValueName,
    _Out_ PULONG OutValue
    )
{
    OBJECT_ATTRIBUTES oa;
    HANDLE key = NULL;
    UNICODE_STRING valueName;
    UCHAR infoBuf[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG) * 2];
    PKEY_VALUE_PARTIAL_INFORMATION info = (PKEY_VALUE_PARTIAL_INFORMATION)infoBuf;
    ULONG resultLen = 0;
    BOOLEAN ok = FALSE;

    ASSERT(RootPath != NULL && RootPath->Buffer != NULL);

    InitializeObjectAttributes(&oa,
                               RootPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    if (!NT_SUCCESS(ZwOpenKey(&key, KEY_QUERY_VALUE, &oa)))
    {
        return FALSE;
    }

    RtlInitUnicodeString(&valueName, ValueName);
    RtlZeroMemory(infoBuf, sizeof(infoBuf));

    if (NT_SUCCESS(ZwQueryValueKey(key,
                                   &valueName,
                                   KeyValuePartialInformation,
                                   info,
                                   sizeof(infoBuf),
                                   &resultLen)) &&
        info->Type == REG_DWORD &&
        info->DataLength == sizeof(ULONG))
    {
        *OutValue = *(ULONG *)info->Data;
        ok = TRUE;
    }

    ZwClose(key);
    return ok;
}

// 单个设置项导入：专有名优先，遗留名兜底。
static VOID
RokCccImportValue(
    _In_ PUNICODE_STRING RootPath,
    _In_ PCWSTR RokName,
    _In_ PCWSTR LegacyName,
    _Out_ PULONG Value,
    _Inout_ PULONG ValidMask,
    _In_ ULONG ValidBit
    )
{
    ULONG v;

    if (RokCccRegReadUlong(RootPath, RokName, &v) ||
        (LegacyName != NULL && RokCccRegReadUlong(RootPath, LegacyName, &v)))
    {
        *Value = v;
        *ValidMask |= ValidBit;
    }
}

// ------------------------------------------------------------------
// 导出接口
// ------------------------------------------------------------------

NTSTATUS
RokCccImport(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _In_opt_ PUNICODE_STRING DeviceRegistryPath
    )
{
    PROK_CCC_SETTINGS s;
    ULONG mask = 0;

    PAGED_CODE();

    if (DeviceRegistryPath == NULL || DeviceRegistryPath->Buffer == NULL)
    {
        return STATUS_SUCCESS;      // 无键可读：保持默认（非错误）
    }

    s = &Adapter->CccSettings;
    RtlZeroMemory(s, sizeof(*s));
    s->Version = ROK_CCC_VERSION;

    RokCccImportValue(DeviceRegistryPath, L"OverdriveEngineClockKHz",
                      L"EngineClockOverride", &s->EngineClockKHz, &mask,
                      ROK_CCC_VALID_ENGINE_CLOCK);
    RokCccImportValue(DeviceRegistryPath, L"OverdriveMemoryClockKHz",
                      L"MemoryClockOverride", &s->MemoryClockKHz, &mask,
                      ROK_CCC_VALID_MEMORY_CLOCK);
    RokCccImportValue(DeviceRegistryPath, L"Overdrive",
                      L"EnableOverdrive", &s->OverdriveEnable, &mask,
                      ROK_CCC_VALID_OD_ENABLE);
    RokCccImportValue(DeviceRegistryPath, L"OverdriveFanSpeedPercent",
                      NULL, &s->FanSpeedPercent, &mask,
                      ROK_CCC_VALID_FAN_SPEED);

    if (mask == 0)
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: ccc — no imported settings (defaults)\n"));
        return STATUS_SUCCESS;
    }

    s->ValidMask = mask;
    s->Source = ROK_CCC_SOURCE_REGISTRY;

    // 时钟合法范围钳制（导入路径与 escape 路径同一规则）。
    if (s->ValidMask & ROK_CCC_VALID_ENGINE_CLOCK)
    {
        s->EngineClockKHz = ROK_CCC_CLAMP(s->EngineClockKHz,
                                          ROK_CCC_ENGINE_CLOCK_MIN_KHZ,
                                          ROK_CCC_ENGINE_CLOCK_MAX_KHZ);
    }
    if (s->ValidMask & ROK_CCC_VALID_MEMORY_CLOCK)
    {
        s->MemoryClockKHz = ROK_CCC_CLAMP(s->MemoryClockKHz,
                                          ROK_CCC_MEMORY_CLOCK_MIN_KHZ,
                                          ROK_CCC_MEMORY_CLOCK_MAX_KHZ);
    }
    if (s->ValidMask & ROK_CCC_VALID_FAN_SPEED)
    {
        s->FanSpeedPercent = ROK_CCC_CLAMP(s->FanSpeedPercent,
                                           ROK_CCC_FAN_MIN_PERCENT,
                                           ROK_CCC_FAN_MAX_PERCENT);
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: ccc — imported (mask=%#x engine=%uKHz mem=%uKHz od=%u fan=%u)\n",
               s->ValidMask, s->EngineClockKHz, s->MemoryClockKHz,
               s->OverdriveEnable, s->FanSpeedPercent));
    return STATUS_SUCCESS;
}

NTSTATUS
RokCccGetSettings(
    _In_ struct _ROK_ADAPTER *Adapter,
    _Out_ PROK_CCC_SETTINGS Out
    )
{
    if (Adapter == NULL || Out == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // 整块拷贝（含 Version/Source），调用方按 ValidMask 解释。
    *Out = Adapter->CccSettings;
    if (Out->Version == 0)
    {
        // 尚未导入过的适配器：返回结构化默认值而非全 0。
        Out->Version = ROK_CCC_VERSION;
        Out->ValidMask = 0;
        Out->Source = ROK_CCC_SOURCE_NONE;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
RokCccSetSettings(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _In_ CONST PROK_CCC_SETTINGS In
    )
{
    PROK_CCC_SETTINGS s;

    if (Adapter == NULL || In == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (In->Version != ROK_CCC_VERSION)
    {
        return STATUS_REVISION_MISMATCH;
    }

    s = &Adapter->CccSettings;
    if (s->Version == 0)
    {
        RtlZeroMemory(s, sizeof(*s));
        s->Version = ROK_CCC_VERSION;
    }

    if (In->ValidMask & ROK_CCC_VALID_ENGINE_CLOCK)
    {
        s->EngineClockKHz = ROK_CCC_CLAMP(In->EngineClockKHz,
                                          ROK_CCC_ENGINE_CLOCK_MIN_KHZ,
                                          ROK_CCC_ENGINE_CLOCK_MAX_KHZ);
    }
    if (In->ValidMask & ROK_CCC_VALID_MEMORY_CLOCK)
    {
        s->MemoryClockKHz = ROK_CCC_CLAMP(In->MemoryClockKHz,
                                          ROK_CCC_MEMORY_CLOCK_MIN_KHZ,
                                          ROK_CCC_MEMORY_CLOCK_MAX_KHZ);
    }
    if (In->ValidMask & ROK_CCC_VALID_OD_ENABLE)
    {
        s->OverdriveEnable = (In->OverdriveEnable != 0) ? 1 : 0;
    }
    if (In->ValidMask & ROK_CCC_VALID_FAN_SPEED)
    {
        s->FanSpeedPercent = ROK_CCC_CLAMP(In->FanSpeedPercent,
                                           ROK_CCC_FAN_MIN_PERCENT,
                                           ROK_CCC_FAN_MAX_PERCENT);
    }
    s->ValidMask |= In->ValidMask;
    s->Source = ROK_CCC_SOURCE_ESCAPE;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "rokmd: ccc — set (mask=%#x engine=%uKHz mem=%uKHz od=%u fan=%u)\n",
               s->ValidMask, s->EngineClockKHz, s->MemoryClockKHz,
               s->OverdriveEnable, s->FanSpeedPercent));

    // M3：在此调用时钟编程序列（像素/显存 PLL）；当前设置仅存储并回读。
    return STATUS_SUCCESS;
}
