// SPDX-License-Identifier: MIT
// vdxgkrnl.c — E2E harness 的"虚拟 dxgkrnl"实现。
//
// 扮演 Windows 图形栈对 KMD 的一切交付面：
//   * 内核原语：UNICODE_STRING/链表/自旋锁/原子/池/注册表/CPUID；
//   * DxgkInitialize：捕获 DriverEntry 注册的 DDI 表（harness 随后
//     像真 OS 一样通过该表调用 DDI —— 而非直接链接驱动函数）；
//   * DxgkCb* 回调：虚拟 PCI 配置空间（可编程 VID/DID/REV）、虚拟
//     注册表、翻译后资源清单、BAR0 内存映射、句柄表翻译；
//   * rok_kdprintf：支持 %wZ/%I64x 等 MSVC 扩展的迷你格式化器
//     （驱动日志原样透出 —— E2E 输出的主体）。
//
// 单线程 harness：自旋锁为空操作，注册表以"最近一次 ZwOpenKey 的
// 路径"作为查询键（驱动每次 open→query→close 顺序执行）。
#include "../kmock2/ntddk.h"
#include "../kmock2/dispmprt.h"
#include "../kmock2/d3dkmddi.h"

#include <stdlib.h>
#include <ctype.h>
#include <wctype.h>

// ---- vdxgkrnl 对 harness 的接口（run_simdevice.c 使用）----
void    RokVdxgReset(VOID);
void    RokVdxgConfigurePci(ULONG VendorId, ULONG DeviceId, ULONG RevisionId,
                           BOOLEAN HasBar0, UINT64 Bar0Base, ULONG Bar0Size);
void    RokVdxgSetDriverKeyPath(PCWSTR Path);
void    RokVdxgFillInterface(DXGKRNL_INTERFACE *ifc);   // 填 DxgkCb* 回调表
PDRIVER_INITIALIZATION_DATA RokVdxgCapturedDdiTable(VOID);

// 前向声明（ZwOpenKey/资源清单依赖）
static CM_RESOURCE_LIST *VdxgkBuildResourceList(void);

// ==================================================================
// 迷你格式化器（%wZ / %I64x / %#x / %ls / %lu / %p / %u / %d / %X）
// Windows 的 long = 32 位 → 单 'l' 长度在宿主剥掉；I64 → ll。
// ==================================================================

static void RokKPutPiece(char **out, size_t *left, const char *spec,
                         va_list *ap)
{
    char piece[1200];
    char conv = spec[strlen(spec) - 1];

    switch (conv) {
    case 'd': case 'i': {
        int v = va_arg(*ap, int);
        snprintf(piece, sizeof(piece), spec, v);
        break;
    }
    case 'u': case 'x': case 'X': case 'o': {
        unsigned int v = va_arg(*ap, unsigned int);
        snprintf(piece, sizeof(piece), spec, v);
        break;
    }
    case 'c': {
        int v = va_arg(*ap, int);
        snprintf(piece, sizeof(piece), spec, v);
        break;
    }
    case 's': {
        char *v = va_arg(*ap, char *);
        snprintf(piece, sizeof(piece), spec, v ? v : "(null-str)");
        break;
    }
    case 'S': {                     // %ls / %ws → 宽字符串
        wchar_t *v = va_arg(*ap, wchar_t *);
        snprintf(piece, sizeof(piece), spec, v ? v : L"(null-wstr)");
        break;
    }
    case 'p': {
        void *v = va_arg(*ap, void *);
        snprintf(piece, sizeof(piece), spec, v);
        break;
    }
    case 'Z': {                     // %wZ → UNICODE_STRING*
        UNICODE_STRING *us = va_arg(*ap, UNICODE_STRING *);
        char tmp[1024];
        if (us != NULL && us->Buffer != NULL && us->Length > 0) {
            snprintf(tmp, sizeof(tmp), "%.*ls",
                     (int)(us->Length / sizeof(WCHAR)), us->Buffer);
        } else {
            snprintf(tmp, sizeof(tmp), "(null-wz)");
        }
        snprintf(piece, sizeof(piece), "%s", tmp);
        break;
    }
    case '%':
        snprintf(piece, sizeof(piece), "%%");
        break;
    default:
        snprintf(piece, sizeof(piece), "?badconv:%c?", conv);
        break;
    }

    {
        size_t n = strlen(piece);
        if (n > *left - 1) { n = (*left > 1) ? *left - 1 : 0; }
        memcpy(*out, piece, n);
        *out += n;
        *left -= n;
    }
}

