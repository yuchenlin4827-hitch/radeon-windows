# API × GPU 代际 兼容矩阵

回答一个核心问题：**"Linux 的 radeon 开源驱动，各代硬件在各 API 上到底支持到哪？"**
本项目所有目标（Vulkan 1.0 / GL 4.6 / DX12 / WDDM 2.0）都要放在这张表里校准。

更新时间：2026-11。条目均附来源。

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
| **Vulkan 1.3** | ❌ terakan 当前 1.0 | ✅ RADV（仅 Linux） | Mesa 文档 |
| **OpenGL 4.6** | ❌ r600 gallium 上限 **4.5** | ✅ radeonsi（仅 Linux） | Mesa 功能矩阵（docs/features.txt） |
| **OpenGL（Windows 开源）** | ❌ | ❌ | Zink 可达 4.6 但需 Vulkan 1.3 基座 |
| **DX12（官方）** | ❌（官方当年止步 DX11） | ⚠️ 闭源 UMD | AMD 驱动支持列表 |
| **DX12（开源路径）** | ❌ 无现实路径 | ⚠️ vkd3d-proton 式转换层（需 Vulkan 1.3） | vkd3d-proton 需求表 |

### 结论（直接决定 roadmap）

1. **Vulkan 1.0 / TeraScale 是唯一已打通的开源 API**（terakan），是项目第一落点。
2. **GL 4.6**：开源栈里只有"GCN + radeonsi（Linux）"成立；Windows 上的开源路径
   = Zink，前置是 Vulkan 1.3 —— 对 TeraScale 意味着 terakan 生态先补 1.3 能力，
   这是 M5 的评估核心，**不应作为近期承诺**。
3. **DX12**：TeraScale 连官方支持都没有（历史止步 DX11），开源路径只有
   DX12→Vulkan 转换层且同样卡 Vulkan 1.3；M6 仅评估。
4. **WDDM 2.0 KMD**：任何代际都缺失，是本项目要补的最大空白（M2–M4）。

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
- Mesa 功能矩阵（r600/radeonsi/Zink 的 GL 能力逐条）:
  <https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/docs/features.txt>
- AMD 开源文档：TeraScale ISA、PM4/寄存器（Mesa 与 Linux 内核注释是实践参考）:
  <https://www.x.org/wiki/RadeonFeature/> 与内核源码 `drivers/gpu/drm/radeon`
- GPUOpen（AMD 开发者站，ISA/固件资料）:
  <https://gpuopen.com/>
