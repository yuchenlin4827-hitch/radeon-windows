// 家族分发表单元测试（host 侧）—— v2，已迁移至 M3.1 精确表 API。
//
// 旧版测的是 rok_escape.h 内联启发式 RokGpuFamilyFromDeviceId（22 用例）。
// 该内联启发式已迁至 kmd/src/r600hw.c 并被精确 DID→芯片分发表取代：
//   * RokHwLookupChip     —— 120 个 TeraScale 2 DID（源：Mesa r600_pci_ids.h；
//                            与 UMD GL46 表一致性由 scripts/check_pci_tables.py 校验）
//   * RokHwFamilyHeuristic —— 表未收录 DID 的段规则兜底（与旧内联同规则）
// 本文件保留原 22 用例作回归：查表命中即期望 TS2（表全为 TeraScale 2）
// 并断言芯片名；未命中回退启发式，期望值与旧版一致。唯一例外：
//   * 0x6720（HD 6970M / Blackcomb）：旧段规则误判 TS3（0x6700-0x672F
//     整段归 Cayman），精确表证其为 BARTS / TS2 —— 移动版 HD 69xxM 本就
//     是 TeraScale 2，修正后期望 2。
// 编译运行（宿主，无需 WDK；kmock 提供内核原语 mock；源码须拷入 scratch
// 编译，令 r600hw.c 的 "precomp.h" 落到 kmock mock 版 —— 同 build-and-run.sh）：
//   SCRATCH=$(mktemp -d) && cp kmd/src/{adapter.h,sim.h,ccc.h,r600hw.h,r600hw.c,rok_escape.h} $SCRATCH/
//   cc -std=gnu11 -I tests/host_tests/kmock -I $SCRATCH scripts/test_family.c $SCRATCH/r600hw.c tests/host_tests/mock_registry.c -o /tmp/test_family
//   /tmp/test_family
#include <stdio.h>
#include <string.h>
#include "r600hw.h"

struct { unsigned id, want, chip; const char *name; } cases[] = {
    // chip 列：查表命中时期望的 ROK_HW_CHIP_*；HEUR 表示期望表未命中、走启发式。
    { 0x6760, 2, ROK_HW_CHIP_CAICOS,  "HD 7470M 实机候选 DID（Mesa 归 CAICOS）" },
    { 0x6761, 2, ROK_HW_CHIP_CAICOS,  "Seymour 段 DID（Mesa 归 CAICOS）" },
    { 0x6768, 2, ROK_HW_CHIP_CAICOS,  "Caicos HD 6490M 形态" },
    { 0x6779, 2, ROK_HW_CHIP_CAICOS,  "Caicos HD 7470M 实机候选 DID" },
    { 0x677B, 2, ROK_HW_CHIP_CAICOS,  "Caicos PRO" },
    { 0x6758, 2, ROK_HW_CHIP_TURKS,   "Turks HD 6670" },
    { 0x6738, 2, ROK_HW_CHIP_BARTS,   "Barts XT HD 6870" },
    { 0x6720, 2, ROK_HW_CHIP_BARTS,   "Blackcomb HD 6970M（旧启发式误判 TS3，精确表修正）" },
    { 0x6840, 2, ROK_HW_CHIP_TURKS,   "Thames/Turks HD 7670M（旧注释误标 Seymour）" },
    { 0x68B8, 2, ROK_HW_CHIP_JUNIPER, "Juniper HD 5770" },
    { 0x68F9, 2, ROK_HW_CHIP_CEDAR,   "Cedar HD 5450" },
    { 0x9647, 2, ROK_HW_CHIP_SUMO,    "Sumo HD 6520G（Llano APU）" },
    { 0x6718, 3, 0xFFFFFFFFu,         "Cayman XT HD 6970（TS3，不在 TS2 表）" },
    { 0x9903, 3, 0xFFFFFFFFu,         "Trinity A10-5800K 形态（TS3）" },
    { 0x9440, 2, 0xFFFFFFFFu,         "RV770 HD 4850（TS2，表未收录走启发式）" },
    { 0x94A0, 2, 0xFFFFFFFFu,         "RV740 HD 4770（TS2，启发式）" },
    { 0x9540, 2, 0xFFFFFFFFu,         "RV730 HD 4650（TS2，启发式）" },
    { 0x9400, 1, 0xFFFFFFFFu,         "RV670 HD 3870（TS1 边界）" },
    { 0x94C5, 1, 0xFFFFFFFFu,         "RV620 HD 3450（TS1）" },
    { 0x9615, 1, 0xFFFFFFFFu,         "RS780 HD 3200（TS1）" },
    { 0x9712, 1, 0xFFFFFFFFu,         "RS880 HD 4250（TS1）" },
    { 0x791E, 1, 0xFFFFFFFFu,         "RV635 HD 3650（TS1）" },
};

int main(void)
{
    unsigned i, fails = 0;
    ROK_HW_CHIP_INFO info;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        unsigned got;
        BOOLEAN hit = RokHwLookupChip(cases[i].id, &info);
        const char *how;

        if (hit)
        {
            got = info.Family;
            how = info.Name;
            if (cases[i].chip != 0xFFFFFFFFu && info.Chip != cases[i].chip)
            {
                printf("FAIL 0x%04X (%s): chip want %u got %u\n",
                       cases[i].id, cases[i].name, cases[i].chip, info.Chip);
                fails++;
                continue;
            }
        }
        else
        {
            got = RokHwFamilyHeuristic(cases[i].id);
            how = "heuristic";
            if (cases[i].chip != 0xFFFFFFFFu)
            {
                printf("FAIL 0x%04X (%s): expected exact-table hit, got fallback\n",
                       cases[i].id, cases[i].name);
                fails++;
                continue;
            }
        }

        if (got != cases[i].want)
        {
            printf("FAIL 0x%04X (%s): want TS%u got TS%u (%s)\n",
                   cases[i].id, cases[i].name, cases[i].want, got, how);
            fails++;
        }
        else
        {
            printf("ok    0x%04X (%s) -> TS%u [%s]\n",
                   cases[i].id, cases[i].name, got, how);
        }
    }
    printf("----\n%d/%d passed\n",
           (unsigned)(sizeof(cases) / sizeof(cases[0])) - fails,
           (unsigned)(sizeof(cases) / sizeof(cases[0])));
    return fails != 0;
}
