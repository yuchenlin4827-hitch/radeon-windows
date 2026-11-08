// SPDX-License-Identifier: MIT
// sim.c — rokmd 模拟设备后端实现（M2.6）
//
// 职责（对照 sim.h 头注释）：
//   * hypervisor 探测（CPUID leaf 1 / 0x40000000）；
//   * 模拟设备 ID 表匹配（INF SIM 模型行 + 注册表强制开关 + 自动判定）；
//   * 64KB 模拟寄存器空间（NonPagedPool，M3 寄存器代码的前哨）；
//   * 提交路径 PM4 解码（type-0/2/3 遍历 + opcode 直方图，DISPATCH 安全）。
//
// 参考：Linux 内核 drivers/gpu/drm/radeon/r600d.h 的 PM4 包定义（GPL-2.0，
// 仅对照 opcode 数值；本文件全部为自写实现，MIT）。
#include "precomp.h"
#include "adapter.h"
#include "sim.h"

#include <intrin.h>

// 模拟寄存器空间大小（TeraScale BAR0 实际为 64KB~16MB，模拟固定 64KB）。
#define ROK_SIM_REGSPACE_SIZE   0x10000

// ------------------------------------------------------------------
// hypervisor 探测
// ------------------------------------------------------------------

// 常见 hypervisor 的 CPUID leaf 0x40000000 厂商串（12 字节 ASCII）。
static const CHAR RokSimHypervisorSigs[][13] =
{
    "KVMKVMKVM\0\0\0",      // KVM / QEMU(KVM)
    "Microsoft Hv",         // Hyper-V
    "VMwareVMware",         // VMware
    "VBoxVBoxVBox",         // VirtualBox
    "XenVMMXenVMM",         // Xen / QEMU(TCG 经 HVM)
    "TCGTCGTCGTCG",         // QEMU TCG（无 KVM）
    "prl hyperv",           // Parallels
};

BOOLEAN
RokSimHypervisorPresent(
    VOID
    )
{
    INT regs[4];
    CHAR vendor[13];
    ULONG i, j;

    // leaf 1: ECX bit 31 = hypervisor present。
    __cpuid(regs, 1);
    if ((regs[2] & (1 << 31)) == 0)
    {
        return FALSE;
    }

    // leaf 0x40000000: EBX/ECX/EDX = 12 字节厂商串。
    __cpuid(regs, 0x40000000);
    RtlCopyMemory(vendor + 0, &regs[1], 4);
    RtlCopyMemory(vendor + 4, &regs[2], 4);
    RtlCopyMemory(vendor + 8, &regs[3], 4);
    vendor[12] = '\0';

    for (i = 0; i < RTL_NUMBER_OF(RokSimHypervisorSigs); i++)
    {
        for (j = 0; j < 12; j++)
        {
            if (vendor[j] != RokSimHypervisorSigs[i][j])
            {
                break;
            }
        }
        if (j == 12)
        {
            return TRUE;
        }
    }

    // bit31 置位但厂商串未列出：仍视为存在 hypervisor（保守开启探测日志）。
    return TRUE;
}

// ------------------------------------------------------------------
// 注册表开关
// ------------------------------------------------------------------

