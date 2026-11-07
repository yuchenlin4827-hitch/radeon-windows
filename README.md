# hd-6490_7470_M-windows-driver

> Drivers compatible with DirectX 12, Vulkan 1.0, WDDM 2.0, and OpenGL 4.6
> for HD 6490M or 7470M devices.（目标：HD 6490M / 7470M）

把 Linux 上成熟的 AMD 开源显卡栈（radeon 内核驱动 + Mesa 系用户态驱动）带回到
Windows。目标 API：**Vulkan 1.0（terakan 路线）→ OpenGL / DX12 → 自研 WDDM 2.0
内核驱动**。

## 目标硬件

| 卡型 | 核心 | 代际 | PCI Device ID* |
| --- | --- | --- | --- |
| Radeon HD 6490M | Seymour | TeraScale 2 (VLIW5) | 0x6840 / 0x6841 |
| Radeon HD 7470M | Caicos | TeraScale 2 (VLIW5) | 0x6779 / 0x677B |

\* 常见 ID 列表，装卡前请用 `gpu_probe.exe` 或设备管理器核对实际值。

这正是 AMD 官方 Windows 驱动支持早已冻结（~2016，Crimson Beta / Win10）的代际；
而 Linux 侧开源栈（radeon 内核驱动、Mesa r600、terakan Vulkan 1.0）仍在活跃维护。

## 现实约束（先读这个）

不同 API 对硬件代际的要求不同，下方是**当前开源生态的真实上限**（详见
[docs/api-matrix.md](docs/api-matrix.md)）：

| API | TeraScale 2（Seymour/Caicos） | 备注 |
| --- | --- | --- |
| Vulkan 1.0 | ✅ terakan（Linux + Windows 均已演示） | Triang3l，MIT，Mesa 树 |
| OpenGL 4.6 | ❌ r600 gallium 上限 4.5 | Windows 上需经 Zink（要求 Vulkan 1.3+ 基座） |
| DX12 | ❌ 官方当年也只到 DX11 | 开源路径需 Vulkan 1.3 基座（vkd3d-proton 式转换层） |
| WDDM 2.0 KMD | ❌ 缺失 ← **本项目核心攻坚点** | Linux radeon KMD 逻辑 → DXGK DDI 移植 |

结论：**Vulkan 1.0 是唯一已经打通的开源 API**；WDDM 2.0 KMD 是最大的缺口；
GL 4.6 与 DX12 受底层 Vulkan 版本约束，属长线目标（路线图 M5/M6 评估）。

## 仓库结构

```
hd-6490_7470_M-windows-driver/
├── docs/
│   ├── architecture.md        # 总体架构：WDDM 2.0 / terakan / 技术路线
│   ├── roadmap.md             # M0–M6 分阶段计划（当前状态：M0）
│   ├── api-matrix.md          # API × GPU 代际 兼容矩阵（含来源）
│   ├── research/terakan.md    # terakan 调研笔记
│   └── licensing.md           # MIT / GPL-2.0 许可证分析
├── kmd/                       # 自研 WDDM 2.0 显示微型端口（KMD）骨架
│   ├── rokmd.vcxproj          #   VS2022 + WDK NuGet 工程
│   ├── rokmd.inf              #   安装 INF（DISPLAY 类，含 Seymour/Caicos ID）
│   └── src/                   #   DriverEntry + DXGK DDI 骨架
├── umd/
│   └── README.md              # 用户态驱动（terakan 集成）计划
├── tests/gpu_probe/
│   ├── gpu_probe.c            # PCI / WDDM / Vulkan 三段探测工具
│   ├── build-mingw.sh         # Linux 下交叉编译脚本
│   └── gpu_probe.exe          # 预编译 x64 版（llvm-mingw 交叉编译）
├── .github/workflows/
│   ├── build-kmd.yml          # Windows Runner + WDK NuGet 自动编译 .sys
│   └── build-gpu-probe.yml    # gpu_probe 双端构建
├── packages.config            # WDK NuGet 包清单（微软官方 CI 方案）
└── Directory.Build.props      # MSBuild 自动导入的 NuGet props
```

