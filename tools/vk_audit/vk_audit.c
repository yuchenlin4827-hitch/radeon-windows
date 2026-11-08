// SPDX-License-Identifier: MIT
// vk_audit — radeon-windows M5.5/M7 能力审计工具（Windows 控制台）
//
// 回答两个问题：
//   [1] 当前系统的 Vulkan ICD 距离 Vulkan 1.1 / 1.2 / 1.3 还差什么？
//   [2] 这块 GPU 在两条 OpenGL 4.6 路线上各自处在什么状态？
//         - 主路线：R600g（Mesa TeraScale gallium 驱动）原生移植
//         - 备选：Zink（GL-on-Vulkan，要求 Vulkan 1.3 基座）
//
// 实现说明：
//   - 通过 LoadLibrary("vulkan-1.dll") + vkGetInstanceProcAddr 动态加载，
//     只依赖官方 Vulkan-Headers 的 vulkan_core.h（构建脚本按需下载，
//     pinned v1.4.330，Apache-2.0/MIT 双许可）；
//   - 特性/子群组/格式/limits 逐项填入 umd/vk13 与 umd/gl46 的门控模块，
//     判定逻辑与单元测试完全同源；
//   - 控制台输出用英文标签：避免 Windows 控制台代码页对 UTF-8 中文乱码
//     （与 tests/gpu_probe/gpu_probe.c 的做法一致）。
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan_core.h>

#include "vk13_promoted.h"
#include "vk13_features.h"
#include "gl46_r600g.h"
#include "gl46_zink.h"

/* ------------------------------------------------------------------ */
/* 动态加载的 Vulkan 入口                                              */
/* ------------------------------------------------------------------ */
static HMODULE g_vkdll = NULL;
static PFN_vkGetInstanceProcAddr g_vkGetInstanceProcAddr = NULL;

#define VKE(name) static PFN_##name g_pfn_##name = NULL
VKE(vkCreateInstance);
VKE(vkEnumeratePhysicalDevices);
VKE(vkGetPhysicalDeviceProperties);
VKE(vkGetPhysicalDeviceProperties2);
VKE(vkGetPhysicalDeviceFeatures);
VKE(vkGetPhysicalDeviceFeatures2);
VKE(vkEnumerateDeviceExtensionProperties);
VKE(vkGetPhysicalDeviceFormatProperties);
#undef VKE

static int load_vulkan_loader(void)
{
    g_vkdll = LoadLibraryW(L"vulkan-1.dll");
    if (g_vkdll == NULL) {
        printf("  vulkan-1.dll NOT found - Vulkan loader not installed.\n");
        printf("  Install the Vulkan runtime (or terakan ICD + loader).\n");
        return 0;
    }
    g_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)(void *)
        GetProcAddress(g_vkdll, "vkGetInstanceProcAddr");
    if (g_vkGetInstanceProcAddr == NULL) {
        printf("  vkGetInstanceProcAddr missing in vulkan-1.dll\n");
        return 0;
    }
#define GET(name)                                                          \
    g_pfn_##name = (PFN_##name)(void *)g_vkGetInstanceProcAddr(            \
        VK_NULL_HANDLE, #name);                                            \
    if (g_pfn_##name == NULL) {                                            \
        printf("  global export missing: %s\n", #name);                    \
        return 0;                                                          \
    }
    GET(vkCreateInstance);
    GET(vkEnumeratePhysicalDevices);
    GET(vkGetPhysicalDeviceProperties);
    GET(vkGetPhysicalDeviceFeatures);
    GET(vkEnumerateDeviceExtensionProperties);
    GET(vkGetPhysicalDeviceFormatProperties);
#undef GET
    /* vkGetPhysicalDevice{Properties2,Features2} 需要实例句柄才能解析，
     * 在 main() 中做；1.0 ICD 下可能为 NULL（属正常降级）。 */
    return 1;
}

