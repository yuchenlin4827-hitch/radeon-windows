// SPDX-License-Identifier: MIT
// run_simdevice.c — 模拟设备端到端运行（L1.5：主机上的完整 WDDM 2.0 管线）。
//
// 本文件扮演"调用方"：PnP 管理器（AddDevice/StartDevice/StopDevice）、
// dxgkrnl（经 DriverEntry 注册的 DDI 表调用全部 DDI）、VidMm（分配/提交/
// fence 回收）与用户态探针（D3DKMTEscape 形态的 escape 报文）。
//
// 编译的是 kmd/src 的真实源码字节（driver.c/adapter.c/ddi.c/sim.c/ccc.c，
// sha256 前后校验）。设备 = QEMU ati-vga（1002:5046，模拟设备表首选 ID），
// 两个 adapter pass：
//   Pass A — 模拟设备：SIM 位点亮，PM4 解码直方图逐项核对，CCC 导入/
//            set/get 钳制，escape 契约（正例 7 + 负例 4）；
//   Pass B — 真实形态（Caicos 0x6779，无 hypervisor）：SIM 关、BAR0 真实
//            映射（MMIO 位）、Catalyst 遗留名导入、提交不影响 sim 统计。
//
// 退出码 = 失败的检查数（0 = 全过）。ASan/UBSan 全程开启。
#include "../kmock2/ntddk.h"
#include "../kmock2/dispmprt.h"
#include "../kmock2/d3dkmddi.h"
#include "../../kmd/src/rok_escape.h"

#include <stdlib.h>

// DriverEntry（真实驱动入口，driver.c）。
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);

// vdxgkrnl 接口。
void    RokVdxgReset(VOID);
void    RokVdxgConfigurePci(ULONG VendorId, ULONG DeviceId, ULONG RevisionId,
                           BOOLEAN HasBar0, UINT64 Bar0Base, ULONG Bar0Size);
void    RokVdxgSetDriverKeyPath(PCWSTR Path);
PDRIVER_INITIALIZATION_DATA RokVdxgCapturedDdiTable(VOID);
void    RokVdxgFillInterface(DXGKRNL_INTERFACE *ifc);
extern void (*rok_mock_cpuid)(INT regs[4], INT leaf);
ULONG   RokMockPoolOutstanding(VOID);

// ------------------------------------------------------------------
// 检查器
// ------------------------------------------------------------------