// 读 <RootPath>[\<SubKey>]\ValueName 的 DWORD 值。失败返回 FALSE。
static BOOLEAN
RokSimRegReadUlong(
    _In_ PUNICODE_STRING RootPath,
    _In_opt_ PCWSTR SubKey,
    _In_ PCWSTR ValueName,
    _Out_ PULONG OutValue
    )
{
    WCHAR pathBuf[512];
    UNICODE_STRING path;
    UNICODE_STRING subKey;
    UNICODE_STRING valueName;
    OBJECT_ATTRIBUTES oa;
    HANDLE key = NULL;
    UCHAR infoBuf[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG) * 2];
    PKEY_VALUE_PARTIAL_INFORMATION info = (PKEY_VALUE_PARTIAL_INFORMATION)infoBuf;
    ULONG resultLen = 0;
    BOOLEAN ok = FALSE;

    ASSERT(RootPath != NULL && RootPath->Buffer != NULL);
    // 路径缓冲 512 wchar 足够（驱动键路径 + "\Parameters"）；
    // 超长时 RtlAppend* 返回非成功，函数安全返回 FALSE。

    path.Length = 0;
    path.MaximumLength = sizeof(pathBuf);
    path.Buffer = pathBuf;
    RtlCopyUnicodeString(&path, RootPath);

    if (SubKey != NULL)
    {
        RtlInitUnicodeString(&subKey, SubKey);
        if (!NT_SUCCESS(RtlAppendUnicodeToString(&path, L"\\")) ||
            !NT_SUCCESS(RtlAppendUnicodeStringToString(&path, &subKey)))
        {
            return FALSE;
        }
    }

    InitializeObjectAttributes(&oa,
                               &path,
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

// ------------------------------------------------------------------
// 初始化 / 关闭
// ------------------------------------------------------------------

BOOLEAN
RokSimIsSimulatedDeviceId(
    _In_ ULONG DeviceId
    )
{
    switch (DeviceId)
    {
    case ROK_SIM_QEMU_ATI_VGA:
    case ROK_SIM_QEMU_BOCHS_VGA:
    case ROK_SIM_VBOX_VGA:
    case ROK_SIM_VMWARE_SVGA:
    case ROK_SIM_HYPERV_VIDEO:
        return TRUE;
    default:
        return FALSE;
    }
}

BOOLEAN
RokSimEnabled(
    _In_ struct _ROK_ADAPTER *Adapter
    )
{
    return (Adapter != NULL && Adapter->SimulationMode) ? TRUE : FALSE;
}

NTSTATUS
RokSimInitialize(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _In_opt_ PUNICODE_STRING DeviceRegistryPath
    )
{
    ULONG forced = 0;
    BOOLEAN forceByRegistry = FALSE;
    BOOLEAN forceByDeviceId;
    BOOLEAN autoByHypervisor;

    PAGED_CODE();

    forceByDeviceId = RokSimIsSimulatedDeviceId(Adapter->PciDeviceId);
    autoByHypervisor = FALSE;

    // 注册表强制开关：<DriverKey>\Parameters\SimulatedDevice，其次 <DriverKey>\SimulatedDevice。
    if (DeviceRegistryPath != NULL && DeviceRegistryPath->Buffer != NULL)
    {
        if (RokSimRegReadUlong(DeviceRegistryPath, L"Parameters", L"SimulatedDevice", &forced) ||
            RokSimRegReadUlong(DeviceRegistryPath, NULL, L"SimulatedDevice", &forced))
        {
            forceByRegistry = (forced != 0);
        }
    }

    // 自动判定：hypervisor 存在且不是 AMD 设备（VM 的 VGA 都不是 0x1002，
    // 除 ati-vga 外；ati-vga 已由 forceByDeviceId 覆盖）。
    Adapter->HypervisorPresent = RokSimHypervisorPresent();
    if (Adapter->HypervisorPresent && Adapter->PciVendorId != 0x1002)
    {
        autoByHypervisor = TRUE;
    }

    if (!forceByRegistry && !forceByDeviceId && !autoByHypervisor)
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: sim — real/unknown hardware (VID=0x%04X DID=0x%04X hv=%u), simulation off\n",
                   Adapter->PciVendorId, Adapter->PciDeviceId,
                   Adapter->HypervisorPresent));
        return STATUS_SUCCESS;      // 非错误：保持真实设备路径
    }

    Adapter->SimulationMode = TRUE;
    RtlZeroMemory(&Adapter->SimStats, sizeof(Adapter->SimStats));

    // 模拟寄存器空间：64KB NonPagedPool，清零（读出全 0 = 寄存器未编程，
    // 与真实硬件上电语义一致；写操作保留在缓冲内供调试器检查）。
    Adapter->SimRegisterSpaceSize = ROK_SIM_REGSPACE_SIZE;
    Adapter->SimRegisterSpace = ExAllocatePoolWithTag(
        NonPagedPool, Adapter->SimRegisterSpaceSize, ROK_POOL_TAG);
    if (Adapter->SimRegisterSpace == NULL)
    {
        Adapter->SimulationMode = FALSE;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Adapter->SimRegisterSpace, Adapter->SimRegisterSpaceSize);

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
               "rokmd: SIM MODE — VID=0x%04X DID=0x%04X hv=%u (reg=%u id=%u)\n",
               Adapter->PciVendorId, Adapter->PciDeviceId,
               Adapter->HypervisorPresent,
               forceByRegistry ? 1 : 0, forceByDeviceId ? 1 : 0));
    return STATUS_SUCCESS;
}

VOID
RokSimShutdown(
    _Inout_ struct _ROK_ADAPTER *Adapter
    )
{
    if (Adapter->SimRegisterSpace != NULL)
    {
        ExFreePoolWithTag(Adapter->SimRegisterSpace, ROK_POOL_TAG);
        Adapter->SimRegisterSpace = NULL;
    }
    Adapter->SimRegisterSpaceSize = 0;
    Adapter->SimulationMode = FALSE;
}

