/* SPDX-License-Identifier: MIT
 * vk13_features.h — Vulkan 1.1 / 1.2 / 1.3 核心必选特性（mandatory core
 * features）表与层级判定。
 *
 * 数据来源：Khronos 官方注册表 vk.xml <feature name="VK_VERSION_x_y">
 * 区块中不带 depends 的 <require>（即无条件必选项；带 depends 的条件项
 * 仅在其扩展/前置特性启用时生效，表内以 conditional=1 单列，供审计
 * 输出与文档使用，不参与门控）。
 *
 * 无条件必选特性（去重后）：
 *   1.1: multiview
 *   1.2: hostQueryReset, imagelessFramebuffer, separateDepthStencilLayouts,
 *        shaderSubgroupExtendedTypes, timelineSemaphore,
 *        uniformBufferStandardLayout, subgroupBroadcastDynamicId
 *   1.3: bufferDeviceAddress, vulkanMemoryModel, vulkanMemoryModelDeviceScope
 *        （属 1.2 结构体）、dynamicRendering, inlineUniformBlock, maintenance4,
 *        pipelineCreationCacheControl, privateData, robustImageAccess,
 *        shaderDemoteToHelperInvocation, shaderIntegerDotProduct,
 *        shaderSubgroupSizeControl, shaderSubgroupComputeFullSubgroups,
 *        shaderTerminateInvocation, synchronization2,
 *        shaderZeroInitializeWorkgroupMemory
 *
 * 判定语义：
 *   - 层级 L 可达 = apiVersion >= L 且 L 及其以下所有层级的无条件特性均为真
 *     （核心版本向下包含，与规范 Feature Requirements 一致）；
 *   - 不检查 promoted 扩展是否枚举（见 vk13_promoted.h 的语义说明）。
 */
#ifndef ROK_VK13_FEATURES_H
#define ROK_VK13_FEATURES_H

#include <stddef.h>
#include <stdint.h>

#include "vk13_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 子群组位段（与 vk.xml VkSubgroupFeatureFlagBits 一致，本模块只用到 VOTE）。 */
#define VK13_SUBGROUP_FEATURE_BASIC_BIT      0x00000001u
#define VK13_SUBGROUP_FEATURE_VOTE_BIT       0x00000002u
#define VK13_SUBGROUP_FEATURE_ARITHMETIC_BIT 0x00000004u
#define VK13_SUBGROUP_FEATURE_BALLOT_BIT     0x00000008u

/* 着色器阶段位段（与 vk.xml VkShaderStageFlagBits 一致）。 */
#define VK13_SHADER_STAGE_VERTEX_BIT   0x00000001u
#define VK13_SHADER_STAGE_FRAGMENT_BIT 0x00000010u
#define VK13_SHADER_STAGE_COMPUTE_BIT  0x00000020u

/* 设备侧"特性快照"：由调用方填充。
 *   - vk_audit 用真实 ICD 的 features2/pNext 链映射；
 *   - 单元测试直接构造合成状态。
 * 命名与 vk.xml / vulkan_core.h 成员一一对应（下划线化）。 */
