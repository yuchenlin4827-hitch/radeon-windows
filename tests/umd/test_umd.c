/* SPDX-License-Identifier: MIT
 * test_umd.c — umd/vk13 与 umd/gl46 模块的单元测试（Linux 本机 gcc 跑通）。
 *
 * 覆盖目标：
 *   1. promoted 扩展表计数 = 23 / 24 / 23（与 vk.xml 逐一核对）；
 *   2. 必选核心特性表：层级门控、缺口枚举、最大可达层级；
 *   3. R600g GL 4.6 门控：evergreen+ 通过、TeraScale 1 → GL 3.3、
 *      缺 cap 时的缺口报告、PCI ID 表（0x6840→TURKS、0x6779→CAICOS）；
 *   4. Zink 备选路线门控：1.3 + VOTE + scalarBlockLayout 等组合判定。
 */
#include <stdio.h>
#include <string.h>

#include "vk13_promoted.h"
#include "vk13_features.h"
#include "gl46_r600g.h"
#include "gl46_zink.h"

static int g_failed = 0;
static int g_total = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_total++;                                                           \
        if (!(cond)) {                                                       \
            g_failed++;                                                      \
            printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                    \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                   \
    do {                                                                     \
        long long _va = (long long)(a);                                      \
        long long _vb = (long long)(b);                                      \
        g_total++;                                                           \
        if (_va != _vb) {                                                    \
            g_failed++;                                                      \
            printf("  [FAIL] %s:%d: %s == %s (%lld != %lld)\n",              \
                   __FILE__, __LINE__, #a, #b, _va, _vb);                    \
        }                                                                    \
    } while (0)

/* 构造一个"全绿"的 Vulkan 1.3 特性快照。 */
static void fill_perfect_vk13(vk13_feature_state_t *st)
{
    memset(st, 0, sizeof(*st));
    st->api_version = VK13_API_VERSION_1_3;
    st->multiview = 1;
    st->shader_draw_parameters = 1;
    st->host_query_reset = 1;
    st->imageless_framebuffer = 1;
    st->separate_depth_stencil_layouts = 1;
    st->shader_subgroup_extended_types = 1;
    st->timeline_semaphore = 1;
    st->uniform_buffer_standard_layout = 1;
    st->subgroup_broadcast_dynamic_id = 1;
    st->buffer_device_address = 1;
    st->vulkan_memory_model = 1;
    st->vulkan_memory_model_device_scope = 1;
    st->scalar_block_layout = 1;
    st->draw_indirect_count = 1;
    st->sampler_mirror_clamp_to_edge = 1;
    st->dynamic_rendering = 1;
    st->inline_uniform_block = 1;
    st->maintenance4 = 1;
    st->pipeline_creation_cache_control = 1;
    st->private_data = 1;
    st->robust_image_access = 1;
    st->shader_demote_to_helper_invocation = 1;
    st->shader_integer_dot_product = 1;
    st->subgroup_size_control = 1;
    st->compute_full_subgroups = 1;
    st->shader_terminate_invocation = 1;
    st->synchronization2 = 1;
    st->zero_initialize_workgroup_memory = 1;
    st->f_robust_buffer_access = 1;
    st->f_independent_blend = 1;
    st->f_geometry_shader = 1;
    st->f_tessellation_shader = 1;
    st->f_sample_rate_shading = 1;
    st->f_dual_src_blend = 1;
    st->f_logic_op = 1;
    st->f_multi_viewport = 1;
    st->f_multi_draw_indirect = 1;
    st->f_depth_clamp = 1;
    st->f_fill_mode_non_solid = 1;
    st->f_alpha_to_one = 1;
    st->f_shader_clip_distance = 1;
    st->f_shader_cull_distance = 1;
    st->f_image_cube_array = 1;
    st->f_shader_storage_image_extended_formats = 1;
    st->f_shader_storage_image_write_without_format = 1;
    st->f_vertex_pipeline_stores_and_atomics = 1;
    st->f_fragment_stores_and_atomics = 1;
    st->f_sampler_anisotropy = 1;
    st->f_depth_bias_clamp = 1;
    st->subgroup_supported_operations = VK13_SUBGROUP_FEATURE_BASIC_BIT |
                                        VK13_SUBGROUP_FEATURE_VOTE_BIT |
                                        VK13_SUBGROUP_FEATURE_BALLOT_BIT;
    st->subgroup_supported_stages = VK13_SHADER_STAGE_VERTEX_BIT |
                                    VK13_SHADER_STAGE_FRAGMENT_BIT |
                                    VK13_SHADER_STAGE_COMPUTE_BIT;
}

static void fill_perfect_r600g(gl46_r600g_state_t *st, gl46_chip_family_t fam)
{
    memset(st, 0, sizeof(*st));
    st->family = fam;
    st->winsys_drm_version = gl46_r600g_version_simple(2, 51, 0);
    st->ws_hw_atomic_counters = 1;
    st->ws_streamout = 1;
    st->cap_gl_spirv = 1;
    st->cap_shader_group_vote = 1;
    st->cap_polygon_offset_clamp = 1;
    st->cap_query_pipeline_statistics = 1;
    st->cap_query_so_overflow = 1;
    st->cap_texture_barrier = 1;
    st->cap_anisotropic_filter = 1;
    st->cap_draw_parameters = 1;
}

static void fill_perfect_zink(gl46_zink_state_t *st,
                              const vk13_feature_state_t *vk)
{
    memset(st, 0, sizeof(*st));
    st->vk = vk;
    st->ext_scalar_block_layout = 0; /* 走 feats12.scalarBlockLayout */
    st->ext_draw_indirect_count = 0; /* 走 feats12.drawIndirectCount */
    st->ext_custom_border_color = 1;
    st->ext_border_color_swizzle = 1;
    st->ext_line_rasterization = 1;
    st->ext_transform_feedback = 1;
    st->ext_conditional_rendering = 1;
    st->ext_depth_clip_enable = 1;
    st->ext_vertex_attribute_divisor = 1;
    st->ext_image_2d_view_of_3d = 1;
    st->feat_custom_border_color_without_format = 1;
    st->feat_border_color_swizzle_from_image = 1;
    st->feat_rectangular_lines = 1;
    st->feat_transform_feedback = 1;
    st->feat_conditional_rendering = 1;
    st->feat_depth_clip_enable = 1;
    st->feat_vertex_attribute_instance_rate_divisor = 1;
    st->feat_image_2d_view_of_3d = 1;
    st->max_per_stage_descriptor_samplers = 16;
    st->max_image_dimension_1d = 16384;
    st->max_image_dimension_2d = 16384;
    st->max_image_dimension_3d = 2048;
    st->max_image_dimension_cube = 16384;
    st->max_image_array_layers = 2048;
    st->max_viewports = 16;
    st->fmt_d32s8_depth_stencil_attachment = 1;
    st->fmt_rgb32f_uniform_texel_buffer = 1;
    st->fmt_rgba8_sampled = 1;
    st->fmt_r16_r16g16_unorm_sampled = 1;
    st->fmt_b10g11r11_vertex = 1;
}

/* ------------------------------------------------------------------ */
static size_t g_foreach_seen[3];

static void foreach_counter(uint32_t level, const char *name, void *user)
{
    (void)name;
    (void)user;
    if (level == VK13_LEVEL_1_1) {
        g_foreach_seen[0]++;
    } else if (level == VK13_LEVEL_1_2) {
        g_foreach_seen[1]++;
    } else if (level == VK13_LEVEL_1_3) {
        g_foreach_seen[2]++;
    }
}

static void test_promoted_tables(void)
{
    printf("[suite] vk13_promoted\n");
    CHECK_EQ_INT(vk13_promoted_extension_count(VK13_LEVEL_1_1), 23);
    CHECK_EQ_INT(vk13_promoted_extension_count(VK13_LEVEL_1_2), 24);
    CHECK_EQ_INT(vk13_promoted_extension_count(VK13_LEVEL_1_3), 23);
    CHECK_EQ_INT(vk13_promoted_extension_count(VK13_LEVEL_1_0), 0);

    {
        size_t n = 0;
        const char *const *t = vk13_promoted_extensions(VK13_LEVEL_1_3, &n);
        CHECK(t != NULL);
        CHECK_EQ_INT(n, 23);
        CHECK(strcmp(t[0], "VK_EXT_4444_formats") == 0);
        CHECK(strcmp(t[n - 1], "VK_KHR_zero_initialize_workgroup_memory") == 0);
    }

    {
        /* foreach 遍历总数 = 23 + 24 + 23 */
        g_foreach_seen[0] = g_foreach_seen[1] = g_foreach_seen[2] = 0;
        vk13_promoted_foreach(foreach_counter, NULL);
        CHECK_EQ_INT(g_foreach_seen[0], 23);
        CHECK_EQ_INT(g_foreach_seen[1], 24);
        CHECK_EQ_INT(g_foreach_seen[2], 23);
    }
}

static void test_vk13_gate(void)
{
    vk13_feature_state_t st;
    vk13_required_feature_t miss[32];
    size_t n;

    printf("[suite] vk13_features\n");
    {
        size_t m = 0, c = 0;
        (void)vk13_required_features_mandatory(&m);
        (void)vk13_required_features_conditional(&c);
        /* 无条件必选：1.1 的 1 项 + 1.2 的 7 项 + 1.3 的 16 项 = 24 */
        CHECK_EQ_INT(m, 24);
        CHECK(c >= 5);
    }

    fill_perfect_vk13(&st);
    CHECK_EQ_INT(vk13_max_reachable_level(&st), VK13_LEVEL_1_3);
    CHECK_EQ_INT(vk13_level_supported(&st, VK13_LEVEL_1_3), 1);
    CHECK_EQ_INT(vk13_missing_features(&st, VK13_LEVEL_1_3, NULL, 0), 0);

    /* API 版本不足：即使特性全绿也判失败。 */
    st.api_version = VK13_API_VERSION_1_2;
    CHECK_EQ_INT(vk13_level_supported(&st, VK13_LEVEL_1_3), 0);
    CHECK_EQ_INT(vk13_max_reachable_level(&st), VK13_LEVEL_1_2);
    st.api_version = VK13_API_VERSION_1_3;

    /* 缺 multiview：1.1 及以上全不可达。 */
    st.multiview = 0;
    CHECK_EQ_INT(vk13_level_supported(&st, VK13_LEVEL_1_1), 0);
    CHECK_EQ_INT(vk13_max_reachable_level(&st), VK13_LEVEL_1_0);
    n = vk13_missing_features(&st, VK13_LEVEL_1_1, miss, 32);
    CHECK_EQ_INT(n, 1);
    CHECK(strcmp(miss[0].member, "multiview") == 0);
    st.multiview = 1;

    /* 缺 timelineSemaphore：1.2/1.3 不可达，1.1 可达。 */
    st.timeline_semaphore = 0;
    CHECK_EQ_INT(vk13_level_supported(&st, VK13_LEVEL_1_1), 1);
    CHECK_EQ_INT(vk13_level_supported(&st, VK13_LEVEL_1_2), 0);
    st.timeline_semaphore = 1;

    /* 缺 bufferDeviceAddress（1.3 新增强制项）：1.2 可达。 */
    st.buffer_device_address = 0;
    CHECK_EQ_INT(vk13_level_supported(&st, VK13_LEVEL_1_2), 1);
    CHECK_EQ_INT(vk13_level_supported(&st, VK13_LEVEL_1_3), 0);
    n = vk13_missing_features(&st, VK13_LEVEL_1_3, miss, 32);
    CHECK_EQ_INT(n, 1);
    CHECK(strcmp(miss[0].member, "bufferDeviceAddress") == 0);
    st.buffer_device_address = 1;

    /* 缺 vulkanMemoryModelDeviceScope（1.3 强制项，属于 1.2 结构体）。 */
    st.vulkan_memory_model_device_scope = 0;
    CHECK_EQ_INT(vk13_level_supported(&st, VK13_LEVEL_1_3), 0);
    st.vulkan_memory_model_device_scope = 1;

    /* 缺 dynamicRendering（1.3 代表性强制项）。 */
    st.dynamic_rendering = 0;
    CHECK_EQ_INT(vk13_level_supported(&st, VK13_LEVEL_1_3), 0);
    st.dynamic_rendering = 1;

    /* 多缺口场景：1.3 缺 3 项应精确计数。 */
    st.dynamic_rendering = 0;
    st.synchronization2 = 0;
    st.maintenance4 = 0;
    n = vk13_missing_features(&st, VK13_LEVEL_1_3, miss, 32);
    CHECK_EQ_INT(n, 3);
    CHECK_EQ_INT(vk13_max_reachable_level(&st), VK13_LEVEL_1_2);

    /* terakan 现状模拟：1.0 驱动、特性全无 → 只到 1.0。 */
    memset(&st, 0, sizeof(st));
    st.api_version = VK13_API_VERSION_1_0;
    CHECK_EQ_INT(vk13_max_reachable_level(&st), VK13_LEVEL_1_0);
    CHECK_EQ_INT(vk13_missing_features(&st, VK13_LEVEL_1_0, NULL, 0), 0);
}

static void test_r600g_gate(void)
{
    gl46_r600g_state_t st;
    gl46_r600g_verdict_t v;
    gl46_gap_t gaps[16];
    size_t n;

    printf("[suite] gl46_r600g\n");

    /* PCI 表抽查：验证对象两块卡。 */
    CHECK_EQ_INT(gl46_chip_family_from_device_id(0x6840), GL46_CHIP_TURKS);
    CHECK_EQ_INT(gl46_chip_family_from_device_id(0x6841), GL46_CHIP_TURKS);
    CHECK_EQ_INT(gl46_chip_family_from_device_id(0x6779), GL46_CHIP_CAICOS);
    CHECK_EQ_INT(gl46_chip_family_from_device_id(0x677B), GL46_CHIP_CAICOS);
    CHECK_EQ_INT(gl46_chip_family_from_device_id(0x6760), GL46_CHIP_CAICOS);
    CHECK_EQ_INT(gl46_chip_family_from_device_id(0x9400), GL46_CHIP_R600);
    CHECK_EQ_INT(gl46_chip_family_from_device_id(0x6720), GL46_CHIP_BARTS);
    CHECK_EQ_INT(gl46_chip_family_from_device_id(0x6718), GL46_CHIP_CAYMAN);
    CHECK_EQ_INT(gl46_chip_family_from_device_id(0x6704), GL46_CHIP_CAYMAN);
    CHECK_EQ_INT(gl46_chip_family_from_device_id(0xFFFF), GL46_CHIP_UNKNOWN);
    CHECK(gl46_chip_family_is_evergreen_plus(GL46_CHIP_TURKS));
    CHECK(gl46_chip_family_is_evergreen_plus(GL46_CHIP_CAICOS));
    CHECK(gl46_chip_family_is_evergreen_plus(GL46_CHIP_CEDAR));
    CHECK(!gl46_chip_family_is_evergreen_plus(GL46_CHIP_RV770));
    CHECK(!gl46_chip_family_is_evergreen_plus(GL46_CHIP_UNKNOWN));

    /* evergreen+ + 全部 cap/winsys 就绪 → GL 4.6 READY。 */
    fill_perfect_r600g(&st, GL46_CHIP_TURKS);
    v = GL46_R600G_FAIL;
    CHECK_EQ_INT(gl46_r600g_evaluate(&st, &v, NULL, 0), 0);
    CHECK_EQ_INT(v, GL46_R600G_GL46_READY);

    fill_perfect_r600g(&st, GL46_CHIP_CAICOS);
    CHECK_EQ_INT(gl46_r600g_evaluate(&st, &v, NULL, 0), 0);
    CHECK_EQ_INT(v, GL46_R600G_GL46_READY);

    /* TeraScale 1（RV770）→ GL 3.3，非缺口。 */
    fill_perfect_r600g(&st, GL46_CHIP_RV770);
    CHECK_EQ_INT(gl46_r600g_evaluate(&st, &v, NULL, 0), 0);
    CHECK_EQ_INT(v, GL46_R600G_GL330);

    /* 未知芯片 → FAIL + 1 条缺口。 */
    fill_perfect_r600g(&st, GL46_CHIP_UNKNOWN);
    n = gl46_r600g_evaluate(&st, &v, gaps, 16);
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_INT(v, GL46_R600G_FAIL);
    CHECK(strcmp(gaps[0].id, "chip-family") == 0);

    /* evergreen+ 但 winsys 版本过旧 → 报 indirect_parameters 缺口。 */
    fill_perfect_r600g(&st, GL46_CHIP_TURKS);
    st.winsys_drm_version = gl46_r600g_version_simple(2, 50, 9);
    n = gl46_r600g_evaluate(&st, &v, gaps, 16);
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_INT(v, GL46_R600G_FAIL);
    CHECK(strcmp(gaps[0].id, "winsys_drm_version") == 0);

    /* 全空状态（evergreen 芯片、cap/winsys 全无）→ 11 条缺口（8 cap +
     * winsys 版本 + 原子计数器 + streamout）。 */
    memset(&st, 0, sizeof(st));
    st.family = GL46_CHIP_TURKS;
    n = gl46_r600g_evaluate(&st, &v, NULL, 0);
    CHECK_EQ_INT(n, 11);
    CHECK_EQ_INT(v, GL46_R600G_FAIL);
}

static void test_zink_gate(void)
{
    vk13_feature_state_t vk;
    gl46_zink_state_t zk;
    gl46_zink_verdict_t v;
    gl46_gap_t gaps[32];
    size_t n;

    printf("[suite] gl46_zink\n");

    fill_perfect_vk13(&vk);
    fill_perfect_zink(&zk, &vk);
    v = GL46_ZINK_FAIL;
    CHECK_EQ_INT(gl46_zink_evaluate(&zk, &v, NULL, 0), 0);
    CHECK_EQ_INT(v, GL46_ZINK_GL46_READY);

    /* Vulkan 只有 1.2 → 失败（gl46_baseline 要求 1.3.0）。 */
    vk.api_version = VK13_API_VERSION_1_2;
    n = gl46_zink_evaluate(&zk, &v, gaps, 32);
    CHECK(n >= 1);
    CHECK_EQ_INT(v, GL46_ZINK_FAIL);
    vk.api_version = VK13_API_VERSION_1_3;

    /* 子群组缺 VOTE → ARB_shader_group_vote 缺口。 */
    vk.subgroup_supported_operations &= ~(uint32_t)VK13_SUBGROUP_FEATURE_VOTE_BIT;
    n = gl46_zink_evaluate(&zk, &v, gaps, 32);
    CHECK_EQ_INT(n, 1);
    CHECK(strcmp(gaps[0].id, "subgroup-vote") == 0);
    vk.subgroup_supported_operations |= VK13_SUBGROUP_FEATURE_VOTE_BIT;

    /* 子群组未覆盖 COMPUTE 阶段 → 缺口。 */
    vk.subgroup_supported_stages &= ~(uint32_t)VK13_SHADER_STAGE_COMPUTE_BIT;
    n = gl46_zink_evaluate(&zk, &v, NULL, 0);
    CHECK_EQ_INT(n, 1);
    vk.subgroup_supported_stages |= VK13_SHADER_STAGE_COMPUTE_BIT;

    /* scalarBlockLayout 与 EXT 都缺 → Zink 初始化硬校验失败。 */
    vk.scalar_block_layout = 0;
    n = gl46_zink_evaluate(&zk, &v, gaps, 32);
    CHECK_EQ_INT(n, 1);
    CHECK(strcmp(gaps[0].id, "scalar_block_layout") == 0);
    /* 换成 EXT 补齐 → 通过。 */
    zk.ext_scalar_block_layout = 1;
    CHECK_EQ_INT(gl46_zink_evaluate(&zk, &v, NULL, 0), 0);
    CHECK_EQ_INT(v, GL46_ZINK_GL46_READY);
    vk.scalar_block_layout = 1;
    zk.ext_scalar_block_layout = 0;

    /* samplerAnisotropy 缺 → gl46_baseline 增量缺口。 */
    vk.f_sampler_anisotropy = 0;
    n = gl46_zink_evaluate(&zk, &v, gaps, 32);
    CHECK_EQ_INT(n, 1);
    CHECK(strcmp(gaps[0].id, "sampler_anisotropy") == 0);
    vk.f_sampler_anisotropy = 1;

    /* depthBiasClamp 缺 → gl46_baseline 增量缺口。 */
    vk.f_depth_bias_clamp = 0;
    n = gl46_zink_evaluate(&zk, &v, gaps, 32);
    CHECK_EQ_INT(n, 1);
    CHECK(strcmp(gaps[0].id, "depth_bias_clamp") == 0);
    vk.f_depth_bias_clamp = 1;

    /* drawIndirectCount 双通道都缺 → gl46_baseline 扩展缺口。 */
    vk.draw_indirect_count = 0;
    n = gl46_zink_evaluate(&zk, &v, gaps, 32);
    CHECK_EQ_INT(n, 1);
    CHECK(strcmp(gaps[0].id, "draw_indirect_count") == 0);
    zk.ext_draw_indirect_count = 1;
    CHECK_EQ_INT(gl46_zink_evaluate(&zk, &v, NULL, 0), 0);
    zk.ext_draw_indirect_count = 0;
    vk.draw_indirect_count = 1;

    /* limits 不足：maxViewports = 8 → 缺口。 */
    zk.max_viewports = 8;
    n = gl46_zink_evaluate(&zk, &v, gaps, 32);
    CHECK_EQ_INT(n, 1);
    CHECK(strcmp(gaps[0].id, "limits_viewports") == 0);
    zk.max_viewports = 16;

    /* 全空状态 → 大量缺口（> 25）。 */
    memset(&vk, 0, sizeof(vk));
    memset(&zk, 0, sizeof(zk));
    zk.vk = &vk;
    n = gl46_zink_evaluate(&zk, &v, NULL, 0);
    CHECK(n > 25);
    CHECK_EQ_INT(v, GL46_ZINK_FAIL);
}

int main(void)
{
    printf("umd 单元测试（radeon-windows M5.5/M7 门控模块）\n");
    test_promoted_tables();
    test_vk13_gate();
    test_r600g_gate();
    test_zink_gate();
    printf("\n结果：%d 项检查，%d 失败\n", g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
