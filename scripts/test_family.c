// 家族分发表单元测试（host 侧，gcc 直接编译 rok_escape.h 用户态路径）。
// 用法：gcc -std=c99 scripts/test_family.c -o /tmp/test_family && /tmp/test_family
#include <stdio.h>
#include "../kmd/src/rok_escape.h"

struct { unsigned id, want; const char *name; } cases[] = {
    { 0x6760, 2, "Seymour HD 7470M（实机）" },
    { 0x6761, 2, "Seymour LP" },
    { 0x6768, 2, "Caicos HD 6490M 形态" },
    { 0x6779, 2, "Caicos HD 7470M" },
    { 0x677B, 2, "Caicos PRO" },
    { 0x6758, 2, "Turks HD 6670" },
    { 0x6738, 2, "Barts XT HD 6870" },
    { 0x6840, 2, "Thames HD 7670M" },
    { 0x68B8, 2, "Juniper HD 5770" },
    { 0x68F9, 2, "Cedar HD 5450" },
    { 0x9440, 2, "RV770 HD 4850" },
    { 0x94A0, 2, "RV740 HD 4770" },
    { 0x9540, 2, "RV730 HD 4650" },
    { 0x9647, 2, "Sumo HD 6520G" },
    { 0x6718, 3, "Cayman XT HD 6970" },
    { 0x6720, 3, "Blackcomb HD 6970M" },
    { 0x9903, 3, "Trinity A10-5800K 形态" },
    { 0x9400, 1, "RV670 HD 3870（TS1 边界）" },
    { 0x94C5, 1, "RV620 HD 3450" },
    { 0x9615, 1, "RS780 HD 3200" },
    { 0x9712, 1, "RS880 HD 4250" },
    { 0x791E, 1, "RV635 HD 3650" },
};

int main(void)
{
    unsigned i, fails = 0;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        unsigned got = RokGpuFamilyFromDeviceId(cases[i].id);
        if (got != cases[i].want)
        {
            printf("FAIL 0x%04X (%s): want TS%u got TS%u\n",
                   cases[i].id, cases[i].name, cases[i].want, got);
            fails++;
        }
        else
        {
            printf("ok    0x%04X (%s) -> TS%u\n",
                   cases[i].id, cases[i].name, got);
        }
    }
    printf("----\n%d/%d passed\n",
           (unsigned)(sizeof(cases) / sizeof(cases[0])) - fails,
           (unsigned)(sizeof(cases) / sizeof(cases[0])));
    return fails != 0;
}
