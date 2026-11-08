# terakan 调研笔记（活文档，M1 持续更新）

## 是什么

- **terakan**：独立开发者 Vitaliy Kuzmin（网名 **Triang3l**，Xbox 模拟器
  Xenia 的图形贡献者）在 Mesa 代码库里从零编写的 **Vulkan 1.0 驱动**，
  目标是 AMD **TeraScale 2/3**（HD 2000–7000 区间的 VLIW 架构卡）——
  这是 AMD 从未给过官方 Vulkan 支持的代际。
- 许可证 **MIT**（随 Mesa 体系）。
- 代码位置：<https://gitlab.freedesktop.org/Triang3l/mesa>（Triang3l 的
  Mesa 分支树；driver 子树与分支名以仓库实际为准）。
- 注意区分：**terakan ≠ RADV**。RADV 是 Mesa 官方 GCN Vulkan 驱动
  （GCN1+、Vulkan 1.3、Linux-only）；terakan 与它同库不同卡、不同目标。

## 时间线（公开报道）

| 时间 | 事件 | 来源 |
| --- | --- | --- |
| 2023-05 | Phoronix 首次报道：Triang3l 在 Mesa 树内开发 terakan（HD 6000 系） | Phoronix 2023-05-19 |
| 2023-11 | "Does Triangles"：开始出第一个三角形 | Phoronix 2023-11-09 |
| 2024-04-20 | **在 Windows 上演示**：Radeon HD 6800（Barts）跑通多个 Vulkan demo | Phoronix 2024-04-20 |
| 2025-04 | Linux 侧出现发行版打包（AUR `vulkan-terakan`：libdrm/Wayland/X11 依赖，Vulkan 1.0，MIT） | AUR 页面 |

## 能力现状

- Vulkan **1.0**（AUR 描述"High-performance Vulkan 1.0"），扩展覆盖以仓库
  `terakan` 子树的 instance/device extensions 清单为准（M1 核对）。
- 已验证：Vulkan demo / vkcube 级别负载（Barts）。
- 着色器：Vulkan 的 SPIR-V → TeraScale VLIW ISA 是 terakan 自研后端
  （不等同于老的 r600 compiler；细节 M1 读码确认）。

## 关键调研题（M1 必答）

1. **Windows 内核依赖边界**：terakan Windows 路径驱动栈怎么构成？
   - 疑似依赖：官方最后一版 Catalyst KMD（2016 Crimson Beta）承担
     资源管理/调度；terakan 用户态 DLL 直填命令流。
   - 验证方法：读码（winsys/内核交互层 import 了哪些 KMT/DDI 入口）、
     Triang3l 公开状态更新、实机卸载 Catalyst 后观察 ICD 是否仍能枚举。
   - **影响**：决定 M4（接自研 KMD）需要替换的通道清单。
2. **构建系统与工具链**：MSVC/clang-cl？Mesa 的 Meson 在 Windows 上的
   子项目裁剪范围；DXC/LLVM 依赖版本。
3. **支持的 ASIC 表**：从 PCI ID 表读出精确覆盖（预期 Evergreen/NI 全系，
   HD 2000-4000 的 TeraScale 1 待确认）。
4. **显示路径**：Vulkan 无 WSI 需求时可 headless 跑；Windows 窗口 WSI
   （VK_KHR_win32_surface）实现状态。

## 对本项目的意义

- 本项目目标卡 **HD 6490M（Seymour）/ HD 7470M（Caicos）** 均属 TeraScale 2，
  正是 terakan 的目标代际（terakan 官方演示卡为 HD 6800/Barts，同为 TeraScale 2
  家族，编程序列同源）。
- **M1**：CI 编出 `terakan_icd.dll` + ICD JSON，实机冒烟。
- **M4**：terakan 的 TeraScale 命令流/着色器后端直接复用，替换其内核
  通道为自研 `rokmd`（WDDM 2.0）。
- **M5**：terakan 的 Vulkan 版本上限决定 Zink/GL4.6 可达性（当前 1.0 →
  需要先把 1.3 关键特性补齐，才谈 GL 4.6）。

## M4 通道就绪状态（自研 KMD ⇄ ICD 契约，2026-11）

M2.8 起，`kmd/src/rok_escape.h`（v1 查询面）已是 KMD/gpu_probe/未来 ICD
三方同源的 ABI 契约头，terakan 接自研 KMD 时可直接编译复用：

| Escape 类型 | 内容 | ICD 用途 |
| --- | --- | --- |
| 1 QUERY_VERSION | KMD 版本 + WDDM DDI 版本（0x5023） | 识别 rokmd、协商能力 |
| 2 QUERY_PCI_ID | VID/DID/REV + TeraScale 家族号 | 装载期 ASIC 分发表键值 |
| 3 QUERY_VRAM / 5 QUERY_SEGMENT_INFO | VRAM/GTT 尺寸 + 段号（M3 填充） | VkMemoryHeap/Type 布局 |
| 4 QUERY_CAPS | 能力位（MMIO/段/模拟/引擎状态查询） | 特性门控 |
| 6 QUERY_SIM_STATUS | 模拟设备 PM4 解码直方图 | VM 联调 |
| 7/8 CCC get/set | Catalyst 设置兼容 | ADL shim |
| 9 QUERY_ENGINE_STATUS（M2.8 新增） | submitted/completed fence、in-flight、提交/分页计数、Responsive | **fence 进度轮询通道**（M3 接 IH 前的回收判据） |

安装侧：`umd/install-terakan.ps1` 支持系统级（HKLM，需管理员）与
`-PerUser`（HKCU，免管理员）两种 ICD 注册；临时测试可用
`VK_DRIVER_FILES`（loader ≥ 1.3.207）或旧名 `VK_ICD_FILENAMES`。

## 参考链接

- Phoronix: 《Open-Source "Terakan" Vulkan Driver For Radeon HD 6000 Series
  Shown On Windows》 <https://www.phoronix.com/news/Terakan-HD-6000-VLK-Windows>
- Phoronix: 《Mesa "Terakan" Driver Aims To Provide Vulkan Support For
  Radeon HD 6000 GPUs》（2023-05）
- Phoronix: 《Terakan Driver Continues Working To Bring Vulkan To The
  Radeon HD 6000 Series》（2023-11）
- AUR: `vulkan-terakan` <https://aur.archlinux.org/packages/vulkan-terakan>
- 仓库：<https://gitlab.freedesktop.org/Triang3l/mesa>
