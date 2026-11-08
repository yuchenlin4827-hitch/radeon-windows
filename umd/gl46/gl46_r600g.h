/* SPDX-License-Identifier: MIT
 * gl46_r600g.h — GL 4.6 主路线门控：Mesa R600g（TeraScale Gallium 驱动）
 * 原生移植到 Windows。
 *
 * 权威依据（Mesa 26.2.6 源码 + 官方功能矩阵，2026-11 核对）：
 *   1. Mesa features.txt GL 4.6 行：
 *      "GL 4.6, GLSL 4.60 -- all DONE: ..., r600/evergreen+, ..."
 *      —— r600 驱动在 evergreen 及以上硬件上 GL 4.6 全部完成；
 *   2. src/gallium/drivers/r600/r600_pipe.c:
 *         caps->glsl_feature_level = family >= CHIP_CEDAR ? 460 : 330;
 *      —— GL 4.6 的硬件门就是"evergreen 家族及以上"（TeraScale 2/3），
 *         TeraScale 1（R600..RV740）为 GL 3.3；
 *   3. r600 无 LLVM 依赖（src/gallium/drivers/r600/meson.build 无 llvm），
 *      后端为自研汇编器 + sfn C++ 前端，交叉移植可行；
 *   4. GL 4.6 扩展 → Gallium cap 映射（src/mesa/state_tracker/st_extensions.c）：
 *      ARB_gl_spirv ← caps.gl_spirv（r600: true）
 *      ARB_spirv_extensions ← caps.gl_spirv
 *      ARB_shader_group_vote ← caps.shader_group_vote（r600: true）
 *      ARB_polygon_offset_clamp ← caps.polygon_offset_clamp（r600: true）
 *      ARB_pipeline_statistics_query ← caps.query_pipeline_statistics（true）
 *      ARB_transform_feedback_overflow_query ← caps.query_so_overflow
 *                                                （r600: evergreen+）
 *      ARB_shader_draw_parameters ← caps.draw_parameters（evergreen+）
 *      ARB_indirect_parameters ← caps.multi_draw_indirect_params
 *                                （evergreen+ 且 winsys 报告版本 >= 2.51.0）
 *      ARB_shader_atomic_counter_ops ← 原生原子计数器（evergreen+）
 *      ARB_texture_filter_anisotropic ← caps.anisotropic_filter（true）
 *      KHR_no_error ← 前端（所有驱动）
 *
 * 因此本项目 GL 4.6 = "R600g 移植 + evergreen+ 硬件 + 自研 KMD 提供的
 * winsys 服务"，与 Vulkan（terakan）路线完全解耦。
 */
#ifndef ROK_GL46_R600G_H
#define ROK_GL46_R600G_H

#include <stddef.h>
#include <stdint.h>

#include "gl46_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* radeon_family 芯片代际（与 Mesa src/amd/common/amd_family.h 的
 * enum radeon_family 对齐，只保留本项目相关区段；枚举值顺序即大小序）。 */
typedef enum gl46_chip_family {
    GL46_CHIP_UNKNOWN = 0,
    /* TeraScale 1（GFX3，R6xx/R7xx）—— GL 3.3 上限 */
    GL46_CHIP_R600,
    GL46_CHIP_RV610,
    GL46_CHIP_RV630,
    GL46_CHIP_RV670,
    GL46_CHIP_RV620,
    GL46_CHIP_RV635,
    GL46_CHIP_RS780,
    GL46_CHIP_RS880,
    GL46_CHIP_RV770,
    GL46_CHIP_RV730,
    GL46_CHIP_RV710,
    GL46_CHIP_RV740,
    /* TeraScale 2 Evergreen（GFX4，VLIW5）—— GL 4.6 从此开始 */
    GL46_CHIP_CEDAR,
    GL46_CHIP_REDWOOD,
    GL46_CHIP_JUNIPER,
    GL46_CHIP_CYPRESS,
    GL46_CHIP_HEMLOCK,
    GL46_CHIP_PALM,
    GL46_CHIP_SUMO,
    GL46_CHIP_SUMO2,
    /* TeraScale 2 Northern Islands */
    GL46_CHIP_BARTS,
    GL46_CHIP_TURKS,
    GL46_CHIP_CAICOS,
    /* TeraScale 3（GFX5，VLIW4） */
    GL46_CHIP_CAYMAN,
    GL46_CHIP_ARUBA
} gl46_chip_family_t;