static int g_checks;
static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        g_checks++;                                                     \
        if (!(cond)) {                                                  \
            g_failed++;                                                 \
            printf("  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                               \
    } while (0)

#define CHECK_EQ_U(a, b)                                                \
    do {                                                                \
        unsigned long long _a = (unsigned long long)(a);                \
        unsigned long long _b = (unsigned long long)(b);                \
        g_checks++;                                                     \
        if (_a != _b) {                                                 \
            g_failed++;                                                 \
            printf("  [FAIL] %s:%d  %s == %s (got %#llx want %#llx)\n", \
                   __FILE__, __LINE__, #a, #b, _a, _b);                 \
        }                                                               \
    } while (0)

// ------------------------------------------------------------------
// CPUID 注入（hypervisor 开关）
// ------------------------------------------------------------------

static void CpuidKvmHypervisor(INT regs[4], INT leaf)
{
    memset(regs, 0, 4 * sizeof(INT));
    switch (leaf) {
    case 1:
        regs[2] = (1u << 31);                   // ECX.bit31 = hypervisor
        break;
    case 0x40000000:
        regs[0] = 0x40000001;
        regs[1] = 0x4B4D564B;                   // "KVMK"
        regs[2] = 0x564B4D56;                   // "VMKV"
        regs[3] = 0x0000004D;                   // "M\0\0\0"
        break;
    default:
        break;
    }
}

static void CpuidBareMetal(INT regs[4], INT leaf)
{
    (void)leaf;
    memset(regs, 0, 4 * sizeof(INT));
}

// ------------------------------------------------------------------
// escape 辅助（D3DKMTEscape 形态：[HEADER][OUT] 单缓冲）
// ------------------------------------------------------------------

static NTSTATUS RokEscape(HANDLE hAdapter, ULONG type, ULONG outBytes,
                          PVOID payloadIn, ULONG payloadInBytes,
                          PVOID outBuf, ULONG outBufBytes,
                          ULONG inMagic, ULONG inVersion)
{
    UCHAR buf[512];
    DXGKARG_ESCAPE esc;
    ROK_ESCAPE_HEADER *hdr = (ROK_ESCAPE_HEADER *)buf;

    memset(buf, 0, sizeof(buf));
    hdr->Magic = inMagic;
    hdr->Version = inVersion;
    hdr->Type = type;
    if (payloadIn != NULL && payloadInBytes > 0) {
        memcpy(buf + sizeof(ROK_ESCAPE_HEADER), payloadIn, payloadInBytes);
    }
    memset(&esc, 0, sizeof(esc));
    esc.hDevice = NULL;                         // 适配器级（gpu_probe 形态）
    esc.pPrivateDriverData = buf;
    esc.PrivateDriverDataSize = sizeof(ROK_ESCAPE_HEADER) +
                                (payloadInBytes > outBytes ? payloadInBytes : outBytes);
    if (outBuf != NULL && outBufBytes > 0) {
        memcpy(buf + sizeof(ROK_ESCAPE_HEADER), outBuf, outBufBytes);
    }
    {
        NTSTATUS st = RokVdxgCapturedDdiTable()->DxgkDdiEscape(hAdapter, &esc);
        if (NT_SUCCESS(st) && outBuf != NULL && outBufBytes > 0) {
            memcpy(outBuf, buf + sizeof(ROK_ESCAPE_HEADER), outBufBytes);
        }
        return st;
    }
}

static NTSTATUS RokEscapeQuery(HANDLE hAdapter, ULONG type, PVOID outBuf,
                               ULONG outBytes)
{
    return RokEscape(hAdapter, type, outBytes, NULL, 0, outBuf, outBytes,
                     ROK_ESCAPE_MAGIC, ROK_ESCAPE_VERSION);
}

// ------------------------------------------------------------------
// PM4 构造
// ------------------------------------------------------------------

#define PM4_TYPE2_FILLER        0x80000000u
#define PM4_TYPE3(op, body)     (0xC0000000u | (((body) - 1) << 16) | ((op) << 8))
#define PM4_NOP_DWORD           0xC0001000u

static UINT RokBuildRenderPm4(UINT32 *dw)
{
    UINT n = 0;

    // type-0 寄存器写包：body=4（包头 + 3 数据）。
    dw[n++] = (UINT32)((3u << 16) | 0x0000);
    dw[n++] = 0x00002820;
    dw[n++] = 0x00000001;
    dw[n++] = 0x00000002;

    // type-2 filler ×3。
    dw[n++] = PM4_TYPE2_FILLER;
    dw[n++] = PM4_TYPE2_FILLER;
    dw[n++] = PM4_TYPE2_FILLER;

    // type-3 INDIRECT_BUFFER（op 0x32）：base=0x10000000，512 dwords。
    dw[n++] = PM4_TYPE3(0x32, 4);
    dw[n++] = 0x10000000;
    dw[n++] = 0x00000000;
    dw[n++] = 512;

    // type-3 EVENT_WRITE_EOP（op 0x47）：fence 值 77。
    dw[n++] = PM4_TYPE3(0x47, 4);
    dw[n++] = 0x00001000;
    dw[n++] = 0x00000000;
    dw[n++] = 77;

    // type-3 NOP（op 0x10）。
    dw[n++] = PM4_NOP_DWORD;

    return n * 4;   // 字节数
}

// ------------------------------------------------------------------
// DriverEntry + PnP + 管线骨架（两个 pass 共用）
// ------------------------------------------------------------------

typedef struct _ROK_E2E_CTX {
    HANDLE                          hAdapter;
    HANDLE                          hDevice;
    HANDLE                          hProcess;
    HANDLE                          hContext;
    HANDLE                          hAlloc[3];
    PDRIVER_INITIALIZATION_DATA     ddi;
    DXGKRNL_INTERFACE               dxgk;
    ULONG                           sources;
    ULONG                           children;
} ROK_E2E_CTX;

static NTSTATUS RokE2EBoot(PCWSTR driverKeyPath, ROK_E2E_CTX *c)
{
    NTSTATUS st;
    UNICODE_STRING regPath;

    RtlInitUnicodeString(&regPath, L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Services\\rokmd");

    st = DriverEntry((PDRIVER_OBJECT)(UINT_PTR)0x2000, &regPath);
    if (!NT_SUCCESS(st)) { return st; }

    c->ddi = RokVdxgCapturedDdiTable();
    if (c->ddi == NULL) { return STATUS_UNSUCCESSFUL; }

    // ---- PnP：AddDevice → StartDevice ----
    st = c->ddi->DxgkDdiAddDevice((const PVOID)(UINT_PTR)0x1000, &c->hAdapter);
    if (!NT_SUCCESS(st)) { return st; }

    memset(&c->dxgk, 0, sizeof(c->dxgk));
    c->dxgk.Size = sizeof(DXGKRNL_INTERFACE);
    c->dxgk.Version = DXGKDDI_INTERFACE_VERSION;
    c->dxgk.DeviceHandle = (HANDLE)(UINT_PTR)0x3000;
    RokVdxgFillInterface(&c->dxgk);     // dxgkrnl 交付回调表

    {
        DXGK_START_INFO startInfo;
        memset(&startInfo, 0, sizeof(startInfo));
        startInfo.AdapterLuid.LowPart = 0x3F2;
        startInfo.AdapterLuid.HighPart = 0;
        st = c->ddi->DxgkDdiStartDevice(c->hAdapter, &startInfo, &c->dxgk,
                                        &c->sources, &c->children);
    }
    (void)driverKeyPath;
    return st;
}

static void RokE2ETeardown(ROK_E2E_CTX *c)
{
    DXGKARG_DESTROYALLOCATION da;
    HANDLE allocList[3] = { c->hAlloc[0], c->hAlloc[1], c->hAlloc[2] };

    c->ddi->DxgkDdiDestroyContext(c->hContext);
    memset(&da, 0, sizeof(da));
    da.NumAllocations = 3;
    da.pAllocationList = allocList;
    c->ddi->DxgkDdiDestroyAllocation(c->hAdapter, &da);
    c->ddi->DxgkDdiDestroyDevice(c->hDevice);
    c->ddi->DxgkDdiDestroyProcess(c->hAdapter, c->hProcess);
    c->ddi->DxgkDdiStopDevice(c->hAdapter);
    c->ddi->DxgkDdiRemoveDevice(c->hAdapter);

    c->hAdapter = NULL;
    c->hDevice = NULL;
    c->hContext = NULL;
    c->hProcess = NULL;
}

// ------------------------------------------------------------------
// Pass A：QEMU ati-vga（1002:5046）—— 模拟设备全管线
// ------------------------------------------------------------------

static void RunPassSimulated(VOID)
{
    ROK_E2E_CTX c;
    NTSTATUS st;
    ULONG poolBefore;
    PCWSTR driverKey =
        L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0001";

    printf("================================================================\n");
    printf(" PASS A — simulated device: QEMU ati-vga (1002:5046)\n");
    printf("================================================================\n");

    RokVdxgReset();
    rok_mock_cpuid = CpuidKvmHypervisor;
    RokVdxgConfigurePci(0x1002, 0x5046, 0x02,
                        TRUE, 0xFDE00000ULL, 0x10000);
    RokVdxgSetDriverKeyPath(driverKey);

    // CCC（官方 Catalyst 体系）驱动键值 —— rokmd 专有名；
    // 另加 M3.1 的 sim 显存覆盖 Parameters\VramSizeMb=512（默认 256）。
    {
        ROK_MOCK_REG_VALUE seed[] = {
            { driverKey, L"Overdrive", 1 },
            { driverKey, L"OverdriveEngineClockKHz", 900000 },
            { driverKey, L"OverdriveMemoryClockKHz", 1150000 },
            { driverKey, L"OverdriveFanSpeedPercent", 60 },
            { L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0001\\Parameters",
              L"VramSizeMb", 512 },
        };
        int i;
        for (i = 0; i < 5; i++) { rok_mock_registry[i] = seed[i]; }
    }

    poolBefore = RokMockPoolOutstanding();
    memset(&c, 0, sizeof(c));

    st = RokE2EBoot(driverKey, &c);
    CHECK(NT_SUCCESS(st));
    if (!NT_SUCCESS(st)) { return; }
    CHECK(c.sources == 0);          // render-only：0 路 source
    CHECK(c.children == 0);         // 0 个子设备（监视器）

    // ---- QueryAdapterInfo：DRIVERCAPS / 段 / MMU ----
    {
        DXGKARG_QUERYADAPTERINFO q;
        DXGK_DRIVERCAPS caps;
        DXGK_QUERYSEGMENTOUT4 seg4;
        DXGK_GPUMMUCAPS mmu;
        memset(&q, 0, sizeof(q));
        q.Type = DXGKQAITYPE_DRIVERCAPS;
        q.pOutputData = &caps;
        q.OutputDataSize = sizeof(caps);
        st = c.ddi->DxgkDdiQueryAdapterInfo(c.hAdapter, &q);
        CHECK(NT_SUCCESS(st));
        CHECK(caps.WDDMVersion == DXGKDDI_WDDMv2);
        CHECK(caps.SchedulingCaps.MultiEngineAware == 1);
        CHECK(caps.SchedulingCaps.NoDmaPatching == 1);
        CHECK(caps.GpuEngineTopology.NbAsymetricProcessingNodes == 1);
        CHECK(caps.SupportSurpriseRemoval == TRUE);

        memset(&q, 0, sizeof(q));
        q.Type = DXGKQAITYPE_QUERYSEGMENT4;
        q.pOutputData = &seg4;
        q.OutputDataSize = sizeof(seg4);
        st = c.ddi->DxgkDdiQueryAdapterInfo(c.hAdapter, &q);
        CHECK(NT_SUCCESS(st));
        CHECK(seg4.NbSegment == 0);     // 0 段模式（全系统内存）

        memset(&q, 0, sizeof(q));
        q.Type = DXGKQAITYPE_GPUMMUCAPS;
        q.pOutputData = &mmu;
        q.OutputDataSize = sizeof(mmu);
        memset(&mmu, 0xAA, sizeof(mmu));
        st = c.ddi->DxgkDdiQueryAdapterInfo(c.hAdapter, &q);
        CHECK(NT_SUCCESS(st));
        CHECK(mmu.ReadOnly32BitRegisters == FALSE && mmu.Reserved == 0);

        // ---- M2.8 追加类型：电源组件数 / 段无效内存范围 / 显示扩展能力 ----
        {
            UINT nPower;
            DXGK_QUERYSEGMENTMEMORYSTATE memState;
            DXGK_DISPLAY_DRIVERCAPS_EXTENSION extCaps;

            memset(&q, 0, sizeof(q));
            q.Type = DXGKQAITYPE_NUMPOWERCOMPONENTS;
            q.pOutputData = &nPower;
            q.OutputDataSize = sizeof(nPower);
            memset(&nPower, 0xAA, sizeof(nPower));
            st = c.ddi->DxgkDdiQueryAdapterInfo(c.hAdapter, &q);
            CHECK(NT_SUCCESS(st));
            CHECK(nPower == 0);     // 无电源组件（配套 FState 拒绝语义）

            memset(&q, 0, sizeof(q));
            q.Type = DXGKQAITYPE_SEGMENTMEMORYSTATE;
            q.pOutputData = &memState;
            q.OutputDataSize = sizeof(memState);
            memset(&memState, 0xAA, sizeof(memState));
            st = c.ddi->DxgkDdiQueryAdapterInfo(c.hAdapter, &q);
            CHECK(NT_SUCCESS(st));
            CHECK(memState.NumInvalidMemoryRanges == 0);    // 0 段模式恒 0 条
            CHECK(memState.pMemoryRanges == NULL);

            memset(&q, 0, sizeof(q));
            q.Type = DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION;
            q.pOutputData = &extCaps;
            q.OutputDataSize = sizeof(extCaps);
            memset(&extCaps, 0xAA, sizeof(extCaps));
            st = c.ddi->DxgkDdiQueryAdapterInfo(c.hAdapter, &q);
            CHECK(NT_SUCCESS(st));
            CHECK(extCaps.Caps.Value == 0);                 // render-only 全 0
        }
    }

    // ---- 进程 / 设备 / 分配 / 上下文 ----
    {
        DXGKARG_CREATEPROCESS cp;
        DXGKARG_CREATEDEVICE cd;
        DXGKARG_CREATEALLOCATION ca;
        DXGK_ALLOCATIONINFO infos[3];
        DXGKARG_OPENALLOCATION oa;
        DXGK_OPENALLOCATIONINFO opens[3];
        DXGKARG_CLOSEALLOCATION cla;
        DXGKARG_CREATECONTEXT cc;
        int i;

        memset(&cp, 0, sizeof(cp));
        cp.hDxgkProcess = (HANDLE)(UINT_PTR)0x4000;
        st = c.ddi->DxgkDdiCreateProcess(c.hAdapter, &cp);
        CHECK(NT_SUCCESS(st));
        CHECK(cp.hKmdProcess != NULL);
        c.hProcess = cp.hKmdProcess;

        memset(&cd, 0, sizeof(cd));
        st = c.ddi->DxgkDdiCreateDevice(c.hAdapter, &cd);
        CHECK(NT_SUCCESS(st));
        CHECK(cd.hDevice != NULL);
        c.hDevice = cd.hDevice;

        memset(&ca, 0, sizeof(ca));
        ca.NumAllocations = 3;
        ca.pAllocationInfo = infos;
        memset(infos, 0, sizeof(infos));
        st = c.ddi->DxgkDdiCreateAllocation(c.hAdapter, &ca);
        CHECK(NT_SUCCESS(st));
        for (i = 0; i < 3; i++) {
            CHECK(infos[i].hAllocation != NULL);
            c.hAlloc[i] = infos[i].hAllocation;
        }
        CHECK(c.hAlloc[0] != c.hAlloc[1] && c.hAlloc[1] != c.hAlloc[2]);

        memset(&oa, 0, sizeof(oa));
        oa.NumAllocations = 3;
        oa.pOpenAllocation = opens;
        memset(opens, 0, sizeof(opens));
        for (i = 0; i < 3; i++) { opens[i].hAllocation = c.hAlloc[i]; }
        st = c.ddi->DxgkDdiOpenAllocation(c.hDevice, &oa);
        CHECK(NT_SUCCESS(st));
        for (i = 0; i < 3; i++) {
            // DxgkCbGetHandleData 原样取回驱动登记的分配记录。
            CHECK(opens[i].hDeviceSpecificAllocation == c.hAlloc[i]);
        }

        memset(&cla, 0, sizeof(cla));
        cla.NumAllocations = 3;
        st = c.ddi->DxgkDdiCloseAllocation(c.hDevice, &cla);
        CHECK(NT_SUCCESS(st));

        memset(&cc, 0, sizeof(cc));
        cc.NodeOrdinal = 0;
        cc.EngineAffinity = 0;
        st = c.ddi->DxgkDdiCreateContext(c.hDevice, &cc);
        CHECK(NT_SUCCESS(st));
        CHECK(cc.hContext != NULL);
        c.hContext = cc.hContext;
        CHECK(cc.ContextInfo.DmaBufferSize == 0x10000);
        CHECK(cc.ContextInfo.DmaBufferSegmentSet == 0);
        CHECK(cc.ContextInfo.DmaBufferPrivateDataSize >= 16);
        CHECK(cc.ContextInfo.PatchLocationListSize == 0);
        CHECK(cc.ContextInfo.Caps.NoPatchingRequired == 1);
    }

    // ---- 分页 buffer：驱动应填 PM4 type-3 NOP ----
    {
        DXGKARG_BUILDPAGINGBUFFER bp;
        UINT32 dma[64];
        UINT i;

        memset(&bp, 0, sizeof(bp));
        bp.Operation = DXGK_BUILDPAGINGBUFFER_OPERATION_FILL;
        bp.pDmaBuffer = dma;
        bp.DmaSize = sizeof(dma);
        st = c.ddi->DxgkDdiBuildPagingBuffer(c.hAdapter, &bp);
        CHECK(NT_SUCCESS(st));
        for (i = 0; i < 64; i++) {
            if (dma[i] != PM4_NOP_DWORD) { break; }
        }
        CHECK(i == 64);
        CHECK(bp.DmaBufferWriteOffset == sizeof(dma));
    }

    // ---- 提交 #1：渲染 PM4（WDDM 2.0 虚拟地址路径）----
    {
        DXGKARG_SUBMITCOMMANDVIRTUAL sv;
        UINT32 pm4[32];
        UINT pm4Bytes = RokBuildRenderPm4(pm4);
        ROK_ESC_SIM_STATUS_OUT sim;
        int i;

        memset(&sv, 0, sizeof(sv));
        sv.hContext = c.hContext;
        sv.SubmissionFenceId = 77;
        sv.DmaBufferVirtualAddress = pm4;
        sv.DmaBufferSize = pm4Bytes;
        st = c.ddi->DxgkDdiSubmitCommandVirtual(c.hAdapter, &sv);
        CHECK(NT_SUCCESS(st));

        // sim 解码器应完整解析：t3=3（IB/EOP/NOP）、t0=1、t2=3。
        st = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_SIM_STATUS,
                            &sim, sizeof(sim));
        CHECK(NT_SUCCESS(st));
        CHECK(sim.Enabled == 1);
        CHECK(sim.HypervisorPresent == 1);
        CHECK(sim.RegisterSpaceSize == 0x10000);
        CHECK(sim.PacketCount == 3);
        CHECK(sim.Type0Count == 1);
        CHECK(sim.Type2Count == 3);
        CHECK(sim.UnknownOpcodes == 0);
        CHECK(sim.LastIndirectBaseLo == 0x10000000);
        CHECK(sim.LastIndirectSizeDwords == 512);
        CHECK(sim.LastFenceSubmitted == 77);
        CHECK(sim.OpcodeCounts[0x10 & 0x3F] == 1);      // NOP
        CHECK(sim.OpcodeCounts[0x32 & 0x3F] == 1);      // INDIRECT_BUFFER
        CHECK(sim.OpcodeCounts[0x47 & 0x3F] == 1);      // EVENT_WRITE_EOP → 槽 0x07
        for (i = 0; i < 64; i++) {
            // 直方图槽位 = opcode & 0x3F（契约定义）：0x47 落槽 0x07。
            if (i != (0x10 & 0x3F) && i != (0x32 & 0x3F) &&
                i != (0x47 & 0x3F) && sim.OpcodeCounts[i] != 0) {
                break;
            }
        }
        CHECK(i == 64);     // 直方图无杂散计数
    }

    // ---- 提交 #2：遗留 SubmitCommand（fence 78）----
    {
        DXGKARG_SUBMITCOMMAND sc;
        memset(&sc, 0, sizeof(sc));
        sc.hContext = c.hContext;
        sc.SubmissionFenceId = 78;
        sc.DmaBufferSize = 64;
        st = c.ddi->DxgkDdiSubmitCommand(c.hAdapter, &sc);
        CHECK(NT_SUCCESS(st));
    }

    // ---- 提交 #3：把 BuildPagingBuffer 的产物也走一遍虚拟路径 ----
    {
        DXGKARG_SUBMITCOMMANDVIRTUAL sv;
        UINT32 dma[64];
        ROK_ESC_SIM_STATUS_OUT sim;
        UINT i;

        memset(dma, 0, sizeof(dma));
        for (i = 0; i < 64; i++) { dma[i] = PM4_NOP_DWORD; }

        memset(&sv, 0, sizeof(sv));
        sv.hContext = c.hContext;
        sv.SubmissionFenceId = 79;
        sv.DmaBufferVirtualAddress = dma;
        sv.DmaBufferSize = sizeof(dma);
        st = c.ddi->DxgkDdiSubmitCommandVirtual(c.hAdapter, &sv);
        CHECK(NT_SUCCESS(st));

        st = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_SIM_STATUS,
                            &sim, sizeof(sim));
        CHECK(NT_SUCCESS(st));
        CHECK(sim.PacketCount == 3 + 64);
        CHECK(sim.OpcodeCounts[0x10 & 0x3F] == 65);     // 1 + 64 NOP
        CHECK(sim.LastFenceSubmitted == 79);
        CHECK(sim.LastIndirectBaseLo == 0x10000000);    // 不被后续提交覆盖
    }

    // ---- fence 回收（OS 轮询路径）----
    {
        DXGKARG_QUERYCURRENTFENCE fence;
        memset(&fence, 0, sizeof(fence));
        st = c.ddi->DxgkDdiQueryCurrentFence(c.hAdapter, &fence);
        CHECK(NT_SUCCESS(st));
        CHECK(fence.CurrentFence == 79);
    }

    // ---- escape 契约：版本 / PCI / 能力 / CCC ----
    {
        ROK_ESC_VERSION_OUT ver;
        ROK_ESC_PCI_ID_OUT pci;
        ROK_ESC_CAPS_OUT caps;
        ROK_ESC_CCC_SETTINGS ccc;
        UCHAR smallBuf[8];
        NTSTATUS stEsc;

        st = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_VERSION,
                            &ver, sizeof(ver));
        CHECK(NT_SUCCESS(st));
        CHECK(ver.Major == 0 && ver.Minor == 1 && ver.Patch == 0);
        CHECK(ver.WddmInterfaceVersion == 0x5023);

        st = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_PCI_ID,
                            &pci, sizeof(pci));
        CHECK(NT_SUCCESS(st));
        CHECK(pci.VendorId == 0x1002);
        CHECK(pci.DeviceId == 0x5046);
        CHECK(pci.RevisionId == 0x02);
        CHECK(pci.Family == 1);     // TS1 = 未表列 DID 的默认（文档预期）

        st = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_CAPS,
                            &caps, sizeof(caps));
        CHECK(NT_SUCCESS(st));
        CHECK((caps.CapsMask & ROK_CAP_SIM_DEVICE) != 0);
        CHECK((caps.CapsMask & ROK_CAP_PCI_ID_VALID) != 0);
        CHECK((caps.CapsMask & ROK_CAP_SEGMENT_QUERY) != 0);
        CHECK((caps.CapsMask & ROK_CAP_MMIO_MAPPED) == 0);  // sim 不映射 BAR0
        CHECK(caps.Pm4PacketVersion == 3);

        st = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_CCC_GET_SETTINGS,
                            &ccc, sizeof(ccc));
        CHECK(NT_SUCCESS(st));
        CHECK(ccc.Version == 1);
        CHECK(ccc.ValidMask == (ROK_ESC_CCC_VALID_ENGINE_CLOCK |
                                ROK_ESC_CCC_VALID_MEMORY_CLOCK |
                                ROK_ESC_CCC_VALID_OD_ENABLE |
                                ROK_ESC_CCC_VALID_FAN_SPEED));
        CHECK(ccc.EngineClockKHz == 900000);
        CHECK(ccc.MemoryClockKHz == 1150000);
        CHECK(ccc.OverdriveEnable == 1);
        CHECK(ccc.FanSpeedPercent == 60);
        CHECK(ccc.Source == ROK_ESC_CCC_SOURCE_REGISTRY);

        // CCC set：超界值 → 钳制（Overdrive "range-clamp" 语义）。
        {
            ROK_ESC_CCC_SETTINGS set;
            memset(&set, 0, sizeof(set));
            set.Version = 1;
            set.ValidMask = ROK_ESC_CCC_VALID_ENGINE_CLOCK |
                            ROK_ESC_CCC_VALID_FAN_SPEED |
                            ROK_ESC_CCC_VALID_OD_ENABLE;
            set.EngineClockKHz = 3000000;   // > 2 GHz 上限
            set.FanSpeedPercent = 150;      // > 100%
            set.OverdriveEnable = 7;        // 非 0 → 归一化为 1
            st = RokEscape(c.hAdapter, ROK_ESC_TYPE_CCC_SET_SETTINGS,
                           sizeof(set), &set, sizeof(set), &set, sizeof(set),
                           ROK_ESCAPE_MAGIC, ROK_ESCAPE_VERSION);
            CHECK(NT_SUCCESS(st));

            st = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_CCC_GET_SETTINGS,
                                &ccc, sizeof(ccc));
            CHECK(NT_SUCCESS(st));
            CHECK(ccc.EngineClockKHz == 2000000);   // 钳到上限
            CHECK(ccc.FanSpeedPercent == 100);
            CHECK(ccc.OverdriveEnable == 1);
            CHECK(ccc.Source == ROK_ESC_CCC_SOURCE_ESCAPE);
        }

        // ---- M3.1 追加 escape 面：VRAM / SEGINFO（sim 覆盖值生效）----
        {
            ROK_ESC_VRAM_OUT vram;
            ROK_ESC_SEGMENT_INFO_OUT segInfo;

            stEsc = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_VRAM,
                                   &vram, sizeof(vram));
            CHECK(NT_SUCCESS(stEsc));
            CHECK(vram.VramSizeBytes == 512ULL * 1024ULL * 1024ULL);
            CHECK(vram.GartSizeBytes == 0);         // GTT 未落地（M3.2）
            CHECK(vram.VramSegmentId == 0);         // 0 段模式

            stEsc = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_SEGMENT_INFO,
                                   &segInfo, sizeof(segInfo));
            CHECK(NT_SUCCESS(stEsc));
            CHECK(segInfo.VramSizeBytes == 512ULL * 1024ULL * 1024ULL);
            CHECK(segInfo.GartSizeBytes == 0);
            CHECK(segInfo.VramSegmentId == 0);
            CHECK(segInfo.GartSegmentId == 0);
            CHECK(segInfo.Flags == ROK_SEG_FLAG_VRAM_POPULATED);
        }

        // ---- escape 负例 ----
        smallBuf[0] = 0;
        stEsc = RokEscape(c.hAdapter, ROK_ESC_TYPE_QUERY_VERSION, sizeof(ver),
                          NULL, 0, smallBuf, sizeof(smallBuf),
                          0xDEADBEEF, ROK_ESCAPE_VERSION);
        CHECK(stEsc == STATUS_INVALID_PARAMETER);           // 坏 magic

        stEsc = RokEscape(c.hAdapter, ROK_ESC_TYPE_QUERY_VERSION, sizeof(ver),
                          NULL, 0, smallBuf, sizeof(smallBuf),
                          ROK_ESCAPE_MAGIC, 99);
        CHECK(stEsc == STATUS_REVISION_MISMATCH);           // 坏版本

        stEsc = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_SIM_STATUS,
                               smallBuf, sizeof(smallBuf));
        CHECK(stEsc == STATUS_BUFFER_TOO_SMALL);            // 缓冲不足

        stEsc = RokEscapeQuery(c.hAdapter, 99, &ver, sizeof(ver));
        CHECK(stEsc == STATUS_NOT_IMPLEMENTED);             // 未实现类型
    }

    // ---- 引擎管理 / 元数据 / MPO（WDDM 1.3+ 面）----
    {
        DXGKARG_QUERYDEPENDENTENGINEGROUP qdg;
        DXGKARG_QUERYENGINESTATUS qes;
        DXGKARG_RESETENGINE re;
        DXGKARG_GETNODEMETADATA meta;
        DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT mpo;

        memset(&qdg, 0, sizeof(qdg));
        qdg.NodeOrdinal = 0;
        st = c.ddi->DxgkDdiQueryDependentEngineGroup(c.hAdapter, &qdg);
        CHECK(NT_SUCCESS(st));
        CHECK(qdg.DependentNodeOrdinalMask == 1);

        memset(&qes, 0, sizeof(qes));
        qes.EngineOrdinal = 0;
        st = c.ddi->DxgkDdiQueryEngineStatus(c.hAdapter, &qes);
        CHECK(NT_SUCCESS(st));
        CHECK(qes.EngineStatus.Responsive == 1);

        memset(&re, 0, sizeof(re));
        st = c.ddi->DxgkDdiResetEngine(c.hAdapter, &re);
        CHECK(NT_SUCCESS(st));
        CHECK(re.LastAbortedFenceId == 79);

        memset(&meta, 0, sizeof(meta));
        st = c.ddi->DxgkDdiGetNodeMetadata(c.hAdapter, 0, &meta);
        CHECK(NT_SUCCESS(st));
        CHECK(meta.EngineType == DXGK_ENGINE_TYPE_3D);
        CHECK(meta.FriendlyName[0] == L'r' && meta.FriendlyName[1] == L'o');

        memset(&mpo, 0, sizeof(mpo));
        st = c.ddi->DxgkDdiCheckMultiPlaneOverlaySupport(c.hAdapter, &mpo);
        CHECK(NT_SUCCESS(st));
        CHECK(mpo.Supported == FALSE);
    }

    // ---- 桩 DDI 抽查（签名绑定 + 不崩溃）----
    {
        DXGKARG_PREEMPTCOMMAND pc;
        DXGKARG_PATCH patch;
        DXGKARG_CREATEPROCESS cp2;
        memset(&pc, 0, sizeof(pc));
        CHECK(NT_SUCCESS(c.ddi->DxgkDdiPreemptCommand(c.hAdapter, &pc)));
        memset(&patch, 0, sizeof(patch));
        CHECK(NT_SUCCESS(c.ddi->DxgkDdiPatch(c.hAdapter, &patch)));
        memset(&cp2, 0, sizeof(cp2));
        cp2.hDxgkProcess = (HANDLE)(UINT_PTR)0x5000;
        CHECK(NT_SUCCESS(c.ddi->DxgkDdiCreateProcess(c.hAdapter, &cp2)));
        CHECK(cp2.hKmdProcess != NULL);
        c.ddi->DxgkDdiDestroyProcess(c.hAdapter, cp2.hKmdProcess);
    }

    // ---- M2.8 追加 DDI 面：LDA/电源组件/PEP/P-state/MPO v1
    //      （签名绑定 + 与能力声明一致的确定性答复）----
    {
        LINKED_DEVICE linked;
        GUID ctrlCode;
        SIZE_T bytesReturned;
        DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY mpo1;

        memset(&linked, 0, sizeof(linked));
        CHECK(c.ddi->DxgkDdiLinkDevice((PDEVICE_OBJECT)(UINT_PTR)0x6000,
                                       c.hAdapter, &linked)
              == STATUS_NOT_SUPPORTED);                     // 单适配器明确拒绝

        CHECK(c.ddi->DxgkDdiSetPowerComponentFState(c.hAdapter, 0, 0)
              == STATUS_INVALID_PARAMETER);                 // 组件不存在语义

        memset(&ctrlCode, 0, sizeof(ctrlCode));
        bytesReturned = 0x1234;
        CHECK(c.ddi->DxgkDdiPowerRuntimeControlRequest(
                  c.hAdapter, &ctrlCode, NULL, 0, NULL, 0, &bytesReturned)
              == STATUS_NOT_SUPPORTED);                     // runtime pm 未启用
        CHECK(bytesReturned == 0);                          // 回执字节数必清零

        CHECK(c.ddi->DxgkDdiSetPowerPState(c.hAdapter, 0, 0)
              == STATUS_NOT_SUPPORTED);                     // 无时钟控制路径

        memset(&mpo1, 0, sizeof(mpo1));
        CHECK(c.ddi->DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay(
                  c.hAdapter, &mpo1) == STATUS_NOT_IMPLEMENTED);    // 0 路 source
    }

    // ---- 拆除：链表/池全平衡（ASan leak 核对）----
    RokE2ETeardown(&c);
    CHECK(RokMockPoolOutstanding() == poolBefore);
}

