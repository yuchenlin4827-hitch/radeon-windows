# 版本上限调研：WDDM 与 Vulkan

回答两个问题：**"WDDM 最高多少？Vulkan 最高多少？"**——分"规范/OS 上限"
和"本项目目标硬件的现实上限"两层。

更新时间：2026-11。

## 1. WDDM 版本时间线与上限

### 1.1 全谱系（规范/OS 侧）

| WDDM | Windows | 备注 |
| --- | --- | --- |
| 1.0 / 1.1 / 1.2 / 1.3 | Vista / 7 / 8 / 8.1 | 1.x 为旧模型 |
| **2.0** | Win10 1507 | 驻留模型重写（MakeResident/Evict）、SubmitCommandVirtual |
| 2.1–2.6 | Win10 1607–20H2 | 渐进改进 |
| 2.7 | Win10 21H1 | 硬件加速调度（HwSch，可选） |
| 2.9 | Win10 22H2 | Win10 终点 |
| 3.0 | Win11 21H2 | D3D12 增强屏障、D3D12 视频编码、USB4 DP |
| 3.1 | Win11 22H2 | SM 6.7 等 |
| **3.2** | Win11 24H2 | **当前最新**（来源：Microsoft Learn《What's new for Windows 11 graphics display drivers》，2025-11 更新） |

### 1.2 本项目怎么选

- **实现目标：WDDM 2.0 基线**。理由：
  1. Win10/Win11 全系兼容——OS 向下兼容旧 DDI 版本，2.0 KMD 在 24H2（WDDM 3.2）上仍可加载运行；
  2. 2.0 的驻留/提交模型与 Linux radeon 的 TTM+IB 语义对齐最好，移植阻力最小；
  3. 更高版本的新特性（HwSch、增强屏障、硬件翻转队列）对 VLIW 老 GPU **没有可用收益**
     （HwSch 需要硬件队列支持，TeraScale 无此能力，继续用 CPU 调度即可）。
- **结论**：OS 侧上限 WDDM 3.2；本项目 KMD 锁 2.0，不为"数字"升版本。
- 合规注意：Windows 11 显示驱动要求 WDDM 2.0+（XDDM/WDDM 1.x 已不受支持），
  我们直接从 2.0 起步正好满足。

## 2. Vulkan 版本上限

### 2.1 规范侧

- 最新规范：**Vulkan 1.4**（2024-12-03 发布，Khronos 官方；2026-11 周更至
  1.4.36x 系列）。1.3 = 2022-01，1.2 = 2020-01，1.1 = 2018-03。

### 2.2 各代际开源驱动的现实上限

| 驱动 | 硬件 | Vulkan 上限 | 状态 |
| --- | --- | --- | --- |
| RADV（Mesa） | GCN 1+ | **1.4**（已过 Khronos 一致性） | Linux |
| **terakan** | **TeraScale 1–3** | **1.0** | Linux + Windows（HD 6800 实机演示） |
| NVK / ANV 等 | — | 1.3/1.4 | 对照参考 |

### 2.3 TeraScale 再往上冲的可行性（评估）

| 目标 | 新增硬性要求（节选） | TeraScale 可行性 |
| --- | --- | --- |
| 1.1 | subgroup operations（compute/graphics）、host query reset | 理论可行：VLIW wavefront 可当 subgroup；工程量大，收益中等 |
| 1.2 | timeline semaphore、buffer device address、16-bit storage | 边缘可行，多为软件层包装 |
| 1.3 | dynamic rendering、descriptor indexing 全集、subgroup 大小控制 | 难度高，特性面太广 |
| 1.4 | maintenance6、scalar block layout、push descriptor 强制 | 对 VLIW 无硬件依据，不建议 |

- **结论：现实上限 = Vulkan 1.0（terakan 已达成）；1.1 可作为 M5 评估项**
  （附带收益：Zink 的 OpenGL 4.5 需要 Vulkan 1.1——详见 api-matrix.md，
  即"冲 1.1 换 GL 4.5"是一条性价比相对高的路线；GL 4.6 需要 Vulkan 1.3，
  对 TeraScale 不设为目标）。

## 3. 对路线图的影响

- M2–M4（KMD / terakan 对接）：维持 WDDM 2.0 + Vulkan 1.0，不变。
- M5（GL 评估）：目标改为"Zink + Vulkan 1.1 → GL 4.5"为主攻，
  GL 4.6 降级为远期观察项。
- 该决策写入 roadmap.md 的 M5 描述。
