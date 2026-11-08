# radeon-windows

> Drivers compatible with DirectX 12, Vulkan 1.0→1.3, WDDM 2.0, and OpenGL 4.6
> for HD 6490M or 7470M devices.（起点：HD 6490M / 7470M；现已扩展到 TeraScale 全家族）

把 Linux 上成熟的 AMD 开源显卡栈（radeon 内核驱动 + Mesa 系用户态驱动）带回到
Windows。目标 API：**Vulkan 1.0（terakan）→ 冲 1.3（M5.5）；OpenGL 4.6 用
R600g 原生移植（M7，Zink 备选）；DX12 → 自研 WDDM 2.0 内核驱动**。

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
  M5.5 逐特性冲 **1.3**（promoted 23/24/23 + 核心必选 24 项，见
  [docs/vulkan13-plan.md](docs/vulkan13-plan.md)）。
- **OpenGL**：**GL 4.6 主路线 = R600g 原生移植**（Mesa 25.2 起 r600 在
  evergreen+ 完成 GL 4.6，与 Vulkan 版本解耦，见
  [docs/r600g-gl46.md](docs/r600g-gl46.md)）；Zink（GL-on-Vulkan）为备选，
  前置 Vulkan 1.3。

## 现实约束（先读这个）

不同 API 对硬件代际的要求不同，下方是**当前开源生态的真实上限**（详见
[docs/api-matrix.md](docs/api-matrix.md)）：

| API | TeraScale 2（Seymour/Caicos） | 备注 |
| --- | --- | --- |
| Vulkan 1.0 | ✅ terakan（Linux + Windows 均已演示） | Triang3l，MIT，Mesa 树 |
| Vulkan 1.3 | ⚠️ M5.5 目标（24 项核心特性差距） | 逐项计划见 docs/vulkan13-plan.md |
| OpenGL 4.6 | ✅ **R600g 原生移植**（Mesa 25.2 起 r600/evergreen+ 完成） | 主路线；Zink（需 Vulkan 1.3）为备选 |
| DX12 | ❌ 官方当年也只到 DX11 | 开源路径需 Vulkan 1.3 基座（vkd3d-proton 式转换层） |
| WDDM 2.0 KMD | 🚧 骨架已就位 ← **本项目核心攻坚点** | DDI 表完整（M2.8：0x5023 全量无 NULL）；硬件能力 M3 移植 |

结论：**Vulkan 1.0 已打通、GL 4.6 主路线已锁定（R600g）**；WDDM 2.0 KMD 是
最大的缺口；Vulkan 1.3 与 DX12 按 M5.5/M6 推进。

## 仓库结构

