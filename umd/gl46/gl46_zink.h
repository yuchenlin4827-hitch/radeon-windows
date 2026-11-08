/* SPDX-License-Identifier: MIT
 * gl46_zink.h — GL 4.6 备选路线门控：Mesa Zink（GL-on-Vulkan）。
 *
 * 主路线是 R600g 原生移植（见 gl46_r600g.h）；Zink 作为备选：当
 * R600g 移植受阻、而 terakan（或继任者）已把 Vulkan 推到 1.3 时，
 * Zink 可以直接搭 Vulkan 1.3 的便车给出 OpenGL。
 *
 * 权威依据（Mesa 26.2.6，2026-11 核对）：
 *   1. src/util/VP_ZINK_requirements.json（Zink 官方机器可读 profile）：
 *      VP_ZINK_gl46_baseline 的 api-version 要求 = "1.3.0"；
 *      相对基线链（gl21→gl45）新增：VK_KHR_draw_indirect_count、
 *      VkPhysicalDeviceFeatures.samplerAnisotropy、
 *      VkPhysicalDeviceFeatures.depthBiasClamp。
 *   2. src/gallium/drivers/zink/zink_screen.c：
 *      - caps->shader_group_vote = (vulkan11 && subgroup supportedOperations
 *        含 VOTE && supportedStages 含 COMPUTE) || EXT_shader_subgroup_vote
 *        （第 830-834 行）；
 *      - caps->polygon_offset_clamp = features.depthBiasClamp（第 841 行）；
 *      - caps->multi_draw_indirect_params = have_KHR_draw_indirect_count
 *        （第 862 行）；
 *      - caps->anisotropic_filter = features.samplerAnisotropy（第 736 行）；
 *      - 初始化强校验：scalarBlockLayout（Vulkan12Features）或
 *        EXT_scalar_block_layout 必须其一（第 2893-2906 行，否则退出）。
 *   3. GL 4.5→4.6 增量与 R600g 一致（同一份 features.txt GL 4.6 行）。
 *
 * 本门控按"目标驱动为 Vulkan 1.3"的最短路径编码：1.3 核心（动态渲染、
 * timeline semaphore、maintenance1-4 等）已覆盖链上多数 KHR 扩展，
 * 剩余显式扩展单独列出。
 */
#ifndef ROK_GL46_ZINK_H
#define ROK_GL46_ZINK_H

#include <stddef.h>
#include <stdint.h>

#include "gl46_common.h"

#include "../vk13/vk13_features.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Zink 备选路线状态快照（除 vk13 特性快照外，还需以下项）。 */
typedef struct gl46_zink_state {
    const vk13_feature_state_t *vk;   /* apiVersion + 特性 + 子群组位段 */

    /* ---- 显式扩展（未被 1.3 核心覆盖的部分；1 = 驱动枚举并可用） ---- */
    unsigned char ext_scalar_block_layout;       /* 或 feats12.scalarBlockLayout */
    unsigned char ext_draw_indirect_count;       /* 或 feats12.drawIndirectCount */
    unsigned char ext_custom_border_color;
    unsigned char ext_border_color_swizzle;
    unsigned char ext_line_rasterization;
    unsigned char ext_transform_feedback;
    unsigned char ext_conditional_rendering;
    unsigned char ext_depth_clip_enable;
    unsigned char ext_vertex_attribute_divisor;
    unsigned char ext_image_2d_view_of_3d;

    /* ---- 上述扩展对应的 feature 结构成员（profile 的 features 检查） ---- */
    unsigned char feat_custom_border_color_without_format;  /* EXT */
    unsigned char feat_border_color_swizzle_from_image;     /* EXT */
    unsigned char feat_rectangular_lines;                   /* EXT */
    unsigned char feat_transform_feedback;                  /* EXT */
    unsigned char feat_conditional_rendering;               /* EXT */
    unsigned char feat_depth_clip_enable;                   /* EXT */
    unsigned char feat_vertex_attribute_instance_rate_divisor; /* EXT */
    unsigned char feat_image_2d_view_of_3d;                 /* EXT */

    /* ---- 关键 limits（VP_ZINK 链：gl31/gl41 baseline） ---- */
    uint32_t max_per_stage_descriptor_samplers;  /* >= 16 */
    uint32_t max_image_dimension_1d;             /* >= 16384 */
    uint32_t max_image_dimension_2d;             /* >= 16384 */
    uint32_t max_image_dimension_3d;             /* >= 2048 */
    uint32_t max_image_dimension_cube;           /* >= 16384 */
    uint32_t max_image_array_layers;             /* >= 2048 */
    uint32_t max_viewports;                      /* >= 16 */

    /* ---- 关键格式支持（VkFormatProperties 简化标志，1 = 支持） ---- */
    unsigned char fmt_d32s8_depth_stencil_attachment; /* D32_SFLOAT_S8_UINT */
    unsigned char fmt_rgb32f_uniform_texel_buffer;    /* R32G32B32_SFLOAT 等 */
    unsigned char fmt_rgba8_sampled;                  /* R8G8B8A8_UNORM/SSRGB */
    unsigned char fmt_r16_r16g16_unorm_sampled;       /* R16/R16G16_UNORM */
    unsigned char fmt_b10g11r11_vertex;               /* B10G11R11_UFLOAT_PACK32 */
} gl46_zink_state_t;

typedef enum gl46_zink_verdict {
    GL46_ZINK_FAIL = 0,
    GL46_ZINK_GL46_READY = 1
} gl46_zink_verdict_t;

/* 评估 Zink 备选路线。gaps/max_gaps 语义同 gl46_r600g_evaluate。 */
size_t gl46_zink_evaluate(const gl46_zink_state_t *st,
                          gl46_zink_verdict_t *verdict,
                          gl46_gap_t *gaps, size_t max_gaps);

#ifdef __cplusplus
}
#endif

#endif /* ROK_GL46_ZINK_H */
