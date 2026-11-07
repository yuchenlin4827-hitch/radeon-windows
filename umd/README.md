# 用户态驱动（UMD / ICD）—— terakan 集成计划

本目录承载两件事：

1. **terakan 的 CI 构建与打包**（M1）
   - 源：`https://gitlab.freedesktop.org/Triang3l/mesa`（terakan 子树）
   - 产物：`terakan_icd.dll` + `terakan_icd.x86_64.json`（Vulkan ICD 清单）
   - 安装：`install-terakan.ps1`（复制 DLL、写 ICD JSON、注册到
     `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` —— Vulkan loader 的 Windows
     ICD 发现机制），或临时设置环境变量
     `VK_ICD_FILENAMES=<json 路径>` 直接测试；
   - 验证：`tests/vk_smoke/vk_smoke.exe`（端到端 compute，PASS = 全链路
     打通）→ `vkcube`（M1 验收门）。
   - 调研与依赖边界结论见 [../docs/research/terakan.md](../docs/research/terakan.md)

2. **M4：terakan winsys → 自研 rokmd 对接层**
   - 目标：Vulkan ICD 不再依赖官方 Catalyst KMD，命令提交/驻留全部走
     `kmd/` 的 WDDM 2.0 DDI。
   - 前置：M3 完成（KMD 具备 ring 提交与驻留能力）。

## KMD ⇄ ICD 私有通道（M4 契约，现已可用）

KMD 通过 `DxgkDdiEscape` 提供**版本化私有查询通道**，契约头
[kmd/src/rok_escape.h](../kmd/src/rok_escape.h)（KMD 与 ICD 共用同一头文件编译）：

| Escape 类型 | 输出 | 说明 |
| --- | --- | --- |
| `ROK_ESC_TYPE_QUERY_VERSION` | `ROK_ESC_VERSION_OUT` | 驱动版本 + WDDM DDI 版本 |
| `ROK_ESC_TYPE_QUERY_PCI_ID` | `ROK_ESC_PCI_ID_OUT` | VID/DID/REV + TeraScale 家族（1/2/3） |
| `ROK_ESC_TYPE_QUERY_VRAM` | `ROK_ESC_VRAM_OUT` | 显存/GTT 尺寸（M3 前为 0） |
| `ROK_ESC_TYPE_QUERY_CAPS` | `ROK_ESC_CAPS_OUT` | 能力位（MMIO/PM4/IH/VRAM/SIM 就绪标记） |
| `ROK_ESC_TYPE_QUERY_SIM_STATUS` | `ROK_ESC_SIM_STATUS_OUT` | 模拟设备状态 + PM4 解码统计（M2.6） |
| `ROK_ESC_TYPE_CCC_GET/SET_SETTINGS` | `ROK_ESC_CCC_SETTINGS` | CCC 设置兼容读/写（M2.6，见 docs/ccc.md） |

报文格式：缓冲区 = `[ROK_ESCAPE_HEADER][OUT]`（16 字节头 + 输出体），
magic = `0x4B4F5241`，version = 1。IUserModeDriver 经
`D3DKMTEscape(hAdapter, ...)` 调用，OS 转交 KMD。

**已打通的 KMD 能力面**（M2.5，2026-11）：

- 适配器加载/启动：PCI 标识读取（DxgkCbReadDeviceSpace）、BAR0 MMIO 映射
  （DxgkCbMapMemory）、LUID 日志；
- 分配生命周期：CreateAllocation/DestroyAllocation/OpenAllocation/
  CloseAllocation 簿记（0 段模式下 VidMm 以系统内存承载）；
- 上下文与提交：CreateContext（64KB DMA buffer、免 patch）→
  SubmitCommand/SubmitCommandVirtual **即时完成模型**（fence 立即置位，
  OS 经 QueryCurrentFence 轮询回收，无 IH 前不会假死/TDR）；
- BuildPagingBuffer：写入 TeraScale PM4 type-3 NOP（0xC0001000），
  M3 在此生成 r600 VM 页表更新包。

M4 时 ICD 的命令流（PM4 INDIRECT_BUFFER）经 `SubmitCommandVirtual`
提交，进度经 escape 通道的 fence 查询对齐 —— 即 terakan winsys 的
自研 KMD 后端形态。

## 当前状态

- ICD JSON 模板 + 安装脚本已就位（本目录）。
- **ADL 兼容 shim（adl/）已就位**：官方 CCC 生态的设置习惯桥接到 rokmd
  （ADL2 同名枚举面 + Overdrive 设置桥），详见
  [adl/README.md](adl/README.md) 与 [../docs/ccc.md](../docs/ccc.md)。
- M1 启动前 terakan 本体仍需从 Triang3l/mesa 构建（CI 管线待建）。

### 为什么用户态先行