void rok_kdprintf(ULONG Component, ULONG Level, PCSTR Fmt, ...)
{
    char out[4096];
    char *o = out;
    size_t left = sizeof(out);
    va_list ap;
    const char *p = Fmt;

    (void)Component; (void)Level;

    va_start(ap, Fmt);

    while (*p != '\0' && left > 1) {
        if (*p != '%') {
            *o++ = *p++;
            left--;
            continue;
        }

        {
            char spec[64];
            size_t si = 0;

            spec[si++] = '%';
            p++;
            while (strchr("#0- +", *p) != NULL && si < sizeof(spec) - 8) {
                spec[si++] = *p++;
            }
            while (*p >= '0' && *p <= '9' && si < sizeof(spec) - 8) {
                spec[si++] = *p++;
            }
            if (*p == '.') {
                spec[si++] = *p++;
                while (*p >= '0' && *p <= '9' && si < sizeof(spec) - 8) {
                    spec[si++] = *p++;
                }
            }
            // 长度修饰符 → 宿主规范化
            if (p[0] == 'I' && p[1] == '6' && p[2] == '4') {
                spec[si++] = 'l'; spec[si++] = 'l';
                p += 3;
            } else if (p[0] == 'l' && p[1] == 'l') {
                spec[si++] = 'l'; spec[si++] = 'l';
                p += 2;
            } else if (p[0] == 'l') {
                p += 1;
                if (*p == 's') { spec[si++] = 'S'; p++; goto emit; }   // %ls
                if (*p == 'Z') { spec[si++] = 'Z'; p++; goto emit; }   // %lZ
                // 其余（%lu/%ld…）：32 位 long → 剥掉长度
            } else if (p[0] == 'w') {
                p += 1;
                if (*p == 's') { spec[si++] = 'S'; p++; goto emit; }   // %ws
                if (*p == 'Z') { spec[si++] = 'Z'; p++; goto emit; }   // %wZ
            } else if (p[0] == 'h') {
                spec[si++] = 'h';
                p += 1;
            }
            if (*p == '\0') {
                break;              // 尾截断防御
            }
            spec[si++] = *p;        // 转换字符
            p++;
        emit:
            spec[si] = '\0';
            RokKPutPiece(&o, &left, spec, &ap);
        }
    }

    va_end(ap);

    *o = '\0';
    fputs(out, stdout);
    fflush(stdout);
}

// ==================================================================
// UNICODE_STRING / 链表 / 自旋锁 / 原子
// ==================================================================

void RtlInitUnicodeString(PUNICODE_STRING s, PCWSTR buf)
{
    size_t chars = 0;
    if (s == NULL) { return; }
    if (buf != NULL) {
        while (buf[chars] != L'\0') { chars++; }
    }
    s->Length = (USHORT)(chars * (USHORT)sizeof(WCHAR));
    s->MaximumLength = (USHORT)(s->Length + sizeof(WCHAR));
    s->Buffer = (PWSTR)buf;
}

void RtlCopyUnicodeString(PUNICODE_STRING dst, PCUNICODE_STRING src)
{
    USHORT n;
    if (dst == NULL) { return; }
    if (src == NULL || src->Buffer == NULL) {
        dst->Length = 0;
        return;
    }
    n = src->Length;
    if (n > dst->MaximumLength - (USHORT)sizeof(WCHAR)) {
        n = (USHORT)(dst->MaximumLength - sizeof(WCHAR));
    }
    memcpy(dst->Buffer, src->Buffer, n);
    dst->Length = n;
}

BOOLEAN RtlAppendUnicodeToString(PUNICODE_STRING dst, PCWSTR suffix)
{
    size_t addChars = 0;
    size_t addBytes;
    if (suffix != NULL) {
        while (suffix[addChars] != L'\0') { addChars++; }
    }
    addBytes = addChars * sizeof(WCHAR);
    if ((size_t)dst->Length + addBytes > (size_t)dst->MaximumLength - sizeof(WCHAR)) {
        return FALSE;
    }
    memcpy((UCHAR *)dst->Buffer + dst->Length, suffix, addBytes);
    dst->Length = (USHORT)(dst->Length + addBytes);
    return TRUE;
}

BOOLEAN RtlAppendUnicodeStringToString(PUNICODE_STRING dst, PCUNICODE_STRING src)
{
    USHORT n;
    if (src == NULL || src->Buffer == NULL) { return TRUE; }
    n = src->Length;
    if ((size_t)dst->Length + n > (size_t)dst->MaximumLength - sizeof(WCHAR)) {
        return FALSE;
    }
    memcpy((UCHAR *)dst->Buffer + dst->Length, src->Buffer, n);
    dst->Length = (USHORT)(dst->Length + n);
    return TRUE;
}

