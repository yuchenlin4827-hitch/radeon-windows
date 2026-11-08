#!/usr/bin/env python3
"""gen_ucode.py — 从 linux-firmware 的 radeon/*.bin 生成 kmd/src/evergreen_ucode.c

dword 序列 = 内核 be32_to_cpup 后写入 CP_PFP_UCODE_DATA / CP_ME_RAM_DATA 的值
（与 Linux radeon r600_cp_load_microcode 语义一致）。

用法：
  python3 scripts/gen_ucode.py /tmp/CAICOS_pfp.bin /tmp/CAICOS_me.bin \
      > kmd/src/evergreen_ucode.c
"""
import struct
import sys

PFP_DWORDS = 1120   # EVERGREEN_PFP_UCODE_SIZE（radeon_ucode.h）
ME_DWORDS = 1376    # EVERGREEN_PM4_UCODE_SIZE


def load_be(path, expect_dwords, label):
    data = open(path, 'rb').read()
    if len(data) != expect_dwords * 4:
        raise SystemExit('%s: %s bytes != %d dwords' %
                         (path, len(data), expect_dwords))
    return struct.unpack('>%dI' % expect_dwords, data)


def rows(dwords, per_row=8):
    out = []
    for i in range(0, len(dwords), per_row):
        out.append('    ' + ', '.join('0x%08X' % x for x in dwords[i:i + per_row]) + ',')
    return '\n'.join(out)


def main():
    pfp_path, me_path = sys.argv[1], sys.argv[2]
    pfp = load_be(pfp_path, PFP_DWORDS, 'PFP')
    me = load_be(me_path, ME_DWORDS, 'ME')

    print('// SPDX-License-Identifier: MIT')
    print('// evergreen_ucode.c — TeraScale 2 CP 微码（PFP + ME），M3.4 硬件 ring 初始化用。')
    print('//')
    print('// 来源与许可：')
    print('//   radeon/CAICOS_pfp.bin / radeon/CAICOS_me.bin —— linux-firmware.git')
    print('//   （gitlab.com/kernel-firmware/linux-firmware，main 分支，2026-11 抓取）。')
    print('//   AMD 微码二进制，随 linux-firmware 分发条款再分发；本文件仅为其')
    print('//   机械转换：dword 序列 = 内核 be32_to_cpup 后写入 CP_PFP_UCODE_DATA /')
    print('//   CP_ME_RAM_DATA 的值（与 Linux radeon r600_cp_load_microcode 一致）。')
    print('//')
    print('// 嵌入原因：Windows 无 request_firmware；WDDM KMD 必须自带微码')
    print('//   （冷启动场景 VBIOS 不加载 CP 微码，ring 无法启动）。')
    print('//')
    print('// 目标家族：Seymour 0x6760 / Caicos 0x677x（CHIP_CAICOS，TS2/NI）。')
    print('//   实测 TURKS_me == CAICOS_me（sha256 一致）；TURKS_pfp 不同 ——')
    print('//   Whistler/Turks（HD 6490M）路径在 M3.4b 追加 TURKS_pfp。')
    print('//')
    print('// 本文件为生成物（scripts/gen_ucode.py），勿手改。')
    print('')
    print('#include "precomp.h"')
    print('')
    print('const UINT32 RokEvergreenPfpUcode[%d] = {' % PFP_DWORDS)
    print(rows(pfp))
    print('};')
    print('')
    print('const UINT32 RokEvergreenMeUcode[%d] = {' % ME_DWORDS)
    print(rows(me))
    print('};')
    print('')
    print('C_ASSERT(RTL_NUMBER_OF(RokEvergreenPfpUcode) == %d);' % PFP_DWORDS)
    print('C_ASSERT(RTL_NUMBER_OF(RokEvergreenMeUcode)  == %d);' % ME_DWORDS)
    print('')


if __name__ == '__main__':
    main()
