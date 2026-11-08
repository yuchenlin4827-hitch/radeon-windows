# API × GPU 代际 兼容矩阵

回答一个核心问题：**"Linux 的 radeon 开源驱动，各代硬件在各 API 上到底支持到哪？"**
本项目所有目标（Vulkan 1.0 / GL 4.6 / DX12 / WDDM 2.0）都要放在这张表里校准。

更新时间：2026-11（r600 GL 4.6 依据 Mesa 25.2 修订）。条目均附来源。

## 1. 硬件代际速览

| 代际 | 代表芯片 | Linux KMD | Windows 官方驱动状态 |
| --- | --- | --- | --- |
| TeraScale 2/3 (VLIW) | RV770 (HD 4870)、Barts (HD 6870)、Cayman (HD 6950) | `radeon`（功能全） | **2016 年冻结**（Crimson Beta，Win10） |
| GCN 1–5 | Tahiti (HD 7970)、Polaris (RX 580)、Vega | `radeon`/`amdgpu` | 官方闭源 Adrenalin 分批退役中 |
| RDNA+ | Navi 系列 | `amdgpu` | 官方闭源，活跃 |

## 2. 用户态 API 支持矩阵（开源栈）

| API | TeraScale 2/3 | GCN 1–5 | 来源 |
| --- | --- | --- | --- |
| **Vulkan 1.0** | ✅ **terakan**（Triang3l；Linux 成熟、Windows 已演示 HD 6800 vkcube/demo） | ✅ RADV（仅 Linux） | Phoronix 2024-04-20《Terakan … Shown On Windows》；AUR `vulkan-terakan`（"High-performance Vulkan 1.0 for AMD Terascale (HD 2000-7000)"，MIT） |
| **Vulkan 1.3** | ⚠️ terakan 当前 1.0，**M5.5 目标**（promoted 23/24/23 + 核心特性 24 项，见 vulkan13-plan.md） | ✅ RADV（仅 Linux） | vk.xml 必选特性表（`umd/vk13/`） |
| **OpenGL 4.6** | ✅ **r600/evergreen+（Mesa 25.2 起完成，`r600_pipe.c: glsl_feature_level = family >= CHIP_CEDAR ? 460 : 330`）** | ✅ radeonsi（仅 Linux） | Mesa features.txt GL 4.6 行；Phoronix 2025-06-12 |
| **OpenGL（Windows 开源）** | ⚠️ **主路线：R600g 原生移植**（M7，需 WDDM winsys） | ❌ | 本项目决策；Zink（GL-on-Vulkan）为备选，需 Vulkan 1.3 基座（VP_ZINK_gl46_baseline：api-version 1.3.0） |
| **DX12（官方）** | ❌（官方当年止步 DX11） | ⚠️ 闭源 UMD | AMD 驱动支持列表 |
| **DX12（开源路径）** | ❌ 无现实路径 | ⚠️ vkd3d-proton 式转换层（需 Vulkan 1.3） | vkd3d-proton 需求表 |

> 修订记录：本表旧版记载"r600 gallium 上限 4.5"，该结论基于 2021 年前的
> 功能矩阵；Mesa 25.2（2025-06）起 r600/evergreen+ 已完成 GL 4.6
> （Phoronix 2025-06-12，Patrick Lerda），GL 4.6 不再依赖 Vulkan 版本。

### 结论（直接决定 roadmap）

1. **Vulkan 1.0 / TeraScale 是唯一已打通的开源 API**（terakan），是项目第一落点。
2. **GL 4.6 主路线已锁定：R600g 原生移植**（M7）——r600/evergreen+ 在 Mesa 主线
   已完成 GL 4.6，且 r600 无 LLVM 依赖；Windows 侧只差 WDDM winsys + WGL 前端。
   Zink 备选路线需 Vulkan 1.3（M5.5）。
3. **Vulkan 1.3（M5.5）**：promoted 23/24/23、无条件核心特性 24 项，
   逐项工程评估见 [vulkan13-plan.md](vulkan13-plan.md)。
4. **DX12**：TeraScale 连官方支持都没有（历史止步 DX11），开源路径只有
   DX12→Vulkan 转换层且同样卡 Vulkan 1.3；M6 仅评估。
5. **WDDM 2.0 KMD**：任何代际都缺失，是本项目要补的最大空白（M2–M4）。

## 3. WDDM 版本对照（内核侧）

| WDDM | Windows | 关键变化（与我们相关） |
| --- | --- | --- |
| 1.0/1.1 | Vista/7 | 官方 Catalyst 对 TeraScale 的终点区域 |
| 1.3 | 8.1 | — |
| **2.0** | Win10 1507 | 内存驻留模型重写（MakeResident/Evict）、SubmitCommandVirtual、更低提交开销 |
| 2.7 | Win10 2004 | 硬件加速调度（HwSch）可选开启（我们不做） |