static void print_api_version(uint32_t v, const char *suffix)
{
    printf("%u.%u.%u%s", vk13_version_major(v), vk13_version_minor(v),
           vk13_version_patch(v), suffix);
}

/* ------------------------------------------------------------------ */
/* [A] Vulkan 1.1/1.2/1.3 门控                                         */
/* ------------------------------------------------------------------ */
static void fill_vk13_state(VkPhysicalDevice pdev, uint32_t api_version,
                            vk13_feature_state_t *st)
{
    memset(st, 0, sizeof(*st));
    st->api_version = api_version;

    if (g_pfn_vkGetPhysicalDeviceFeatures2 == NULL ||
        g_pfn_vkGetPhysicalDeviceProperties2 == NULL) {
        return; /* 1.0-only ICD：版本门控即判 1.1 不可达 */
    }

    {
        VkPhysicalDeviceVulkan11Features f11;
        VkPhysicalDeviceVulkan12Features f12;
        VkPhysicalDeviceVulkan13Features f13;
        VkPhysicalDeviceFeatures2 f2;

        memset(&f11, 0, sizeof(f11));
        f11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        memset(&f12, 0, sizeof(f12));
        f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        f12.pNext = &f11;
        memset(&f13, 0, sizeof(f13));
        f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        f13.pNext = &f12;
        memset(&f2, 0, sizeof(f2));
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &f13;

        g_pfn_vkGetPhysicalDeviceFeatures2(pdev, &f2);

        st->multiview = f11.multiview ? 1 : 0;
        st->shader_draw_parameters = f11.shaderDrawParameters ? 1 : 0;
        st->host_query_reset = f12.hostQueryReset ? 1 : 0;
        st->imageless_framebuffer = f12.imagelessFramebuffer ? 1 : 0;
        st->separate_depth_stencil_layouts = f12.separateDepthStencilLayouts ? 1 : 0;
        st->shader_subgroup_extended_types = f12.shaderSubgroupExtendedTypes ? 1 : 0;
        st->timeline_semaphore = f12.timelineSemaphore ? 1 : 0;
        st->uniform_buffer_standard_layout = f12.uniformBufferStandardLayout ? 1 : 0;
        st->subgroup_broadcast_dynamic_id = f12.subgroupBroadcastDynamicId ? 1 : 0;
        st->buffer_device_address = f12.bufferDeviceAddress ? 1 : 0;
        st->vulkan_memory_model = f12.vulkanMemoryModel ? 1 : 0;
        st->vulkan_memory_model_device_scope = f12.vulkanMemoryModelDeviceScope ? 1 : 0;
        st->scalar_block_layout = f12.scalarBlockLayout ? 1 : 0;
        st->draw_indirect_count = f12.drawIndirectCount ? 1 : 0;
        st->sampler_mirror_clamp_to_edge = f12.samplerMirrorClampToEdge ? 1 : 0;
        st->dynamic_rendering = f13.dynamicRendering ? 1 : 0;
        st->inline_uniform_block = f13.inlineUniformBlock ? 1 : 0;
        st->maintenance4 = f13.maintenance4 ? 1 : 0;
        st->pipeline_creation_cache_control = f13.pipelineCreationCacheControl ? 1 : 0;
        st->private_data = f13.privateData ? 1 : 0;
        st->robust_image_access = f13.robustImageAccess ? 1 : 0;
        st->shader_demote_to_helper_invocation = f13.shaderDemoteToHelperInvocation ? 1 : 0;
        st->shader_integer_dot_product = f13.shaderIntegerDotProduct ? 1 : 0;
        st->subgroup_size_control = f13.subgroupSizeControl ? 1 : 0;
        st->compute_full_subgroups = f13.computeFullSubgroups ? 1 : 0;
        st->shader_terminate_invocation = f13.shaderTerminateInvocation ? 1 : 0;
        st->synchronization2 = f13.synchronization2 ? 1 : 0;
        st->zero_initialize_workgroup_memory = f13.shaderZeroInitializeWorkgroupMemory ? 1 : 0;

        st->f_robust_buffer_access = f2.features.robustBufferAccess ? 1 : 0;
        st->f_independent_blend = f2.features.independentBlend ? 1 : 0;
        st->f_geometry_shader = f2.features.geometryShader ? 1 : 0;
        st->f_tessellation_shader = f2.features.tessellationShader ? 1 : 0;
        st->f_sample_rate_shading = f2.features.sampleRateShading ? 1 : 0;
        st->f_dual_src_blend = f2.features.dualSrcBlend ? 1 : 0;
        st->f_logic_op = f2.features.logicOp ? 1 : 0;
        st->f_multi_viewport = f2.features.multiViewport ? 1 : 0;
        st->f_multi_draw_indirect = f2.features.multiDrawIndirect ? 1 : 0;
        st->f_depth_clamp = f2.features.depthClamp ? 1 : 0;
        st->f_fill_mode_non_solid = f2.features.fillModeNonSolid ? 1 : 0;
        st->f_alpha_to_one = f2.features.alphaToOne ? 1 : 0;
        st->f_shader_clip_distance = f2.features.shaderClipDistance ? 1 : 0;
        st->f_shader_cull_distance = f2.features.shaderCullDistance ? 1 : 0;
        st->f_image_cube_array = f2.features.imageCubeArray ? 1 : 0;
        st->f_shader_storage_image_extended_formats =
            f2.features.shaderStorageImageExtendedFormats ? 1 : 0;
        st->f_shader_storage_image_write_without_format =
            f2.features.shaderStorageImageWriteWithoutFormat ? 1 : 0;
        st->f_vertex_pipeline_stores_and_atomics =
            f2.features.vertexPipelineStoresAndAtomics ? 1 : 0;
        st->f_fragment_stores_and_atomics =
            f2.features.fragmentStoresAndAtomics ? 1 : 0;
        st->f_sampler_anisotropy = f2.features.samplerAnisotropy ? 1 : 0;
        st->f_depth_bias_clamp = f2.features.depthBiasClamp ? 1 : 0;
    }

    {
        /* 子群组支持（props2 链）。 */
        VkPhysicalDeviceSubgroupProperties sub;
        VkPhysicalDeviceProperties2 props2;

        memset(&sub, 0, sizeof(sub));
        sub.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
        memset(&props2, 0, sizeof(props2));
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &sub;
        g_pfn_vkGetPhysicalDeviceProperties2(pdev, &props2);
        st->subgroup_supported_operations = sub.supportedOperations;
        st->subgroup_supported_stages = sub.supportedStages;
    }
}

