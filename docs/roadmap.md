# 分阶段路线图（M0–M7）

> 状态标记：⬜ 未开始 ｜ 🚧 进行中 ｜ ✅ 完成
> 当前状态：**M0 完成、M2 编译通过（2026-11）**——CI 已在 windows-2022 上
> 自动产出 rokmd.sys（WDK NuGet + VS2022），待实机加载验证；
> 2026-11 增补 M5.5（Vulkan 1.3）与 M7（R600g GL 4.6），原 M5 保留并加注；
> 2026-11 M2.8：WDDM 2.0 (0x5023) DDI 表全量补齐（机械验证无 NULL 成员）。
> 2026-11 实机基线：HD 7470M（Win11 26100）上官方 Vulkan 栈实测不可用
> （vk_audit v1.2：manifest/DLL 完整仍 -9），详见「实机基线」一节。
> 2026-11-08 M3 第一批：3.1 寄存器直访 + 120 项 TS2 精确 PCI 表 +
> 3.2 GRBM 状态/WaitIdle/CP 软复位（宿主单测 187 项，escape 类型 10/11）。

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
- ✅ M2.5：QueryAdapterInfo（DRIVERCAPS/SEGMENT/GPUMMUCAPS）、分配/上下文/
  进程簿记、即时完成提交模型、BuildPagingBuffer NOP、Escape v1
- ✅ M2.6：WDK 26100 结构用法修复（StatusBlock/OpenAllocation）+
  **0x5023 DDI 表补全**（RestartFromTimeout/GetNodeMetadata/SystemDisplay
  系列/遗留显示面/PowerRuntime 等，共 24 个新成员；显示/GDI 路径保持
  NOT_IMPLEMENTED）+ DRIVERCAPS.SupportSurpriseRemoval；
  Escape 契约双模式化（`kmd/src/rok_escape.h`：KMD/ICD/gpu_probe 同源
  ABI）+ QUERY_SEGMENT_INFO + gpu_probe [4] escape 通道实测段
- ✅ M2.7a：**模拟设备**（kmd/src/sim.c）—— 设备 ID/注册表/hypervisor 三路
  判定、64KB 模拟寄存器空间、提交路径 PM4 解码（opcode 直方图）、
  escape `QUERY_SIM_STATUS`；INF 模拟设备段（QEMU ati-vga 等 5 个 ID）
- ✅ M2.7b：**CCC 设置兼容**（kmd/src/ccc.c + umd/adl/rok_adl.dll）——
  Catalyst 遗留值名 + rokmd 专有名导入、escape CCC get/set、ADL2 同名
  枚举面；详见 docs/ccc.md
- ✅ M2.8：**WDDM 2.0 (0x5023) DDI 表全量补齐**——对照 WDK 26100
  dispmprt.h 的 DRIVER_INITIALIZATION_DATA 逐成员核对，补齐最后 5 个
  函数成员（DxgkDdiLinkDevice / SetPowerComponentFState /
  PowerRuntimeControlRequest / SetPowerPState /
  SetVidPnSourceAddressWithMultiPlaneOverlay v1）；QueryAdapterInfo 扩面
  （NUMPOWERCOMPONENTS / SEGMENTMEMORYSTATE /
  DISPLAY_DRIVERCAPS_EXTENSION）；escape v1 追加
  QUERY_ENGINE_STATUS（type 9，submitted/completed fence + in-flight +
  提交/分页计数，M4 ICD 的 fence 进度通道）；QueryEngineStatus 改为
  fence 进度判定；gpu_probe [4.8] 同步。94/94 函数成员非 NULL（脚本
  scripts/check_ddi_table.py 机械验证）
- ⬜ 修复首轮 CI / 实机 / VM 加载暴露的签名与 INF 问题
- ⬜ 双机调试环境文档化（WinDbg + 1394/网络，或 Hyper-V Gen1 + 调试内核）

**验收门**：实机或 VM（[docs/simulated-device.md](simulated-device.md) 的
QEMU ati-vga 路径）`pnputil /add-driver rokmd.inf /install` 后适配器以
"代码 43（请求的资源未实现）"或"正常启动但无输出"状态稳定存在，**无 BSOD**；
gpu_probe [4] 段对 rokmd 适配器回显版本/PCI/能力/段/模拟状态/CCC。

## M3 —— KMD 基础硬件能力 🚧（第一批 3.1 全量 + 3.2 GRBM 段已交付）

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

