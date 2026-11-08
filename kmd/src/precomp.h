// SPDX-License-Identifier: MIT
// rokmd — Radeon Open Kernel Mode Driver
// WDDM 2.0 显示微型端口（KMD）骨架 —— 预编译头
//
// DXGKDDI_INTERFACE_VERSION 的常量名定义在 SDK 的 d3dukmdt.h，但该头文件
// 禁止直接包含（#error 守卫）。因此这里直接取 WDDM2_0 的数值（0x5023，
// 见 SDK d3dukmdt.h），并在引入 dispmprt.h / d3dkmddi.h 之前锁定。
#pragma once

#include <ntddk.h>

// 锁定 WDDM 2.0 基线（= DXGKDDI_INTERFACE_VERSION_WDDM2_0）。
// 上调节本会启用更新的 DDI 字段，需与 docs/wddm-vulkan-limits.md 的决策一致。
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x5023
#endif

#include <dispmprt.h>
#include <d3dkmddi.h>

// 内存池标签（调试时用于 pool 查找）
#define ROK_POOL_TAG 'DkoR'

// 驱动版本（escape QUERY_VERSION 上报；与 Release tag 同步升位）。
#define ROK_VERSION_MAJOR   0
#define ROK_VERSION_MINOR   2
#define ROK_VERSION_PATCH   3

// 内核构建标记：rok_escape.h 依此在"内核类型（ntddk）"与
// "用户态类型（stdint）"之间切换 —— 同一份 ABI 契约头同时供
// KMD、gpu_probe（用户态）与未来的 terakan ICD 编译。
#define ROK_KERNEL_BUILD 1
