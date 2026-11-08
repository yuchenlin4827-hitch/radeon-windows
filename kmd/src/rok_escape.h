// SPDX-License-Identifier: MIT
// rok_escape.h — rokmd 私有 Escape 通道 ABI（KMD ⇄ 用户态 ICD/探针）
//
// 用途与边界：
//   * WDDM 2.0 下，用户态驱动（Vulkan ICD / D3D UMD）通过 D3DKMTEscape
//     与 KMD 通信，OS 侧经 DxgkDdiEscape 转交；探针工具（gpu_probe [4]）
//     用同一通道在实机上验证 KMD 可达性。
//   * 本文件是 M4 阶段 terakan ICD 接自研 KMD 的**契约头**：v1 为查询面
//     （版本/PCI/显存/能力/段信息），提交类接口随 M3 的 ring/驻留能力
//     落地后以 v2 追加（v1 查询保持向后兼容）。
//   * **双模式头文件**：内核侧（precomp.h 定义 ROK_KERNEL_BUILD）使用
//     ntddk 类型；用户态侧（gpu_probe / terakan ICD）直接包含本文件，
//     使用 stdint。两侧编译同一结构体布局，C_ASSERT 防漂移。
//   * 结构体布局按 x64 自然对齐设计。
#pragma once

#if defined(ROK_KERNEL_BUILD)

#include <ntddk.h>
typedef UINT32   ROK_U32;
typedef UINT64   ROK_U64;
typedef char     ROK_CHAR8;

#else  /* 用户态（Windows / MinGW / clang-cl） */

#include <stdint.h>
typedef uint32_t ROK_U32;
typedef uint64_t ROK_U64;
typedef char     ROK_CHAR8;

#ifndef C_ASSERT
#define ROK_CASSERT_PASTE2(x, y) x##y
#define ROK_CASSERT_PASTE(x, y)  ROK_CASSERT_PASTE2(x, y)
#define C_ASSERT(e) \
    typedef char ROK_CASSERT_PASTE(rok_c_assert_, __LINE__)[((e) ? 1 : -1)]
#endif

#endif /* ROK_KERNEL_BUILD */

// 魔数 "AROK"（内存中小端序显示为 41 52 4F 4B）。
#define ROK_ESCAPE_MAGIC        0x4B4F5241UL
// 协议版本：v1 = 查询面。M3/M4 追加提交类接口时升 v2 并保留 v1 查询。
#define ROK_ESCAPE_VERSION      1UL

// Escape 类型（v1 查询面；M2.6 追加 SIM/CCC 为 6/7/8，M2.8 追加引擎状态为 9，
// M3.1/M3.2 追加硬件信息为 10/CP 软复位触发为 11，M3.4a 追加寄存器快照为
// 12；均向后兼容：旧调用方不感知新增类型）。类型 11 会写硬件寄存器，
// 仅供探测工具显式调用；KMD 侧在 BAR0 未映射时拒绝。
typedef enum _ROK_ESCAPE_TYPE
{
    ROK_ESC_TYPE_QUERY_VERSION   = 1,    // -> ROK_ESC_VERSION_OUT
    ROK_ESC_TYPE_QUERY_PCI_ID    = 2,    // -> ROK_ESC_PCI_ID_OUT
    ROK_ESC_TYPE_QUERY_VRAM      = 3,    // -> ROK_ESC_VRAM_OUT（M3.1 起填探测值）
    ROK_ESC_TYPE_QUERY_CAPS      = 4,    // -> ROK_ESC_CAPS_OUT
    ROK_ESC_TYPE_QUERY_SEGMENT_INFO = 5, // -> ROK_ESC_SEGMENT_INFO_OUT（M3 前全 0）
    ROK_ESC_TYPE_QUERY_SIM_STATUS = 6,   // -> ROK_ESC_SIM_STATUS_OUT（M2.6，sim.c）
    ROK_ESC_TYPE_CCC_GET_SETTINGS = 7,   // -> ROK_ESC_CCC_SETTINGS（M2.6，ccc.c）
    ROK_ESC_TYPE_CCC_SET_SETTINGS = 8,   // <- ROK_ESC_CCC_SETTINGS（M2.6，输入报文）
    ROK_ESC_TYPE_QUERY_ENGINE_STATUS = 9, // -> ROK_ESC_ENGINE_STATUS_OUT（M2.8）
    ROK_ESC_TYPE_QUERY_HW_INFO   = 10,   // -> ROK_ESC_HW_INFO_OUT（M3.1，只读）
    ROK_ESC_TYPE_HW_CP_RESET     = 11,   // -> ROK_ESC_HW_RESET_OUT（M3.2，写硬件）
    ROK_ESC_TYPE_QUERY_HW_STATUS = 12,   // -> ROK_ESC_HW_STATUS_OUT（M3.4a，只读）
} ROK_ESCAPE_TYPE;

