# 用户态驱动（UMD / ICD）—— terakan 集成计划

本目录未来承载两个东西：

1. **terakan 的 CI 构建与打包**（M1）
   - 源：`https://gitlab.freedesktop.org/Triang3l/mesa`（terakan 子树）
   - 产物：`terakan_icd.dll` + `terakan_icd.x86_64.json`（Vulkan ICD 清单）
   - 安装：JSON 放入 `%SYSTEMROOT%\System32\`（或设置环境变量
     `VK_ICD_FILENAMES` 指向该 JSON），Loader 即可枚举。
   - 调研与依赖边界结论见 [../docs/research/terakan.md](../docs/research/terakan.md)

2. **M4：terakan winsys → 自研 rokmd 对接层**
   - 目标：Vulkan ICD 不再依赖官方 Catalyst KMD，命令提交/驻留全部走
     `kmd/` 的 WDDM 2.0 DDI。
   - 前置：M3 完成（KMD 具备 ring 提交与驻留能力）。

## 当前状态

占位目录。M1 启动前不需要任何代码。

### 为什么用户态先行

terakan 是纯用户态 DLL（Vulkan ICD），崩溃不蓝屏、迭代快、可独立验证硬件。
先让 Vulkan 1.0 出画面，再啃内核 —— 风险曲线最平滑。
