/* SPDX-License-Identifier: MIT
 * vk13_features.c — 必选核心特性表 + 层级判定实现。
 *
 * 表内容与 vk.xml <feature name="VK_VERSION_x_y"> 的无条件 <require> 一致。
 * 条件项（conditional=1）仅用于审计展示：vk.xml 中它们挂 depends=
 * （扩展名或前置特性名），启用对应依赖时才变为强制。
 */
#include "vk13_features.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* 无条件必选特性（参与门控）                                          */
/* ------------------------------------------------------------------ */
static const vk13_required_feature_t k_required_mandatory[] = {
    /* ---- 1.1 ---- */
    { VK13_LEVEL_1_1, "VkPhysicalDeviceMultiviewFeatures",
      "multiview", 0, NULL },

    /* ---- 1.2 ---- */
    { VK13_LEVEL_1_2, "VkPhysicalDeviceVulkan12Features",
      "hostQueryReset", 0, NULL },
    { VK13_LEVEL_1_2, "VkPhysicalDeviceVulkan12Features",
      "imagelessFramebuffer", 0, NULL },
    { VK13_LEVEL_1_2, "VkPhysicalDeviceVulkan12Features",
      "separateDepthStencilLayouts", 0, NULL },
    { VK13_LEVEL_1_2, "VkPhysicalDeviceVulkan12Features",
      "shaderSubgroupExtendedTypes", 0, NULL },
    { VK13_LEVEL_1_2, "VkPhysicalDeviceVulkan12Features",
      "timelineSemaphore", 0, NULL },
    { VK13_LEVEL_1_2, "VkPhysicalDeviceVulkan12Features",
      "uniformBufferStandardLayout", 0, NULL },
    { VK13_LEVEL_1_2, "VkPhysicalDeviceVulkan12Features",
      "subgroupBroadcastDynamicId", 0, NULL },

    /* ---- 1.3（含 1.2 结构体上的新增强制项） ---- */
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan12Features",
      "bufferDeviceAddress", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan12Features",
      "vulkanMemoryModel", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan12Features",
      "vulkanMemoryModelDeviceScope", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan13Features",
      "dynamicRendering", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan13Features",
      "inlineUniformBlock", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan13Features",
      "maintenance4", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan13Features",
      "pipelineCreationCacheControl", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan13Features",
      "privateData", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan13Features",
      "robustImageAccess", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan13Features",
      "shaderDemoteToHelperInvocation", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan13Features",
      "shaderIntegerDotProduct", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan13Features",
      "subgroupSizeControl", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan13Features",
      "computeFullSubgroups", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan13Features",
      "shaderTerminateInvocation", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan13Features",
      "synchronization2", 0, NULL },
    { VK13_LEVEL_1_3, "VkPhysicalDeviceVulkan13Features",
      "shaderZeroInitializeWorkgroupMemory", 0, NULL },
};

#define MANDATORY_COUNT \
    (sizeof(k_required_mandatory) / sizeof(k_required_mandatory[0]))

/* ------------------------------------------------------------------ */
/* 条件必选特性（vk.xml 带 depends 的 <require>；仅展示，不参与门控）   */
/* ------------------------------------------------------------------ */
static const vk13_required_feature_t k_required_conditional[] = {
    { VK13_LEVEL_1_1, "VkPhysicalDevice16BitStorageFeatures",
      "storageBuffer16BitAccess", 1,
      "uniformAndStorageBuffer16BitAccess 已支持时" },
    { VK13_LEVEL_1_1, "VkPhysicalDeviceShaderDrawParametersFeatures",
      "shaderDrawParameters", 1,
      "VK_KHR_shader_draw_parameters 启用时" },
    { VK13_LEVEL_1_2, "VkPhysicalDeviceVulkan12Features",
      "storageBuffer8BitAccess", 1,
      "uniformAndStorageBuffer8BitAccess 已支持时" },
    { VK13_LEVEL_1_2, "VkPhysicalDeviceVulkan12Features",
      "uniformAndStorageBuffer8BitAccess", 1,
      "VK_KHR_8bit_storage 启用时" },
    { VK13_LEVEL_1_2, "VkPhysicalDeviceVulkan12Features",
      "shaderFloat16", 1,
      "VK_KHR_shader_float16_int8 启用时" },
    { VK13_LEVEL_1_2, "VkPhysicalDeviceVulkan12Features",
      "descriptorIndexing", 1,
      "VK_EXT_descriptor_indexing 启用时" },
    { VK13_LEVEL_1_2, "VkPhysicalDeviceVulkan12Features",
      "shaderOutputViewportIndex", 1,
      "VK_EXT_shader_viewport_index_layer 启用时" },
};

#define CONDITIONAL_COUNT \
    (sizeof(k_required_conditional) / sizeof(k_required_conditional[0]))

const vk13_required_feature_t *vk13_required_features_mandatory(size_t *count)
{
    if (count != NULL) {
        *count = MANDATORY_COUNT;
    }
    return k_required_mandatory;
}

const vk13_required_feature_t *vk13_required_features_conditional(
    size_t *count)
{
    if (count != NULL) {
        *count = CONDITIONAL_COUNT;
    }
    return k_required_conditional;
}

const vk13_required_feature_t *vk13_required_features_for_level(
    uint32_t level, size_t *count)
{
    size_t n = 0;
    size_t i;

    if (count != NULL) {
        *count = 0;
    }
    /* 线性统计一次即可（表规模 < 30）。 */
    for (i = 0; i < MANDATORY_COUNT; i++) {
        if (k_required_mandatory[i].level <= level) {
            n++;
        }
    }
    if (count != NULL) {
        *count = n;
    }
    return k_required_mandatory;
}