// 能力位（ROK_ESC_CAPS_OUT.CapsMask）。
#define ROK_CAP_MMIO_MAPPED         0x00000001UL  // BAR0 已映射（StartDevice 成功）
#define ROK_CAP_PCI_ID_VALID        0x00000002UL  // PCI 标识已读取
#define ROK_CAP_SIM_DEVICE          0x00000004UL  // M2.6：模拟设备模式（sim.c）
#define ROK_CAP_PM4_SUBMIT          0x00000100UL  // M3：ring 提交可用
#define ROK_CAP_IH_INTERRUPT        0x00000200UL  // M3：IH 中断可用
#define ROK_CAP_VRAM_SEGMENT        0x00000400UL  // M3：VRAM 段可用
#define ROK_CAP_SEGMENT_QUERY       0x00000800UL  // QUERY_SEGMENT_INFO 可用（v1.1）
#define ROK_CAP_ENGINE_STATUS_QUERY 0x00001000UL  // M2.8：QUERY_ENGINE_STATUS 可用
#define ROK_CAP_HW_INFO_QUERY       0x00002000UL  // M3.1：QUERY_HW_INFO 可用
#define ROK_CAP_HW_STATUS_QUERY     0x00004000UL  // M3.4a：QUERY_HW_STATUS 可用

// 段标志（ROK_ESC_SEGMENT_INFO_OUT.Flags）。
#define ROK_SEG_FLAG_VRAM_POPULATED 0x00000001UL  // VramSizeBytes 已探测
#define ROK_SEG_FLAG_GART_POPULATED 0x00000002UL  // GartSizeBytes 已探测

// ---- 报文头（每个 escape 缓冲区必须以其开头）----
typedef struct _ROK_ESCAPE_HEADER
{
    ROK_U32 Magic;          // = ROK_ESCAPE_MAGIC
    ROK_U32 Version;        // = ROK_ESCAPE_VERSION
    ROK_U32 Type;           // ROK_ESCAPE_TYPE
    ROK_U32 Reserved;       // 0
} ROK_ESCAPE_HEADER, *PROK_ESCAPE_HEADER;

// ---- 查询输出（紧随报文头之后写入，缓冲区布局 [HEADER][OUT]，
//      调用方传入的 PrivateDriverDataSize 须 >= 16 + sizeof(OUT)）----

typedef struct _ROK_ESC_VERSION_OUT
{
    ROK_U32 Major;
    ROK_U32 Minor;
    ROK_U32 Patch;
    ROK_U32 WddmInterfaceVersion;   // DXGKDDI_INTERFACE_VERSION（0x5023 = WDDM 2.0）
} ROK_ESC_VERSION_OUT, *PROK_ESC_VERSION_OUT;

typedef struct _ROK_ESC_PCI_ID_OUT
{
    ROK_U32 VendorId;
    ROK_U32 DeviceId;
    ROK_U32 RevisionId;
    ROK_U32 Family;         // 1=TeraScale1 2=TeraScale2 3=TeraScale3 0=未知
} ROK_ESC_PCI_ID_OUT, *PROK_ESC_PCI_ID_OUT;

typedef struct _ROK_ESC_VRAM_OUT
{
    ROK_U64 VramSizeBytes;  // M3.1 起为探测值（CONFIG_MEMSIZE / sim 配置）
    ROK_U64 GartSizeBytes;  // GTT 未落地（M3.2）前为 0
    ROK_U32 VramSegmentId;  // QuerySegment 上报的段号；0 = 暂无段
    ROK_U32 Reserved;
} ROK_ESC_VRAM_OUT, *PROK_ESC_VRAM_OUT;

typedef struct _ROK_ESC_CAPS_OUT
{
    ROK_U32 CapsMask;
    ROK_U32 Pm4PacketVersion;   // TeraScale PM4 = 3（type-3 包）；未探测时 0
    ROK_U32 Reserved[2];
} ROK_ESC_CAPS_OUT, *PROK_ESC_CAPS_OUT;

