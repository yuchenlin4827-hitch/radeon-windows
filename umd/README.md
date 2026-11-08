# 用户态驱动（UMD / ICD）—— terakan 集成 + 门控模块

本目录承载三个东西：

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

3. **M5.5/M7 门控模块（已交付，纯 C11，无外部依赖）**
   - [`vk13/`](vk13/)：Vulkan 1.1/1.2/1.3 promoted 扩展表（23/24/23）+
     必选核心特性表（vk.xml 权威提取）+ 层级判定/缺口枚举。
   - [`gl46/`](gl46/)：OpenGL 4.6 两条路线的门控——
     R600g 主路线（芯片代际表 + cap/winsys 检查）与
     Zink 备选路线（VP_ZINK gl46 链完整编码）。
   - 数据与判定逻辑与 docs 中的计划一一对应，由 `tests/umd/` 单测锁定
     （85 项，gcc/MSVC 双编译），实机验证用 `tools/vk_audit/`。

## 当前状态

- `vk13/`、`gl46/`、`tests/umd/` 已交付；`tools/vk_audit/` 已交付
  （含 llvm-mingw 交叉编译产物 vk_audit.exe，CI 每次 push 自动构建）。
- terakan 集成（第 1/2 项）仍为占位：M1 启动前不需要代码。

### 为什么用户态先行

terakan 是纯用户态 DLL（Vulkan ICD），崩溃不蓝屏、迭代快、可独立验证硬件。
先让 Vulkan 1.0 出画面，再啃内核 —— 风险曲线最平滑。

### 为什么要门控模块（vk13/gl46）

在写任何 UMD 代码之前，先把"目标 API 到底要实现什么"变成机器可校验的数据：
promoted 扩展表、必选核心特性表、GL 4.6 硬件门，全部代码化并配单测，
避免计划文档与实际实现脱节；vk_audit.exe 则把同一套表用于实机审计，
每个里程碑的验收门都能自动检查。