**进展（2026-11-08，第一批）**：

- ✅ 3.1 寄存器直访封装 `kmd/src/r600hw.c`：RokRegRead32/Write32（BAR0
  边界检查，NULL/非对齐/越界一律拒绝）；精确 PCI 分发表 120 个
  Evergreen 家族 DID（源：Mesa r600_pci_ids.h，MIT；与 umd/gl46 表
  一致性由 scripts/check_pci_tables.py 机械校验），替换 rok_escape.h
  旧启发式（并修正其 0x6840 误标 Seymour —— Mesa 实归 TURKS）。
  MM_INDEX/MM_DATA 间接寻址推迟至 3.3：总线空间语义尚待权威出处
  核实，BAR0 直访已覆盖 GRBM 域，不把未核实常量写进内核。
- ✅ 3.2（GRBM 段）GRBM_STATUS 解码 / WaitIdle（10µs × 10000 轮上限）/
  CP 域软复位脉冲（GRBM_SOFT_RESET 0x8020 = SOFT_RESET_CP，置位→停顿
  100µs→清零→WaitIdle；地址/位域取证：Linux radeon r600d.h v6.6）。
  MC 段 ⬜ 随 3.3。
- ✅ escape 类型 10（QUERY_HW_INFO，只读识别快照 + GRBM 采样）/ 11
  （HW_CP_RESET，gpu_probe --cp-reset 显式触发；ResetFromTimeout 待
  3.4 ring 重置落地后接线）；gpu_probe [4.9]/[4.10]。
- 宿主单测 78 → 187（芯片表不变量/边界/解码/超时路径/复位序列寄存器
  轨迹，kmock 新增 MMIO mock + 停顿计数）。

**验收门**：KMD 日志完整走通"探测 → 复位 → ring 空转 → 收到一条 IH 中断"；
提交 NOP IB 无 TDR。

**M3.1 进展（v0.2.2-alpha）** ✅：
- PCI 家族分发表修正：Seymour/Caicos/Turks/Lombok/Thames 全段归入 TS2
  （v0.2.1 实测修正——旧表 0x6760 误判 TS1；0x6840 实为 Thames 段）。
- VRAM 大小探测落地：真实路径读 `CONFIG_MEMSIZE`（0x5428，KB，与
  `r600.c` 同源）；模拟路径 `<DriverKey>\Parameters\VramSizeMb`
  （默认 256MB）。
- escape `QUERY_VRAM` / `QUERY_SEGMENT_INFO` 开始回填探测值
  （VRAM POPULATED 位如实设置）。GTT/段号待 3.6。
- 真机 bring-up 风险与恢复流程入档：见
  [bringup-safety.md](bringup-safety.md)（render-only 接管唯一显示 GPU
  = 无头风险，虚拟机先行）。

**M3.4a 进展（v0.2.3-alpha）** ✅（3.4 的"初始化+观测"半步）：

- **CP 微码内嵌**：`kmd/src/evergreen_ucode.c`（`scripts/gen_ucode.py`
  机械生成）——linux-firmware 的 `CAICOS_pfp.bin`（1120 dwords）+
  `CAICOS_me.bin`（1376 dwords），覆盖 Seymour/Caicos（CHIP_CAICOS）。
  TURKS_me 实测与 CAICOS_me 相同；TURKS_pfp 不同（Whistler/Turks 的
  HD 6490M 路径 M3.4b 补）。
- **Evergreen CP ring 编程序列**（`kmd/src/gfxinit.c`，逐条对照
  evergreen.c 的 `evergreen_cp_load_microcode` / `evergreen_cp_resume` /
  `evergreen_cp_start` + r600.c 的 `r600_ring_test`）：PFP/ME 微码写入 →
  GRBM 软复位（CP|PA|SH|VGT|SPI|SX）→ CP_RB_BASE/CNTL/RPTR/WPTR 编程
  （NO_UPDATE，rptr 从 MMIO 0x8700 轮询，绕开 writeback 依赖）→
  ME_INIT 7 dwords → 解除停机（CP_ME_CNTL=0xFF）→ scratch 回环测试
  （0xCAFEDEAD → SET_CONFIG_REG → 0xDEADBEEF）。
- **默认关闭**：`<DriverKey>\Parameters\EnableRing = 1` 才执行；未开启时
  对硬件零写入，WDDM 提交保持即时完成模型（零 TDR 风险）。真实模式
  执行门槛：非 sim + BAR0 已映射 + TS2 家族 + 孔径存在 +
  `MC_VM_FB_LOCATION` 非 0。