#define GL46_CHIP_FIRST_EVERGREEN GL46_CHIP_CEDAR

/* PCI 设备 ID → 芯片代际。表来源：Mesa include/pci_ids/r600_pci_ids.h
 * （MIT）。只收录 TeraScale 区段；未命中返回 GL46_CHIP_UNKNOWN。 */
gl46_chip_family_t gl46_chip_family_from_device_id(uint32_t device_id);

/* 芯片代际名（用于报告输出）。 */
const char *gl46_chip_family_name(gl46_chip_family_t family);

/* 该代际是否 evergreen+（GL 4.6 硬件门）。 */
int gl46_chip_family_is_evergreen_plus(gl46_chip_family_t family);

/* R600g 路线的状态快照：硬件代际 + 移植须提供的 Gallium cap/winsys 服务。 */
typedef struct gl46_r600g_state {
    gl46_chip_family_t family;

    /* ---- 自研 KMD / winsys 必须提供的服务（对应 r600g 的使用点） ---- */
    /* DRM 版本等价值：Linux 上 multi_draw_indirect_params 需要
     * radeon DRM >= 2.51.0（r600_pipe.c:522）；rokmd 用自版本号对齐。 */
    uint32_t      winsys_drm_version;     /* r600_version_simple 打包值 */
    unsigned char ws_hw_atomic_counters;  /* has_atomics：原生原子计数器 */
    unsigned char ws_streamout;           /* has_streamout：变换反馈 */

    /* ---- Gallium cap 布线检查（真实移植中这些是编译期常量；
     *     保留为标志以便在模拟/降级配置下审计） ---- */
    unsigned char cap_gl_spirv;               /* ARB_gl_spirv / ARB_spirv_extensions */
    unsigned char cap_shader_group_vote;      /* ARB_shader_group_vote */
    unsigned char cap_polygon_offset_clamp;   /* ARB_polygon_offset_clamp */
    unsigned char cap_query_pipeline_statistics; /* ARB_pipeline_statistics_query */
    unsigned char cap_query_so_overflow;      /* ARB_transform_feedback_overflow_query */
    unsigned char cap_texture_barrier;        /* GL 4.5 依赖（GL_ARB_texture_barrier） */
    unsigned char cap_anisotropic_filter;     /* ARB_texture_filter_anisotropic */
    unsigned char cap_draw_parameters;        /* ARB_shader_draw_parameters */
} gl46_r600g_state_t;

/* GL 核心版本判定结果。 */
typedef enum gl46_r600g_verdict {
    GL46_R600G_FAIL = 0,          /* 硬件低于 evergreen 或存在缺口 */
    GL46_R600G_GL330 = 1,         /* TeraScale 1：GL 3.3 上限（合理结论） */
    GL46_R600G_GL46_READY = 2     /* evergreen+ 且全部 cap/winsys 就绪 */
} gl46_r600g_verdict_t;

/* 评估。gaps 可为 NULL（仅取结论）；否则写入至多 max_gaps 条并返回总数。 */
size_t gl46_r600g_evaluate(const gl46_r600g_state_t *st,
                           gl46_r600g_verdict_t *verdict,
                           gl46_gap_t *gaps, size_t max_gaps);

/* winsys 版本号打包（对应 Mesa r600_version_simple(major, minor, patch)）。 */
uint32_t gl46_r600g_version_simple(uint32_t major, uint32_t minor,
                                   uint32_t patch);

#ifdef __cplusplus
}
#endif

#endif /* ROK_GL46_R600G_H */
