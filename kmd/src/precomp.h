// SPDX-License-Identifier: MIT
// rokmd — Radeon Open Kernel Mode Driver
// WDDM 2.0 显示微型端口（KMD）骨架 —— 预编译头
//
// 骨架阶段策略：把 dxgkrnl 需要的 DDI 全部挂上（stub 返回未实现 + 日志留痕），
// 先做到"可加载、可观测、不蓝屏"；M3 起逐个替换为真实硬件实现（见 docs/roadmap.md）。
#pragma once

#include <ntddk.h>
#include <dispmprt.h>

// 显式选择 WDDM 2.0（WIN10）DDI 集，不依赖头文件的默认值。
// 后续若要启用更高版本特性（HwSch 等），在此处上调并按
// d3dkmddi.h 的版本守卫补充对应 DDI。
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION DXGKDDI_INTERFACE_VERSION_WIN10
#endif

#include <d3dkmddi.h>

// 内存池标签（调试时用于 pool 查找）
#define ROK_POOL_TAG 'DkoR'