// ------------------------------------------------------------------
// Pass B：真实形态（Caicos 0x6779，无 hypervisor）—— sim 关闭路径
// ------------------------------------------------------------------

static void RunPassRealHardware(VOID)
{
    ROK_E2E_CTX c;
    NTSTATUS st;
    ULONG poolBefore;
    PCWSTR driverKey =
        L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000";

    printf("================================================================\n");
    printf(" PASS B — real form: Caicos (1002:6779), no hypervisor\n");
    printf("================================================================\n");

    RokVdxgReset();
    rok_mock_cpuid = CpuidBareMetal;
    RokVdxgConfigurePci(0x1002, 0x6779, 0x91,
                        TRUE, 0xFDE00000ULL, 0x10000);
    RokVdxgSetDriverKeyPath(driverKey);

    // Catalyst 遗留名（无 rokmd 专有名时兜底导入）。
    {
        ROK_MOCK_REG_VALUE seed[] = {
            { driverKey, L"EnableOverdrive", 1 },
            { driverKey, L"EngineClockOverride", 960000 },
        };
        int i;
        for (i = 0; i < 2; i++) { rok_mock_registry[i] = seed[i]; }
    }

    poolBefore = RokMockPoolOutstanding();
    memset(&c, 0, sizeof(c));

    st = RokE2EBoot(driverKey, &c);
    CHECK(NT_SUCCESS(st));
    if (!NT_SUCCESS(st)) { return; }

    // 真实模式：BAR0 应被真实映射（caps 的 MMIO 位点亮、SIM 位熄灭）。
    {
        ROK_ESC_CAPS_OUT caps;
        ROK_ESC_PCI_ID_OUT pci;
        ROK_ESC_SIM_STATUS_OUT sim;
        ROK_ESC_CCC_SETTINGS ccc;
        NTSTATUS stEsc;

        stEsc = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_CAPS,
                               &caps, sizeof(caps));
        CHECK(NT_SUCCESS(stEsc));
        CHECK((caps.CapsMask & ROK_CAP_MMIO_MAPPED) != 0);
        CHECK((caps.CapsMask & ROK_CAP_SIM_DEVICE) == 0);

        stEsc = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_PCI_ID,
                               &pci, sizeof(pci));
        CHECK(NT_SUCCESS(stEsc));
        CHECK(pci.DeviceId == 0x6779);
        CHECK(pci.Family == 2);     // Caicos → TeraScale 2

        stEsc = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_SIM_STATUS,
                               &sim, sizeof(sim));
        CHECK(NT_SUCCESS(stEsc));
        CHECK(sim.Enabled == 0);
        CHECK(sim.HypervisorPresent == 0);
        CHECK(sim.PacketCount == 0);

        stEsc = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_CCC_GET_SETTINGS,
                               &ccc, sizeof(ccc));
        CHECK(NT_SUCCESS(stEsc));
        CHECK(ccc.EngineClockKHz == 960000);            // 遗留名兜底
        CHECK(ccc.OverdriveEnable == 1);
        CHECK(ccc.Source == ROK_ESC_CCC_SOURCE_REGISTRY);
        CHECK((ccc.ValidMask & ROK_ESC_CCC_VALID_MEMORY_CLOCK) == 0);
    }

    // 真实模式提交：fence 推进但 sim 统计不动。
    {
        DXGKARG_SUBMITCOMMANDVIRTUAL sv;
        UINT32 pm4[32];
        UINT pm4Bytes = RokBuildRenderPm4(pm4);
        ROK_ESC_SIM_STATUS_OUT sim;
        DXGKARG_QUERYCURRENTFENCE fence;

        memset(&sv, 0, sizeof(sv));
        sv.hContext = NULL;     // 提交路径不查上下文（簿记）
        sv.SubmissionFenceId = 42;
        sv.DmaBufferVirtualAddress = pm4;
        sv.DmaBufferSize = pm4Bytes;
        st = c.ddi->DxgkDdiSubmitCommandVirtual(c.hAdapter, &sv);
        CHECK(NT_SUCCESS(st));

        memset(&fence, 0, sizeof(fence));
        st = c.ddi->DxgkDdiQueryCurrentFence(c.hAdapter, &fence);
        CHECK(NT_SUCCESS(st));
        CHECK(fence.CurrentFence == 42);

        st = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_SIM_STATUS,
                            &sim, sizeof(sim));
        CHECK(NT_SUCCESS(st));
        CHECK(sim.PacketCount == 0);        // 真实硬件不解码
    }

    // M3.1 escape：真实形态 BAR0 内容全 0 → CONFIG_MEMSIZE=0 出界 →
    // 不上报显存（诚实边界：探测值不满足阈值范围时宁可不报）。
    {
        ROK_ESC_VRAM_OUT vram;
        ROK_ESC_SEGMENT_INFO_OUT segInfo;

        st = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_VRAM,
                            &vram, sizeof(vram));
        CHECK(NT_SUCCESS(st));
        CHECK(vram.VramSizeBytes == 0);

        st = RokEscapeQuery(c.hAdapter, ROK_ESC_TYPE_QUERY_SEGMENT_INFO,
                            &segInfo, sizeof(segInfo));
        CHECK(NT_SUCCESS(st));
        CHECK(segInfo.Flags == 0);          // 未 POPULATED
    }

    RokE2ETeardown(&c);
    CHECK(RokMockPoolOutstanding() == poolBefore);
}

