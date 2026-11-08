# ADL 兼容 shim（rok_adl.dll）—— 官方 CCC 设置生态的桥梁

`rok_adl.dll` 是 rokmd 的 ADL（AMD Display Library，官方
`atiadlxx.dll`）兼容 shim：**与官方 ADL2 接口同名同签名的枚举面**
+ **rokmd 专有的 Overdrive 设置桥**，底层全部走 `kmd/src/rok_escape.h`
的 escape 契约。

## 为什么需要它

官方 CCC（Catalyst Control Center）/ Radeon Settings 本体绑定
Catalyst KMD（atikmdag），无法驱动 rokmd（见 docs/ccc.md 的架构分析）。
但"CCC 时代的设置习惯"可以保留：

- 工具链用 **ADL 调用习惯**（`ADL2_Main_Control_Create` →
  `ADL2_Adapter_NumberOfAdapters_Get` → `ADL2_Adapter_AdapterInfo_Get` …）
  访问 rokmd 适配器；
- Overdrive 类设置（引擎/显存时钟覆盖、手动档、风扇）经
  `ADL2_Overdrive5_Settings_Get/Set` 读写，KMD 校验/钳制后存储，
  **M3 寄存器编程就绪后直接生效到硬件**。

## 构建产物

| 产物 | 说明 |
| --- | --- |
| `rok_adl.dll` | ADL shim 本体（本目录 `rok_adl.c/h`） |
| `adl_smoke.exe` | 链接冒烟客户端（`tests/adl_smoke/`，CI 同时验证 DLL 导出可链接） |

## 使用（链接 rok_adl.dll）

```c
#include "rok_adl.h"    // 本目录头文件（或官方 ADL 头 + 本文件扩展）

ROKADLContext ctx = NULL;
if (ADL2_Main_Control_Create(NULL, 0, &ctx) == ROK_ADL_OK) {
    int n = 0;
    ADL2_Adapter_NumberOfAdapters_Get(ctx, &n);
    for (int i = 0; i < n; i++) {
        ROKAdapterInfo info; int active = 0;
        ZeroMemory(&info, sizeof(info));
        info.iSize = sizeof(info);
        ADL2_Adapter_AdapterInfo_Get(ctx, &info, sizeof(info));
        ADL2_Adapter_Active_Get(ctx, i, &active);
        printf("[%d] %s (%s)\n", i, info.strAdapterName, info.strUDID);

        ROKOD5Settings od;
        if (ADL2_Overdrive5_Settings_Get(ctx, i, &od) == ROK_ADL_OK)
            printf("    engine=%uKHz mem=%uKHz od=%u fan=%u%%\n",
                   od.EngineClock, od.MemoryClock, od.OverdriveEnable,
                   od.FanSpeedPercent);
    }
    ADL2_Main_Control_Destroy(ctx);
}
```

## 与官方 ADL 的差异（诚实清单）

| 项目 | 官方 ADL | rok_adl shim |
| --- | --- | --- |
| 枚举适配器 | 所有 AMD 适配器（VID 0x1002） | 仅 rokmd 绑定的适配器（escape 探测通过者） |
| `AdapterInfo` | 官方完整布局 | 核心字段兼容（iSize/iAdapterIndex/strUDID/iVendorID/strAdapterName/版本/日期），后续字段零值 |
| Overdrive5 | 完整 OD5 ABI（时钟/风扇/电压/温度逐项） | 单一 Settings Get/Set（min/max/default + 当前值），范围与 KMD `ccc.h` 一致 |
| Main_Control malloc 回调 | 用于上下文分配 | 保留签名但自管内存（HeapAlloc/HeapFree） |
| 显示器/风扇 RPM/温度 | ADL2_Display_* 等 | 未实现（显示路径 M3+；返回 `ROK_ADL_ERR_NOT_SUPPORTED` 由调用方容错） |

OD5 完整 ABI 未冒充的原因：官方 OD5 结构体布局在不同 SDK 版本间有
微调，逐字段对齐需要实机对拍；先交付语义等价的单一 Settings 面，
后续如需接第三方监控工具再按实测补齐（见 docs/ccc.md 路线）。

## CI

`.github/workflows/build-adl.yml`：mingw（`-shared`）与 MSVC（`cl /LD`）
双工具链编 `rok_adl.dll`，并用 `adl_smoke.c` 验证导出可链接。