static void audit_vk13_levels(const vk13_feature_state_t *st, int *has_gap)
{
    static const struct {
        uint32_t    level;
        const char *name;
    } k_levels[] = {
        { VK13_LEVEL_1_1, "1.1" },
        { VK13_LEVEL_1_2, "1.2" },
        { VK13_LEVEL_1_3, "1.3" },
    };
    size_t i;
    uint32_t reachable = vk13_max_reachable_level(st);

    for (i = 0; i < sizeof(k_levels) / sizeof(k_levels[0]); i++) {
        if (vk13_level_supported(st, k_levels[i].level)) {
            printf("    Vulkan %s : REACHABLE\n", k_levels[i].name);
        } else {
            vk13_required_feature_t miss[32];
            size_t n, j;
            n = vk13_missing_features(st, k_levels[i].level, miss, 32);
            printf("    Vulkan %s : %zu gap(s)\n", k_levels[i].name, n);
            for (j = 0; j < n && j < 32; j++) {
                printf("      - %s.%s\n",
                       miss[j].struct_name, miss[j].member);
            }
        }
    }
    printf("    max reachable : ");
    print_api_version(reachable, "\n");
    if (reachable < VK13_LEVEL_1_3) {
        *has_gap = 1;
    }
}

/* ------------------------------------------------------------------ */
/* [B] promoted 扩展信息性对比                                         */
/* ------------------------------------------------------------------ */
typedef struct promoted_ctx {
    VkPhysicalDevice pdev;
    uint32_t         level;
    size_t           total;
    size_t           advertised;
} promoted_ctx;