typedef struct vk13_feature_state {
    uint32_t api_version;   /* VK13_MAKE_VERSION 打包值 */

    /* ---- VkPhysicalDeviceVulkan11Features ---- */
    unsigned char multiview;
    unsigned char shader_draw_parameters;

    /* ---- VkPhysicalDeviceVulkan12Features ---- */
    unsigned char host_query_reset;
    unsigned char imageless_framebuffer;
    unsigned char separate_depth_stencil_layouts;
    unsigned char shader_subgroup_extended_types;
    unsigned char timeline_semaphore;
    unsigned char uniform_buffer_standard_layout;
    unsigned char subgroup_broadcast_dynamic_id;
    unsigned char buffer_device_address;
    unsigned char vulkan_memory_model;
    unsigned char vulkan_memory_model_device_scope;
    unsigned char scalar_block_layout;        /* Zink 备选路线需要 */
    unsigned char draw_indirect_count;        /* Zink 备选路线需要 */
    unsigned char sampler_mirror_clamp_to_edge; /* Zink 备选路线需要 */

    /* ---- VkPhysicalDeviceVulkan13Features ---- */
    unsigned char dynamic_rendering;
    unsigned char inline_uniform_block;
    unsigned char maintenance4;
    unsigned char pipeline_creation_cache_control;
    unsigned char private_data;
    unsigned char robust_image_access;
    unsigned char shader_demote_to_helper_invocation;
    unsigned char shader_integer_dot_product;
    unsigned char subgroup_size_control;
    unsigned char compute_full_subgroups;
    unsigned char shader_terminate_invocation;
    unsigned char synchronization2;
    unsigned char zero_initialize_workgroup_memory;

    /* ---- VkPhysicalDeviceFeatures（核心 1.0 特性，Zink 备选路线需要） ---- */
    unsigned char f_robust_buffer_access;
    unsigned char f_independent_blend;
    unsigned char f_geometry_shader;
    unsigned char f_tessellation_shader;
    unsigned char f_sample_rate_shading;
    unsigned char f_dual_src_blend;
    unsigned char f_logic_op;
    unsigned char f_multi_viewport;
    unsigned char f_multi_draw_indirect;
    unsigned char f_depth_clamp;
    unsigned char f_fill_mode_non_solid;
    unsigned char f_alpha_to_one;
    unsigned char f_shader_clip_distance;
    unsigned char f_shader_cull_distance;
    unsigned char f_image_cube_array;
    unsigned char f_shader_storage_image_extended_formats;
    unsigned char f_shader_storage_image_write_without_format;
    unsigned char f_vertex_pipeline_stores_and_atomics;
    unsigned char f_fragment_stores_and_atomics;
    unsigned char f_sampler_anisotropy;
    unsigned char f_depth_bias_clamp;

    /* ---- 子群组支持（VkPhysicalDeviceSubgroupProperties） ---- */
    uint32_t subgroup_supported_operations;   /* VK13_SUBGROUP_FEATURE_* 位段 */
    uint32_t subgroup_supported_stages;       /* VK13_SHADER_STAGE_* 位段 */
} vk13_feature_state_t;

/* 必选特性表条目。 */
typedef struct vk13_required_feature {
    uint32_t    level;        /* 所属层级 VK13_LEVEL_* */
    const char *struct_name;  /* 特性所在结构体（vk.xml 名） */
    const char *member;       /* 特性成员名（vk.xml 名） */
    int         conditional;  /* 1 = 条件项（不参与门控） */
    const char *condition;    /* 条件项的依赖说明；无条件项为 NULL */
} vk13_required_feature_t;

/* 全部无条件必选特性（按层级升序）。count 输出条数。 */
const vk13_required_feature_t *vk13_required_features_mandatory(
    size_t *count);

/* 条件必选特性（不参与门控，仅供审计展示）。count 输出条数。 */
const vk13_required_feature_t *vk13_required_features_conditional(
    size_t *count);

/* 指定层级的无条件必选特性子表（供门控与审计）。 */
const vk13_required_feature_t *vk13_required_features_for_level(
    uint32_t level, size_t *count);

/* 层级判定：apiVersion 与该层级及以下的全部无条件特性均满足返回 1。 */
int vk13_level_supported(const vk13_feature_state_t *st, uint32_t level);

/* 当前特性集可达的最高层级（VK13_LEVEL_1_0 .. VK13_LEVEL_1_3）。 */
uint32_t vk13_max_reachable_level(const vk13_feature_state_t *st);

/* 缺口枚举：把达到 level 所缺的无条件特性写入 out（最多 max_out 条），
 * 返回缺口总数（可大于 max_out）。out 可为 NULL（仅计数）。 */
size_t vk13_missing_features(const vk13_feature_state_t *st, uint32_t level,
                             vk13_required_feature_t *out, size_t max_out);

#ifdef __cplusplus
}
#endif

#endif /* ROK_VK13_FEATURES_H */