void InitializeListHead(PLIST_ENTRY head)
{
    head->Flink = head->Blink = head;
}

void InsertTailList(PLIST_ENTRY head, PLIST_ENTRY entry)
{
    entry->Flink = head;
    entry->Blink = head->Blink;
    head->Blink->Flink = entry;
    head->Blink = entry;
}

void InsertHeadList(PLIST_ENTRY head, PLIST_ENTRY entry)
{
    entry->Flink = head->Flink;
    entry->Blink = head;
    head->Flink->Blink = entry;
    head->Flink = entry;
}

int IsListEmpty(CONST PLIST_ENTRY head)
{
    return head->Flink == head;
}

int RemoveEntryList(PLIST_ENTRY entry)
{
    entry->Blink->Flink = entry->Flink;
    entry->Flink->Blink = entry->Blink;
    return (int)(entry->Flink == entry->Blink);
}

void KeInitializeSpinLock(PKSPIN_LOCK lock) { *lock = 0; }

void KeAcquireSpinLock(PKSPIN_LOCK lock, KIRQL *oldIrql)
{
    *lock = 1;
    *oldIrql = DISPATCH_LEVEL;
}

void KeReleaseSpinLock(PKSPIN_LOCK lock, KIRQL oldIrql)
{
    (void)oldIrql;
    *lock = 0;
}

// ==================================================================
// 池分配（malloc 底座 + tag 记账；ASan 退出时核对无泄漏）
// ==================================================================

typedef struct _ROK_POOL_HDR {
    ULONG   Tag;
    SIZE_T  Size;
} ROK_POOL_HDR;

static ULONG g_PoolOutstanding;

PVOID ExAllocatePoolWithTag(POOL_TYPE PoolType, SIZE_T Size, ULONG Tag)
{
    ROK_POOL_HDR *hdr;
    (void)PoolType;
    hdr = (ROK_POOL_HDR *)malloc(sizeof(ROK_POOL_HDR) + Size);
    if (hdr == NULL) { return NULL; }
    hdr->Tag = Tag;
    hdr->Size = Size;
    g_PoolOutstanding++;
    return (PVOID)(hdr + 1);
}

VOID ExFreePoolWithTag(PVOID Ptr, ULONG Tag)
{
    ROK_POOL_HDR *hdr;
    if (Ptr == NULL) { return; }
    hdr = (ROK_POOL_HDR *)Ptr - 1;
    assert(hdr->Tag == Tag);    // tag 漂移 = 驱动 bug
    assert(g_PoolOutstanding > 0);
    g_PoolOutstanding--;
    free(hdr);
}

ULONG RokMockPoolOutstanding(VOID) { return g_PoolOutstanding; }

// ==================================================================
// CPUID（钩子可注入；默认真实执行）
// ==================================================================

static void RokMockCpuidReal(INT regs[4], INT leaf)
{
#if defined(__x86_64__) || defined(__i386__)
    __asm__ __volatile__(
        "cpuid"
        : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
        : "a"(leaf), "c"(0));
#else
    (void)leaf;
    regs[0] = regs[1] = regs[2] = regs[3] = 0;
#endif
}

void (*rok_mock_cpuid)(INT regs[4], INT leaf) = RokMockCpuidReal;

void __cpuid(INT regs[4], INT leaf)
{
    rok_mock_cpuid(regs, leaf);
}

// ==================================================================
// 注册表（内存表；键路径 = 最近一次 ZwOpenKey 的 ObjectName）
// ==================================================================

ROK_MOCK_REG_VALUE rok_mock_registry[64];
const int rok_mock_registry_count =
    (int)(sizeof(rok_mock_registry) / sizeof(rok_mock_registry[0]));

static int RokMockWcicmpN(PCWSTR a, size_t aChars, PCWSTR b)
{
    size_t i = 0;
    for (i = 0; i < aChars; i++) {
        int ca = towlower((wint_t)a[i]);
        int cb = towlower((wint_t)b[i]);
        if (cb == L'\0' || ca != cb) { return ca - cb; }
    }
    return (b[aChars] == L'\0') ? 0 : (int)b[aChars];
}

static int RokMockWcicmp(PCWSTR a, PCWSTR b)
{
    size_t aChars = 0;
    while (a[aChars] != L'\0') { aChars++; }
    return RokMockWcicmpN(a, aChars, b);
}

