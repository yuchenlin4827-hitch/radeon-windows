// SPDX-License-Identifier: MIT
// kmock/ntddk.h — 宿主测试的最小 ntddk 桩。
//
// 用途：rok_escape.h 在 ROK_KERNEL_BUILD=1（kmock/precomp.h 保真定义）下
// 走内核分支 `#include <ntddk.h>`。宿主不需要真实 DDK：该分支只用到
// UINT32/UINT64，而 kmock/precomp.h 已提供同宽 typedef；真正的内核类型
// （NTSTATUS/LIST_ENTRY/池/注册表…）也全部由 kmock/precomp.h 提供。
// 本桩存在仅为让 `#include <ntddk.h>` 可解析。KMD 真实构建用的是
// WDK 的 ntddk.h，与本桩无关。
#pragma once
