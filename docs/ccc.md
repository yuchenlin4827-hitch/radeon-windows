# CCC 设置兼容（Catalyst Control Center / Overdrive 体系）

> 用户诉求："能兼容 radeon 的官方 CCC 设置"。本文回答三个问题：
> **官方 CCC 的设置存在哪里、rokmd 认哪些、怎么与官方工具习惯对齐**。

## 1. 官方 CCC 的架构与 rokmd 的边界

官方 CCC / Radeon Settings 是 **绑死 Catalyst KMD（atikmdag）** 的：

- UI 层（.NET / Qt）→ ADL（atiadlxx.dll）→ 私有 ioctl/escape → atikmdag；
- atikmdag 与 dxgkrnl 的接口里带大量 AMD 私有 DDI/escape，rokmd 没有也
  不应伪造（伪造私有通道既无文档也无测试）；
- 因此 **CCC 本体无法直接驱动 rokmd** —— 这是架构事实，不是"未完成"。

rokmd 的兼容策略是"**设置兼容**"而非"**UI 兼容**"：

1. **注册表导入**：Catalyst 体系写在驱动键的设置值，rokmd 启动时识别并
   应用（同值名、同语义）；
2. **ADL 习惯保留**：`umd/adl/rok_adl.dll` 提供官方 ADL2 同名同签名的
   枚举面 + Overdrive 设置桥，CCC 生态的工具可以不改调用习惯接入；
3. **生效点**：M2.6 完成校验/存储/回读闭环；时钟/风扇的真实编程序列在
   M3（r600 PLL/VRAM 寄存器可用后）自动接管 —— 设置一旦写入，M3 后
   无需迁移。

## 2. 设置存放位置（官方体系）

| 位置 | 谁写 | 内容 |
| --- | --- | --- |
| `HKLM\SYSTEM\CurrentControlSet\Control\Class\{4D36E968-E325-11CE-BFC1-08002BE10318}\00XX` | 驱动安装/CCC | **驱动键**：Overdrive、面板适配、时钟覆盖等大量值名 |
| `HKLM\SYSTEM\...\Services\atikmdag\Profiles` | CCC 配置引擎 | per-app 图形设置（AA/AF/垂直同步覆盖） |
| `HKLM\SOFTWARE\AMD\CN`（Radeon Settings 时代） | Adrenalin | 每用户 UI 偏好 |

rokmd 读取 **驱动键**（KMD StartDevice 时
`DXGK_DEVICE_INFO.DeviceRegistryPath` 指向的位置）；
Profiles 引擎属 UMD/游戏画像体系，M5+（Zink/terakan 生态）再评估。

## 3. 值名映射表（rokmd 实现清单 —— kmd/src/ccc.c）

### 3.1 rokmd 专有名（推荐工具链写入）

| 值名 | 类型 | 语义 | KMD 字段 |
| --- | --- | --- | --- |
| `OverdriveEngineClockKHz` | REG_DWORD | 引擎时钟覆盖（KHz） | `EngineClockKHz` |
| `OverdriveMemoryClockKHz` | REG_DWORD | 显存时钟覆盖（KHz） | `MemoryClockKHz` |
| `Overdrive` | REG_DWORD 0/1 | Overdrive 手动档开关 | `OverdriveEnable` |
| `OverdriveFanSpeedPercent` | REG_DWORD 0–100 | 固定风扇转速 | `FanSpeedPercent` |

### 3.2 Catalyst 遗留名（尽力识别；专有名优先）

| 值名 | 类型 | 语义 |
| --- | --- | --- |
| `EnableOverdrive` | REG_DWORD 0/1 | 同 `Overdrive` |
| `EngineClockOverride` | REG_DWORD | 同 `OverdriveEngineClockKHz` |
| `MemoryClockOverride` | REG_DWORD | 同 `OverdriveMemoryClockKHz` |

> 老版本 Catalyst 在驱动键写过不少值名（不同 CCC 代际不一致），
> 3.2 是**高置信同语义**清单；发现新的历史值名时按"遗留名兜底"
> 模式加入 `RokCccImport`，不覆盖专有名。

