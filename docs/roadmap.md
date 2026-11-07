# 分阶段路线图（M0–M6）

> 状态标记：⬜ 未开始 ｜ 🚧 进行中 ｜ ✅ 完成
> 当前状态：**M0 完成、M2 编译通过（2026-11）**——CI 已在 windows-2022 上
> 自动产出 rokmd.sys（WDK NuGet + VS2022），待实机加载验证。

每个阶段都有明确的"验收门"（DoD），未过验收不进入下一阶段。

---

## M0 —— 项目基础设施 ✅

**目标**：仓库、文档、CI、探测工具全部就位。

| 事项 | 状态 |
| --- | --- |
| 仓库结构 + README + 架构/路线图/矩阵文档 | ✅ |
| `build-kmd.yml`：Windows Runner + WDK NuGet 编译 `rokmd.sys` | ✅（首次运行待验证） |
| `build-gpu-probe.yml`：gpu_probe 双平台构建 | ✅ |
| `gpu_probe.exe`（WDDM 适配器 / PCI / Vulkan 探测） | ✅ 已交叉编译 |

**验收门**：推送 GitHub 后 Actions 绿灯，能下载到 `rokmd.sys` 与 `gpu_probe.exe`。

## M1 —— terakan 构建管线与依赖边界调研 🚧

**目标**：把 terakan 在 CI 上编出来；搞清它在 Windows 上的运行条件。

1. 克隆 `Triang3l/mesa`（freedesktop GitLab），定位 terakan 目录与文档
   （预期在 `src/` 下的 terakan 子树，以实际仓库为准）。
2. 摸清 Windows 构建要求：MSVC/clang-cl、DXC（SPIR-V→TeraScale 需要自定义后端，
   terakan 自带）、依赖库清单。
3. 写 `umd/build-terakan.yml`：CI 产出 `terakan_icd.dll` + `terakan_icd.x86_64.json`。
4. **关键调研题（决定 M4 形态）**：terakan Windows 路径是否强依赖官方 Catalyst KMD？
   - 证据渠道：terakan 源码 winsys 层、Triang3l 的公开状态更新、AUR 包的 Linux 依赖
     （libdrm → Linux 路径独立于 Windows 路径）。
   - 结论写入 `docs/research/terakan.md` 的"依赖边界"一节。

**验收门**：CI 产出可安装的 ICD；在 HD 6000 系实机（装老 Catalyst）上
`vkcube` 出三角。

## M2 —— KMD 骨架可加载 🚧（骨架代码已交付）

**目标**：`rokmd.sys` 能被 Windows 加载、在设备管理器中启动，不蓝屏、不点屏。

- ✅ `DriverEntry` + `DxgkInitialize` DDI 注册
- ✅ `AddDevice`（PDO 绑定 + 上下文分配）、`StartDevice`（BAR/IRQ 资源枚举 + 日志）
- ✅ 其余 DDI stub（返回 `STATUS_NOT_IMPLEMENTED`，KdPrint 留痕）
- ⬜ 修复首轮 CI / 实机安装暴露的签名与 INF 问题
- ⬜ 双机调试环境文档化（WinDbg + 1394/网络，或 Hyper-V Gen1 + 调试内核）

**验收门**：实机 `pnputil /add-driver rokmd.inf /install` 后适配器以"代码 43
（请求的资源未实现）"或"正常启动但无输出"状态稳定存在，**无 BSOD**；
dbgview/Windbg 能看到 StartDevice 打印的 BAR 清单。

## M3 —— KMD 基础硬件能力 ⬜

**目标**：KMD 真正"认识"目标硬件（**HD 6490M = Seymour / HD 7470M = Caicos，
TeraScale 2 家族**；寄存器编程序列在 r600 家族内通用）。

按序攻坚（对应 Linux `drivers/gpu/drm/radeon` 源文件）：

| 步骤 | 内容 | 参考（GPL-2.0） |
| --- | --- | --- |
| 3.1 | 寄存器访问封装（MM_INDEX/MM_DATA 间接寻址）、PCI ID 表 | `radeon_device.c`、`rv770.c` |
| 3.2 | 时钟/复位：MC、GRBM 软复位 | `r600.c: r600_init` |
| 3.3 | VRAM/GTT：大小探测、页表格式 | `radeon_ttm.c`、`r600_gart` |
| 3.4 | CP ring + PM4：RING_INIT、INDIRECT_BUFFER 提交 | `r600_cp.c`、`ni_dma` |
| 3.5 | IH 中断环 + `DxgkDdiInterruptRoutine` 接线 | `evergreen_irq.c` |
| 3.6 | VidMm 对接：CreateAllocation/MakeResident/Evict/BuildPagingBuffer | 上 3.3 |

**验收门**：KMD 日志完整走通"探测 → 复位 → ring 空转 → 收到一条 IH 中断"；
提交 NOP IB 无 TDR。

## M4 —— terakan 接自研 KMD ⬜

**目标**：Vulkan ICD 不再依赖官方 Catalyst KMD，整个栈全开源。

- 把 terakan 的 Windows 内核交互层（winsys）重定向到 `rokmd` 的
  提交/驻留接口（形态取决于 M1 调研结论：官方 KMD 的哪些私有通道必须替换）。
- 建立一致性测试：vkcube、dota2-vulkan 级别冒烟（老卡预期低帧，能跑即可）。

**验收门**：干净系统（无 Catalyst）+ `rokmd.sys` + `terakan_icd.dll` → vkcube。

## M5 —— OpenGL 可行性评估 ⬜

- 主攻路线：**terakan 冲 Vulkan 1.1 → Zink → OpenGL 4.5**
  （Zink 的 GL 4.5 要求 Vulkan 1.1；GL 4.6 要求 Vulkan 1.3，对 TeraScale
  不设为目标——依据见 [wddm-vulkan-limits.md](wddm-vulkan-limits.md) 第 2.3 节）。
- 产出：Vulkan 1.1 必须特性（subgroup ops、host query reset 等）在 VLIW 上的
  逐项核对表 + 决策（上 1.1 或收敛到"纯 1.0 + 软件包装"）。

## M6 —— DX12 层评估 ⬜

- 唯一现实路径：vkd3d-proton 式 DX12→Vulkan 转换层（需 Vulkan 1.3+）。
- 评估其在原生 Windows（非 Wine）部署的可行性与性能预期；只做决策文档，
  不提前写代码。

---

## 里程碑之外的持续工程

- **CI 固化**：每个里程碑合并后 Actions 必须绿；KMD 二进制附带测试签名说明
  （`bcdedit /set testsigning on`，见 architecture.md 风险清单）。
- **文档同步**：每阶段结论回写 `docs/`，terakan 调研页保持"活文档"。
