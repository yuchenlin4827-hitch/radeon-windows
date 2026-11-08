#!/usr/bin/env bash
# 本机构建并运行 umd 门控模块单元测试（Linux，gcc/clang，纯 C11，无外部依赖）。
# 用法：bash tests/umd/build.sh
set -euo pipefail
cd "$(dirname "$0")"

CC="${CC:-gcc}"
CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -Wshadow -O2 -D_POSIX_C_SOURCE=200809L"

SRC=(
    test_umd.c
    ../../umd/vk13/vk13_promoted.c
    ../../umd/vk13/vk13_features.c
    ../../umd/gl46/gl46_r600g.c
    ../../umd/gl46/gl46_zink.c
)

echo "[umd-tests] CC = ${CC}"
${CC} ${CFLAGS} -I../../umd/vk13 -I../../umd/gl46 \
    -o test_umd "${SRC[@]}"

./test_umd
