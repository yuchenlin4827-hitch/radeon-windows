#!/usr/bin/env bash
# host_tests — rokmd KMD 纯逻辑单元测试（Linux/macOS 宿主，无 WDK）
#
# 原理：
#   ccc.c / sim.c 的纯逻辑（CCC 导入钳制、PM4 解码、sim 三路判定）不依赖
#   内核服务，只依赖少量内核原语。本脚本把它们拷进 scratch（**不含
#   precomp.h**），以 -Ikmock 提供 mock 内核环境编译 —— 源文件里的
#   `#include "precomp.h"` 落到 kmock/ 的 mock 版。
#
#   测试的是仓库真实字节：编译前后各做一次 sha256 校验，任何被测源文件
#   变动而测试未同步时，manifest 比对会失败（防测试漂移）。
#
# 用法：bash tests/host_tests/build-and-run.sh
set -euo pipefail
cd "$(dirname "$0")"

REPO_ROOT="$(cd ../.. && pwd)"
SCRATCH="$(mktemp -d)"
trap 'rm -rf "${SCRATCH}"' EXIT

KMD_SRC="${REPO_ROOT}/kmd/src"
FILES="adapter.h sim.h ccc.h ccc.c sim.c"

echo "[host-tests] repo = ${REPO_ROOT}"

# 1) 记录被测源文件的 sha256（先校验，后编译）。
echo "[host-tests] sha256 manifest (pre-build):"
( cd "${KMD_SRC}" && sha256sum ${FILES} ) | tee "${SCRATCH}/manifest.pre"

# 2) 拷贝被测源码（不含 precomp.h —— mock 版经 -Ikmock 提供）。
for f in ${FILES}; do
    cp "${KMD_SRC}/${f}" "${SCRATCH}/"
done

CC="${CC:-cc}"
command -v "${CC}" >/dev/null || CC=clang

# 3) 编译（-Ikmock 在前，-Iscratch 提供同目录引用的 adapter.h 等）。
"${CC}" -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -Wno-multichar -Wno-discarded-qualifiers \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I kmock -I "${SCRATCH}" \
    -o "${SCRATCH}/test_kmd_logic" \
    test_kmd_logic.c mock_registry.c "${SCRATCH}/ccc.c" "${SCRATCH}/sim.c"

# 4) 编译后再校验一次：确保编译过程没有副作用改动源文件。
( cd "${KMD_SRC}" && sha256sum ${FILES} ) > "${SCRATCH}/manifest.post"
diff "${SCRATCH}/manifest.pre" "${SCRATCH}/manifest.post" >/dev/null \
    || { echo "[host-tests] ERROR: kmd sources changed during build"; exit 2; }

# 5) 运行（ASan/UBSan 打开）。
ASAN_OPTIONS=detect_leaks=1 "${SCRATCH}/test_kmd_logic"
