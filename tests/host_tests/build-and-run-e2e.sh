#!/usr/bin/env bash
# host_tests E2E — 模拟设备端到端运行（L1.5）。
#
# 编译 kmd/src 的全部五个真实源文件（driver/adapter/ddi/sim/ccc），
# 以 kmock2 为内核环境、vdxgkrnl 为虚拟 dxgkrnl，在主机上完整走一遍
# WDDM 2.0 管线（AddDevice → StartDevice → 分配 → 上下文 → 提交 →
# fence 回收 → escape → 拆除），ASan/UBSan 全程开启。
#
# 与 build-and-run.sh（kmock1 逻辑单测）互为补充：那份测 ccc.c/sim.c
# 的纯逻辑，这份测 DDI 管线与生命周期平衡（池分配泄漏即失败）。
#
# 用法：bash tests/host_tests/build-and-run-e2e.sh
set -euo pipefail
cd "$(dirname "$0")"

REPO_ROOT="$(cd ../.. && pwd)"
SCRATCH="$(mktemp -d)"
trap 'rm -rf "${SCRATCH}"' EXIT

KMD_SRC="${REPO_ROOT}/kmd/src"
FILES="adapter.h adapter.c sim.h sim.c ccc.h ccc.c ddi.h ddi.c driver.c precomp.h rok_escape.h ring.h ring.c gfxinit.h gfxinit.c regs_ts2.h evergreen_ucode.c"
COMPILE_SRCS="adapter.c sim.c ccc.c ddi.c driver.c ring.c gfxinit.c evergreen_ucode.c"

echo "[e2e] repo = ${REPO_ROOT}"

# 1) 记录被测源文件的 sha256（先校验，后编译）。
echo "[e2e] sha256 manifest (pre-build):"
( cd "${KMD_SRC}" && sha256sum ${FILES} ) | tee "${SCRATCH}/manifest.pre"

# 2) 拷贝被测源码进 scratch（相对 include 同目录解析）。
for f in ${FILES}; do
    cp "${KMD_SRC}/${f}" "${SCRATCH}/"
done

CC="${CC:-cc}"
command -v "${CC}" >/dev/null || CC=clang

# 3) 编译（-Ikmock2 在最前：#include "precomp.h" 落到 mock 版；
#    -Iscratch 提供同目录 include 的驱动头文件）。
# -fno-sanitize=shift-base：驱动按 MSVC 语义使用 (1 << 31)（如 CPUID
# ECX.bit31 掩码、PM4 包头），宿主 UBSan 的 shift-base 子检查会误报，
# 其余 shift 检查保持开启。
"${CC}" -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -Wno-multichar -Wno-discarded-qualifiers -Wno-sign-compare \
    -fsanitize=address,undefined -fno-sanitize=shift-base \
    -fno-omit-frame-pointer \
    -I kmock2 -I "${SCRATCH}" -I . \
    -o "${SCRATCH}/run_simdevice" \
    run_simdevice.c vdxgkrnl.c \
    "${SCRATCH}/adapter.c" "${SCRATCH}/sim.c" "${SCRATCH}/ccc.c" \
    "${SCRATCH}/ddi.c" "${SCRATCH}/driver.c" \
    "${SCRATCH}/ring.c" "${SCRATCH}/gfxinit.c" "${SCRATCH}/evergreen_ucode.c"

# 4) 编译后再校验：确保编译过程没有副作用改动源文件。
( cd "${KMD_SRC}" && sha256sum ${FILES} ) > "${SCRATCH}/manifest.post"
diff "${SCRATCH}/manifest.pre" "${SCRATCH}/manifest.post" >/dev/null \
    || { echo "[e2e] ERROR: kmd sources changed during build"; exit 2; }

# 5) 运行（ASan/UBSan 开启；退出码 = 失败检查数）。
ASAN_OPTIONS=detect_leaks=1 "${SCRATCH}/run_simdevice"
