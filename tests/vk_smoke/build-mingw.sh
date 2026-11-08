#!/usr/bin/env bash
# 交叉编译 vk_smoke.exe（x64, Windows）
# 优先用本地 llvm-mingw 便携工具链，其次系统 x86_64-w64-mingw32-gcc。
set -euo pipefail
cd "$(dirname "$0")"

# SPIR-V 头缺失时自动再生成（正常情况下已入库，无需 Python）
if [ ! -f spirv_compound.h ]; then
    echo "[vk_smoke] spirv_compound.h 缺失，重新生成"
    python3 ../../scripts/gen_spirv.py
fi

LLVM_MINGW_DIR="$(ls -d /home/z/tools/llvm-mingw-*-x86_64 2>/dev/null | head -1 || true)"
if [ -n "${LLVM_MINGW_DIR}" ] && [ -x "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-gcc" ]; then
    export PATH="${LLVM_MINGW_DIR}/bin:${PATH}"
    CC="${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-gcc"
else
    CC=x86_64-w64-mingw32-gcc
fi

echo "[vk_smoke] CC = ${CC}"
# llvm-mingw 自动链接 compiler-rt/libunwind；系统 gcc-mingw 需要 -static-libgcc
if [[ "${CC}" == *llvm-mingw* ]]; then
    EXTRA_LIBS=""
else
    EXTRA_LIBS="-static-libgcc"
fi
"${CC}" -O2 -Wall -Wextra -Wno-unused-parameter \
    -o vk_smoke.exe vk_smoke.c ${EXTRA_LIBS}

echo "[vk_smoke] built: $(ls -l vk_smoke.exe | awk '{print $5}') bytes"
file vk_smoke.exe
