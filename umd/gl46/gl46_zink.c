/* SPDX-License-Identifier: MIT
 * gl46_zink.c — Zink 备选路线门控实现（VP_ZINK gl46 链的 C 编码）。
 */
#include "gl46_zink.h"

#include <string.h>

size_t gl46_zink_evaluate(const gl46_zink_state_t *st,
                          gl46_zink_verdict_t *verdict,
                          gl46_gap_t *gaps, size_t max_gaps)
{
    size_t missing = 0;
    const vk13_feature_state_t *vk;

    if (st == NULL || st->vk == NULL) {
        if (verdict != NULL) {
            *verdict = GL46_ZINK_FAIL;
        }
        return 0;
    }
    vk = st->vk;

#define ADD_GAP(id_, why_)                                                \
    do {                                                                  \
        if (gaps != NULL && missing < max_gaps) {                         \
            gaps[missing].id = (id_);                                     \
            gaps[missing].why = (why_);                                   \
        }                                                                 \
        missing++;                                                        \
    } while (0)

    /* ---- 1. API 版本与核心特性：gl46_baseline 要求 api-version 1.3.0，
     *      且链上的 1.1/1.2/1.3 无条件核心特性必须齐
     *      （timeline semaphore、drawIndirectCount、scalarBlockLayout、
     *        dynamic rendering、bufferDeviceAddress、memory model …）。 */
    if (!vk13_level_supported(vk, VK13_LEVEL_1_3)) {
        size_t i, n = 0;
        vk13_required_feature_t miss[32];
        ADD_GAP("vk13-core",
                "Vulkan 1.3 门控未通过（apiVersion < 1.3 或核心必选特性缺失）");
        n = vk13_missing_features(vk, VK13_LEVEL_1_3, miss,
                                  sizeof(miss) / sizeof(miss[0]));
        for (i = 0; i < n && i < sizeof(miss) / sizeof(miss[0]); i++) {
            (void)miss[i]; /* 细节缺口由 vk13 模块报告，这里只计数 */
        }
    }

    /* ---- 2. Zink 初始化硬校验：scalarBlockLayout 或 EXT 二选一
     *      （zink_screen.c:2893-2906）。 */
    if (!vk->scalar_block_layout && !st->ext_scalar_block_layout) {
        ADD_GAP("scalar_block_layout",
                "scalarBlockLayout（Vulkan12Features）或 "
                "VK_EXT_scalar_block_layout 必须其一（Zink 硬性要求）");
    }

    /* ---- 3. 子群组 VOTE：ARB_shader_group_vote（GL 4.6 必选）。
     *      zink_screen.c:830：vulkan11 && supportedOperations 含 VOTE &&
     *      supportedStages 含 COMPUTE。 */
    if (!(vk->subgroup_supported_operations & VK13_SUBGROUP_FEATURE_VOTE_BIT)) {
        ADD_GAP("subgroup-vote",
                "子群组不支持 VOTE（VK_SUBGROUP_FEATURE_VOTE_BIT）");
    }
    if (!(vk->subgroup_supported_stages & VK13_SHADER_STAGE_COMPUTE_BIT)) {
        ADD_GAP("subgroup-compute-stage",
                "子群组操作未覆盖 COMPUTE 阶段");
    }

    /* ---- 4. GL 4.6 直接增量（VP_ZINK_gl46_baseline） ---- */
    if (!vk->f_sampler_anisotropy) {
        ADD_GAP("sampler_anisotropy",
                "VkPhysicalDeviceFeatures.samplerAnisotropy（gl46_baseline）");
    }
    if (!vk->f_depth_bias_clamp) {
        ADD_GAP("depth_bias_clamp",
                "VkPhysicalDeviceFeatures.depthBiasClamp（gl46_baseline，"
                "映射 ARB_polygon_offset_clamp）");
    }
    if (!vk->draw_indirect_count && !st->ext_draw_indirect_count) {
        ADD_GAP("draw_indirect_count",
                "drawIndirectCount（Vulkan12Features）或 "
                "VK_KHR_draw_indirect_count 必须其一（gl46_baseline 扩展）");
    }

    /* ---- 5. 链上非核心扩展（gl21/30/32/33/42 baseline 显式要求） ---- */
    if (!st->ext_custom_border_color || !st->feat_custom_border_color_without_format) {
        ADD_GAP("custom_border_color",
                "VK_EXT_custom_border_color + customBorderColorWithoutFormat");
    }
    if (!st->ext_border_color_swizzle || !st->feat_border_color_swizzle_from_image) {
        ADD_GAP("border_color_swizzle",
                "VK_EXT_border_color_swizzle + borderColorSwizzleFromImage");
    }
    if (!st->ext_line_rasterization || !st->feat_rectangular_lines) {
        ADD_GAP("line_rasterization",
                "VK_EXT_line_rasterization + rectangularLines");
    }
    if (!st->ext_transform_feedback || !st->feat_transform_feedback) {
        ADD_GAP("transform_feedback",
                "VK_EXT_transform_feedback + transformFeedback（GL 3.0 变换反馈）");
    }
    if (!st->ext_conditional_rendering || !st->feat_conditional_rendering) {
        ADD_GAP("conditional_rendering",
                "VK_EXT_conditional_rendering + conditionalRendering");
    }
    if (!st->ext_depth_clip_enable || !st->feat_depth_clip_enable) {
        ADD_GAP("depth_clip_enable",
                "VK_EXT_depth_clip_enable + depthClipEnable");
    }
    if (!st->ext_vertex_attribute_divisor ||
        !st->feat_vertex_attribute_instance_rate_divisor) {
        ADD_GAP("vertex_attribute_divisor",
                "VK_EXT_vertex_attribute_divisor + "
                "vertexAttributeInstanceRateDivisor（GL 3.3）");
    }
    if (!st->ext_image_2d_view_of_3d || !st->feat_image_2d_view_of_3d) {
        ADD_GAP("image_2d_view_of_3d",
                "VK_EXT_image_2d_view_of_3d + image2DViewOf3D（GL 4.2）");
    }

    /* ---- 6. 链上核心 VkPhysicalDeviceFeatures（gl21/30/32/33/40/41/42/43/45） */
    if (!vk->f_robust_buffer_access) { ADD_GAP("robust_buffer_access", "robustBufferAccess"); }
    if (!vk->f_logic_op)             { ADD_GAP("logic_op", "logicOp"); }
    if (!vk->f_fill_mode_non_solid)  { ADD_GAP("fill_mode_non_solid", "fillModeNonSolid"); }
    if (!vk->f_alpha_to_one)         { ADD_GAP("alpha_to_one", "alphaToOne"); }
    if (!vk->f_shader_clip_distance) { ADD_GAP("shader_clip_distance", "shaderClipDistance"); }
    if (!vk->f_shader_cull_distance) { ADD_GAP("shader_cull_distance", "shaderCullDistance（gl45_baseline）"); }
    if (!vk->f_independent_blend)    { ADD_GAP("independent_blend", "independentBlend（GL 3.0）"); }
    if (!vk->f_depth_clamp)          { ADD_GAP("depth_clamp", "depthClamp（GL 3.2）"); }
    if (!vk->f_geometry_shader)      { ADD_GAP("geometry_shader", "geometryShader（GL 3.2）"); }
    if (!vk->f_tessellation_shader)  { ADD_GAP("tessellation_shader", "tessellationShader（GL 4.0）"); }
    if (!vk->f_sample_rate_shading)  { ADD_GAP("sample_rate_shading", "sampleRateShading（GL 4.0）"); }
    if (!vk->f_image_cube_array)     { ADD_GAP("image_cube_array", "imageCubeArray（GL 4.0）"); }
    if (!vk->f_multi_viewport)       { ADD_GAP("multi_viewport", "multiViewport（GL 4.1）"); }
    if (!vk->f_dual_src_blend)       { ADD_GAP("dual_src_blend", "dualSrcBlend（GL 3.3）"); }
    if (!vk->f_multi_draw_indirect)  { ADD_GAP("multi_draw_indirect", "multiDrawIndirect（GL 4.3）"); }
    if (!vk->f_shader_storage_image_extended_formats) {
        ADD_GAP("storage_image_extended_formats",
                "shaderStorageImageExtendedFormats（GL 4.2）");
    }
    if (!vk->f_shader_storage_image_write_without_format) {
        ADD_GAP("storage_image_write_without_format",
                "shaderStorageImageWriteWithoutFormat（GL 4.2）");
    }
    if (!vk->f_vertex_pipeline_stores_and_atomics) {
        ADD_GAP("vertex_pipeline_stores_and_atomics",
                "vertexPipelineStoresAndAtomics（GL 4.2）");
    }
    if (!vk->f_fragment_stores_and_atomics) {
        ADD_GAP("fragment_stores_and_atomics",
                "fragmentStoresAndAtomics（GL 4.2）");
    }
    if (!vk->shader_draw_parameters) {
        ADD_GAP("shader_draw_parameters",
                "shaderDrawParameters（Vulkan11Features，gl42_baseline_vk12）");
    }
    if (!vk->sampler_mirror_clamp_to_edge) {
        ADD_GAP("sampler_mirror_clamp_to_edge",
                "samplerMirrorClampToEdge（gl44_baseline_vk12）");
    }

    /* ---- 7. 关键 limits（gl31/gl41 baseline） ---- */
    if (st->max_per_stage_descriptor_samplers < 16u) {
        ADD_GAP("limits_samplers", "maxPerStageDescriptorSamplers >= 16");
    }
    if (st->max_image_dimension_1d < 16384u) {
        ADD_GAP("limits_dim1d", "maxImageDimension1D >= 16384");
    }
    if (st->max_image_dimension_2d < 16384u) {
        ADD_GAP("limits_dim2d", "maxImageDimension2D >= 16384");
    }
    if (st->max_image_dimension_3d < 2048u) {
        ADD_GAP("limits_dim3d", "maxImageDimension3D >= 2048");
    }
    if (st->max_image_dimension_cube < 16384u) {
        ADD_GAP("limits_dim_cube", "maxImageDimensionCube >= 16384");
    }
    if (st->max_image_array_layers < 2048u) {
        ADD_GAP("limits_array_layers", "maxImageArrayLayers >= 2048");
    }
    if (st->max_viewports < 16u) {
        ADD_GAP("limits_viewports", "maxViewports >= 16");
    }

    /* ---- 8. 关键格式（gl30/40/43/44 baseline） ---- */
    if (!st->fmt_d32s8_depth_stencil_attachment) {
        ADD_GAP("fmt_d32s8", "VK_FORMAT_D32_SFLOAT_S8_UINT 深度模板附件");
    }
    if (!st->fmt_rgb32f_uniform_texel_buffer) {
        ADD_GAP("fmt_rgb32_texel", "R32G32B32_SFLOAT/SINT/UINT 均匀纹素缓冲");
    }
    if (!st->fmt_rgba8_sampled) {
        ADD_GAP("fmt_rgba8", "R8G8B8A8_UNORM/SSRGB 采样（gl43）");
    }
    if (!st->fmt_r16_r16g16_unorm_sampled) {
        ADD_GAP("fmt_r16", "R16/R16G16_UNORM 采样（gl43）");
    }
    if (!st->fmt_b10g11r11_vertex) {
        ADD_GAP("fmt_b10g11r11", "B10G11R11_UFLOAT_PACK32 顶点（gl44）");
    }

#undef ADD_GAP

    if (verdict != NULL) {
        *verdict = (missing == 0) ? GL46_ZINK_GL46_READY : GL46_ZINK_FAIL;
    }
    return missing;
}
