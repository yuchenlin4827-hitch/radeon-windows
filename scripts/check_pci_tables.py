#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# check_pci_tables.py — KMD 精确分发表与 UMD GL46 门控表的一致性机械校验。
#
# 两处表同源（Mesa include/pci_ids/r600_pci_ids.h，MIT）：
#   * kmd/src/r600hw.c        : { 0x6720, ROK_HW_CHIP_BARTS }   （M3.1 起）
#   * umd/gl46/gl46_r600g.c   : { 0x6720, GL46_CHIP_BARTS }
#
# 校验项：
#   1. KMD 表非空、DID 严格升序、无重复；
#   2. 两表重叠 DID 的芯片归属必须一致（防单侧修改漂移）；
#   3. 打印两侧规模。任何不符 exit 1。
import re
import sys

KMD_SRC = "kmd/src/r600hw.c"
UMD_SRC = "umd/gl46/gl46_r600g.c"


def parse(path, pattern):
    table = {}
    with open(path, encoding="utf-8") as f:
        for m in re.finditer(pattern, f.read()):
            did = int(m.group(1), 16)
            chip = m.group(2)
            if did in table and table[did] != chip:
                print(f"[check_pci] ERROR: {path}: DID 0x{did:04X} 重复且冲突")
                sys.exit(1)
            table[did] = chip
    return table


def main():
    kmd = parse(KMD_SRC, r"\{\s*(0x[0-9A-Fa-f]+),\s*ROK_HW_CHIP_([A-Z0-9_]+)\s*\}")
    umd = parse(UMD_SRC, r"\{\s*(0x[0-9A-Fa-f]+),\s*GL46_CHIP_([A-Z0-9_]+)\s*\}")

    dids = sorted(kmd)
    if not dids:
        print("[check_pci] ERROR: KMD 表为空")
        return 1
    for a, b in zip(dids, dids[1:]):
        if a >= b:
            print(f"[check_pci] ERROR: KMD 表非严格升序：0x{a:04X} >= 0x{b:04X}")
            return 1

    overlap = [d for d in dids if d in umd]
    mismatch = [d for d in overlap if kmd[d] != umd[d]]
    if mismatch:
        for d in mismatch[:10]:
            print(f"[check_pci] ERROR: DID 0x{d:04X}: KMD={kmd[d]} UMD={umd[d]}")
        return 1

    print(f"[check_pci] KMD 精确表: {len(kmd)} DIDs（升序无重复 ✓）")
    print(f"[check_pci] UMD GL46 表: {len(umd)} DIDs")
    print(f"[check_pci] 重叠 {len(overlap)} DIDs，芯片归属全部一致 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
