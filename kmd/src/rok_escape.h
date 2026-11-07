// SPDX-License-Identifier: MIT
// rok_escape.h — rokmd 私有 Escape 通道 ABI（KMD ⇄ 用户态 ICD/UMD）
//
// 用途与边界：
//   * WDDM 2.0 下，用户态驱动（Vulkan ICD / D3D UMD）通过
//     D3DKMTEscape 与 KMD 通信，OS 侧经 DxgkDdiEscape 转交。
//   * 本文件是 M4 阶段 terakan ICD 接自研 KMD 的**契约头**：
//     查询类（版本/PCI/显存/能力）现在即可用；提交类在 M3 的
//     ring/驻留能力落地后追加（版本号递增保证向后兼容）。
//   * 结构体布局按 x64 自然对齐设计，两端共用本头文件编译。
#pragma once

#include <ntddk.h>

// 魔数 "AROK"（内存中小端序显示为 41 52 4F 4B）。
#define ROK_ESCAPE_MAGIC        0x4B4F5241UL
// 协议版本：查询类通道 v1。M3/M4 追加提交类接口时升 v2 并保留 v1 查询。
#define ROK_ESCAPE_VERSION      1UL

// Escape 类型（WDDM 2.0 骨架已实现的查询面）。
typedef enum _ROK_ESCAPE_TYPE
{
    ROK_ESC_TYPE_QUERY_VERSION  = 1,    // -> ROK_ESC_VERSION_OUT
    ROK_ESC_TYPE_QUERY_PCI_ID   = 2,    // -> ROK_ESC_PCI_ID_OUT
    ROK_ESC_TYPE_QUERY_VRAM     = 3,    // -> ROK_ESC_VRAM_OUT（M3 前全 0）
    ROK_ESC_TYPE_QUERY_CAPS     = 4,    // -> ROK_ESC_CAPS_OUT
} ROK_ESCAPE_TYPE;

// 能力位（ROK_ESC_CAPS_OUT.CapsMask）。
#define ROK_CAP_MMIO_MAPPED         0x00000001UL  // BAR0 已映射（StartDevice 成功）
#define ROK_CAP_PCI_ID_VALID        0x00000002UL  // PCI 标识已读取
#define ROK_CAP_PM4_SUBMIT          0x00000100UL  // M3：ring 提交可用
#define ROK_CAP_IH_INTERRUPT        0x00000200UL  // M3：IH 中断可用
#define ROK_CAP_VRAM_SEGMENT        0x00000400UL  // M3：VRAM 段可用

// ---- 报文头（每个 escape 缓冲区必须以其开头）----
typedef struct _ROK_ESCAPE_HEADER
{
    UINT32 Magic;           // = ROK_ESCAPE_MAGIC
    UINT32 Version;         // = ROK_ESCAPE_VERSION
    UINT32 Type;            // ROK_ESCAPE_TYPE
    UINT32 Reserved;        // 0
} ROK_ESCAPE_HEADER, *PROK_ESCAPE_HEADER;

// ---- 查询输出（紧随报文头之后写入，缓冲区布局 [HEADER][OUT]，
//      调用方传入的 PrivateDriverDataSize 须 >= 16 + sizeof(OUT)）----

typedef struct _ROK_ESC_VERSION_OUT
{
    UINT32 Major;
    UINT32 Minor;
    UINT32 Patch;
    UINT32 WddmInterfaceVersion;    // DXGKDDI_INTERFACE_VERSION（0x5023 = WDDM 2.0）
} ROK_ESC_VERSION_OUT, *PROK_ESC_VERSION_OUT;

typedef struct _ROK_ESC_PCI_ID_OUT
{
    UINT32 VendorId;
    UINT32 DeviceId;
    UINT32 RevisionId;
    UINT32 Family;          // 1=TeraScale1 2=TeraScale2 3=TeraScale3 0=未知
} ROK_ESC_PCI_ID_OUT, *PROK_ESC_PCI_ID_OUT;

typedef struct _ROK_ESC_VRAM_OUT
{
    UINT64 VramSizeBytes;   // M3 前为 0
    UINT64 GartSizeBytes;   // M3 前为 0
    UINT32 VramSegmentId;   // QuerySegment 上报的段号；0 = 暂无段
    UINT32 Reserved;
} ROK_ESC_VRAM_OUT, *PROK_ESC_VRAM_OUT;

typedef struct _ROK_ESC_CAPS_OUT
{
    UINT32 CapsMask;
    UINT32 Pm4PacketVersion;    // TeraScale PM4 = 3（type-3 包）；未探测时 0
    UINT32 Reserved[2];
} ROK_ESC_CAPS_OUT, *PROK_ESC_CAPS_OUT;

#if defined(_WIN32)
C_ASSERT(sizeof(ROK_ESCAPE_HEADER) == 16);
C_ASSERT(sizeof(ROK_ESC_VERSION_OUT) == 16);
C_ASSERT(sizeof(ROK_ESC_PCI_ID_OUT) == 16);
C_ASSERT(sizeof(ROK_ESC_VRAM_OUT) == 24);
C_ASSERT(sizeof(ROK_ESC_CAPS_OUT) == 16);
#endif

// TeraScale 家族启发式（M3 用完整分发表替换）。
// 依据 Linux radeon PCI 表的 ID 段分布：
//   0x6700-0x671F / 0x9900-0x99AF -> TeraScale 3（Cayman / Trinity）
//   0x67xx(高段)/0x68xx/0x94xx-0x97xx/0x98xx -> TeraScale 1/2
//   其余（0x79xx R600 早期）-> TeraScale 1
#define ROK_GPU_FAMILY_TS1  1
#define ROK_GPU_FAMILY_TS2  2
#define ROK_GPU_FAMILY_TS3  3

__inline
ULONG
RokGpuFamilyFromDeviceId(_In_ ULONG DeviceId)
{
    switch (DeviceId)
    {
    case 0x6840:    // Seymour (HD 6490M) — 主验证卡
    case 0x6841:
    case 0x6779:    // Caicos (HD 7470M) — 主验证卡
    case 0x677B:
        return ROK_GPU_FAMILY_TS2;
    default:
        break;
    }
    if ((DeviceId >= 0x6700 && DeviceId <= 0x671F) ||
        (DeviceId >= 0x9900 && DeviceId <= 0x99AF))
    {
        return ROK_GPU_FAMILY_TS3;
    }
    if (DeviceId >= 0x9400 && DeviceId <= 0x9900)
    {
        return ROK_GPU_FAMILY_TS2;      // Evergreen / Northern Islands 主段
    }
    return ROK_GPU_FAMILY_TS1;          // R600-RV770 主段
}
