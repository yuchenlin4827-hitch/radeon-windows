// SPDX-License-Identifier: MIT
// rokmd — Radeon Open Kernel Mode Driver
// WDDM 2.0 显示微型端口（KMD）骨架 —— 预编译头
//
// 包含顺序很关键：d3dukmdt.h（SDK shared）提供 DXGKDDI_INTERFACE_VERSION_*
// 常量；随后锁定 WDDM 2.0 基线，再引入 dispmprt.h / d3dkmddi.h。
#pragma once

#include <ntddk.h>
#include <d3dukmdt.h>

// 锁定 WDDM 2.0 基线（0x5023）。上调节本会启用更新的 DDI 字段，
// 需与 docs/wddm-vulkan-limits.md 的决策一致。
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WDDM2_0
#endif

#include <dispmprt.h>
#include <d3dkmddi.h>

// 内存池标签（调试时用于 pool 查找）
#define ROK_POOL_TAG 'DkoR'