// 段信息（M3.1 填 VRAM 尺寸 + POPULATED 位；段号 M3.2 起填充。M4 的 ICD 用它
// 决定 VkMemoryHeap/Type 布局 —— deviceLocal 堆 = VRAM 段）。
typedef struct _ROK_ESC_SEGMENT_INFO_OUT
{
    ROK_U64 VramSizeBytes;
    ROK_U64 GartSizeBytes;
    ROK_U32 VramSegmentId;
    ROK_U32 GartSegmentId;
    ROK_U32 Flags;          // ROK_SEG_FLAG_*
    ROK_U32 Reserved;
} ROK_ESC_SEGMENT_INFO_OUT, *PROK_ESC_SEGMENT_INFO_OUT;

// ---- 模拟设备状态（M2.6，sim.c；escape 查询 6 回传给 gpu_probe/ICD）----
// 结构必须 POD 且稳定：字段增删只能追加，两侧 C_ASSERT 防漂移。
typedef struct _ROK_ESC_SIM_STATUS_OUT
{
    ROK_U32 Enabled;             // 1 = 模拟模式（sim.c 判定命中）
    ROK_U32 HypervisorPresent;   // CPUID leaf 1 ECX bit31
    ROK_U32 RegisterSpaceSize;   // 模拟寄存器空间字节数（64KB）
    ROK_U32 PacketCount;         // type-3 包总数（累计）
    ROK_U32 Type2Count;          // type-2 filler 个数
    ROK_U32 Type0Count;          // type-0 寄存器写包个数
    ROK_U32 UnknownOpcodes;      // 未识别 opcode 个数
    ROK_U32 LastIndirectBaseLo;  // 最近 INDIRECT_BUFFER 基址低 32 位
    ROK_U32 LastIndirectSizeDwords;
    ROK_U32 LastFenceSubmitted;
    ROK_U32 OpcodeCounts[64];    // type-3 opcode 直方图（opcode & 0x3F）
} ROK_ESC_SIM_STATUS_OUT, *PROK_ESC_SIM_STATUS_OUT;

// ---- CCC 设置块（M2.6，ccc.c；escape get=7 / set=8 报文体）----
// 语义与 kmd/src/ccc.h 的 ROK_CCC_SETTINGS 一致（同一份定义镜像，
// 保持契约头自包含）；CCC 值名映射与钳制规则见 docs/ccc.md。
#define ROK_ESC_CCC_VERSION                 1
#define ROK_ESC_CCC_VALID_ENGINE_CLOCK      0x00000001UL
#define ROK_ESC_CCC_VALID_MEMORY_CLOCK      0x00000002UL
#define ROK_ESC_CCC_VALID_OD_ENABLE         0x00000004UL
#define ROK_ESC_CCC_VALID_FAN_SPEED         0x00000008UL
#define ROK_ESC_CCC_SOURCE_NONE             0
#define ROK_ESC_CCC_SOURCE_REGISTRY         1
#define ROK_ESC_CCC_SOURCE_ESCAPE           2

typedef struct _ROK_ESC_CCC_SETTINGS
{
    ROK_U32 Version;            // ROK_ESC_CCC_VERSION
    ROK_U32 ValidMask;          // ROK_ESC_CCC_VALID_* 组合
    ROK_U32 EngineClockKHz;     // 引擎时钟覆盖（0 = 未覆盖）
    ROK_U32 MemoryClockKHz;     // 显存时钟覆盖（0 = 未覆盖）
    ROK_U32 OverdriveEnable;    // 1 = Overdrive 手动档
    ROK_U32 FanSpeedPercent;    // 0..100
    ROK_U32 Source;             // ROK_ESC_CCC_SOURCE_*
    ROK_U32 Reserved;           // 0
} ROK_ESC_CCC_SETTINGS, *PROK_ESC_CCC_SETTINGS;

// ---- 引擎状态（M2.8；escape 查询 9）----
// 用途：探测/工具核对提交管线活性；M4 的 terakan ICD 以同一通道
// 轮询 fence 进度（即时完成模型 submitted==completed，M3 接 IH 后分离）。
// 结构必须 POD 且稳定：字段增删只能追加，两侧 C_ASSERT 防漂移。
typedef struct _ROK_ESC_ENGINE_STATUS_OUT
{
    ROK_U32 LastSubmittedFenceId;   // 最近提交的 fence id（SubmitCommand/Virtual）
    ROK_U32 LastCompletedFenceId;   // 最近完成的 fence id
    ROK_U32 InFlightFences;         // submitted - completed（负值钳为 0）
    ROK_U32 SubmitCount;            // 提交次数累计（回绕观测值）
    ROK_U32 PagingOpsCount;         // BuildPagingBuffer 调用累计
    ROK_U32 Responsive;             // 1 = 引擎在推进（fence 进度判定）
    ROK_U32 Reserved[2];            // 0
} ROK_ESC_ENGINE_STATUS_OUT, *PROK_ESC_ENGINE_STATUS_OUT;