static ROK_MOCK_REG_VALUE *RokMockRegFind(PCWSTR path, size_t pathChars,
                                          PCWSTR name)
{
    int i;
    for (i = 0; i < rok_mock_registry_count; i++) {
        if (rok_mock_registry[i].Path == NULL) { break; }
        if (RokMockWcicmpN(rok_mock_registry[i].Path, pathChars, path) == 0 &&
            RokMockWcicmp(rok_mock_registry[i].ValueName, name) == 0) {
            return &rok_mock_registry[i];
        }
    }
    return NULL;
}

void rok_mock_registry_reset(void)
{
    memset(rok_mock_registry, 0, sizeof(rok_mock_registry));
}

// 最近一次打开的键路径（单线程 harness 的"当前键"语义）。
static WCHAR g_LastOpenedPath[560];
static size_t g_LastOpenedChars;

NTSTATUS ZwOpenKey(PHANDLE KeyHandle, ULONG DesiredAccess, POBJECT_ATTRIBUTES oa)
{
    (void)DesiredAccess;
    if (oa == NULL || oa->ObjectName == NULL ||
        oa->ObjectName->Buffer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    g_LastOpenedChars = oa->ObjectName->Length / sizeof(WCHAR);
    if (g_LastOpenedChars >= sizeof(g_LastOpenedPath) / sizeof(WCHAR)) {
        g_LastOpenedChars = sizeof(g_LastOpenedPath) / sizeof(WCHAR) - 1;
    }
    memcpy(g_LastOpenedPath, oa->ObjectName->Buffer,
           g_LastOpenedChars * sizeof(WCHAR));
    g_LastOpenedPath[g_LastOpenedChars] = L'\0';
    *KeyHandle = (HANDLE)(UINT_PTR)0x5A5A0001;  // 哨兵句柄
    return STATUS_SUCCESS;
}

NTSTATUS ZwQueryValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName,
                         KEY_INFORMATION_CLASS Class,
                         PVOID KeyValueInformation, ULONG Length,
                         PULONG ResultLength)
{
    PKEY_VALUE_PARTIAL_INFORMATION info =
        (PKEY_VALUE_PARTIAL_INFORMATION)KeyValueInformation;
    ROK_MOCK_REG_VALUE *hit;

    (void)KeyHandle; (void)ResultLength;
    if (Class != KeyValuePartialInformation ||
        Length < FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    hit = RokMockRegFind(g_LastOpenedPath, g_LastOpenedChars,
                         ValueName->Buffer);
    if (hit == NULL) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    info->TitleIndex = 0;
    info->Type = REG_DWORD;
    info->DataLength = sizeof(ULONG);
    memcpy(info->Data, &hit->Dword, sizeof(ULONG));
    return STATUS_SUCCESS;
}

NTSTATUS ZwClose(HANDLE KeyHandle)
{
    (void)KeyHandle;
    g_LastOpenedChars = 0;
    g_LastOpenedPath[0] = L'\0';
    return STATUS_SUCCESS;
}

// ==================================================================
// 虚拟 PCI 设备 + 资源清单 + 驱动键路径
// ==================================================================

static CM_RESOURCE_LIST *VdxgkBuildResourceList(void);

static UCHAR    g_VpciConfig[256];
static BOOLEAN  g_VpciHasBar0;
static UINT64   g_VpciBar0Base;
static ULONG    g_VpciBar0Size;
static PVOID    g_VpciBar0Mapping;
static WCHAR    g_DriverKeyPath[256];
static UNICODE_STRING g_DriverKeyPathUS;
static PCM_RESOURCE_LIST g_ResourceList;

void RokVdxgConfigurePci(ULONG VendorId, ULONG DeviceId, ULONG RevisionId,
                         BOOLEAN HasBar0, UINT64 Bar0Base, ULONG Bar0Size)
{
    memset(g_VpciConfig, 0, sizeof(g_VpciConfig));
    g_VpciConfig[0] = (UCHAR)(VendorId & 0xFF);
    g_VpciConfig[1] = (UCHAR)(VendorId >> 8);
    g_VpciConfig[2] = (UCHAR)(DeviceId & 0xFF);
    g_VpciConfig[3] = (UCHAR)(DeviceId >> 8);
    g_VpciConfig[8] = (UCHAR)RevisionId;
    g_VpciHasBar0 = HasBar0;
    g_VpciBar0Base = Bar0Base;
    g_VpciBar0Size = Bar0Size;
    g_VpciBar0Mapping = NULL;
}

void RokVdxgSetDriverKeyPath(PCWSTR Path)
{
    size_t i = 0;
    while (Path[i] != L'\0' && i + 1 < 256) {
        g_DriverKeyPath[i] = Path[i];
        i++;
    }
    g_DriverKeyPath[i] = L'\0';
    RtlInitUnicodeString(&g_DriverKeyPathUS, g_DriverKeyPath);
}

// ==================================================================
// DxgkCb* 实现（driver.c/adapter.c 真实调用这些）
// ==================================================================

static NTSTATUS VdxgkCbReadDeviceSpace(HANDLE DeviceHandle,
                                       DXGK_WHICHSPACE WhichSpace,
                                       PVOID Buffer, ULONG Offset,
                                       ULONG Length, PULONG BytesRead)
{
    (void)DeviceHandle;
    if (WhichSpace != DXGK_WHICHSPACE_CONFIG) {
        return STATUS_NOT_SUPPORTED;
    }
    if (Offset >= sizeof(g_VpciConfig)) {
        *BytesRead = 0;
        return STATUS_INVALID_PARAMETER;
    }
    if (Offset + Length > sizeof(g_VpciConfig)) {
        Length = sizeof(g_VpciConfig) - Offset;
    }
    memcpy(Buffer, g_VpciConfig + Offset, Length);
    *BytesRead = Length;
    return STATUS_SUCCESS;
}

// 翻译后资源清单：BAR0（可配）+ BAR2 256MB（应被驱动跳过）+ IRQ 16。
static CM_RESOURCE_LIST *VdxgkBuildResourceList(void)
{
    CM_RESOURCE_LIST *list;
    CM_FULL_RESOURCE_DESCRIPTOR *full;
    CM_PARTIAL_RESOURCE_DESCRIPTOR desc[3];
    // 1 个 full descriptor 自带 1 个 partial，再补 2 个。
    size_t total = sizeof(CM_RESOURCE_LIST) + 2 * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

    memset(desc, 0, sizeof(desc));
    desc[0].Type = CmResourceTypeMemory;
    desc[0].u.Memory.Start.QuadPart = (LONGLONG)g_VpciBar0Base;
    desc[0].u.Memory.Length = g_VpciBar0Size;

    desc[1].Type = CmResourceTypeMemory;
    desc[1].u.Memory.Start.QuadPart = (LONGLONG)0xD0000000ULL;
    desc[1].u.Memory.Length = 0x10000000;

    desc[2].Type = CmResourceTypeInterrupt;
    desc[2].u.Interrupt.Level = 16;
    desc[2].u.Interrupt.Vector = 0x81;

    list = (CM_RESOURCE_LIST *)calloc(1, total);
    if (list == NULL) { return NULL; }
    list->Count = 1;
    full = &list->List[0];
    full->InterfaceType = 5;        // PCIBus
    full->BusNumber = 0;
    full->PartialResourceList.Version = 1;
    full->PartialResourceList.Revision = 1;
    full->PartialResourceList.Count = 3;
    memcpy(full->PartialResourceList.PartialDescriptors, desc, sizeof(desc));

    if (g_ResourceList != NULL) { free(g_ResourceList); }
    g_ResourceList = list;
    return list;
}

static NTSTATUS VdxgkCbGetDeviceInformation(HANDLE DeviceHandle,
                                            DXGK_DEVICE_INFO *DeviceInfo)
{
    (void)DeviceHandle;
    if (DeviceInfo == NULL) { return STATUS_INVALID_PARAMETER; }
    DeviceInfo->PhysicalDeviceObject = (PDEVICE_OBJECT)(UINT_PTR)0x1000;
    DeviceInfo->DeviceRegistryPath = g_DriverKeyPathUS;
    DeviceInfo->TranslatedResourceList = VdxgkBuildResourceList();
    return STATUS_SUCCESS;
}

static NTSTATUS VdxgkCbMapMemory(HANDLE DeviceHandle,
                                 PHYSICAL_ADDRESS TranslatedAddress,
                                 ULONG NumberOfBytes, BOOLEAN InIoSpace,
                                 BOOLEAN MapToUserMode,
                                 MEMORY_CACHING_TYPE CacheType,
                                 PVOID *VirtualAddress)
{
    (void)DeviceHandle; (void)InIoSpace; (void)MapToUserMode; (void)CacheType;
    if (g_VpciHasBar0 &&
        (UINT64)TranslatedAddress.QuadPart == g_VpciBar0Base &&
        NumberOfBytes == g_VpciBar0Size) {
        if (g_VpciBar0Mapping == NULL) {
            g_VpciBar0Mapping = calloc(1, NumberOfBytes);
        }
        *VirtualAddress = g_VpciBar0Mapping;
        return STATUS_SUCCESS;
    }
    *VirtualAddress = NULL;
    return STATUS_INVALID_PARAMETER;    // 非法地址 = 模拟配置错误
}

static VOID VdxgkCbUnmapMemory(HANDLE DeviceHandle, PVOID VirtualAddress)
{
    (void)DeviceHandle;
    if (VirtualAddress != NULL && VirtualAddress == g_VpciBar0Mapping) {
        free(g_VpciBar0Mapping);
        g_VpciBar0Mapping = NULL;
    }
}

// 回调实现前向声明（RokVdxgFillInterface 引用）。
static NTSTATUS VdxgkCbReadDeviceSpace(HANDLE DeviceHandle,
                                       DXGK_WHICHSPACE WhichSpace,
                                       PVOID Buffer, ULONG Offset,
                                       ULONG Length, PULONG BytesRead);
static NTSTATUS VdxgkCbGetDeviceInformation(HANDLE DeviceHandle,
                                            DXGK_DEVICE_INFO *DeviceInfo);
static NTSTATUS VdxgkCbMapMemory(HANDLE DeviceHandle,
                                 PHYSICAL_ADDRESS TranslatedAddress,
                                 ULONG NumberOfBytes, BOOLEAN InIoSpace,
                                 BOOLEAN MapToUserMode,
                                 MEMORY_CACHING_TYPE CacheType,
                                 PVOID *VirtualAddress);
static VOID VdxgkCbUnmapMemory(HANDLE DeviceHandle, PVOID VirtualAddress);
static PVOID VdxgkCbGetHandleData(DXGKARGCB_GETHANDLEDATA *query);

// 填回调表（harness 在 StartDevice 前调用，模拟 dxgkrnl 交付）。
void RokVdxgFillInterface(DXGKRNL_INTERFACE *ifc)
{
    ifc->DxgkCbReadDeviceSpace     = VdxgkCbReadDeviceSpace;
    ifc->DxgkCbGetDeviceInformation = VdxgkCbGetDeviceInformation;
    ifc->DxgkCbMapMemory           = VdxgkCbMapMemory;
    ifc->DxgkCbUnmapMemory         = VdxgkCbUnmapMemory;
    ifc->DxgkCbGetHandleData       = VdxgkCbGetHandleData;
}

// 句柄翻译：CreateAllocation 存的驱动句柄原样取回（模拟 OS 对象表）。
static PVOID VdxgkCbGetHandleData(DXGKARGCB_GETHANDLEDATA *query)
{
    if (query == NULL || query->hObject == NULL) {
        return NULL;
    }
    return query->hObject;
}

// ==================================================================
// DxgkInitialize：捕获 DDI 表
// ==================================================================

static DRIVER_INITIALIZATION_DATA g_CapturedDdi;
static BOOLEAN g_DdiCaptured;

NTSTATUS DxgkInitialize(PDRIVER_OBJECT DriverObject,
                        PUNICODE_STRING RegistryPath,
                        PDRIVER_INITIALIZATION_DATA DdiData)
{
    (void)DriverObject; (void)RegistryPath;
    if (DdiData == NULL || DdiData->Version != DXGKDDI_INTERFACE_VERSION) {
        return STATUS_INVALID_PARAMETER;
    }
    g_CapturedDdi = *DdiData;
    g_DdiCaptured = TRUE;
    return STATUS_SUCCESS;
}

PDRIVER_INITIALIZATION_DATA RokVdxgCapturedDdiTable(VOID)
{
    return g_DdiCaptured ? &g_CapturedDdi : NULL;
}

// harness 复位（两个 adapter pass 之间调用）。
void RokVdxgReset(VOID)
{
    rok_mock_registry_reset();
    if (g_ResourceList != NULL) {
        free(g_ResourceList);
        g_ResourceList = NULL;
    }
    if (g_VpciBar0Mapping != NULL) {
        free(g_VpciBar0Mapping);
        g_VpciBar0Mapping = NULL;
    }
    memset(&g_CapturedDdi, 0, sizeof(g_CapturedDdi));
    g_DdiCaptured = FALSE;
    g_LastOpenedChars = 0;
    g_LastOpenedPath[0] = L'\0';
}