// ------------------------------------------------------------------
// PM4 解码（提交路径，DISPATCH_LEVEL）
// ------------------------------------------------------------------

// TeraScale PM4 type-3 opcode 名称（对照 Linux r600d.h，M3 校对全集）。
// 未列入的 opcode 计入 UnknownOpcodes，不假名乱报。
static PCWSTR
RokSimOpcodeName(
    _In_ ULONG Opcode
    )
{
    switch (Opcode)
    {
    case 0x10: return L"NOP";
    case 0x11: return L"INDIRECT_BUFFER_OFFSET";
    case 0x17: return L"INDIRECT_BUFFER_END";
    case 0x19: return L"INDIRECT_BUFFER_CPLT";
    case 0x26: return L"WAIT_REG_MEM";
    case 0x27: return L"MEM_SEMAPHORE";
    case 0x32: return L"INDIRECT_BUFFER";
    case 0x33: return L"INDIRECT_BUFFER_CONST";
    case 0x43: return L"SURFACE_SYNC";
    case 0x45: return L"COND_WRITE";
    case 0x46: return L"EVENT_WRITE";
    case 0x47: return L"EVENT_WRITE_EOP";
    case 0x68: return L"SET_RESOURCE";
    case 0x69: return L"SET_CONTEXT_REG";
    case 0x36: return L"DRAW_INDEX_AUTO";
    default:   return NULL;
    }
}

VOID
RokSimObserveDma(
    _Inout_ struct _ROK_ADAPTER *Adapter,
    _In_reads_bytes_(SizeBytes) CONST VOID *DmaBuffer,
    _In_ UINT SizeBytes
    )
{
    PROK_SIM_STATS st;
    CONST UINT32 *dw;
    UINT count, i = 0;
    ULONG indirectSeen = 0;

    if (!RokSimEnabled(Adapter) || DmaBuffer == NULL || SizeBytes < 4)
    {
        return;
    }

    st = &Adapter->SimStats;
    dw = (CONST UINT32 *)DmaBuffer;
    count = SizeBytes / sizeof(UINT32);

    while (i < count)
    {
        UINT32 word = dw[i];

        if (word == 0x80000000)
        {
            // type-2 filler NOP
            st->Type2Count++;
            i++;
            continue;
        }

        if ((word & 0xC0000000) == 0xC0000000)
        {
            // type-3：包头 = 0xC0000000 | (count-1)<<16 | opcode<<8
            UINT32 opcode = (word >> 8) & 0xFF;
            UINT32 body = ((word >> 16) & 0x3FFF) + 1;  // 含包头的 dword 数
            UINT32 end = (UINT32)(i + body);

            if (end > count)
            {
                // 跨界残包：统计后终止（提交缓冲本应完整）。
                st->UnknownOpcodes++;
                break;
            }

            st->PacketCount++;
            st->OpcodeCounts[opcode & 0x3F]++;

            if (opcode == 0x32 || opcode == 0x33)
            {
                // INDIRECT_BUFFER：dword0/1 = 基址，dword2 = 大小（dword）。
                if (body >= 4)
                {
                    st->LastIndirectBaseLo = dw[i + 1];
                    st->LastIndirectSizeDwords = dw[i + 3];
                }
                indirectSeen++;
            }

            {
                PCWSTR name = RokSimOpcodeName(opcode);
                if (name != NULL)
                {
                    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL,
                               "rokmd: sim PM4 %ls (op %#x, %u dwords)\n",
                               name, opcode, body));
                }
                else
                {
                    st->UnknownOpcodes++;
                    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL,
                               "rokmd: sim PM4 op %#x (%u dwords) [unnamed]\n",
                               opcode, body));
                }
            }

            i = end;
            continue;
        }

        if ((word & 0xC0000000) == 0x00000000)
        {
            // type-0 寄存器写包：包头 + count 个数据 dword。
            UINT32 body = ((word >> 16) & 0x3FFF) + 1;
            UINT32 end = (UINT32)(i + body);
            if (end > count)
            {
                break;
            }
            st->Type0Count++;
            i = end;
            continue;
        }

        // 其余（type-1 等）逐字跳过。
        i++;
    }

    if (indirectSeen > 0)
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "rokmd: sim decode — %lu pkts total, %lu INDIRECT_BUFFER "
                   "(base %#x, %u dwords), unknown=%lu\n",
                   st->PacketCount, indirectSeen,
                   st->LastIndirectBaseLo, st->LastIndirectSizeDwords,
                   st->UnknownOpcodes));
    }
}