### 3.3 范围钳制（与官方 Overdrive 的 range-clamp 行为一致）

| 字段 | 下限 | 上限 |
| --- | --- | --- |
| 引擎时钟 | 50 MHz | 2 GHz |
| 显存时钟 | 50 MHz | 2 GHz |
| 风扇 | 0% | 100% |

超界值导入/写入时被钳制并回读生效值 —— 与官方 Overdrive "超出范围
回落"语义一致。

## 4. escape 通道（工具 → KMD）

报文布局 `[ROK_ESCAPE_HEADER][ROK_ESC_CCC_SETTINGS]`（16+32 字节），
类型 `ROK_ESC_TYPE_CCC_GET_SETTINGS` / `ROK_ESC_TYPE_CCC_SET_SETTINGS`：

```c
typedef struct {   // 契约见 kmd/src/rok_escape.h（两处严格一致）
    UINT32 Version;           // 1
    UINT32 ValidMask;         // 0x1=engine 0x2=mem 0x4=od 0x8=fan
    UINT32 EngineClockKHz;
    UINT32 MemoryClockKHz;
    UINT32 OverdriveEnable;
    UINT32 FanSpeedPercent;
    UINT32 Source;            // 0=none 1=registry 2=escape
    UINT32 Reserved;
} ROK_ESC_CCC_SETTINGS;
```

`Source` 字段可区分设置来自注册表导入还是 escape 写入 —— gpu_probe
`[2.6]` 段直接展示（`ccc : mask=... source=N`）。

## 5. ADL 兼容 shim（umd/adl/rok_adl.dll）

| 能力 | 状态 |
| --- | --- |
| `ADL2_Main_Control_Create/Destroy` | ✅（上下文自管，回调保留签名） |
| `ADL2_Adapter_NumberOfAdapters_Get` | ✅（仅枚举 rokmd 绑定适配器） |
| `ADL2_Adapter_AdapterInfo_Get` | ✅（核心字段：UDID/名称/版本，官方布局前缀） |
| `ADL2_Adapter_Active_Get` / `ID_Get` | ✅ |
| `ADL2_Overdrive5_Settings_Get/Set` | ✅（rokmd 扩展面：min/max/default + 当前值） |
| `ADL2_Display_*` / 温度 / RPM 细粒度 | ❌ M3+（显示路径与传感器 DDI 之后） |
| 官方 OD5 完整逐字段 ABI | ❌ 不冒充 —— 各 SDK 版本布局有差异，需实机对拍后再补 |

工具链用法见 [umd/adl/README.md](../umd/adl/README.md)。

## 6. 验证步骤

```text
1. 装好 rokmd（真机或 VM 模拟模式均可）；
2. 管理员 PowerShell 写一条测试设置（驱动键路径从
   HKLM\SYSTEM\CCS\Control\Class\{4D36E968...}\ 下找 rokmd 实例）：
     reg add "<驱动键>" /v OverdriveEngineClockKHz /t REG_DWORD /d 600000 /f
3. 重启设备（设备管理器 禁用/启用 rokmd 适配器）；
4. gpu_probe.exe → [2.6] 应显示：
     ccc : mask=0x1 engine=600000KHz ... (source=1)
5. 经 ADL shim 写入再读回（adl_smoke.exe 展示 Get 面）。
```

## 7. 路线

| 阶段 | 内容 |
| --- | --- |
| M2.6 ✅ | 注册表导入 + escape get/set + ADL shim 枚举/Settings 桥 + gpu_probe 展示 |
| M3 | 时钟设置生效到硬件（r600 PLL 编程序列接入 `RokCccSetSettings` 的生效点）；风扇 PWM |
| M4 | terakan ICD 的 Vulkan 时钟查询对接（VkPhysicalDeviceLimits 与当前时钟一致性） |
| M5+ | Profiles（per-app）体系评估；OD5 完整 ABI 对拍 |
