#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# gen_spirv.py — 生成 vk_smoke 用的最小 SPIR-V 1.0 计算着色器二进制，
# 输出为 C 头文件 tests/vk_smoke/spirv_compound.h。
#
# 着色器（GLSL 等价）：
#   #version 450
#   layout(local_size_x=1) in;
#   layout(std430, binding=0) buffer Buf { uint data[8]; };
#   void main() {
#       data[gl_GlobalInvocationID.x] = data[gl_GlobalInvocationID.x] * 2u + 1u;
#   }
#
# SPIR-V 手工组装（word = (wordcount<<16)|opcode），opcode 对照 SPIR-V 1.0
# 规范 §3.32。SSBO 走 SPV_KHR_storage_buffer_storage_class 扩展
#（Vulkan 1.0 核心compute 要求 ICD 接受该扩展声明）。

def w(op, *operands):
    """一条 SPIR-V 指令：(wordcount << 16) | opcode + operands。"""
    wc = 1 + len(operands)
    return [(wc << 16) | op] + list(operands)

def s(text):
    """字符串 -> 32-bit 字数组（NUL 结尾、4 字节对齐）。"""
    b = text.encode() + b"\0"
    while len(b) % 4:
        b += b"\0"
    return [int.from_bytes(b[i:i+4], "little") for i in range(0, len(b), 4)]

# ---- id 分配（bound 覆盖）----
I = {
    "main":      1,
    "glglobal":  2,   # gl_GlobalInvocationID（Input，v3uint）
    "bufvar":    3,   # SSBO 模块级变量
    "idx":       4,   # AccessChain 结果
    "val":       5,   # Load
    "mul":       6,   # IMul
    "add":       7,   # IAdd
    "ptr_ssbo_uint": 8,
    "ptr_input_v3":  9,
    "v3uint":    10,
    "fn":        11,
    "ptr_ssbo":  12,
    "struct":    13,
    "arr":       14,
    "uint":      15,
    "void":      16,
    "u0":        17,
    "u1":        18,
    "u2":        19,
    "u8":        20,
}
LABEL = 21
BOUND = LABEL + 1

words = []
# ---- 头部 ----
words += [0x07230203,   # magic
          0x00010000,   # version 1.0（Vulkan 1.0 要求）
          0x00080001,   # generator（自定义）
          BOUND,
          0]            # schema

# ---- 能力 / 扩展 / 内存模型 ----
words += w(17, 1)                                   # OpCapability Shader
words += w(10, *s("SPV_KHR_storage_buffer_storage_class"))   # OpExtension
words += w(14, 0, 1)                                # OpMemoryModel Logical GLSL450
# OpEntryPoint GLCompute(5) %main "main" <interface>
words += w(15, 5, I["main"], *s("main"), I["glglobal"], I["bufvar"])
# OpExecutionMode %main LocalSize(17) 1 1 1
words += w(16, I["main"], 17, 1, 1, 1)
# OpSource GLSL(2) 450
words += w(3, 2, 450)

# ---- 装饰 ----
words += w(71, I["bufvar"], 1)                      # OpDecorate Block
words += w(72, I["struct"], 0, 35, 0)               # OpMemberDecorate Offset 0
words += w(71, I["arr"], 6, 4)                      # OpDecorate ArrayStride 4
words += w(71, I["glglobal"], 11, 24)               # OpDecorate BuiltIn GlobalInvocationId

# ---- 类型 ----
words += w(19, I["void"])                           # OpTypeVoid
words += w(21, I["uint"], 32, 0)                    # OpTypeInt 32 unsigned
words += w(43, I["uint"], I["u0"], 0)               # OpConstant 0
words += w(43, I["uint"], I["u1"], 1)               # OpConstant 1
words += w(43, I["uint"], I["u2"], 2)               # OpConstant 2
words += w(43, I["uint"], I["u8"], 8)               # OpConstant 8
words += w(28, I["arr"], I["uint"], I["u8"])        # OpTypeArray
words += w(30, I["struct"], I["arr"])               # OpTypeStruct
words += w(32, I["ptr_ssbo"], 12, I["struct"])      # OpTypePointer StorageBuffer
words += w(23, I["v3uint"], I["uint"], 3)           # OpTypeVector 3
words += w(32, I["ptr_input_v3"], 7, I["v3uint"])   # OpTypePointer Input
words += w(32, I["ptr_ssbo_uint"], 12, I["uint"])   # OpTypePointer StorageBuffer->uint
words += w(33, I["fn"], I["void"])                  # OpTypeFunction

# ---- 变量 ----
words += w(59, I["ptr_input_v3"], I["glglobal"], 7)     # OpVariable Input
words += w(59, I["ptr_ssbo"],   I["bufvar"], 12)        # OpVariable StorageBuffer

# ---- 函数 ----
words += w(54, I["void"], I["main"], 0, I["fn"])    # OpFunction None
words += w(248, LABEL)                              # OpLabel
words += w(82, I["ptr_ssbo_uint"], I["idx"], I["bufvar"], I["glglobal"])  # OpAccessChain
words += w(61, I["uint"], I["val"], I["idx"])       # OpLoad
words += w(129, I["uint"], I["mul"], I["val"], I["u2"])   # OpIMul
words += w(128, I["uint"], I["add"], I["mul"], I["u1"])   # OpIAdd
words += w(62, I["idx"], I["add"])                  # OpStore
words += [(1 << 16) | 253]                          # OpReturn
words += [(1 << 16) | 56]                           # OpFunctionEnd

# ---- 校验：所有指令字数与 wc 字段自洽 ----
i = 5  # 跳过头部
while i < len(words):
    wc = words[i] >> 16
    op = words[i] & 0xFFFF
    assert wc >= 1 and i + wc <= len(words), f"bad wc at word {i}: op={op} wc={wc}"
    i += wc
assert i == len(words), "trailing words"

# ---- 输出 C 头文件 ----
lines = []
lines.append("// SPDX-License-Identifier: MIT")
lines.append("// spirv_compound.h — 由 scripts/gen_spirv.py 生成（勿手改）。")
lines.append("// SPIR-V 1.0 计算着色器：data[gl_GlobalInvocationID.x] = data[x] * 2 + 1")
lines.append("// 目标：vk_smoke 端到端验证 Vulkan ICD 的 compute 提交路径。")
lines.append("#pragma once")
lines.append("#include <stdint.h>")
lines.append("")
lines.append("#define ROK_SPIRV_NUM_WORDS %du" % len(words))
lines.append("")
lines.append("static const uint32_t RokSpirvCompound[ROK_SPIRV_NUM_WORDS] = {")
for i in range(0, len(words), 4):
    chunk = ", ".join("0x%08X" % x for x in words[i:i+4])
    lines.append("    " + chunk + ",")
lines.append("};")

import os
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "vk_smoke", "spirv_compound.h")
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w") as f:
    f.write("\n".join(lines) + "\n")
print("OK: %d words (bound=%d) -> %s" % (len(words), BOUND, out))