- **观测通道**：escape v1 追加 `QUERY_HW_STATUS (type 12)`（只读快照 29 个
  寄存器/状态字段，真实/模拟/无 MMIO 三态如实标注）；gpu_probe 新增
  [1.7]（cfgmgr32 资源/BAR 形态，用户态即可）与 [4.11]（寄存器快照）。
- **PM4 count 语义修正**：sim.c 解码器与 ddi.c 注释对齐内核
  `r600_cs.c` 解析器（包总长 = count 字段 + 2，包体 = count+1）——
  旧实现少算 1 dword，M4 ICD 生成命令流前必须钉死此语义。
- **主机端 176 项单测**：演练模式（`Rehearse=TRUE`）把完整启动序列打到
  sim 寄存器空间并逐一断言终值（CP_RB_BASE/CNTL/ME_CNTL/SCRATCH_REG6
  哨兵/ring 内存包内容），ASan/UBSan 兜底。
- 已知边界（下步）：M3.4b 真实提交 + fence；M3.5 IH/RLC（BTC_rlc.bin）；
  MC 微码（BTC_mc.bin）仅冷启动需评估；RING 在 VRAM 孔径内 8MB 偏移、
  32KB 容量（BUFSZ=12）。

## M4 —— terakan 接自研 KMD ⬜

**目标**：Vulkan ICD 不再依赖官方 Catalyst KMD，整个栈全开源。

- 把 terakan 的 Windows 内核交互层（winsys）重定向到 `rokmd` 的
  提交/驻留接口（形态取决于 M1 调研结论：官方 KMD 的哪些私有通道必须替换）。
- 建立一致性测试：vkcube、dota2-vulkan 级别冒烟（老卡预期低帧，能跑即可）。

**验收门**：干净系统（无 Catalyst）+ `rokmd.sys` + `terakan_icd.dll` → vkcube。

## M5 —— OpenGL 可行性评估 ⬜（结论已被 M5.5/M7 覆盖，保留存档）

> **2026-11 更新**：Mesa 25.2 起 r600 驱动已在 evergreen+ 完成 GL 4.6，
> OpenGL 主路线改为 **R600g 原生移植**（见 M7）；Zink 降级为备选。
> 本节结论按 2021-2024 年的功能矩阵得出，保留存档以追溯决策过程。

- 主攻路线：**terakan 冲 Vulkan 1.1 → Zink → OpenGL 4.5**
  （Zink 的 GL 4.5 要求 Vulkan 1.1；GL 4.6 要求 Vulkan 1.3，对 TeraScale
  不设为目标——依据见 [wddm-vulkan-limits.md](wddm-vulkan-limits.md) 第 2.3 节）。
- 产出：Vulkan 1.1 必须特性（subgroup ops、host query reset 等）在 VLIW 上的
  逐项核对表 + 决策（上 1.1 或收敛到"纯 1.0 + 软件包装"）。

## M5.5 —— Vulkan 1.1 → 1.3 逐特性攻坚 ⬜

**目标**：terakan 从 1.0 推到 Vulkan 1.3（promoted 扩展 23/24/23，
无条件核心特性 24 项）。

- 差距清单与 VLIW 可行性逐项评估：[vulkan13-plan.md](vulkan13-plan.md)
- 门控数据与单测：`umd/vk13/`（promoted 表 + 必选特性表，纯 C11）
- 实机审计：`tools/vk_audit/`（vk_audit.exe，逐设备报 1.1/1.2/1.3 缺口；
  v1.2 起逐份解析注册表 ICD manifest→DLL，可区分「残留注册表」与
  「DLL 在但硬件不支持」，已在 HD 7470M 实机验证，见「实机基线」一节）

**验收门**：vk_audit.exe 在 terakan ICD 上报 "1.3 REACHABLE"；
vkcube + dota2-vulkan 冒烟无回归。

**附带收益**：1.3 达成后，Zink 备选路线（GL 4.6-on-Vulkan）的门控全绿
（见 [r600g-gl46.md](r600g-gl46.md) 第 5 节）。

## M6 —— DX12 层评估 ⬜

- 唯一现实路径：vkd3d-proton 式 DX12→Vulkan 转换层（需 Vulkan 1.3+，
  即 M5.5 的产出）。
- 评估其在原生 Windows（非 Wine）部署的可行性与性能预期；只做决策文档，
  不提前写代码。

