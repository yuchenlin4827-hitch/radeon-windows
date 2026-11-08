// SPDX-License-Identifier: MIT
// adl_smoke — rok_adl.dll 链接冒烟：枚举 rokmd 适配器并读取 Overdrive 设置。
// CI 里同时验证 DLL 导出可链接（MSVC 链 rok_adl.lib / mingw -lrok_adl）。
#include <windows.h>
#include <stdio.h>
#include "rok_adl.h"

int main(void)
{
    ROKADLContext ctx = NULL;
    int n = 0, i;

    if (ADL2_Main_Control_Create(NULL, 0, &ctx) != ROK_ADL_OK)
    {
        printf("ADL smoke: no rokmd adapter found (skip) — exit 0\n");
        return 0;   // 无 rokmd 环境不算失败（CI 无驱动可装）
    }

    ADL2_Adapter_NumberOfAdapters_Get(ctx, &n);
    printf("ADL smoke: %d rokmd adapter(s)\n", n);

    for (i = 0; i < n; i++)
    {
        ROKAdapterInfo info;
        ROKOD5Settings od;
        int active = 0;

        ZeroMemory(&info, sizeof(info));
        info.iSize = (int)sizeof(info);
        if (ADL2_Adapter_AdapterInfo_Get(ctx, &info, (int)sizeof(info)) == ROK_ADL_OK)
            printf("  [%d] %s  %s  ver=%s\n", i, info.strAdapterName,
                   info.strUDID, info.strDisplayDriverVersion);

        ADL2_Adapter_Active_Get(ctx, i, &active);
        printf("      active=%d\n", active);

        if (ADL2_Overdrive5_Settings_Get(ctx, i, &od) == ROK_ADL_OK)
            printf("      od: mask=%#x engine=%uKHz mem=%uKHz od=%u fan=%u%%\n",
                   od.ValidMask, od.EngineClock, od.MemoryClock,
                   od.OverdriveEnable, od.FanSpeedPercent);
    }

    ADL2_Main_Control_Destroy(ctx);
    printf("ADL smoke: PASS\n");
    return 0;
}