// ---- 硬件信息（M3.1；escape 查询 10，只读）----
// StartDevice 阶段的识别快照 + GRBM_STATUS 只读采样。ChipName 为 NUL
// 结尾短名（"CAICOS"/"TURKS"/…；未收录 = "UNKNOWN"，Family 由启发式给出）。
typedef struct _ROK_ESC_HW_INFO_OUT
{
    ROK_U32     Family;         // ROK_GPU_FAMILY_*（ABI 值，同 QUERY_PCI_ID）
    ROK_U32     Chip;           // ROK_HW_CHIP_*（精确表命中；未收录 = 0）
    ROK_CHAR8   ChipName[12];   // NUL 结尾短名
    ROK_U32     RevisionId;     // PCI revision（StartDevice 读取）
    ROK_U32     MmioMapped;     // 1 = BAR0 已映射
    ROK_U32     GrbmStatus;     // GRBM_STATUS(0x8010) 原始快照（未映射 = 0）
    ROK_U32     GuiActive;      // 解码：bit31 图形域忙
    ROK_U32     CmdFifoAvail;   // 解码：命令 FIFO 空槽数（0-31）
    ROK_U32     Reserved[2];    // 0
} ROK_ESC_HW_INFO_OUT, *PROK_ESC_HW_INFO_OUT;

// ---- CP 软复位结果（M3.2；escape 查询 11，写硬件，工具显式触发）----
// 对 GRBM_SOFT_RESET(0x8020) 做 SOFT_RESET_CP 置位→停顿→清零脉冲，
// 随后 WaitIdle。KMD 侧在 BAR0 未映射时拒绝（Status=0）。
typedef struct _ROK_ESC_HW_RESET_OUT
{
    ROK_U32 Status;             // 1 = 序列完成且 WaitIdle 通过，0 = 拒绝/失败
    ROK_U32 GrbmStatusBefore;   // 复位前 GRBM_STATUS（拒绝时为 0）
    ROK_U32 GrbmStatusAfter;    // 复位后 GRBM_STATUS
    ROK_U32 WaitIdleOk;         // 1 = 图形域回到空闲
    ROK_U32 Reserved[4];        // 0
} ROK_ESC_HW_RESET_OUT, *PROK_ESC_HW_RESET_OUT;

// ---- 寄存器/CP 状态快照（M3.4a；escape 查询 12，只读，gfxinit.c 填充）----
// 寄存器偏移见 kmd/src/regs_ts2.h（对照 Linux radeon r600d.h/evergreend.h）。
// 与 type 10 的分工：10 = 启动期识别快照（存进适配器上下文的静态值），
// 12 = 查询当下的完整寄存器面（ring 状态/ring test 结果/CP 细分状态）。
// MMIO 不可达时各寄存器字段为 0，MmioSource 如实区分三态。
// 结构必须 POD 且稳定：字段增删只能追加，两侧 C_ASSERT 防漂移。
#define ROK_HW_SRC_NONE         0UL  // 无 MMIO（未映射）
#define ROK_HW_SRC_REAL_MMIO    1UL  // 真实 BAR0 寄存器窗口
#define ROK_HW_SRC_SIM          2UL  // 模拟寄存器空间（sim.c）

