#!/usr/bin/env bash
# 交叉编译 rok_adl.dll + adl_smoke.exe（x64, Windows）
set -euo pipefail
cd "$(dirname "$0")"

LLVM_MINGW_DIR="$(ls -d /home/z/tools/llvm-mingw-*-x86_64 2>/dev/null | head -1 || true)"
if [ -n "${LLVM_MINGW_DIR}" ] && [ -x "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-gcc" ]; then
    export PATH="${LLVM_MINGW_DIR}/bin:${PATH}"
    CC="${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-gcc"
else
    CC=x86_64-w64-mingw32-gcc
fi

echo "[rok_adl] CC = ${CC}"
if [[ "${CC}" == *llvm-mingw* ]]; then
    EXTRA_LIBS=""
else
    EXTRA_LIBS="-static-libgcc"
fi

# DLL：mingw 自动导出非 static 符号
"${CC}" -O2 -Wall -Wextra -Wno-unused-parameter -shared \
    -o rok_adl.dll ../../umd/adl/rok_adl.c ${EXTRA_LIBS}

# 冒烟客户端（链接 DLL 的导入库）
"${CC}" -O2 -Wall -Wextra -Wno-unused-parameter \
    -I../../umd/adl \
    -o adl_smoke.exe adl_smoke.c -L. -lrok_adl ${EXTRA_LIBS}

echo "[rok_adl] built:"
ls -l rok_adl.dll adl_smoke.exe
file rok_adl.dll adl_smoke.exe
