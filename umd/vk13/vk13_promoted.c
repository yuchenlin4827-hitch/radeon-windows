/* SPDX-License-Identifier: MIT
 * vk13_promoted.c — promoted 扩展表（与 vk.xml 逐一核对，勿手工增删）。
 */
#include "vk13_promoted.h"

/* ---- Vulkan 1.1：23 个（promotedto="VK_VERSION_1_1"） ---- */
static const char *const k_vk13_promoted_1_1[] = {
    "VK_KHR_16bit_storage",
    "VK_KHR_bind_memory2",
    "VK_KHR_dedicated_allocation",
    "VK_KHR_descriptor_update_template",
    "VK_KHR_device_group",
    "VK_KHR_device_group_creation",
    "VK_KHR_external_fence",
    "VK_KHR_external_fence_capabilities",
    "VK_KHR_external_memory",
    "VK_KHR_external_memory_capabilities",
    "VK_KHR_external_semaphore",
    "VK_KHR_external_semaphore_capabilities",
    "VK_KHR_get_memory_requirements2",
    "VK_KHR_get_physical_device_properties2",
    "VK_KHR_maintenance1",
    "VK_KHR_maintenance2",
    "VK_KHR_maintenance3",
    "VK_KHR_multiview",
    "VK_KHR_relaxed_block_layout",
    "VK_KHR_sampler_ycbcr_conversion",
    "VK_KHR_shader_draw_parameters",
    "VK_KHR_storage_buffer_storage_class",
    "VK_KHR_variable_pointers",
};

/* ---- Vulkan 1.2：24 个（promotedto="VK_VERSION_1_2"） ---- */
static const char *const k_vk13_promoted_1_2[] = {
    "VK_EXT_descriptor_indexing",
    "VK_EXT_host_query_reset",
    "VK_EXT_sampler_filter_minmax",
    "VK_EXT_scalar_block_layout",
    "VK_EXT_separate_stencil_usage",
    "VK_EXT_shader_viewport_index_layer",
    "VK_KHR_8bit_storage",
    "VK_KHR_buffer_device_address",
    "VK_KHR_create_renderpass2",
    "VK_KHR_depth_stencil_resolve",
    "VK_KHR_draw_indirect_count",
    "VK_KHR_driver_properties",
    "VK_KHR_image_format_list",
    "VK_KHR_imageless_framebuffer",
    "VK_KHR_sampler_mirror_clamp_to_edge",
    "VK_KHR_separate_depth_stencil_layouts",
    "VK_KHR_shader_atomic_int64",
    "VK_KHR_shader_float16_int8",
    "VK_KHR_shader_float_controls",
    "VK_KHR_shader_subgroup_extended_types",
    "VK_KHR_spirv_1_4",
    "VK_KHR_timeline_semaphore",
    "VK_KHR_uniform_buffer_standard_layout",
    "VK_KHR_vulkan_memory_model",
};

/* ---- Vulkan 1.3：23 个（promotedto="VK_VERSION_1_3"） ---- */
static const char *const k_vk13_promoted_1_3[] = {
    "VK_EXT_4444_formats",
    "VK_EXT_extended_dynamic_state",
    "VK_EXT_extended_dynamic_state2",
    "VK_EXT_image_robustness",
    "VK_EXT_inline_uniform_block",
    "VK_EXT_pipeline_creation_cache_control",
    "VK_EXT_pipeline_creation_feedback",
    "VK_EXT_private_data",
    "VK_EXT_shader_demote_to_helper_invocation",
    "VK_EXT_subgroup_size_control",
    "VK_EXT_texel_buffer_alignment",
    "VK_EXT_texture_compression_astc_hdr",
    "VK_EXT_tooling_info",
    "VK_EXT_ycbcr_2plane_444_formats",
    "VK_KHR_copy_commands2",
    "VK_KHR_dynamic_rendering",
    "VK_KHR_format_feature_flags2",
    "VK_KHR_maintenance4",
    "VK_KHR_shader_integer_dot_product",
    "VK_KHR_shader_non_semantic_info",
    "VK_KHR_shader_terminate_invocation",
    "VK_KHR_synchronization2",
    "VK_KHR_zero_initialize_workgroup_memory",
};

/* 说明：为避免重复维护三份静态结构，遍历接口直接按层级切换数组。 */
static const char *const *k_promoted_table(uint32_t level, size_t *count)
{
    switch (level) {
    case VK13_LEVEL_1_1:
        *count = sizeof(k_vk13_promoted_1_1) / sizeof(k_vk13_promoted_1_1[0]);
        return k_vk13_promoted_1_1;
    case VK13_LEVEL_1_2:
        *count = sizeof(k_vk13_promoted_1_2) / sizeof(k_vk13_promoted_1_2[0]);
        return k_vk13_promoted_1_2;
    case VK13_LEVEL_1_3:
        *count = sizeof(k_vk13_promoted_1_3) / sizeof(k_vk13_promoted_1_3[0]);
        return k_vk13_promoted_1_3;
    default:
        *count = 0;
        return NULL;
    }
}

const char *const *vk13_promoted_extensions(uint32_t level, size_t *count)
{
    size_t n = 0;
    const char *const *t = k_promoted_table(level, &n);
    if (count != NULL) {
        *count = n;
    }
    return t;
}

size_t vk13_promoted_extension_count(uint32_t level)
{
    size_t n = 0;
    (void)k_promoted_table(level, &n);
    return n;
}

void vk13_promoted_foreach(vk13_promoted_iter_fn fn, void *user)
{
    static const uint32_t k_levels[] = {
        VK13_LEVEL_1_1, VK13_LEVEL_1_2, VK13_LEVEL_1_3,
    };
    size_t i, j;

    if (fn == NULL) {
        return;
    }
    for (i = 0; i < sizeof(k_levels) / sizeof(k_levels[0]); i++) {
        size_t count = 0;
        const char *const *t = k_promoted_table(k_levels[i], &count);
        for (j = 0; j < count; j++) {
            fn(k_levels[i], t[j], user);
        }
    }
}

