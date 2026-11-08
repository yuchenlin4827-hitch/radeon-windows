#!/usr/bin/env python3
"""核对 driver.c 的 DRIVER_INITIALIZATION_DATA 赋值是否覆盖 WDK 26100
在 DXGKDDI_INTERFACE_VERSION=0x5023 下所有已编译的结构体成员。

逻辑：
1. 从 dispmprt.h 提取 DRIVER_INITIALIZATION_DATA 的成员清单（含 #if 版本 guard）。
2. 根据接口版本常量表计算哪些成员在 0x5023 下被编译进来。
3. 从 driver.c 提取所有 g_DdiData.<member> = ... 赋值。
4. 报告：0x5023 下应非空但未赋值的成员；赋值了但 0x5023 下不存在的成员（编译错误隐患）。
"""
import re, sys, os

DISPMPRT = os.environ.get(
    "WDK_DISPPMRT",
    "/tmp/wdkx/c/Include/10.0.26100.0/km/dispmprt.h")
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRIVER_C = os.path.join(REPO_ROOT, "kmd/src/driver.c")

VERSIONS = {
    "DXGKDDI_INTERFACE_VERSION_VISTA": 0x1052,
    "DXGKDDI_INTERFACE_VERSION_VISTA_SP1": 0x1053,
    "DXGKDDI_INTERFACE_VERSION_WIN7": 0x2005,
    "DXGKDDI_INTERFACE_VERSION_WIN8": 0x300E,
    "DXGKDDI_INTERFACE_VERSION_WDDM1_3": 0x4002,
    "DXGKDDI_INTERFACE_VERSION_WDDM1_3_PATH_INDEPENDENT_ROTATION": 0x4003,
    "DXGKDDI_INTERFACE_VERSION_WDDM2_0": 0x5023,
    "DXGKDDI_INTERFACE_VERSION_WDDM2_1": 0x6003,
    "DXGKDDI_INTERFACE_VERSION_WDDM2_2": 0x700A,
    "DXGKDDI_INTERFACE_VERSION_WDDM2_3": 0x8001,
    "DXGKDDI_INTERFACE_VERSION_WDDM2_4": 0x9006,
    "DXGKDDI_INTERFACE_VERSION_WDDM2_5": 0xA00B,
    "DXGKDDI_INTERFACE_VERSION_WDDM2_6": 0xB004,
    "DXGKDDI_INTERFACE_VERSION_WDDM2_7": 0xC004,
    "DXGKDDI_INTERFACE_VERSION_WDDM2_8": 0xD001,
    "DXGKDDI_INTERFACE_VERSION_WDDM2_9": 0xE003,
    "DXGKDDI_INTERFACE_VERSION_WDDM3_0": 0xF003,
    "DXGKDDI_INTERFACE_VERSION_WDDM3_1": 0x10004,
    "DXGKDDI_INTERFACE_VERSION_WDDM3_2": 0x11007,
}
PINNED = 0x5023  # precomp.h 锁定的 WDDM 2.0

src = open(DISPMPRT, encoding="utf-8", errors="replace").read()
start = src.index("typedef struct _DRIVER_INITIALIZATION_DATA {")
end = src.index("} DRIVER_INITIALIZATION_DATA, *PDRIVER_INITIALIZATION_DATA;", start)
body = src[start:end]

# 按行扫描，维护 #if 栈
cond_stack = []          # list of active_at_pinned(bool)
members = {}             # name -> pinned_active(bool)
fn_members = {}          # name -> is function-pointer member
for line in body.splitlines():
    s = line.strip()
    m_if = re.match(r"#if\s*\((.*)\)", s)
    if m_if:
        expr = m_if.group(1)
        m_ver = re.search(r"DXGKDDI_INTERFACE_VERSION\s*(>=|<)\s*(DXGKDDI_INTERFACE_VERSION_\w+)", expr)
        active = True
        if m_ver:
            op, name = m_ver.group(1), m_ver.group(2)
            ver = VERSIONS.get(name)
            if ver is None:
                print(f"WARN unknown version const {name}")
                ver = 0xFFFFFFFF
            active = (PINNED >= ver) if op == ">=" else (PINNED < ver)
        cond_stack.append(active)
    elif s.startswith("#endif"):
        if cond_stack:
            cond_stack.pop()
    else:
        # 函数指针成员 = PDXGKDDI* 类型；PVOID/VOID* 是占位保留字段（保持 NULL）
        m_mem = re.match(r"(PDXGKDDI[A-Z0-9_]*|PVOID|VOID\s*\*)\s+(DxgkDdi[A-Za-z0-9_]*|Reserved\d*)\s*;", s)
        if m_mem:
            name = m_mem.group(2)
            is_fn = m_mem.group(1).startswith("PDXGKDDI")
            members[name] = all(cond_stack) if cond_stack else True
            fn_members[name] = is_fn

assigned = set(re.findall(r"g_DdiData\.(DxgkDdi[A-Za-z0-9_]*)\s*=", open(DRIVER_C, encoding="utf-8").read()))

active_members = {n for n, a in members.items() if a}
active_fn = {n for n in active_members if fn_members.get(n)}

missing = sorted(n for n in active_fn if n not in assigned)
extra = sorted(n for n in assigned if n not in members)
extra_inactive = sorted(n for n in assigned if n in members and n not in active_members)

print(f"WDK 26100 DRIVER_INITIALIZATION_DATA: {len(members)} members total, "
      f"{len(active_fn)} function members active at 0x{PINNED:X} (WDDM 2.0)")
print(f"driver.c assignments: {len(assigned)}")
print()
if missing:
    print(f"!! MISSING at 0x{PINNED:X} (compiled-in but NULL in driver.c):")
    for n in missing:
        print(f"   - {n}")
else:
    print("OK: no NULL function members at 0x5023 (WDDM 2.0) — table complete")
if extra:
    print(f"!! ASSIGNED BUT NOT IN STRUCT (would be compile error): {extra}")
if extra_inactive:
    print(f"   note: assigned but compiled-out at 0x{PINNED:X}: {extra_inactive}")

reserved = sorted(n for n in active_members if not fn_members.get(n))
print(f"PVOID reserved members (zeroed, fine): {reserved}")
sys.exit(1 if missing or extra else 0)