## M7 —— R600g 原生移植 → OpenGL 4.6 ⬜

**目标**：把 Mesa r600 Gallium 驱动移植到 Windows（WDDM winsys + WGL 前端），
在 evergreen+（TeraScale 2/3，含验证卡 Seymour/Turks、Caicos）上交付
**OpenGL 4.6**——与 Vulkan 路线完全解耦。

**依据**（源码级，见 [r600g-gl46.md](r600g-gl46.md)）：

- Mesa features.txt：`GL 4.6 -- all DONE: ..., r600/evergreen+, ...`
- `r600_pipe.c:495`：`glsl_feature_level = family >= CHIP_CEDAR ? 460 : 330`
- Mesa 25.2（2025-06）完成 GL 4.6 曝光；r600 无 LLVM 依赖
- 门控代码：`umd/gl46/gl46_r600g.c`（PCI 表 + cap/winsys 检查）

**分批**（GL-W1…W5）：winsys 骨架 → WGL 前端 → GL 3.3 → 4.x 逐级 → 4.6 收口。

**验收门**：wglinfo 报 OpenGL 4.6（HD 6490M / HD 7470M）；
glmark2 与一致性子集无回归；干净系统（无 Catalyst）+ rokmd 下可运行。

---

## 实机基线（2026-11-08，HD 7470M）✅

**机器**：AMD Radeon HD 7470M（M3 所列 TeraScale 2 验证卡，与 HD 6490M
同为项目目标卡）＋ Windows 11 26100。

**实测**（vk_audit v1.2，125,952 字节，main 0ab3966）：

```text
  loader DLL  : C:\Windows\SYSTEM32\vulkan-1.dll
  vkCreateInstance failed, VkResult=-9 (VK_ERROR_INCOMPATIBLE_DRIVER)
  VK_DRIVER_FILES / VK_ICD_FILENAMES: (not set)
  registry ICDs (64-bit view):
    [0] C:\Windows\System32\amd-vulkan64.json
        library_path  : .\amdvlk64.dll -> C:\Windows\System32\.\amdvlk64.dll  [DLL present]
  registry ICDs (32-bit view):
    [0] C:\Windows\SysWOW64\amd-vulkan32.json
        library_path  : .\amdvlk32.dll -> C:\Windows\SysWOW64\.\amdvlk32.dll  [DLL present]
  -> 2 of 2 ICD manifest(s) resolve to an existing driver DLL.
```

（两份 manifest 均未携带 api_version 键，v1.2 对该键的支持已就绪但此处
无输出，符合旧式简表特征。）

**判读**：

1. 加载器、manifest、ICD DLL 三层全部在位 → 排除「残留注册表 / 文件丢失」
   假设，v1.2 的「DLL 在但硬件不支持」分类首次实机验证通过。
2. `vkCreateInstance` 仍报 -9：ICD 被加载但对本机枚举 0 设备——官方
   Windows Vulkan 栈（amdvlk64/amdvlk32，随 Adrenalin 安装）自 GCN 1.0
   （HD 7700 系）起支持，TeraScale 2 在支持矩阵之外（与
   [wddm-vulkan-limits.md](wddm-vulkan-limits.md) 2.2 节「RADV = GCN 1+」
   互证；terakan 的存在正因为官方栈覆盖不到 TeraScale）。
3. **结论：该机「官方栈的 Vulkan」为零基线**——任何自研 ICD 只要从
   rokmd 上枚举出 1 个设备即为净增益。

**对里程碑的意义**：

- **M1**：目标卡「装老 Catalyst」对照组的 Vulkan 预期同为 -9，
  vkcube 验收只能由 terakan/自研 ICD 达成；
- **M4**：干净系统 + rokmd + terakan 的验收前提（零基线）已实测钉死；
- **M5.5**：vk_audit v1.2 分类能力实机验证通过，M1/M4 基线数据归档完毕；
- **M7**：R600g GL 4.6 路线与 Vulkan 栈解耦，本基线不构成约束。

---

## 里程碑之外的持续工程

- **CI 固化**：每个里程碑合并后 Actions 必须绿；KMD 二进制附带测试签名说明
  （`bcdedit /set testsigning on`，见 architecture.md 风险清单）。
  新增 `build-umd.yml`：umd 门控模块单测 + vk_audit.exe 双工具链构建。
- **文档同步**：每阶段结论回写 `docs/`，terakan 调研页保持"活文档"。