static void promoted_cb(uint32_t level, const char *name, void *user)
{
    promoted_ctx *ctx = (promoted_ctx *)user;
    uint32_t count = 0;
    VkExtensionProperties *props = NULL;
    VkResult vr;
    uint32_t i;
    int found = 0;

    if (level != ctx->level) {
        return;
    }
    ctx->total++;
    vr = g_pfn_vkEnumerateDeviceExtensionProperties(ctx->pdev, NULL, &count,
                                                    NULL);
    if (vr != VK_SUCCESS || count == 0) {
        return;
    }
    props = (VkExtensionProperties *)
        malloc(sizeof(VkExtensionProperties) * count);
    if (props == NULL) {
        return;
    }
    if (g_pfn_vkEnumerateDeviceExtensionProperties(ctx->pdev, NULL, &count,
                                                   props) == VK_SUCCESS) {
        for (i = 0; i < count; i++) {
            if (strcmp(props[i].extensionName, name) == 0) {
                found = 1;
                break;
            }
        }
    }
    free(props);
    if (found) {
        ctx->advertised++;
    }
}

static void audit_promoted_extensions(VkPhysicalDevice pdev)
{
    static const uint32_t k_levels[] = {
        VK13_LEVEL_1_1, VK13_LEVEL_1_2, VK13_LEVEL_1_3,
    };
    size_t i;

    printf("    promoted extensions (informational - core coverage counts):\n");
    for (i = 0; i < sizeof(k_levels) / sizeof(k_levels[0]); i++) {
        promoted_ctx ctx;
        ctx.pdev = pdev;
        ctx.level = k_levels[i];
        ctx.total = 0;
        ctx.advertised = 0;
        vk13_promoted_foreach(promoted_cb, &ctx);
        printf("      Vulkan %u.%u : %zu/%zu still advertised\n",
               vk13_version_major(k_levels[i]), vk13_version_minor(k_levels[i]),
               ctx.advertised, ctx.total);
    }
}

/* ------------------------------------------------------------------ */
/* [C] GL 4.6 R600g 主路线                                             */
/* ------------------------------------------------------------------ */
static gl46_r600g_verdict_t audit_r600g(uint32_t device_id)
{
    gl46_r600g_state_t st;
    gl46_r600g_verdict_t verdict = GL46_R600G_FAIL;
    gl46_gap_t gaps[16];
    size_t n;
    gl46_chip_family_t fam = gl46_chip_family_from_device_id(device_id);

    printf("    chip family : %s\n", gl46_chip_family_name(fam));

    /* R600g 源码中这些 cap 为无条件设置（r600_pipe.c:397-495），硬件门
     * 就是 evergreen+；winsys 服务按"移植目标状态"填写——本工具回答
     * 的是"这块硬件走 R600g 理论上能否到 4.6"。 */
    memset(&st, 0, sizeof(st));
    st.family = fam;
    st.winsys_drm_version = gl46_r600g_version_simple(2, 51, 0);
    st.ws_hw_atomic_counters = 1;
    st.ws_streamout = 1;
    st.cap_gl_spirv = 1;
    st.cap_shader_group_vote = 1;
    st.cap_polygon_offset_clamp = 1;
    st.cap_query_pipeline_statistics = 1;
    st.cap_query_so_overflow = 1;
    st.cap_texture_barrier = 1;
    st.cap_anisotropic_filter = 1;
    st.cap_draw_parameters = 1;

    n = gl46_r600g_evaluate(&st, &verdict, gaps, 16);
    if (verdict == GL46_R600G_GL46_READY) {
        printf("    verdict     : GL 4.6 reachable via R600g port (primary)\n");
    } else if (verdict == GL46_R600G_GL330) {
        printf("    verdict     : GL 3.3 ceiling (TeraScale 1, per r600_pipe.c)\n");
    } else {
        size_t i;
        printf("    verdict     : %zu gap(s)\n", n);
        for (i = 0; i < n && i < 16; i++) {
            printf("      - %s: %s\n", gaps[i].id, gaps[i].why);
        }
    }
    return verdict;
}