```
radeon-windows/
├── docs/
│   ├── architecture.md        # 总体架构：WDDM 2.0 / terakan / 技术路线
│   ├── roadmap.md             # M0–M7 分阶段计划（含 M5.5 / M7）
│   ├── vulkan13-plan.md       # Vulkan 1.0→1.3 逐特性计划 + VLIW 可行性
│   ├── r600g-gl46.md          # GL 4.6：R600g 主路线 + Zink 备选
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
│   ├── README.md              # 用户态驱动计划（terakan 集成 + 门控模块）
│   ├── vk13/                  # Vulkan 1.3 门控：promoted 表 + 必选特性 + 层级判定
│   └── gl46/                  # GL 4.6 门控：R600g 主路线 + Zink 备选
├── tests/
│   ├── gpu_probe/             # PCI / WDDM / Vulkan 三段探测工具（M0）
│   └── umd/                   # vk13/gl46 门控模块单元测试（85 项）
├── tools/vk_audit/            # vk_audit.exe：1.1/1.2/1.3 缺口 + 双 GL 路线审计
├── .github/workflows/
│   ├── build-kmd.yml          # Windows Runner + WDK NuGet 自动编译 .sys
│   ├── build-gpu-probe.yml    # gpu_probe 双端构建
│   └── build-umd.yml          # umd 单测 + vk_audit（mingw + MSVC 双验证）
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

### 4. vk_audit（Vulkan 1.3 / GL 4.6 能力审计）

装 terakan（或任意 ICD）前后各跑一遍，直接回答两个问题：

- 当前 ICD 距离 Vulkan 1.1 / 1.2 / 1.3 差哪些核心特性（逐项列出）；
- 这块 GPU 在两条 OpenGL 4.6 路线上的判定（R600g 主路线按芯片代际；
  Zink 备选按 Vulkan 1.3 基座逐项报缺口）。

```bat
vk_audit.exe            :: 全量报告
vk_audit.exe --strict   :: 有缺口时返回非零退出码（CI 友好）
```

产物由 `build-umd.yml` 每次提交自动构建（Actions → Artifacts）；
本地复现见 `tools/vk_audit/build-mingw.sh`。

## 分阶段路线图（摘要）

| 阶段 | 目标 | 状态 |
| --- | --- | --- |
| M0 | 仓库 / CI / 探测工具 / 文档 | ✅（CI 绿灯，rokmd.sys 产出） |
| M1 | terakan Windows 构建管线（CI 编 ICD） | 🚧 |
| M2 | KMD 骨架可加载（AddDevice/StartDevice/资源映射） | 🚧 骨架编译通过（CI 产出 .sys）；M2.8：0x5023 DDI 表全量补齐，待实机加载验证 |
| M3 | KMD 基础能力：VRAM 管理 / 驻留 / PM4 ring 提交 | ⬜（Seymour/Caicos 优先） |
| M4 | terakan 摆脱官方 KMD 依赖 → 接自研 KMD | ⬜ |
| M5 | OpenGL 可行性评估（存档，结论并入 M7） | ⬜ |
| M5.5 | terakan 冲 Vulkan 1.3（门控模块 + vk_audit 已就绪） | ⬜ |
| M6 | DX12 层评估（受 Vulkan 1.3 前置约束） | ⬜ |
| M7 | R600g 原生移植 → OpenGL 4.6（Zink 备选） | ⬜ 门控代码已交付 |

完整定义见 [docs/roadmap.md](docs/roadmap.md)；下一批可支持目标的评估见
[docs/api-matrix.md](docs/api-matrix.md) 第 5 节。

## 许可证

- 本仓库：MIT（见 [LICENSE](LICENSE)）
- 从 Linux 内核（radeon，GPL-2.0）移植的代码段：GPL-2.0，分析见
  [docs/licensing.md](docs/licensing.md)
- terakan / Mesa：MIT

## 参考与致谢

- [Triang3l/mesa（freedesktop GitLab 分支树）](https://gitlab.freedesktop.org/Triang3l/mesa) — terakan 驱动本体
- [Mesa features.txt（GL 4.6：r600/evergreen+ 已完成）](https://docs.mesa3d.org/features.html)
- [Phoronix: OpenGL 4.6 Now Exposed For Old AMD Evergreen & Northern Islands GPUs](https://www.phoronix.com/news/OpenGL-4.6-For-Radeon-R600g)（Mesa 25.2，2025-06-12）
- [Mesa VP_ZINK_requirements.json（Zink GL 4.6 机器可读门控）](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/util/VP_ZINK_requirements.json)
- [KhronosGroup/Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) — vk_audit 构建期引用（v1.4.330）
- [Phoronix: Terakan Vulkan Driver For Radeon HD 6000 Series Shown On Windows](https://www.phoronix.com/news/Terakan-HD-6000-VLK-Windows)（2024-04-20）
- [Microsoft: Building Windows Driver Projects with CI and CD](https://techcommunity.microsoft.com/blog/windowsdriverdev/building-windows-driver-projects-with-ci-and-cd/4379200)（2025-02-14，WDK NuGet CI 方案）
- [Microsoft Learn: What's new for Windows 11 graphics display drivers](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/what-s-new-for-windows-11-display-and-graphics-drivers)（WDDM 3.2）
- [Khronos: Vulkan 1.4 发布公告](https://www.khronos.org/news/press/khronos-streamlines-development-and-deployment-of-gpu-accelerated-applications-with-vulkan-1.4)
- [microsoft/Windows-driver-samples](https://github.com/microsoft/Windows-driver-samples) — KMD DDI 结构参考
- [Microsoft Learn: Display & WDDM 文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/) — DXGK DDI 官方定义
- Linux 内核 `drivers/gpu/drm/radeon`（GPL-2.0）— TeraScale 硬件编程序列与 PCI 表来源
