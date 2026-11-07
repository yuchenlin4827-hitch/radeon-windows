#!/usr/bin/env bash
# 交叉编译 vk_audit.exe（x64, Windows）。
# 依赖：
#   1. mingw 工具链：优先 /home/z/tools/llvm-mingw-*-x86_64（便携版），
#      其次系统 x86_64-w64-mingw32-gcc（与 tests/gpu_probe/build-mingw.sh 一致）。
#   2. 官方 Vulkan-Headers 的 vulkan_core.h（Apache-2.0 OR MIT）：
#      构建时下载并缓存到 /home/z/work-vulkan/include/vulkan/，
#      pinned tag 由 VK_HEADERS_TAG 控制（默认 v1.4.330）。
#      头文件不入库，避免版本漂移与许可文件混杂。
# 用法：bash tools/vk_audit/build-mingw.sh
set -euo pipefail
cd "$(dirname "$0")"

VK_HEADERS_TAG="${VK_HEADERS_TAG:-v1.4.330}"
VK_HEADERS_URL="https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/${VK_HEADERS_TAG}/include/vulkan/vulkan_core.h"
VK_INCLUDE_DIR="${VK_INCLUDE_DIR:-/home/z/work-vulkan/include}"

# --- 头文件（构建时获取并缓存；vulkan_core.h 依赖 vk_platform/vulkan/vk_video） ---
if [ ! -f "${VK_INCLUDE_DIR}/vulkan/vulkan_core.h" ]; then
    echo "[vk_audit] fetching Vulkan-Headers ${VK_HEADERS_TAG}"
    mkdir -p "${VK_INCLUDE_DIR}/vulkan" "${VK_INCLUDE_DIR}/vk_video"
    for h in vk_platform.h vulkan.h vulkan_core.h; do
        curl -sSfL -o "${VK_INCLUDE_DIR}/vulkan/${h}" \
            "https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/${VK_HEADERS_TAG}/include/vulkan/${h}"
    done
    BASE="https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/${VK_HEADERS_TAG}/include/vk_video"
    for h in vulkan_video_codec_h264std.h vulkan_video_codec_h265std.h \
             vulkan_video_codec_h264std_decode.h vulkan_video_codec_h264std_encode.h \
             vulkan_video_codec_h265std_decode.h vulkan_video_codec_h265std_encode.h \
             vulkan_video_codec_av1std.h vulkan_video_codec_av1std_decode.h \
             vulkan_video_codec_av1std_encode.h \
             vulkan_video_codec_vp9std.h vulkan_video_codec_vp9std_decode.h \
             vulkan_video_codecs_common.h; do
        curl -sSfL -o "${VK_INCLUDE_DIR}/vk_video/${h}" "${BASE}/${h}"
    done
fi

# --- 工具链选择 ---
LLVM_MINGW_DIR="$(ls -d /home/z/tools/llvm-mingw-*-x86_64 2>/dev/null | head -1 || true)"
if [ -n "${LLVM_MINGW_DIR}" ] && [ -x "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-gcc" ]; then
    export PATH="${LLVM_MINGW_DIR}/bin:${PATH}"
    CC="${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-gcc"
else
    CC=x86_64-w64-mingw32-gcc
fi

echo "[vk_audit] CC = ${CC}"
if [[ "${CC}" == *llvm-mingw* ]]; then
    EXTRA_LIBS=""
else
    EXTRA_LIBS="-static-libgcc"
fi

"${CC}" -O2 -Wall -Wextra -Wno-unused-parameter -std=c11 \
    -I../../umd/vk13 -I../../umd/gl46 -I"${VK_INCLUDE_DIR}" \
    -o vk_audit.exe vk_audit.c \
    ../../umd/vk13/vk13_promoted.c \
    ../../umd/vk13/vk13_features.c \
    ../../umd/gl46/gl46_r600g.c \
    ../../umd/gl46/gl46_zink.c \
    ${EXTRA_LIBS}

echo "[vk_audit] built: $(ls -l vk_audit.exe | awk '{print $5}') bytes"
file vk_audit.exe
