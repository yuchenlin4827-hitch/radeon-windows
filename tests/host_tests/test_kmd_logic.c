// SPDX-License-Identifier: MIT
// test_kmd_logic.c — rokmd KMD 纯逻辑单元测试（Linux 宿主，无 WDK）。
//
// 被测对象 = kmd/src/{ccc.c, sim.c} 的真实字节（build-and-run.sh 拷贝并校验）。
// 覆盖：
//   [1] PM4 解码器（type-0/2/3、INDIRECT_BUFFER 字段、跨界残包、非 sim 短路）
//   [2] CCC 注册表导入（专有名优先、遗留名兜底、范围钳制、空键默认）
//   [3] CCC escape set/get（版本校验、钳制、roundtrip、未导入适配器默认值）
//   [4] sim 模式三路判定（设备 ID / 注册表强制 / hypervisor 自动 / 真机关闭）
#include "precomp.h"
#include "adapter.h"
#include "sim.h"
#include "ccc.h"

#include <stdlib.h>

static int g_failed = 0;
static int g_total = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_total++;                                                         \
        if (!(cond))                                                       \
        {                                                                  \
            g_failed++;                                                    \
            printf("  [FAIL] %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); \
        }                                                                  \
    } while (0)

#define TEST_BEGIN(name) \
    do { printf("\n---- %s ----\n", name); rok_mock_registry_reset(); } while (0)

// 适配器构造：清零 + PCI 标识。
static void MakeAdapter(ROK_ADAPTER *a, ULONG vid, ULONG did)
{
    memset(a, 0, sizeof(*a));
    a->PciVendorId = vid;
    a->PciDeviceId = did;
}

// 注册表快捷写入（表满直接 abort —— 测试代码不需要优雅）。
static void SetReg(PCWSTR path, PCWSTR value, ULONG dword)
{
    int i;
    for (i = 0; i < rok_mock_registry_count; i++)
    {
        if (rok_mock_registry[i].Path == NULL)
        {
            rok_mock_registry[i].Path = path;
            rok_mock_registry[i].ValueName = value;
            rok_mock_registry[i].Dword = dword;
            return;
        }
    }
    abort();
}

// ---- cpuid 注入钩子 ----
// leaf 1 与 leaf 0x40000000 是两次独立 cpuid 调用，寄存器组必须分开建模
// （厂商串的 ECX 段不得覆盖 leaf1 的 hypervisor 位）。
static INT g_regs_leaf1[4];         // leaf 1：ECX bit31 = hypervisor present
static INT g_regs_hvsig[4];         // leaf 0x40000000：EBX/ECX/EDX = 厂商串

static void MockCpuid(INT regs[4], INT leaf)
{
    const INT *src = NULL;

    if (leaf == 1)
    {
        src = g_regs_leaf1;
    }
    else if (leaf == 0x40000000)
    {
        src = g_regs_hvsig;
    }

    if (src != NULL)
    {
        regs[0] = src[0];
        regs[1] = src[1];
        regs[2] = src[2];
        regs[3] = src[3];
        return;
    }
    regs[0] = regs[1] = regs[2] = regs[3] = 0;
}

// 定长 12 字节数组（literal 不足 12 字节时零填充，避免越界读）。
static const char HV_NONE[12] = { 0 };
static const char HV_KVM[12] = "KVMKVMKVM";
static const char HV_HYPERV[12] = "Microsoft Hv";

static void SetHypervisor(int present, const char vendor[12])
{
    memset(g_regs_leaf1, 0, sizeof(g_regs_leaf1));
    memset(g_regs_hvsig, 0, sizeof(g_regs_hvsig));
    if (present)
    {
        g_regs_leaf1[2] = (INT)0x80000000u;     // leaf1 ECX bit31
        memcpy(&g_regs_hvsig[1], vendor + 0, 4);
        memcpy(&g_regs_hvsig[2], vendor + 4, 4);
        memcpy(&g_regs_hvsig[3], vendor + 8, 4);
    }
}

// UNICODE_STRING 快捷构造（缓冲区 caller 提供）。
static void MakePath(UNICODE_STRING *u, WCHAR *buf, size_t chars, PCWSTR text)
{
    swprintf(buf, chars, L"%ls", text);
    RtlInitUnicodeString(u, buf);
}

// ==================================================================
// [1] PM4 解码器
// ==================================================================

static void TestPm4Decode(void)
{
    ROK_ADAPTER a;
    ROK_SIM_STATS *st;
    UINT32 buf[32];
    UINT n = 0;

    TEST_BEGIN("[1] PM4 decoder");

    // 构造一段混合命令流（包头低 16 位格式：0xC0000000 | (count-1)<<16 |
    // opcode<<8，count 含包头 dword）：
    //   type-3 NOP(0x10)：包头 + 1 数据 dword（count=2）
    //   type-2 filler
    //   type-3 INDIRECT_BUFFER(0x32)：包头 + 3 数据 dword（count=4）
    //   type-0 寄存器写：包头 + 2 数据 dword（count=3）
    //   type-3 未知 opcode 0x77：仅包头（count=1）
    memset(buf, 0, sizeof(buf));
    buf[n++] = 0xC0000000u | (1u << 16) | (0x10u << 8);   // NOP，count=2
    buf[n++] = 0xDEADBEEF;                                // NOP 数据 dword
    buf[n++] = 0x80000000u;                               // type-2 filler
    buf[n++] = 0xC0000000u | (3u << 16) | (0x32u << 8);   // INDIRECT_BUFFER
    buf[n++] = 0x11110000;                                // base lo
    buf[n++] = 0x00000002;                                // base hi
    buf[n++] = 512;                                       // size (dwords)
    buf[n++] = 0x00000000u | (2u << 16) | (0x42u << 8);   // type-0，count=3（位8-15=寄存器索引 0x42）
    buf[n++] = 0x00000001;                                // 数据 dword 1
    buf[n++] = 0x00000002;                                // 数据 dword 2
    buf[n++] = 0xC0000000u | (0u << 16) | (0x77u << 8);   // 未知 opcode，count=1

    MakeAdapter(&a, 0x1002, 0x5046);
    CHECK(RokSimInitialize(&a, NULL) == STATUS_SUCCESS);
    CHECK(a.SimulationMode == TRUE);            // 0x5046 命中模拟设备表
    CHECK(a.SimRegisterSpace != NULL);

    RokSimObserveDma(&a, buf, n * 4);
    st = &a.SimStats;
    printf("  stats: t3=%u t2=%u t0=%u unknown=%u ib(base=%#x,%u)\n",
           st->PacketCount, st->Type2Count, st->Type0Count,
           st->UnknownOpcodes, st->LastIndirectBaseLo,
           st->LastIndirectSizeDwords);

    CHECK(st->PacketCount == 3);                // NOP + IB + unknown
    CHECK(st->Type2Count == 1);
    CHECK(st->Type0Count == 1);
    CHECK(st->UnknownOpcodes == 1);             // 0x77
    CHECK(st->OpcodeCounts[0x10 & 0x3F] == 1);
    CHECK(st->OpcodeCounts[0x32 & 0x3F] == 1);
    CHECK(st->OpcodeCounts[0x77 & 0x3F] == 1);
    CHECK(st->LastIndirectBaseLo == 0x11110000);
    CHECK(st->LastIndirectSizeDwords == 512);

    RokSimShutdown(&a);
    CHECK(a.SimRegisterSpace == NULL);

    // 跨界残包：INDIRECT_BUFFER 声明 4 dword 包体但缓冲只有包头 —— 必须
    // 安全终止且不越界读（ASan 在 CI 里兜底）。
    TEST_BEGIN("[1b] PM4 truncated packet (memory safety)");
    MakeAdapter(&a, 0x1002, 0x5046);
    CHECK(RokSimInitialize(&a, NULL) == STATUS_SUCCESS);
    {
        UINT32 trunc[2];
        trunc[0] = 0xC0000000u | (3u << 16) | (0x32u << 8);
        trunc[1] = 0x12345678;
        RokSimObserveDma(&a, trunc, sizeof(trunc));
        CHECK(a.SimStats.UnknownOpcodes == 1);  // 残包计入 unknown 后终止
        CHECK(a.SimStats.PacketCount == 0);     // 未计为完整包
        CHECK(a.SimStats.LastIndirectBaseLo == 0);  // 未写入字段
    }
    RokSimShutdown(&a);

    // 非 sim 模式：直接短路，零副作用。
    TEST_BEGIN("[1c] PM4 decode disabled when not in sim mode");
    MakeAdapter(&a, 0x1002, 0x6841);            // 真实 TeraScale2 卡
    SetHypervisor(FALSE, HV_NONE);
    CHECK(RokSimInitialize(&a, NULL) == STATUS_SUCCESS);
    CHECK(a.SimulationMode == FALSE);
    RokSimObserveDma(&a, buf, n * 4);
    CHECK(a.SimStats.PacketCount == 0);
    CHECK(a.SimStats.Type0Count == 0);
}

// ==================================================================
// [2] CCC 注册表导入
// ==================================================================

static void TestCccImport(void)
{
    ROK_ADAPTER a;
    UNICODE_STRING path;
    WCHAR pathBuf[128];

    MakePath(&path, pathBuf, 128,
             L"\\Registry\\Machine\\SYSTEM\\CCS\\Control\\Class\\{4d36e968}\\0000");

    // 2a. 遗留名兜底：只写 Catalyst 遗留名。
    TEST_BEGIN("[2a] CCC import — legacy names only");
    MakeAdapter(&a, 0x1002, 0x6841);
    SetReg(pathBuf, L"EngineClockOverride", 600000);
    SetReg(pathBuf, L"EnableOverdrive", 1);
    CHECK(RokCccImport(&a, &path) == STATUS_SUCCESS);
    CHECK(a.CccSettings.ValidMask ==
          (ROK_CCC_VALID_ENGINE_CLOCK | ROK_CCC_VALID_OD_ENABLE));
    CHECK(a.CccSettings.EngineClockKHz == 600000);
    CHECK(a.CccSettings.OverdriveEnable == 1);
    CHECK(a.CccSettings.Source == ROK_CCC_SOURCE_REGISTRY);

    // 2b. 专有名优先：两套名字同时存在时取 rokmd 专有名。
    TEST_BEGIN("[2b] CCC import — rokmd names take precedence");
    MakeAdapter(&a, 0x1002, 0x6841);
    SetReg(pathBuf, L"EngineClockOverride", 999999);
    SetReg(pathBuf, L"OverdriveEngineClockKHz", 700000);
    CHECK(RokCccImport(&a, &path) == STATUS_SUCCESS);
    CHECK(a.CccSettings.EngineClockKHz == 700000);
    CHECK(a.CccSettings.ValidMask == ROK_CCC_VALID_ENGINE_CLOCK);

    // 2c. 范围钳制：低于/高于合法范围回落到边界（对齐官方 Overdrive）。
    TEST_BEGIN("[2c] CCC import — range clamp");
    MakeAdapter(&a, 0x1002, 0x6841);
    SetReg(pathBuf, L"OverdriveEngineClockKHz", 10);        // < 50 MHz
    SetReg(pathBuf, L"OverdriveMemoryClockKHz", 999999999); // > 2 GHz
    SetReg(pathBuf, L"OverdriveFanSpeedPercent", 150);      // > 100%
    CHECK(RokCccImport(&a, &path) == STATUS_SUCCESS);
    CHECK(a.CccSettings.EngineClockKHz == ROK_CCC_ENGINE_CLOCK_MIN_KHZ);
    CHECK(a.CccSettings.MemoryClockKHz == ROK_CCC_MEMORY_CLOCK_MAX_KHZ);
    CHECK(a.CccSettings.FanSpeedPercent == ROK_CCC_FAN_MAX_PERCENT);
    CHECK(a.CccSettings.ValidMask ==
          (ROK_CCC_VALID_ENGINE_CLOCK | ROK_CCC_VALID_MEMORY_CLOCK |
           ROK_CCC_VALID_FAN_SPEED));

    // 2d. 空键：无任何设置 → mask=0、Source=NONE（保持默认，非错误）。
    TEST_BEGIN("[2d] CCC import — empty registry");
    MakeAdapter(&a, 0x1002, 0x6841);
    CHECK(RokCccImport(&a, &path) == STATUS_SUCCESS);
    CHECK(a.CccSettings.ValidMask == 0);
    CHECK(a.CccSettings.Source == ROK_CCC_SOURCE_NONE);

    // 2e. NULL 路径：安全无操作。
    TEST_BEGIN("[2e] CCC import — NULL path");
    MakeAdapter(&a, 0x1002, 0x6841);
    CHECK(RokCccImport(&a, NULL) == STATUS_SUCCESS);
    CHECK(a.CccSettings.ValidMask == 0);
}

// ==================================================================
// [3] CCC escape set/get
// ==================================================================

static void TestCccSetGet(void)
{
    ROK_ADAPTER a;
    ROK_CCC_SETTINGS in, out;

    // 3a. 版本不匹配拒绝。
    TEST_BEGIN("[3a] CCC set — version mismatch rejected");
    MakeAdapter(&a, 0x1002, 0x6841);
    memset(&in, 0, sizeof(in));
    in.Version = 99;
    CHECK(RokCccSetSettings(&a, &in) == STATUS_REVISION_MISMATCH);

    // 3b. set → clamp → Source=ESCAPE；get roundtrip 一致。
    TEST_BEGIN("[3b] CCC set/get roundtrip with clamp");
    MakeAdapter(&a, 0x1002, 0x6841);
    memset(&in, 0, sizeof(in));
    in.Version = ROK_CCC_VERSION;
    in.ValidMask = ROK_CCC_VALID_ENGINE_CLOCK | ROK_CCC_VALID_FAN_SPEED;
    in.EngineClockKHz = 5000000;                // > 2 GHz → 钳到上限
    in.FanSpeedPercent = 75;
    CHECK(RokCccSetSettings(&a, &in) == STATUS_SUCCESS);
    CHECK(a.CccSettings.EngineClockKHz == ROK_CCC_ENGINE_CLOCK_MAX_KHZ);
    CHECK(a.CccSettings.FanSpeedPercent == 75);
    CHECK(a.CccSettings.Source == ROK_CCC_SOURCE_ESCAPE);
    CHECK(a.CccSettings.ValidMask ==
          (ROK_CCC_VALID_ENGINE_CLOCK | ROK_CCC_VALID_FAN_SPEED));

    memset(&out, 0, sizeof(out));
    CHECK(RokCccGetSettings(&a, &out) == STATUS_SUCCESS);
    CHECK(out.Version == ROK_CCC_VERSION);
    CHECK(out.EngineClockKHz == ROK_CCC_ENGINE_CLOCK_MAX_KHZ);
    CHECK(out.FanSpeedPercent == 75);
    CHECK(out.Source == ROK_CCC_SOURCE_ESCAPE);

    // 3c. OverdriveEnable 归一化（非 0 → 1）。
    TEST_BEGIN("[3c] CCC set — Overdrive normalized");
    memset(&in, 0, sizeof(in));
    in.Version = ROK_CCC_VERSION;
    in.ValidMask = ROK_CCC_VALID_OD_ENABLE;
    in.OverdriveEnable = 240;                   // 任意非 0
    CHECK(RokCccSetSettings(&a, &in) == STATUS_SUCCESS);
    CHECK(a.CccSettings.OverdriveEnable == 1);

    // 3d. 未导入过的适配器 get：结构化默认值。
    TEST_BEGIN("[3d] CCC get — structured defaults on fresh adapter");
    MakeAdapter(&a, 0x1002, 0x6841);
    memset(&out, 0, sizeof(out));
    CHECK(RokCccGetSettings(&a, &out) == STATUS_SUCCESS);
    CHECK(out.Version == ROK_CCC_VERSION);
    CHECK(out.ValidMask == 0);
    CHECK(out.Source == ROK_CCC_SOURCE_NONE);

    // 3e. NULL 参数拒绝。
    TEST_BEGIN("[3e] CCC — NULL parameter guard");
    CHECK(RokCccGetSettings(&a, NULL) == STATUS_INVALID_PARAMETER);
    CHECK(RokCccSetSettings(NULL, &in) == STATUS_INVALID_PARAMETER);
}

// ==================================================================
// [4] sim 模式三路判定
// ==================================================================

static void TestSimDecision(void)
{
    ROK_ADAPTER a;
    UNICODE_STRING path;
    WCHAR pathBuf[128];

    MakePath(&path, pathBuf, 128,
             L"\\Registry\\Machine\\SYSTEM\\CCS\\Control\\Class\\{4d36e968}\\0001");

    // 4a. 设备 ID 命中（最高优先级，连 hypervisor 都不需要）。
    TEST_BEGIN("[4a] sim on — device id hit (QEMU ati-vga)");
    SetHypervisor(FALSE, HV_NONE);
    MakeAdapter(&a, 0x1002, 0x5046);
    CHECK(RokSimInitialize(&a, NULL) == STATUS_SUCCESS);
    CHECK(a.SimulationMode == TRUE);
    CHECK(a.HypervisorPresent == FALSE);
    CHECK(a.SimRegisterSpaceSize == 0x10000);
    RokSimShutdown(&a);

    TEST_BEGIN("[4a2] sim device id table coverage");
    CHECK(RokSimIsSimulatedDeviceId(0x5046) == TRUE);   // QEMU ati-vga
    CHECK(RokSimIsSimulatedDeviceId(0x1111) == TRUE);   // Bochs VGA
    CHECK(RokSimIsSimulatedDeviceId(0xBEEF) == TRUE);   // VirtualBox
    CHECK(RokSimIsSimulatedDeviceId(0x0405) == TRUE);   // VMware SVGA
    CHECK(RokSimIsSimulatedDeviceId(0x5353) == TRUE);   // Hyper-V Video
    CHECK(RokSimIsSimulatedDeviceId(0x6841) == FALSE);  // 真实卡不命中

    // 4b. 注册表强制：AMD 真实 ID + 无 hypervisor，Parameters\SimulatedDevice=1。
    TEST_BEGIN("[4b] sim on — registry force");
    SetHypervisor(FALSE, HV_NONE);
    MakeAdapter(&a, 0x1002, 0x6841);
    SetReg(pathBuf, L"SimulatedDevice", 1);     // 驱动键根下
    CHECK(RokSimInitialize(&a, &path) == STATUS_SUCCESS);
    CHECK(a.SimulationMode == TRUE);
    RokSimShutdown(&a);

    // 4c. 自动判定：hypervisor 存在 + VID != 0x1002。
    TEST_BEGIN("[4c] sim auto — hypervisor + non-AMD");
    SetHypervisor(TRUE, HV_KVM);
    MakeAdapter(&a, 0x1234, 0x1111);
    CHECK(RokSimInitialize(&a, NULL) == STATUS_SUCCESS);
    CHECK(a.SimulationMode == TRUE);
    CHECK(a.HypervisorPresent == TRUE);
    RokSimShutdown(&a);

    // 4d. 关闭：AMD 真实 ID + 无 hypervisor + 无注册表开关。
    TEST_BEGIN("[4d] sim off — real AMD hardware");
    SetHypervisor(FALSE, HV_NONE);
    MakeAdapter(&a, 0x1002, 0x6779);
    CHECK(RokSimInitialize(&a, NULL) == STATUS_SUCCESS);
    CHECK(a.SimulationMode == FALSE);
    CHECK(a.SimRegisterSpace == NULL);

    // 4e. 注册表 SimulatedDevice=0：显式关闭压过自动判定（AMD + 无 hv 本就关）。
    TEST_BEGIN("[4e] sim off — registry explicit 0");
    SetHypervisor(FALSE, HV_NONE);
    MakeAdapter(&a, 0x1002, 0x6779);
    SetReg(pathBuf, L"SimulatedDevice", 0);
    CHECK(RokSimInitialize(&a, &path) == STATUS_SUCCESS);
    CHECK(a.SimulationMode == FALSE);
}

int main(void)
{
    // 环境：接管 cpuid（与本沙箱是否在 KVM 里无关，用例自定状态）。
    rok_mock_cpuid = MockCpuid;
    memset(g_regs_leaf1, 0, sizeof(g_regs_leaf1));

    // 环境自检：契约结构体尺寸在宿主与内核侧必须一致（防 mock 漂移）。
    printf("rokmd host unit tests — logic parity with kmd/src bytes\n");
    C_ASSERT(sizeof(ROK_CCC_SETTINGS) == 32);
    C_ASSERT(sizeof(ROK_SIM_STATS) == 284);   // 7×ULONG/UINT32 + 64×ULONG，见 sim.h

    TestPm4Decode();
    TestCccImport();
    TestCccSetGet();
    TestSimDecision();

    printf("\n==== host tests: %d checks, %d failed ====\n",
           g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