## 快速开始

### 1. 编译内核驱动骨架（Windows / GitHub Actions）

本地（VS2022 + WDK NuGet，无需安装完整 WDK）：

```bat
nuget restore .\packages.config -PackagesDirectory .\packages\
msbuild /t:build kmd\rokmd.vcxproj /p:Configuration=Release /p:Platform=x64
```

云端：推送后 `.github/workflows/build-kmd.yml` 自动在 `windows-latest` 上产出
`rokmd.sys`（见 Actions 产物）。

> 骨架工程的 vcxproj / DDI 签名以 WDK 头文件为准，首次 CI 若有编译错误按报错微调即可——
> 这是骨架阶段正常的收敛过程，修复时保持 [docs/roadmap.md](docs/roadmap.md) 的阶段定义不变。

### 2. gpu_probe（探测工具）

仓库内 `tests/gpu_probe/gpu_probe.exe` 可直接在目标机上运行（Win10 x64），输出：

- PCI 显示设备（Vendor 0x1002 = AMD、**实际 Device ID**、驱动版本）
- WDDM 适配器（LUID + 用户态驱动 DLL 名）
- Vulkan：Loader 是否存在、物理设备表（apiVersion / deviceName）

装 terakan / 自研 KMD 前后各跑一遍，是判断"系统看到了什么"的基准。

### 3. 安装 terakan（用户态 Vulkan，先行验证硬件）

见 [docs/research/terakan.md](docs/research/terakan.md)：代码位置、支持的卡、构建与
安装步骤。这一步**不依赖本项目 KMD**，用于先把 Vulkan 1.0 在 HD 6490M/7470M 上
跑起来（vkcube）。

## 分阶段路线图（摘要）

| 阶段 | 目标 | 状态 |
| --- | --- | --- |
| M0 | 仓库 / CI / 探测工具 / 文档 | ✅ |
| M1 | terakan Windows 构建管线（CI 编 ICD） | 🚧 |
| M2 | KMD 骨架可加载（AddDevice/StartDevice/资源映射） | 🚧 骨架已写 |
| M3 | KMD 基础能力：VRAM 管理 / 驻留 / PM4 ring 提交 | ⬜ |
| M4 | terakan 摆脱官方 KMD 依赖 → 接自研 KMD | ⬜ |
| M5 | Zink（GL）可行性验证 | ⬜ |
| M6 | DX12 层评估（受 Vulkan 1.3 前置约束） | ⬜ |

完整定义见 [docs/roadmap.md](docs/roadmap.md)。

## 许可证

- 本仓库：MIT（见 [LICENSE](LICENSE)）
- 从 Linux 内核（radeon，GPL-2.0）移植的代码段：GPL-2.0，分析见
  [docs/licensing.md](docs/licensing.md)
- terakan / Mesa：MIT

## 参考与致谢

- [Triang3l/mesa（freedesktop GitLab 分支树）](https://gitlab.freedesktop.org/Triang3l/mesa) — terakan 驱动本体
- [Phoronix: Terakan Vulkan Driver For Radeon HD 6000 Series Shown On Windows](https://www.phoronix.com/news/Terakan-HD-6000-VLK-Windows)（2024-04-20）
- [Microsoft: Building Windows Driver Projects with CI and CD](https://techcommunity.microsoft.com/blog/windowsdriverdev/building-windows-driver-projects-with-ci-and-cd/4379200)（2025-02-14，WDK NuGet CI 方案）
- [microsoft/Windows-driver-samples](https://github.com/microsoft/Windows-driver-samples) — KMD DDI 结构参考
- [Microsoft Learn: Display & WDDM 文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/) — DXGK DDI 官方定义
- Linux 内核 `drivers/gpu/drm/radeon`（GPL-2.0）— TeraScale 硬件编程序列参考