自研 KMD 直接瞄准 **WDDM 2.0 基线**：接口稳定、Win10/11 全系兼容、
且 2.0 的新驻留模型与 Linux TTM 语义最接近，移植阻力最小。

## 4. 协议 / 规范阅读清单（"读官方协议"对应项）

- Microsoft Learn — Display drivers（WDDM 架构、DXGK DDI、VidMm）:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/>
- Microsoft Learn — Download the WDK（含 WDK NuGet 说明）:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk>
- Khronos Vulkan 1.0 规范 + Vulkan 功能矩阵:
  <https://registry.khronos.org/vulkan/>
- Mesa 功能矩阵（r600/radeonsi/Zink 的 GL 能力逐条，GL 4.6 → r600/evergreen+）:
  <https://docs.mesa3d.org/features.html>
- Mesa 机器可读 Zink 门控（VP_ZINK_gl46_baseline）:
  <https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/util/VP_ZINK_requirements.json>
- Khronos Vulkan 注册表（vk.xml：promoted 扩展与必选特性的权威源）:
  <https://registry.khronos.org/vulkan/>
- AMD 开源文档：TeraScale ISA、PM4/寄存器（Mesa 与 Linux 内核注释是实践参考）:
  <https://www.x.org/wiki/RadeonFeature/> 与内核源码 `drivers/gpu/drm/radeon`
- GPUOpen（AMD 开发者站，ISA/固件资料）:
  <https://gpuopen.com/>

## 5. 还能支持什么（下一批扩展目标的诚实评估）

> 2026-11 评估，与 roadmap M4–M6 对应。原则：**每一项都指明前置条件**，
> 不做无依据的承诺。

### 5.1 短期可行（M4/M5 顺位）

| 目标 | 前置条件 | 依据 |
| --- | --- | --- |
| **Vulkan 1.1**（terakan） | terakan 补 1.1 必须特性：subgroup ops（VLIW 用 DSP 式软件模拟）、host query reset、device groups 可跳过 | Khronos 1.1 diff 规范；terakan 已有 1.0 全量 |
| **OpenGL 4.5**（Zink on terakan 1.1） | Vulkan 1.1 ✅ → Zink（Mesa，Windows 可编） | Zink 4.5 要求 Vulkan 1.1（features.txt） |
| **DX9**（DXVK 旧线 / WineD3D native） | Vulkan 1.1（dxvk-d9vk 线）或 GL 4.3+（wined3d GL 后端） | DXVK 1.x d3d9 最低 Vulkan 1.1；WineD3D 可运行于原生 Windows |
| **模拟设备全链路 CI** | ✅ M2.6 已落地（sim.c）——把 VM 冒烟固化为 Actions job | docs/simulated-device.md |

### 5.2 中期评估（M6，条件性）

| 目标 | 前置条件 | 现实判断 |
| --- | --- | --- |
| **DX10/11**（DXVK 2.x） | **Vulkan 1.3**（DXVK 2.x 硬要求：dynamic rendering、1.3 特性组） | terakan 上 1.3 的 feature gap 大（动态渲染/8 位存储等）；按 vkd3d-proton 同款路线评估，不承诺 |
| **DX12**（vkd3d-proton 式） | Vulkan 1.3 + 大量可选特性 | 同上，且官方当年对 TeraScale 本来就止步 DX11 |
| **GL 4.6** | Vulkan 1.3 基座的 Zink | 长线 |

### 5.3 明确不做（说明理由）

| 目标 | 理由 |
| --- | --- |
| **OpenCL 1.x** | Linux 路线是 Mesa clover，clover 不支持 Windows 构建；Windows 侧无现实开源路径 |
| **VA-API / VDPAU** | Linux-only API；Windows 等价物是 DXVA/DDDI（UVD 视频解码 DDI），TeraScale 的 UVD2/3 可做但优先级远低于渲染主链 |
| **WDDM 2.1+ 升级** | 2.1–2.7 的新特性（HAGS/HwSch、GpuMmu 强制等）对 VLIW 无收益且抬升 DDI 移植成本；锁定 2.0 基线已是决策（见 wddm-vulkan-limits.md） |
| **CCC 本体驱动 rokmd** | CCC UI 绑死 atikmdag 私有通道；rokmd 走"设置兼容"路线（docs/ccc.md），不伪造私有 DDI |
| **官方 ADL 完整 ABI 对拍** | 需真实卡实机抓包 atiadlxx 行为；当前交付语义等价面（umd/adl/README.md 差异表） |