// ------------------------------------------------------------------
// DDI 表完整性（DriverEntry 注册面）
// ------------------------------------------------------------------

static void RunDdiTableChecks(VOID)
{
    PDRIVER_INITIALIZATION_DATA ddi;
    int filled = 0;
    size_t i, members;

    printf("================================================================\n");
    printf(" DDI table completeness (DriverEntry -> DxgkInitialize)\n");
    printf("================================================================\n");

    RokVdxgReset();
    rok_mock_cpuid = CpuidBareMetal;
    RokVdxgConfigurePci(0x1002, 0x5046, 0x02, FALSE, 0, 0);
    RokVdxgSetDriverKeyPath(L"\\REGISTRY\\MACHINE\\SYSTEM\\rokmd");

    {
        UNICODE_STRING regPath;
        RtlInitUnicodeString(&regPath,
                             L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Services\\rokmd");
        CHECK(NT_SUCCESS(DriverEntry((PDRIVER_OBJECT)(UINT_PTR)0x2000,
                                     &regPath)));
    }

    ddi = RokVdxgCapturedDdiTable();
    CHECK(ddi != NULL);
    if (ddi == NULL) { return; }
    CHECK(ddi->Version == 0x5023);      // WDDM 2.0 DDI 版本

    // 结构成员逐个统计非空指针（跳过首个 ULONG Version 及其后的对齐填充）。
    {
        PVOID *slots = (PVOID *)((UCHAR *)ddi + sizeof(ULONG) +
                                 sizeof(PVOID) - sizeof(ULONG) % sizeof(PVOID));
        members = (sizeof(*ddi) - (size_t)((UCHAR *)slots - (UCHAR *)ddi)) /
                  sizeof(PVOID);
        for (i = 0; i < members; i++) {
            if (slots[i] != NULL) {
                filled++;
            }
        }
    }
    printf("  DDI members registered: %d / %d\n", filled, (int)members);
    CHECK(filled == (int)members);      // 全部成员非空 = 表完整

    // 关键成员点名。
    CHECK(ddi->DxgkDdiAddDevice != NULL);
    CHECK(ddi->DxgkDdiStartDevice != NULL);
    CHECK(ddi->DxgkDdiSubmitCommandVirtual != NULL);    // WDDM 2.0 关键
    CHECK(ddi->DxgkDdiCreateProcess != NULL);
    CHECK(ddi->DxgkDdiEscape != NULL);
    CHECK(ddi->DxgkDdiQueryCurrentFence != NULL);
    CHECK(ddi->DxgkDdiGetNodeMetadata != NULL);         // WDDM 1.3 面

    // M2.8 追加成员点名（0x5023 版本全表非空的最后一批）。
    CHECK(ddi->DxgkDdiLinkDevice != NULL);
    CHECK(ddi->DxgkDdiSetPowerComponentFState != NULL);
    CHECK(ddi->DxgkDdiPowerRuntimeControlRequest != NULL);
    CHECK(ddi->DxgkDdiSetPowerPState != NULL);
    CHECK(ddi->DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay != NULL);
}

// ------------------------------------------------------------------
// main
// ------------------------------------------------------------------

int main(VOID)
{
    RunDdiTableChecks();
    RunPassSimulated();
    RunPassRealHardware();

    printf("================================================================\n");
    printf("==== sim-device E2E: %d checks, %d failed ====\n",
           g_checks, g_failed);
    printf("================================================================\n");
    return (g_failed == 0) ? 0 : 1;
}
