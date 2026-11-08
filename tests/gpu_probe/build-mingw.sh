#!/usr/bin/env bash
# 交叉编译 gpu_probe.exe（x64, Windows）
# 优先用本地 llvm-mingw 便携工具链，其次系统 x86_64-w64-mingw32-gcc。
set -euo pipefail
cd "$(dirname "$0")"

LLVM_MINGW_DIR="$(ls -d /home/z/tools/llvm-mingw-*-x86_64 2>/dev/null | head -1 || true)"
if [ -n "${LLVM_MINGW_DIR}" ] && [ -x "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-gcc" ]; then
    export PATH="${LLVM_MINGW_DIR}/bin:${PATH}"
    CC="${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-gcc"
else
    CC=x86_64-w64-mingw32-gcc
fi

echo "[gpu_probe] CC = ${CC}"
# llvm-mingw 自动链接 compiler-rt/libunwind；系统 gcc-mingw 需要 -static-libgcc
if [[ "${CC}" == *llvm-mingw* ]]; then
    EXTRA_LIBS=""
else
    EXTRA_LIBS="-static-libgcc"
fi
# -I：引入共享契约头 rok_escape.h（KMD/ICD/gpu_probe 同源 ABI）
"${CC}" -O2 -Wall -Wextra -Wno-unused-parameter \
    -I../../kmd/src -DROK_PROBE_HAS_ESCAPE_ABI=1 \
    -o gpu_probe.exe gpu_probe.c -lsetupapi ${EXTRA_LIBS}

echo "[gpu_probe] built: $(ls -l gpu_probe.exe | awk '{print $5}') bytes"
file gpu_probe.exe