/* ------------------------------------------------------------------ */
/* [D] GL 4.6 Zink 备选路线                                            */
/* ------------------------------------------------------------------ */
static void fill_zink_formats(VkPhysicalDevice pdev, gl46_zink_state_t *st)
{
    VkFormatProperties fp;

    memset(&fp, 0, sizeof(fp));
    g_pfn_vkGetPhysicalDeviceFormatProperties(pdev, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                              &fp);
    st->fmt_d32s8_depth_stencil_attachment =
        (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            ? 1 : 0;

    {
        VkFormat fmts[3] = { VK_FORMAT_R32G32B32_SFLOAT,
                             VK_FORMAT_R32G32B32_SINT,
                             VK_FORMAT_R32G32B32_UINT };
        unsigned int i;
        st->fmt_rgb32f_uniform_texel_buffer = 1;
        for (i = 0; i < 3; i++) {
            memset(&fp, 0, sizeof(fp));
            g_pfn_vkGetPhysicalDeviceFormatProperties(pdev, fmts[i], &fp);
            if (!(fp.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT)) {
                st->fmt_rgb32f_uniform_texel_buffer = 0;
            }
        }
    }

    {
        VkFormat fmts[2] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB };
        unsigned int i;
        st->fmt_rgba8_sampled = 1;
        for (i = 0; i < 2; i++) {
            memset(&fp, 0, sizeof(fp));
            g_pfn_vkGetPhysicalDeviceFormatProperties(pdev, fmts[i], &fp);
            if (!((fp.linearTilingFeatures | fp.optimalTilingFeatures) &
                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
                st->fmt_rgba8_sampled = 0;
            }
        }
    }

    {
        VkFormat fmts[2] = { VK_FORMAT_R16_UNORM, VK_FORMAT_R16G16_UNORM };
        unsigned int i;
        st->fmt_r16_r16g16_unorm_sampled = 1;
        for (i = 0; i < 2; i++) {
            memset(&fp, 0, sizeof(fp));
            g_pfn_vkGetPhysicalDeviceFormatProperties(pdev, fmts[i], &fp);
            if (!((fp.linearTilingFeatures | fp.optimalTilingFeatures) &
                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
                st->fmt_r16_r16g16_unorm_sampled = 0;
            }
        }
    }

    memset(&fp, 0, sizeof(fp));
    g_pfn_vkGetPhysicalDeviceFormatProperties(pdev,
                                              VK_FORMAT_B10G11R11_UFLOAT_PACK32,
                                              &fp);
    st->fmt_b10g11r11_vertex =
        (fp.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) ? 1 : 0;
}

static void query_ext_flags(VkPhysicalDevice pdev, gl46_zink_state_t *st,
                            int *has_xfb_ext)
{
    const struct {
        const char    *name;
        unsigned char *flag;
    } ext_map[] = {
        { "VK_EXT_scalar_block_layout",      &st->ext_scalar_block_layout },
        { "VK_KHR_draw_indirect_count",      &st->ext_draw_indirect_count },
        { "VK_EXT_custom_border_color",      &st->ext_custom_border_color },
        { "VK_EXT_border_color_swizzle",     &st->ext_border_color_swizzle },
        { "VK_EXT_line_rasterization",       &st->ext_line_rasterization },
        { "VK_EXT_transform_feedback",       &st->ext_transform_feedback },
        { "VK_EXT_conditional_rendering",    &st->ext_conditional_rendering },
        { "VK_EXT_depth_clip_enable",        &st->ext_depth_clip_enable },
        { "VK_EXT_vertex_attribute_divisor", &st->ext_vertex_attribute_divisor },
        { "VK_EXT_image_2d_view_of_3d",      &st->ext_image_2d_view_of_3d },
    };
    uint32_t count = 0;
    VkExtensionProperties *props = NULL;
    unsigned int i, j;
    VkResult vr;

    *has_xfb_ext = 0;
    vr = g_pfn_vkEnumerateDeviceExtensionProperties(pdev, NULL, &count, NULL);
    if (vr != VK_SUCCESS || count == 0) {
        return;
    }
    props = (VkExtensionProperties *)
        malloc(sizeof(VkExtensionProperties) * count);
    if (props == NULL) {
        return;
    }
    if (g_pfn_vkEnumerateDeviceExtensionProperties(pdev, NULL, &count,
                                                   props) == VK_SUCCESS) {
        for (i = 0; i < count; i++) {
            for (j = 0; j < sizeof(ext_map) / sizeof(ext_map[0]); j++) {
                if (strcmp(props[i].extensionName, ext_map[j].name) == 0) {
                    *ext_map[j].flag = 1;
                    if (j == 5) {
                        *has_xfb_ext = 1;
                    }
                }
            }
        }
    }
    free(props);
}

static gl46_zink_verdict_t audit_zink(VkPhysicalDevice pdev,
                                      const vk13_feature_state_t *vk)
{
    gl46_zink_state_t st;
    gl46_zink_verdict_t verdict = GL46_ZINK_FAIL;
    gl46_gap_t gaps[32];
    size_t n;
    int has_xfb_ext = 0;

    memset(&st, 0, sizeof(st));
    st.vk = vk;

    query_ext_flags(pdev, &st, &has_xfb_ext);

    /* 挂在扩展 feature 结构上的成员（profile 的 features 检查）。
     * 只有扩展存在时才用 features2 链查询，避免 1.0 ICD 不认识 sType。 */
    if (g_pfn_vkGetPhysicalDeviceFeatures2 != NULL) {
        void *tail = NULL;
        VkPhysicalDeviceCustomBorderColorFeaturesEXT f_custom;
        VkPhysicalDeviceBorderColorSwizzleFeaturesEXT f_swizzle;
        VkPhysicalDeviceLineRasterizationFeaturesEXT f_line;
        VkPhysicalDeviceTransformFeedbackFeaturesEXT f_xfb;
        VkPhysicalDeviceConditionalRenderingFeaturesEXT f_cond;
        VkPhysicalDeviceDepthClipEnableFeaturesEXT f_clip;
        VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT f_div;
        VkPhysicalDeviceImage2DViewOf3DFeaturesEXT f_2d3d;
        VkPhysicalDeviceFeatures2 f2;

        /* 逆序构建链（f2.pNext 指向链头）。 */
        memset(&f_2d3d, 0, sizeof(f_2d3d));
        f_2d3d.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_2D_VIEW_OF_3D_FEATURES_EXT;
        f_2d3d.pNext = tail;
        tail = st.ext_image_2d_view_of_3d ? &f_2d3d : NULL;

        memset(&f_div, 0, sizeof(f_div));
        f_div.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT;
        f_div.pNext = tail;
        tail = st.ext_vertex_attribute_divisor ? &f_div : tail;

        memset(&f_clip, 0, sizeof(f_clip));
        f_clip.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT;
        f_clip.pNext = tail;
        tail = st.ext_depth_clip_enable ? &f_clip : tail;

        memset(&f_cond, 0, sizeof(f_cond));
        f_cond.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT;
        f_cond.pNext = tail;
        tail = st.ext_conditional_rendering ? &f_cond : tail;

        memset(&f_xfb, 0, sizeof(f_xfb));
        f_xfb.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
        f_xfb.pNext = tail;
        tail = st.ext_transform_feedback ? &f_xfb : tail;

        memset(&f_line, 0, sizeof(f_line));
        f_line.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT;
        f_line.pNext = tail;
        tail = st.ext_line_rasterization ? &f_line : tail;

        memset(&f_swizzle, 0, sizeof(f_swizzle));
        f_swizzle.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT;
        f_swizzle.pNext = tail;
        tail = st.ext_border_color_swizzle ? &f_swizzle : tail;

        memset(&f_custom, 0, sizeof(f_custom));
        f_custom.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
        f_custom.pNext = tail;
        tail = st.ext_custom_border_color ? &f_custom : tail;

        memset(&f2, 0, sizeof(f2));
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = tail;
        g_pfn_vkGetPhysicalDeviceFeatures2(pdev, &f2);

        st.feat_custom_border_color_without_format =
            st.ext_custom_border_color && f_custom.customBorderColorWithoutFormat;
        st.feat_border_color_swizzle_from_image =
            st.ext_border_color_swizzle && f_swizzle.borderColorSwizzleFromImage;
        st.feat_rectangular_lines =
            st.ext_line_rasterization && f_line.rectangularLines;
        st.feat_transform_feedback =
            st.ext_transform_feedback && f_xfb.transformFeedback;
        st.feat_conditional_rendering =
            st.ext_conditional_rendering && f_cond.conditionalRendering;
        st.feat_depth_clip_enable =
            st.ext_depth_clip_enable && f_clip.depthClipEnable;
        st.feat_vertex_attribute_instance_rate_divisor =
            st.ext_vertex_attribute_divisor &&
            f_div.vertexAttributeInstanceRateDivisor;
        st.feat_image_2d_view_of_3d =
            st.ext_image_2d_view_of_3d && f_2d3d.image2DViewOf3D;
        (void)has_xfb_ext;
    }

    fill_zink_formats(pdev, &st);

    if (g_pfn_vkGetPhysicalDeviceProperties2 != NULL) {
        VkPhysicalDeviceProperties2 p2;
        memset(&p2, 0, sizeof(p2));
        p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        g_pfn_vkGetPhysicalDeviceProperties2(pdev, &p2);
        st.max_per_stage_descriptor_samplers =
            p2.properties.limits.maxPerStageDescriptorSamplers;
        st.max_image_dimension_1d = p2.properties.limits.maxImageDimension1D;
        st.max_image_dimension_2d = p2.properties.limits.maxImageDimension2D;
        st.max_image_dimension_3d = p2.properties.limits.maxImageDimension3D;
        st.max_image_dimension_cube = p2.properties.limits.maxImageDimensionCube;
        st.max_image_array_layers = p2.properties.limits.maxImageArrayLayers;
        st.max_viewports = p2.properties.limits.maxViewports;
    } else {
        VkPhysicalDeviceProperties p0;
        g_pfn_vkGetPhysicalDeviceProperties(pdev, &p0);
        st.max_per_stage_descriptor_samplers =
            p0.limits.maxPerStageDescriptorSamplers;
        st.max_image_dimension_1d = p0.limits.maxImageDimension1D;
        st.max_image_dimension_2d = p0.limits.maxImageDimension2D;
        st.max_image_dimension_3d = p0.limits.maxImageDimension3D;
        st.max_image_dimension_cube = p0.limits.maxImageDimensionCube;
        st.max_image_array_layers = p0.limits.maxImageArrayLayers;
        st.max_viewports = p0.limits.maxViewports;
    }

    n = gl46_zink_evaluate(&st, &verdict, gaps, 32);
    if (verdict == GL46_ZINK_GL46_READY) {
        printf("    verdict     : GL 4.6 reachable via Zink (fallback, Vulkan 1.3 base)\n");
    } else {
        size_t g;
        printf("    verdict     : %zu gap(s)\n", n);
        for (g = 0; g < n && g < 32; g++) {
            printf("      - %s: %s\n", gaps[g].id, gaps[g].why);
        }
    }
    return verdict;
}

/* ------------------------------------------------------------------ */
/* 主流程                                                              */
/* ------------------------------------------------------------------ */
static void usage(void)
{
    printf("usage: vk_audit.exe [--strict]\n");
    printf("  --strict : exit code 1 when Vulkan 1.3 or GL 4.6 routes report gaps\n");
}

int main(int argc, char **argv)
{
    VkApplicationInfo app;
    VkInstanceCreateInfo ci;
    VkInstance instance = VK_NULL_HANDLE;
    uint32_t ndev = 0, i;
    VkPhysicalDevice *devices = NULL;
    int strict = 0;
    int has_gap = 0;
    VkResult vr;

    for (i = 1; i < (uint32_t)argc; i++) {
        if (strcmp(argv[i], "--strict") == 0) {
            strict = 1;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        }
    }

    printf("vk_audit - radeon-windows M5.5 (Vulkan 1.3) / M7 (OpenGL 4.6) audit\n");
    printf("====================================================================\n");

    if (!load_vulkan_loader()) {
        return 3;
    }

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vk_audit";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "radeon-windows";
    app.apiVersion = VK_API_VERSION_1_3;

    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    vr = g_pfn_vkCreateInstance(&ci, NULL, &instance);
    if (vr != VK_SUCCESS) {
        printf("  vkCreateInstance failed, VkResult=%d\n", (int)vr);
        return 3;
    }

    g_pfn_vkGetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
        (void *)g_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2");
    g_pfn_vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)
        (void *)g_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2");

    ndev = 0;
    vr = g_pfn_vkEnumeratePhysicalDevices(instance, &ndev, NULL);
    if (vr != VK_SUCCESS || ndev == 0) {
        printf("  no physical devices (ICD not installed? VK_ICD_FILENAMES?)\n");
        return 4;
    }
    devices = (VkPhysicalDevice *)malloc(sizeof(VkPhysicalDevice) * ndev);
    if (devices == NULL) {
        return 4;
    }
    g_pfn_vkEnumeratePhysicalDevices(instance, &ndev, devices);

    for (i = 0; i < ndev; i++) {
        VkPhysicalDeviceProperties props;
        vk13_feature_state_t st;
        gl46_r600g_verdict_t r600g_v;
        gl46_zink_verdict_t zink_v;

        memset(&props, 0, sizeof(props));
        g_pfn_vkGetPhysicalDeviceProperties(devices[i], &props);

        printf("\n----- physical device #%u -----\n", i);
        printf("  deviceName : %s\n", props.deviceName);
        printf("  apiVersion : ");
        print_api_version(props.apiVersion, "");
        printf("  (driver 0x%08X)\n", props.driverVersion);
        printf("  vendor/dev : 0x%04X / 0x%04X\n", props.vendorID, props.deviceID);
        if (props.vendorID == 0x1002) {
            gl46_chip_family_t fam = gl46_chip_family_from_device_id(props.deviceID);
            if (fam != GL46_CHIP_UNKNOWN) {
                printf("  chip       : %s (r600 family)\n",
                       gl46_chip_family_name(fam));
            }
        }

        printf("\n  [A] Vulkan level gate (vk.xml mandatory core features)\n");
        fill_vk13_state(devices[i], props.apiVersion, &st);
        audit_vk13_levels(&st, &has_gap);

        printf("\n  [B] promoted extension inventory\n");
        audit_promoted_extensions(devices[i]);

        printf("\n  [C] OpenGL 4.6 via R600g (primary route)\n");
        r600g_v = audit_r600g(props.deviceID);
        if (r600g_v != GL46_R600G_GL46_READY) {
            has_gap = 1;
        }

        printf("\n  [D] OpenGL 4.6 via Zink (fallback route, needs Vulkan 1.3)\n");
        zink_v = audit_zink(devices[i], &st);
        if (zink_v != GL46_ZINK_GL46_READY) {
            has_gap = 1;
        }
        printf("\n");
    }

    free(devices);
    printf("audit complete.\n");
    if (strict && has_gap) {
        printf("strict mode: gaps detected, exit 1\n");
        return 1;
    }
    return 0;
}
