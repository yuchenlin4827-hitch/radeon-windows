// SPDX-License-Identifier: MIT
// kmock2/precomp.h — 替身预编译头：与 kmd/src/precomp.h 同构
// （ntddk → 版本锁定 → dispmprt → d3dkmddi），使驱动源文件里的
// #include "precomp.h" 在 -Ikmock2 下落到本 mock 环境。
#pragma once

#include "ntddk.h"

// 锁定 WDDM 2.0 基线（与真实 precomp.h 相同的数值）。
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x5023
#endif

#include "dispmprt.h"
#include "d3dkmddi.h"

// 内存池标签（与真实驱动一致）
#define ROK_POOL_TAG 'DkoR'

// 内核构建标记（rok_escape.h 依此选内核分支）
#define ROK_KERNEL_BUILD 1