terakan 是纯用户态 DLL（Vulkan ICD），崩溃不蓝屏、迭代快、可独立验证硬件。
先让 Vulkan 1.0 出画面，再啃内核 —— 风险曲线最平滑。

## ICD ⇄ KMD 对接契约（M4 前置，已冻结的部分）

### 1. 共享 ABI 头

`kmd/src/rok_escape.h` 是**双模式契约头**：内核侧（precomp.h 定义
`ROK_KERNEL_BUILD`）与用户态侧（ICD / gpu_probe，走 stdint）编译同一份
结构体布局，`C_ASSERT` 防字段漂移。ICD 集成时直接包含该文件，不要复制。

### 2. Escape 通道（v1 = 查询面，已实现）

用户态经 `D3DKMTEscape`（gdi32.dll 导出）发起，KMD 在 `DxgkDdiEscape`
应答。缓冲区布局 = `[ROK_ESCAPE_HEADER][OUT]`，大小须 ≥ `16 + sizeof(OUT)`：

| Type | 输出结构 | 说明 |
| --- | --- | --- |
| `ROK_ESC_TYPE_QUERY_VERSION` (1) | `ROK_ESC_VERSION_OUT` | 驱动版本 + WDDM DDI 版本（0x5023） |
| `ROK_ESC_TYPE_QUERY_PCI_ID` (2) | `ROK_ESC_PCI_ID_OUT` | VID/DID/REV + 家族（1=TS1 2=TS2 3=TS3） |
| `ROK_ESC_TYPE_QUERY_VRAM` (3) | `ROK_ESC_VRAM_OUT` | 旧接口，改用 (5) |
| `ROK_ESC_TYPE_QUERY_CAPS` (4) | `ROK_ESC_CAPS_OUT` | 能力位掩码 + PM4 包版本（3 = type-3） |
| `ROK_ESC_TYPE_QUERY_SEGMENT_INFO` (5) | `ROK_ESC_SEGMENT_INFO_OUT` | VRAM/GTT 字节数 + 段号 + 探测标志位 |

- `hDevice=0` 的适配器级调用同样可用（KMD 回退到适配器上下文）。
- escape `Flags.HardwareAccess = 1`（KMD 需要向缓冲区写输出）。
- 能力位：`ROK_CAP_MMIO_MAPPED / PCI_ID_VALID / SEGMENT_QUERY` 现已置位；
  `ROK_CAP_PM4_SUBMIT / IH_INTERRUPT / VRAM_SEGMENT` 随 M3 置位。
- M3 落地 VRAM/GTT 段后，ICD 用 `QUERY_SEGMENT_INFO` 的结果构造
  `VkMemoryHeap` 布局（deviceLocal 堆 = VRAM 段）。

### 3. 提交/驻留路径（WDDM 2.0 原生，M3/M4 落地）

ICD 不走私有 escape 提交命令；标准通道即 WDDM 2.0 的 KMT/DDI 面，KMD 侧
已全部就位（M2.6 DDI 表补全）：

| 用户态调用（d3dkmthk） | KMD DDI | 现状 |
| --- | --- | --- |
| `D3DKMTCreateDevice` | `DxgkDdiCreateDevice` | ✅ 簿记完整 |
| `D3DKMTCreateContextVirtual` | `DxgkDdiCreateContext` | ✅ 即时完成模型 |
| `D3DKMTCreateAllocation` | `DxgkDdiCreateAllocation` | ✅ 0 段（系统内存承载） |
| `D3DKMTOpenAllocation` | `DxgkDdiOpenAllocation` | ✅ GetHandleData 语义已修正 |
| `D3DKMTMakeResident/Evict` | VidMm → `DxgkDdiBuildPagingBuffer` | ⬜ M3 引入段后生效 |
| `D3DKMTSubmitCommandVirtual` | `DxgkDdiSubmitCommandVirtual` | ✅ 即时完成（fence 轮询） |
| `D3DKMTEscape` | `DxgkDdiEscape` | ✅ v1 查询面（见上表） |

### 4. 实机验证

`tests/gpu_probe/gpu_probe.exe` 的 **[4] rokmd escape channel** 段使用与
ICD 完全相同的 ABI（同一契约头编译）走 `D3DKMTCreateDevice → Escape ×5 →
DestroyDevice`。装上 `rokmd.sys` 后运行：

```
gpu_probe.exe
  [4] rokmd escape channel (D3DKMTEscape, AROK magic)
    ---- adapter #0 (hAdapter=0x...) ----
    QUERY_VERSION: OK  driver v0.1.0  WDDM DDI=0x5023
                   -> WDDM 2.0 interface confirmed
    ...
```

非 rokmd 适配器上 KMD 因魔数不符拒绝（`status != 0`），属预期输出。
