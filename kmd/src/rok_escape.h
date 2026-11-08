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

#else  /* 用户态（Windows / MinGW / clang-cl） */

#include <stdint.h>
typedef uint32_t ROK_U32;
typedef uint64_t ROK_U64;

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
// 均向后兼容：旧调用方不感知新增类型）。
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
#endif

// TeraScale 家族启发式（M3 用完整分发表替换）。
// 依据 Linux 内核 radeon drm_pciids.h / pci.ids 核对（v0.2.1 实测修正：旧表
// 把 Seymour 误标 0x6840 —— 那是 Thames 段；实机 HD 7470M = 0x6760 曾被
// 误判 TS1）。
//   0x6700-0x671F / 0x9900-0x99AF -> TeraScale 3（Cayman / Trinity）
//   0x6750-0x675F（Turks/Lombok）、0x6760-0x677F（Seymour/Caicos）、
//   0x6840-0x6849（Thames）、0x9400-0x98FF（Evergreen/NI/Sumo） -> TeraScale 2
//   其余（0x79xx R600 早期、RV770 等）-> TeraScale 1
#define ROK_GPU_FAMILY_TS1  1
#define ROK_GPU_FAMILY_TS2  2
#define ROK_GPU_FAMILY_TS3  3

static __inline
ROK_U32
RokGpuFamilyFromDeviceId(ROK_U32 DeviceId)
{
    switch (DeviceId)
    {
    case 0x6760:    // Seymour (HD 6400M/7400M) — 主验证卡（实机 HD 7470M DID）
    case 0x6761:
    case 0x6766:    // Caicos
    case 0x6767:
    case 0x6768:
    case 0x6770:
    case 0x6771:
    case 0x6772:
    case 0x6778:    // Caicos XT (HD 7470/8470 OEM)
    case 0x6779:    // Caicos (HD 7470M) — 主验证卡
    case 0x677B:
    case 0x6840:    // Thames (HD 7600M/7700M)——旧表曾误标 Seymour
    case 0x6841:
        return ROK_GPU_FAMILY_TS2;
    default:
        break;
    }
    if ((DeviceId >= 0x6700 && DeviceId <= 0x672F) ||      // Cayman/Blackcomb
        (DeviceId >= 0x9900 && DeviceId <= 0x99AF))        // Trinity
    {
        return ROK_GPU_FAMILY_TS3;
    }
    if ((DeviceId >= 0x6730 && DeviceId <= 0x674F) ||   // Barts（HD 6870/6850）
        (DeviceId >= 0x6750 && DeviceId <= 0x677F) ||   // Turks/Lombok/Seymour/Caicos
        (DeviceId >= 0x6840 && DeviceId <= 0x68FF) ||   // Thames/Evergreen/Cedar
        (DeviceId >= 0x9440 && DeviceId <= 0x94BF) ||   // RV770/RV730/RV740
        (DeviceId >= 0x9540 && DeviceId <= 0x955F) ||   // RV730/RV740 变体
        (DeviceId >= 0x9640 && DeviceId <= 0x964F))     // Sumo（Llano 内显）
    {
        return ROK_GPU_FAMILY_TS2;
    }
    return ROK_GPU_FAMILY_TS1;          // R600/RV6xx/RV670/RS780/RS880 主段
}
