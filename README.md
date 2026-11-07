# radeon-windows

> Drivers compatible with DirectX 12, Vulkan 1.0, WDDM 2.0, and OpenGL 4.6
> for HD 6490M or 7470M devices.（起点：HD 6490M / 7470M；现已扩展到 TeraScale 全家族）

把 Linux 上成熟的 AMD 开源显卡栈（radeon 内核驱动 + Mesa 系用户态驱动）带回到
Windows。目标 API：**Vulkan 1.0（terakan 路线）→ OpenGL / DX12 → 自研 WDDM 2.0
内核驱动**。

## 支持的显卡

INF 覆盖 **AMD TeraScale 全家族 306 个设备 ID**（提取自 Linux 内核
`include/drm/drm_pciids.h` 的 radeon_PCI_IDS 表）：

| 家族 | 代表产品 | 设备数 |
| --- | --- | --- |
| TeraScale 1 | HD 2000–4000（R600–RV770） | 134 |
| TeraScale 2 Evergreen | HD 5000（Cedar–Hemlock） | 43 |
| TeraScale 2 APU | Fusion C/E 系列（Palm/Sumo） | 23 |
| TeraScale 2 Northern Islands | HD 6400–6900 / 7400（Barts/Turks/**Caicos**） | 54 |
| TeraScale 3 | HD 6900（Cayman）+ Trinity/Richland APU | 52 |

主要验证对象：**HD 6490M (Seymour, 0x6840/0x6841)** 与 **HD 7470M
(Caicos, 0x6779/0x677B)**。M3+ 的硬件能力按家族推进，Seymour/Caicos 优先。

## 版本上限（WDDM / Vulkan 怎么选）

详见 [docs/wddm-vulkan-limits.md](docs/wddm-vulkan-limits.md)，结论：

- **WDDM**：OS 侧最新为 3.2（Win11 24H2）；本项目 KMD 锁定 **2.0 基线**
  （Win10/11 全系兼容、与 Linux TTM/IB 模型对齐最好、新特性对 VLIW 无收益）。
- **Vulkan**：规范最新 1.4；TeraScale 上 terakan 现实达成 **1.0**；
  M5 评估冲 **1.1**（附带收益：Zink 可给到 GL 4.5；GL 4.6 需 Vulkan 1.3，不设为目标）。

## 现实约束（先读这个）

不同 API 对硬件代际的要求不同，下方是**当前开源生态的真实上限**（详见
[docs/api-matrix.md](docs/api-matrix.md)）：

| API | TeraScale 2（Seymour/Caicos） | 备注 |
| --- | --- | --- |
| Vulkan 1.0 | ✅ terakan（Linux + Windows 均已演示） | Triang3l，MIT，Mesa 树 |
| OpenGL 4.6 | ❌ r600 gallium 上限 4.5 | Windows 上需经 Zink（4.6 要求 Vulkan 1.3+ 基座） |
| DX12 | ❌ 官方当年也只到 DX11 | 开源路径需 Vulkan 1.3 基座（vkd3d-proton 式转换层） |
| WDDM 2.0 KMD | 🚧 骨架已就位 ← **本项目核心攻坚点** | DDI 表完整（M2.6）；硬件能力 M3 移植 |

结论：**Vulkan 1.0 是唯一已经打通的开源 API**；WDDM 2.0 KMD 是最大的缺口；
GL 4.6 与 DX12 受底层 Vulkan 版本约束，属长线目标（路线图 M5/M6 评估）。

## 仓库结构

```
radeon-windows/
├── docs/
│   ├── architecture.md        # 总体架构：WDDM 2.0 / terakan / 技术路线
│   ├── roadmap.md             # M0–M6 分阶段计划
│   ├── api-matrix.md          # API × GPU 代际 兼容矩阵（含来源）
│   ├── wddm-vulkan-limits.md  # WDDM / Vulkan 版本上限调研
│   ├── simulated-device.md    # 模拟设备 VM 运行手册（QEMU ati-vga 等）
│   ├── ccc.md                 # 官方 CCC 设置兼容（值名映射 / ADL shim）
│   ├── research/terakan.md    # terakan 调研笔记
│   └── licensing.md           # MIT / GPL-2.0 许可证分析
├── kmd/                       # 自研 WDDM 2.0 显示微型端口（KMD）
│   ├── rokmd.vcxproj          #   VS2022 + WDK NuGet 工程
│   ├── rokmd.inf              #   安装 INF（TeraScale 306 设备 + 模拟设备段）
│   └── src/                   #   DriverEntry + DXGK DDI
│       ├── sim.c              #   模拟设备后端（VM 里跑通管线，PM4 解码前哨）
│       └── ccc.c              #   CCC 设置兼容层（注册表导入 + escape get/set）
├── umd/
│   ├── README.md              # 用户态驱动（terakan 集成）计划 + KMD⇄ICD 契约
│   ├── icd/                   # terakan ICD manifest
│   ├── install-terakan.ps1    # ICD 安装脚本（loader 注册表机制）
│   └── adl/                   # ADL 兼容 shim（rok_adl.dll，CCC 生态桥梁）
├── tests/
│   ├── gpu_probe/             # 探测工具（PCI / WDDM / hypervisor / Vulkan / escape）
│   ├── vk_smoke/              # Vulkan compute 端到端冒烟
│   └── adl_smoke/             # ADL shim 链接冒烟
├── scripts/
│   └── gen_spirv.py           # vk_smoke 依赖的 SPIR-V 生成器
├── .github/workflows/
│   ├── build-kmd.yml          # Windows Runner + WDK NuGet 自动编译 .sys
│   ├── build-gpu-probe.yml    # gpu_probe 双端构建
│   ├── build-vk-smoke.yml     # vk_smoke 双端构建
│   └── build-adl.yml          # rok_adl.dll 双工具链构建
├── packages.config            # WDK NuGet 包清单
└── Directory.Build.props      # MSBuild 导入 + WDK NuGet 路径修正
```

## 快速开始

### 1. 编译内核驱动骨架（Windows / GitHub Actions）

本地（VS2022 + WDK NuGet，无需安装完整 WDK）：

```bat
nuget restore .\packages.config -PackagesDirectory .\packages\
msbuild /t:build kmd\rokmd.vcxproj /p:Configuration=Release /p:Platform=x64
```

云端：推送后 `.github/workflows/build-kmd.yml` 自动在 `windows-2022` 上产出
`rokmd.sys`（见 Actions 产物）。

### 2. gpu_probe（探测工具）

仓库内 `tests/gpu_probe/gpu_probe.exe` 可直接在目标机上运行（Win10 x64），输出：

- PCI 显示设备（Vendor 0x1002 = AMD、**实际 Device ID**、驱动版本）
- WDDM 适配器（LUID + 用户态驱动 DLL 名）
- Vulkan：Loader 是否存在、物理设备表（apiVersion / deviceName）

装 terakan / 自研 KMD 前后各跑一遍，是判断"系统看到了什么"的基准。

### 3. 安装 terakan（用户态 Vulkan，先行验证硬件）

见 [docs/research/terakan.md](docs/research/terakan.md)。这一步**不依赖本项目 KMD**，
用于先把 Vulkan 1.0 在目标卡上跑起来（vkcube）。

## 分阶段路线图（摘要）

| 阶段 | 目标 | 状态 |
| --- | --- | --- |
| M0 | 仓库 / CI / 探测工具 / 文档 | ✅（CI 绿灯，rokmd.sys 产出） |
| M1 | terakan Windows 构建管线（CI 编 ICD） | 🚧 ICD 安装管线与 vk_smoke 就位，terakan 本体待 CI 编译 |
| M2 | KMD 骨架可加载（AddDevice/StartDevice/资源映射） | 🚧 CI 绿灯产出 .sys；**M2.5** 真实最小实现、**M2.6** 0x5023 DDI 表补全 + Escape v1 双模式、**M2.7** 模拟设备 + CCC 设置兼容落地；待实机/VM 加载验证 |
| M3 | KMD 基础能力：VRAM 管理 / 驻留 / PM4 ring 提交 | ⬜（Seymour/Caicos 优先；sim.c 的 PM4 解码器已就位） |
| M4 | terakan 摆脱官方 KMD 依赖 → 接自研 KMD | ⬜（escape 契约头已交付） |
| M5 | Zink（GL）可行性验证：Vulkan 1.1 → GL 4.5 | ⬜ |
| M6 | DX12 层评估（受 Vulkan 1.3 前置约束） | ⬜ |

完整定义见 [docs/roadmap.md](docs/roadmap.md)；下一批可支持目标的评估见
[docs/api-matrix.md](docs/api-matrix.md) 第 5 节。

## 许可证

- 本仓库：MIT（见 [LICENSE](LICENSE)）
- 从 Linux 内核（radeon，GPL-2.0）移植的代码段：GPL-2.0，分析见
  [docs/licensing.md](docs/licensing.md)
- terakan / Mesa：MIT

## 参考与致谢

- [Triang3l/mesa（freedesktop GitLab 分支树）](https://gitlab.freedesktop.org/Triang3l/mesa) — terakan 驱动本体
- [Phoronix: Terakan Vulkan Driver For Radeon HD 6000 Series Shown On Windows](https://www.phoronix.com/news/Terakan-HD-6000-VLK-Windows)（2024-04-20）
- [Microsoft: Building Windows Driver Projects with CI and CD](https://techcommunity.microsoft.com/blog/windowsdriverdev/building-windows-driver-projects-with-ci-and-cd/4379200)（2025-02-14，WDK NuGet CI 方案）
- [Microsoft Learn: What's new for Windows 11 graphics display drivers](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/what-s-new-for-windows-11-display-and-graphics-drivers)（WDDM 3.2）
- [Khronos: Vulkan 1.4 发布公告](https://www.khronos.org/news/press/khronos-streamlines-development-and-deployment-of-gpu-accelerated-applications-with-vulkan-1.4)
- [microsoft/Windows-driver-samples](https://github.com/microsoft/Windows-driver-samples) — KMD DDI 结构参考
- [Microsoft Learn: Display & WDDM 文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/) — DXGK DDI 官方定义
- Linux 内核 `drivers/gpu/drm/radeon`（GPL-2.0）— TeraScale 硬件编程序列与 PCI 表来源