static int state_member_true(const vk13_feature_state_t *st,
                             const char *struct_name, const char *member)
{
    (void)struct_name; /* 成员名跨结构体唯一，仅按成员匹配；结构体名供审计显示 */
    /* 把 vk.xml 的 (struct, member) 二元组映射到 vk13_feature_state_t。
     * 关键映射集中在这里，避免调用方记忆两套命名。 */
    if (strcmp(member, "multiview") == 0) {
        return st->multiview != 0;
    }
    if (strcmp(member, "hostQueryReset") == 0) {
        return st->host_query_reset != 0;
    }
    if (strcmp(member, "imagelessFramebuffer") == 0) {
        return st->imageless_framebuffer != 0;
    }
    if (strcmp(member, "separateDepthStencilLayouts") == 0) {
        return st->separate_depth_stencil_layouts != 0;
    }
    if (strcmp(member, "shaderSubgroupExtendedTypes") == 0) {
        return st->shader_subgroup_extended_types != 0;
    }
    if (strcmp(member, "timelineSemaphore") == 0) {
        return st->timeline_semaphore != 0;
    }
    if (strcmp(member, "uniformBufferStandardLayout") == 0) {
        return st->uniform_buffer_standard_layout != 0;
    }
    if (strcmp(member, "subgroupBroadcastDynamicId") == 0) {
        return st->subgroup_broadcast_dynamic_id != 0;
    }
    if (strcmp(member, "bufferDeviceAddress") == 0) {
        return st->buffer_device_address != 0;
    }
    if (strcmp(member, "vulkanMemoryModel") == 0) {
        return st->vulkan_memory_model != 0;
    }
    if (strcmp(member, "vulkanMemoryModelDeviceScope") == 0) {
        return st->vulkan_memory_model_device_scope != 0;
    }
    if (strcmp(member, "scalarBlockLayout") == 0) {
        return st->scalar_block_layout != 0;
    }
    if (strcmp(member, "drawIndirectCount") == 0) {
        return st->draw_indirect_count != 0;
    }
    if (strcmp(member, "samplerMirrorClampToEdge") == 0) {
        return st->sampler_mirror_clamp_to_edge != 0;
    }
    if (strcmp(member, "dynamicRendering") == 0) {
        return st->dynamic_rendering != 0;
    }
    if (strcmp(member, "inlineUniformBlock") == 0) {
        return st->inline_uniform_block != 0;
    }
    if (strcmp(member, "maintenance4") == 0) {
        return st->maintenance4 != 0;
    }
    if (strcmp(member, "pipelineCreationCacheControl") == 0) {
        return st->pipeline_creation_cache_control != 0;
    }
    if (strcmp(member, "privateData") == 0) {
        return st->private_data != 0;
    }
    if (strcmp(member, "robustImageAccess") == 0) {
        return st->robust_image_access != 0;
    }
    if (strcmp(member, "shaderDemoteToHelperInvocation") == 0) {
        return st->shader_demote_to_helper_invocation != 0;
    }
    if (strcmp(member, "shaderIntegerDotProduct") == 0) {
        return st->shader_integer_dot_product != 0;
    }
    if (strcmp(member, "subgroupSizeControl") == 0) {
        return st->subgroup_size_control != 0;
    }
    if (strcmp(member, "computeFullSubgroups") == 0) {
        return st->compute_full_subgroups != 0;
    }
    if (strcmp(member, "shaderTerminateInvocation") == 0) {
        return st->shader_terminate_invocation != 0;
    }
    if (strcmp(member, "synchronization2") == 0) {
        return st->synchronization2 != 0;
    }
    if (strcmp(member, "shaderZeroInitializeWorkgroupMemory") == 0) {
        return st->zero_initialize_workgroup_memory != 0;
    }
    /* 未知成员：保守返回假（审计会列出缺口，便于人工复核）。 */
    return 0;
}

int vk13_level_supported(const vk13_feature_state_t *st, uint32_t level)
{
    size_t i;

    if (st == NULL) {
        return 0;
    }
    if (st->api_version < level) {
        return 0;
    }
    for (i = 0; i < MANDATORY_COUNT; i++) {
        const vk13_required_feature_t *r = &k_required_mandatory[i];
        if (r->level <= level && !state_member_true(st, r->struct_name,
                                                    r->member)) {
            return 0;
        }
    }
    return 1;
}

uint32_t vk13_max_reachable_level(const vk13_feature_state_t *st)
{
    static const uint32_t k_levels[] = {
        VK13_LEVEL_1_3, VK13_LEVEL_1_2, VK13_LEVEL_1_1, VK13_LEVEL_1_0,
    };
    size_t i;

    if (st == NULL) {
        return VK13_LEVEL_1_0;
    }
    for (i = 0; i < sizeof(k_levels) / sizeof(k_levels[0]); i++) {
        if (vk13_level_supported(st, k_levels[i])) {
            return k_levels[i];
        }
    }
    return VK13_LEVEL_1_0;
}

size_t vk13_missing_features(const vk13_feature_state_t *st, uint32_t level,
                             vk13_required_feature_t *out, size_t max_out)
{
    size_t i;
    size_t missing = 0;

    if (st == NULL) {
        return 0;
    }
    for (i = 0; i < MANDATORY_COUNT; i++) {
        const vk13_required_feature_t *r = &k_required_mandatory[i];
        if (r->level <= level &&
            !state_member_true(st, r->struct_name, r->member)) {
            if (out != NULL && missing < max_out) {
                out[missing] = *r;
            }
            missing++;
        }
    }
    return missing;
}