typedef struct _ROK_ESC_HW_STATUS_OUT
{
    ROK_U32 MmioSource;         // ROK_HW_SRC_*
    ROK_U32 MmioLength;         // 映射窗口字节数
    ROK_U32 ConfigMemsize;      // CONFIG_MEMSIZE（0x5428，KB）
    ROK_U32 McVmFbLocation;     // MC_VM_FB_LOCATION（0x2180 原值）
    ROK_U32 FbBaseBytes;        // (McVmFbLocation & 0xFFFF) << 24
    ROK_U32 McApertureLow;      // MC_VM_SYSTEM_APERTURE_LOW（0x2190）
    ROK_U32 McApertureHigh;     // MC_VM_SYSTEM_APERTURE_HIGH（0x2194）
    ROK_U32 SrbmStatus;         // SRBM_STATUS（0x0E50）
    ROK_U32 SrbmStatus2;        // SRBM_STATUS2（0x0EC4）
    ROK_U32 GrbmStatus;         // GRBM_STATUS（0x8010）
    ROK_U32 GrbmStatus2;        // GRBM_STATUS2（0x8014）
    ROK_U32 CpStalledStat1;     // CP_STALLED_STAT1（0x8674）
    ROK_U32 CpStalledStat2;     // CP_STALLED_STAT2（0x8678）
    ROK_U32 CpBusyStat;         // CP_BUSY_STAT（0x867C）
    ROK_U32 CpStat;             // CP_STAT（0x8680）
    ROK_U32 CpMeCntl;           // CP_ME_CNTL（0x86D8）
    ROK_U32 CpRbBase;           // CP_RB_BASE（0xC100，原值 = 环地址>>8）
    ROK_U32 CpRbCntl;           // CP_RB_CNTL（0xC104）
    ROK_U32 CpRbRptr;           // CP_RB_RPTR（0x8700）
    ROK_U32 CpRbWptr;           // CP_RB_WPTR（0xC114）
    ROK_U32 ScratchUmsk;        // SCRATCH_UMSK（0x8540）
    ROK_U32 ScratchReg6;        // SCRATCH_REG6（0x8518）
    ROK_U32 ScratchReg7;        // SCRATCH_REG7（0x851C）
    ROK_U32 RingReady;          // rokmd ring 状态：1 = ME_INIT+ring test 通过
    ROK_U32 RingGpuAddrLo;      // rokmd 环总线地址低 32 位
    ROK_U32 RingSizeDwords;     // rokmd 环容量（dword）
    ROK_U32 RingTestRetries;    // ring test 轮询次数（0 = 首轮即过）
    ROK_U32 RingTestValue;      // ring test 末次读到的 scratch 值
    ROK_U32 UcodeLoaded;        // CP PFP/ME 微码已由 rokmd 写入
    ROK_U32 Reserved[3];        // 0（总长对齐 128 字节）
} ROK_ESC_HW_STATUS_OUT, *PROK_ESC_HW_STATUS_OUT;

#if defined(_WIN32)
C_ASSERT(sizeof(ROK_ESCAPE_HEADER) == 16);
C_ASSERT(sizeof(ROK_ESC_VERSION_OUT) == 16);
C_ASSERT(sizeof(ROK_ESC_PCI_ID_OUT) == 16);
C_ASSERT(sizeof(ROK_ESC_VRAM_OUT) == 24);
C_ASSERT(sizeof(ROK_ESC_CAPS_OUT) == 16);
C_ASSERT(sizeof(ROK_ESC_SEGMENT_INFO_OUT) == 32);
C_ASSERT(sizeof(ROK_ESC_SIM_STATUS_OUT) == 296);
C_ASSERT(sizeof(ROK_ESC_CCC_SETTINGS) == 32);
C_ASSERT(sizeof(ROK_ESC_ENGINE_STATUS_OUT) == 32);
C_ASSERT(sizeof(ROK_ESC_HW_INFO_OUT) == 48);
C_ASSERT(sizeof(ROK_ESC_HW_RESET_OUT) == 32);
C_ASSERT(sizeof(ROK_ESC_HW_STATUS_OUT) == 128);
#endif

// TeraScale 家族 ABI 值（QUERY_PCI_ID / QUERY_HW_INFO 的 Family 字段）。
// 精确 DID→芯片分发表与兜底启发式自 M3.1 起迁至 kmd/src/r600hw.c
// （RokHwLookupChip / RokHwFamilyHeuristic；源：Mesa r600_pci_ids.h，
// 与 umd/gl46/gl46_r600g.c 的一致性由 scripts/check_pci_tables.py 机械校验）。
// 勘误存档（v0.2.1 内联启发式时期，均已由精确表纠正）：
//   * 旧段规则曾把 0x6840 标为 Seymour —— 实为 Thames/TURKS 段；
//   * Mesa 表无 SEYMOUR 组：0x6760-0x677B 全归 CAICOS（HD 6490M = 0x6766
//     亦在其中），实机 HD 7470M 候选 DID（0x6760 / 0x6779）均判 TS2；
//   * 22 用例家族回归见 scripts/test_family.c（已迁移至新 API）。
#define ROK_GPU_FAMILY_TS1  1
#define ROK_GPU_FAMILY_TS2  2
#define ROK_GPU_FAMILY_TS3  3
