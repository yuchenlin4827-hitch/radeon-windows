/* SPDX-License-Identifier: MIT
 * gl46_r600g.c — R600g GL 4.6 门控实现（PCI 表 + cap/服务检查）。
 */
#include "gl46_r600g.h"

#include <string.h>

#define R600_SIMPLE_VERSION(major, minor, patch)                    \
    (((uint32_t)(major) << 16u) | ((uint32_t)(minor) << 8u) |       \
     ((uint32_t)(patch)))

struct gl46_pci_id_entry {
    uint32_t           device_id;
    gl46_chip_family_t family;
};

/* PCI 设备 ID -> 芯片代际表（304 条）。
 * 生成自 Mesa include/pci_ids/r600_pci_ids.h（MIT），勿手工增删。 */
static const struct gl46_pci_id_entry k_gl46_pci_ids[] = {
    { 0x6700, GL46_CHIP_CAYMAN },
    { 0x6701, GL46_CHIP_CAYMAN },
    { 0x6702, GL46_CHIP_CAYMAN },
    { 0x6703, GL46_CHIP_CAYMAN },
    { 0x6704, GL46_CHIP_CAYMAN },
    { 0x6705, GL46_CHIP_CAYMAN },
    { 0x6706, GL46_CHIP_CAYMAN },
    { 0x6707, GL46_CHIP_CAYMAN },
    { 0x6708, GL46_CHIP_CAYMAN },
    { 0x6709, GL46_CHIP_CAYMAN },
    { 0x6718, GL46_CHIP_CAYMAN },
    { 0x6719, GL46_CHIP_CAYMAN },
    { 0x671C, GL46_CHIP_CAYMAN },
    { 0x671D, GL46_CHIP_CAYMAN },
    { 0x671F, GL46_CHIP_CAYMAN },
    { 0x6720, GL46_CHIP_BARTS },
    { 0x6721, GL46_CHIP_BARTS },
    { 0x6722, GL46_CHIP_BARTS },
    { 0x6723, GL46_CHIP_BARTS },
    { 0x6724, GL46_CHIP_BARTS },
    { 0x6725, GL46_CHIP_BARTS },
    { 0x6726, GL46_CHIP_BARTS },
    { 0x6727, GL46_CHIP_BARTS },
    { 0x6728, GL46_CHIP_BARTS },
    { 0x6729, GL46_CHIP_BARTS },
    { 0x6738, GL46_CHIP_BARTS },
    { 0x6739, GL46_CHIP_BARTS },
    { 0x673E, GL46_CHIP_BARTS },
    { 0x6740, GL46_CHIP_TURKS },
    { 0x6741, GL46_CHIP_TURKS },
    { 0x6742, GL46_CHIP_TURKS },
    { 0x6743, GL46_CHIP_TURKS },
    { 0x6744, GL46_CHIP_TURKS },
    { 0x6745, GL46_CHIP_TURKS },
    { 0x6746, GL46_CHIP_TURKS },
    { 0x6747, GL46_CHIP_TURKS },
    { 0x6748, GL46_CHIP_TURKS },
    { 0x6749, GL46_CHIP_TURKS },
    { 0x674A, GL46_CHIP_TURKS },
    { 0x6750, GL46_CHIP_TURKS },
    { 0x6751, GL46_CHIP_TURKS },
    { 0x6758, GL46_CHIP_TURKS },
    { 0x6759, GL46_CHIP_TURKS },
    { 0x675B, GL46_CHIP_TURKS },
    { 0x675D, GL46_CHIP_TURKS },
    { 0x675F, GL46_CHIP_TURKS },
    { 0x6760, GL46_CHIP_CAICOS },
    { 0x6761, GL46_CHIP_CAICOS },
    { 0x6762, GL46_CHIP_CAICOS },
    { 0x6763, GL46_CHIP_CAICOS },
    { 0x6764, GL46_CHIP_CAICOS },
    { 0x6765, GL46_CHIP_CAICOS },
    { 0x6766, GL46_CHIP_CAICOS },
    { 0x6767, GL46_CHIP_CAICOS },
    { 0x6768, GL46_CHIP_CAICOS },
    { 0x6770, GL46_CHIP_CAICOS },
    { 0x6771, GL46_CHIP_CAICOS },
    { 0x6772, GL46_CHIP_CAICOS },
    { 0x6778, GL46_CHIP_CAICOS },
    { 0x6779, GL46_CHIP_CAICOS },
    { 0x677B, GL46_CHIP_CAICOS },
    { 0x6840, GL46_CHIP_TURKS },
    { 0x6841, GL46_CHIP_TURKS },
    { 0x6842, GL46_CHIP_TURKS },
    { 0x6843, GL46_CHIP_TURKS },
    { 0x6849, GL46_CHIP_TURKS },
    { 0x6850, GL46_CHIP_TURKS },
    { 0x6858, GL46_CHIP_TURKS },
    { 0x6859, GL46_CHIP_TURKS },
    { 0x6880, GL46_CHIP_CYPRESS },
    { 0x6888, GL46_CHIP_CYPRESS },
    { 0x6889, GL46_CHIP_CYPRESS },
    { 0x688A, GL46_CHIP_CYPRESS },
    { 0x688C, GL46_CHIP_CYPRESS },
    { 0x688D, GL46_CHIP_CYPRESS },
    { 0x6898, GL46_CHIP_CYPRESS },
    { 0x6899, GL46_CHIP_CYPRESS },
    { 0x689B, GL46_CHIP_CYPRESS },
    { 0x689C, GL46_CHIP_HEMLOCK },
    { 0x689D, GL46_CHIP_HEMLOCK },
    { 0x689E, GL46_CHIP_CYPRESS },
    { 0x68A0, GL46_CHIP_JUNIPER },
    { 0x68A1, GL46_CHIP_JUNIPER },
    { 0x68A8, GL46_CHIP_JUNIPER },
    { 0x68A9, GL46_CHIP_JUNIPER },
    { 0x68B0, GL46_CHIP_JUNIPER },
    { 0x68B8, GL46_CHIP_JUNIPER },
    { 0x68B9, GL46_CHIP_JUNIPER },
    { 0x68BA, GL46_CHIP_JUNIPER },
    { 0x68BE, GL46_CHIP_JUNIPER },
    { 0x68BF, GL46_CHIP_JUNIPER },
    { 0x68C0, GL46_CHIP_REDWOOD },
    { 0x68C1, GL46_CHIP_REDWOOD },
    { 0x68C7, GL46_CHIP_REDWOOD },
    { 0x68C8, GL46_CHIP_REDWOOD },
    { 0x68C9, GL46_CHIP_REDWOOD },
    { 0x68D8, GL46_CHIP_REDWOOD },
    { 0x68D9, GL46_CHIP_REDWOOD },
    { 0x68DA, GL46_CHIP_REDWOOD },
    { 0x68DE, GL46_CHIP_REDWOOD },
    { 0x68E0, GL46_CHIP_CEDAR },
    { 0x68E1, GL46_CHIP_CEDAR },
    { 0x68E4, GL46_CHIP_CEDAR },
    { 0x68E5, GL46_CHIP_CEDAR },
    { 0x68E8, GL46_CHIP_CEDAR },
    { 0x68E9, GL46_CHIP_CEDAR },
    { 0x68F1, GL46_CHIP_CEDAR },
    { 0x68F2, GL46_CHIP_CEDAR },
    { 0x68F8, GL46_CHIP_CEDAR },
    { 0x68F9, GL46_CHIP_CEDAR },
    { 0x68FA, GL46_CHIP_CEDAR },
    { 0x68FE, GL46_CHIP_CEDAR },
    { 0x9400, GL46_CHIP_R600 },
    { 0x9401, GL46_CHIP_R600 },
    { 0x9402, GL46_CHIP_R600 },
    { 0x9403, GL46_CHIP_R600 },
    { 0x9405, GL46_CHIP_R600 },
    { 0x940A, GL46_CHIP_R600 },
    { 0x940B, GL46_CHIP_R600 },
    { 0x940F, GL46_CHIP_R600 },
    { 0x9440, GL46_CHIP_RV770 },
    { 0x9441, GL46_CHIP_RV770 },
    { 0x9442, GL46_CHIP_RV770 },
    { 0x9443, GL46_CHIP_RV770 },
    { 0x9444, GL46_CHIP_RV770 },
    { 0x9446, GL46_CHIP_RV770 },
    { 0x944A, GL46_CHIP_RV770 },
    { 0x944B, GL46_CHIP_RV770 },
    { 0x944C, GL46_CHIP_RV770 },
    { 0x944E, GL46_CHIP_RV770 },
    { 0x9450, GL46_CHIP_RV770 },
    { 0x9452, GL46_CHIP_RV770 },
    { 0x9456, GL46_CHIP_RV770 },
    { 0x945A, GL46_CHIP_RV770 },
    { 0x945B, GL46_CHIP_RV770 },
    { 0x945E, GL46_CHIP_RV770 },
    { 0x9460, GL46_CHIP_RV770 },
    { 0x9462, GL46_CHIP_RV770 },
    { 0x946A, GL46_CHIP_RV770 },
    { 0x946B, GL46_CHIP_RV770 },
    { 0x947A, GL46_CHIP_RV770 },
    { 0x947B, GL46_CHIP_RV770 },
    { 0x9480, GL46_CHIP_RV730 },
    { 0x9487, GL46_CHIP_RV730 },
    { 0x9488, GL46_CHIP_RV730 },
    { 0x9489, GL46_CHIP_RV730 },
    { 0x948A, GL46_CHIP_RV730 },
    { 0x948F, GL46_CHIP_RV730 },
    { 0x9490, GL46_CHIP_RV730 },
    { 0x9491, GL46_CHIP_RV730 },
    { 0x9495, GL46_CHIP_RV730 },
    { 0x9498, GL46_CHIP_RV730 },
    { 0x949C, GL46_CHIP_RV730 },
    { 0x949E, GL46_CHIP_RV730 },
    { 0x949F, GL46_CHIP_RV730 },
    { 0x94A0, GL46_CHIP_RV740 },
    { 0x94A1, GL46_CHIP_RV740 },
    { 0x94A3, GL46_CHIP_RV740 },
    { 0x94B1, GL46_CHIP_RV740 },
    { 0x94B3, GL46_CHIP_RV740 },
    { 0x94B4, GL46_CHIP_RV740 },
    { 0x94B5, GL46_CHIP_RV740 },
    { 0x94B9, GL46_CHIP_RV740 },
    { 0x94C0, GL46_CHIP_RV610 },
    { 0x94C1, GL46_CHIP_RV610 },
    { 0x94C3, GL46_CHIP_RV610 },
    { 0x94C4, GL46_CHIP_RV610 },
    { 0x94C5, GL46_CHIP_RV610 },
    { 0x94C6, GL46_CHIP_RV610 },
    { 0x94C7, GL46_CHIP_RV610 },
    { 0x94C8, GL46_CHIP_RV610 },
    { 0x94C9, GL46_CHIP_RV610 },
    { 0x94CB, GL46_CHIP_RV610 },
    { 0x94CC, GL46_CHIP_RV610 },
    { 0x94CD, GL46_CHIP_RV610 },
    { 0x9500, GL46_CHIP_RV670 },
    { 0x9501, GL46_CHIP_RV670 },
    { 0x9504, GL46_CHIP_RV670 },
    { 0x9505, GL46_CHIP_RV670 },
    { 0x9506, GL46_CHIP_RV670 },
    { 0x9507, GL46_CHIP_RV670 },
    { 0x9508, GL46_CHIP_RV670 },
    { 0x9509, GL46_CHIP_RV670 },
    { 0x950F, GL46_CHIP_RV670 },
    { 0x9511, GL46_CHIP_RV670 },
    { 0x9515, GL46_CHIP_RV670 },
    { 0x9517, GL46_CHIP_RV670 },
    { 0x9519, GL46_CHIP_RV670 },
    { 0x9540, GL46_CHIP_RV710 },
    { 0x9541, GL46_CHIP_RV710 },
    { 0x9542, GL46_CHIP_RV710 },
    { 0x954E, GL46_CHIP_RV710 },
    { 0x954F, GL46_CHIP_RV710 },
    { 0x9552, GL46_CHIP_RV710 },
    { 0x9553, GL46_CHIP_RV710 },
    { 0x9555, GL46_CHIP_RV710 },
    { 0x9557, GL46_CHIP_RV710 },
    { 0x955F, GL46_CHIP_RV710 },
    { 0x9580, GL46_CHIP_RV630 },
    { 0x9581, GL46_CHIP_RV630 },
    { 0x9583, GL46_CHIP_RV630 },
    { 0x9586, GL46_CHIP_RV630 },
    { 0x9587, GL46_CHIP_RV630 },
    { 0x9588, GL46_CHIP_RV630 },
    { 0x9589, GL46_CHIP_RV630 },
    { 0x958A, GL46_CHIP_RV630 },
    { 0x958B, GL46_CHIP_RV630 },
    { 0x958C, GL46_CHIP_RV630 },
    { 0x958D, GL46_CHIP_RV630 },
    { 0x958E, GL46_CHIP_RV630 },
    { 0x958F, GL46_CHIP_RV630 },
    { 0x9590, GL46_CHIP_RV635 },
    { 0x9591, GL46_CHIP_RV635 },
    { 0x9593, GL46_CHIP_RV635 },
    { 0x9595, GL46_CHIP_RV635 },
    { 0x9596, GL46_CHIP_RV635 },
    { 0x9597, GL46_CHIP_RV635 },
    { 0x9598, GL46_CHIP_RV635 },
    { 0x9599, GL46_CHIP_RV635 },
    { 0x959B, GL46_CHIP_RV635 },
    { 0x95C0, GL46_CHIP_RV620 },
    { 0x95C2, GL46_CHIP_RV620 },
    { 0x95C4, GL46_CHIP_RV620 },
    { 0x95C5, GL46_CHIP_RV620 },
    { 0x95C6, GL46_CHIP_RV620 },
    { 0x95C7, GL46_CHIP_RV620 },
    { 0x95C9, GL46_CHIP_RV620 },
    { 0x95CC, GL46_CHIP_RV620 },
    { 0x95CD, GL46_CHIP_RV620 },
    { 0x95CE, GL46_CHIP_RV620 },
    { 0x95CF, GL46_CHIP_RV620 },
    { 0x9610, GL46_CHIP_RS780 },
    { 0x9611, GL46_CHIP_RS780 },
    { 0x9612, GL46_CHIP_RS780 },
    { 0x9613, GL46_CHIP_RS780 },
    { 0x9614, GL46_CHIP_RS780 },
    { 0x9615, GL46_CHIP_RS780 },
    { 0x9616, GL46_CHIP_RS780 },
    { 0x9640, GL46_CHIP_SUMO },
    { 0x9641, GL46_CHIP_SUMO },
    { 0x9642, GL46_CHIP_SUMO2 },
    { 0x9643, GL46_CHIP_SUMO2 },
    { 0x9644, GL46_CHIP_SUMO2 },
    { 0x9645, GL46_CHIP_SUMO2 },
    { 0x9647, GL46_CHIP_SUMO },
    { 0x9648, GL46_CHIP_SUMO },
    { 0x9649, GL46_CHIP_SUMO2 },
    { 0x964A, GL46_CHIP_SUMO },
    { 0x964B, GL46_CHIP_SUMO },
    { 0x964C, GL46_CHIP_SUMO },
    { 0x964E, GL46_CHIP_SUMO },
    { 0x964F, GL46_CHIP_SUMO },
    { 0x9710, GL46_CHIP_RS880 },
    { 0x9711, GL46_CHIP_RS880 },
    { 0x9712, GL46_CHIP_RS880 },
    { 0x9713, GL46_CHIP_RS880 },
    { 0x9714, GL46_CHIP_RS880 },
    { 0x9715, GL46_CHIP_RS880 },
    { 0x9802, GL46_CHIP_PALM },
    { 0x9803, GL46_CHIP_PALM },
    { 0x9804, GL46_CHIP_PALM },
    { 0x9805, GL46_CHIP_PALM },
    { 0x9806, GL46_CHIP_PALM },
    { 0x9807, GL46_CHIP_PALM },
    { 0x9808, GL46_CHIP_PALM },
    { 0x9809, GL46_CHIP_PALM },
    { 0x980A, GL46_CHIP_PALM },
    { 0x9900, GL46_CHIP_ARUBA },
    { 0x9901, GL46_CHIP_ARUBA },
    { 0x9903, GL46_CHIP_ARUBA },
    { 0x9904, GL46_CHIP_ARUBA },
    { 0x9905, GL46_CHIP_ARUBA },
    { 0x9906, GL46_CHIP_ARUBA },
    { 0x9907, GL46_CHIP_ARUBA },
    { 0x9908, GL46_CHIP_ARUBA },
    { 0x9909, GL46_CHIP_ARUBA },
    { 0x990A, GL46_CHIP_ARUBA },
    { 0x990B, GL46_CHIP_ARUBA },
    { 0x990C, GL46_CHIP_ARUBA },
    { 0x990D, GL46_CHIP_ARUBA },
    { 0x990E, GL46_CHIP_ARUBA },
    { 0x990F, GL46_CHIP_ARUBA },
    { 0x9910, GL46_CHIP_ARUBA },
    { 0x9913, GL46_CHIP_ARUBA },
    { 0x9917, GL46_CHIP_ARUBA },
    { 0x9918, GL46_CHIP_ARUBA },
    { 0x9919, GL46_CHIP_ARUBA },
    { 0x9990, GL46_CHIP_ARUBA },
    { 0x9991, GL46_CHIP_ARUBA },
    { 0x9992, GL46_CHIP_ARUBA },
    { 0x9993, GL46_CHIP_ARUBA },
    { 0x9994, GL46_CHIP_ARUBA },
    { 0x9995, GL46_CHIP_ARUBA },
    { 0x9996, GL46_CHIP_ARUBA },
    { 0x9997, GL46_CHIP_ARUBA },
    { 0x9998, GL46_CHIP_ARUBA },
    { 0x9999, GL46_CHIP_ARUBA },
    { 0x999A, GL46_CHIP_ARUBA },
    { 0x999B, GL46_CHIP_ARUBA },
    { 0x999C, GL46_CHIP_ARUBA },
    { 0x999D, GL46_CHIP_ARUBA },
    { 0x99A0, GL46_CHIP_ARUBA },
    { 0x99A2, GL46_CHIP_ARUBA },
    { 0x99A4, GL46_CHIP_ARUBA },
};

gl46_chip_family_t gl46_chip_family_from_device_id(uint32_t device_id)
{
    size_t lo = 0;
    size_t hi = sizeof(k_gl46_pci_ids) / sizeof(k_gl46_pci_ids[0]);

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (k_gl46_pci_ids[mid].device_id < device_id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < sizeof(k_gl46_pci_ids) / sizeof(k_gl46_pci_ids[0]) &&
        k_gl46_pci_ids[lo].device_id == device_id) {
        return k_gl46_pci_ids[lo].family;
    }
    return GL46_CHIP_UNKNOWN;
}

const char *gl46_chip_family_name(gl46_chip_family_t family)
{
    switch (family) {
    case GL46_CHIP_R600:    return "R600";
    case GL46_CHIP_RV610:   return "RV610";
    case GL46_CHIP_RV630:   return "RV630";
    case GL46_CHIP_RV670:   return "RV670";
    case GL46_CHIP_RV620:   return "RV620";
    case GL46_CHIP_RV635:   return "RV635";
    case GL46_CHIP_RS780:   return "RS780";
    case GL46_CHIP_RS880:   return "RS880";
    case GL46_CHIP_RV770:   return "RV770";
    case GL46_CHIP_RV730:   return "RV730";
    case GL46_CHIP_RV710:   return "RV710";
    case GL46_CHIP_RV740:   return "RV740";
    case GL46_CHIP_CEDAR:   return "Cedar (Evergreen)";
    case GL46_CHIP_REDWOOD: return "Redwood (Evergreen)";
    case GL46_CHIP_JUNIPER: return "Juniper (Evergreen)";
    case GL46_CHIP_CYPRESS: return "Cypress (Evergreen)";
    case GL46_CHIP_HEMLOCK: return "Hemlock (Evergreen)";
    case GL46_CHIP_PALM:    return "Palm (Fusion)";
    case GL46_CHIP_SUMO:    return "Sumo (Fusion)";
    case GL46_CHIP_SUMO2:   return "Sumo2 (Fusion)";
    case GL46_CHIP_BARTS:   return "Barts (Northern Islands)";
    case GL46_CHIP_TURKS:   return "Turks (Northern Islands)";
    case GL46_CHIP_CAICOS:  return "Caicos (Evergreen-class)";
    case GL46_CHIP_CAYMAN:  return "Cayman (TeraScale 3)";
    case GL46_CHIP_ARUBA:   return "Aruba (Trinity/Richland)";
    default:                return "unknown";
    }
}

int gl46_chip_family_is_evergreen_plus(gl46_chip_family_t family)
{
    return family >= GL46_CHIP_FIRST_EVERGREEN && family != GL46_CHIP_UNKNOWN;
}

uint32_t gl46_r600g_version_simple(uint32_t major, uint32_t minor,
                                   uint32_t patch)
{
    return R600_SIMPLE_VERSION(major, minor, patch);
}

size_t gl46_r600g_evaluate(const gl46_r600g_state_t *st,
                           gl46_r600g_verdict_t *verdict,
                           gl46_gap_t *gaps, size_t max_gaps)
{
    size_t missing = 0;
    gl46_r600g_verdict_t v = GL46_R600G_FAIL;

    if (st == NULL) {
        if (verdict != NULL) {
            *verdict = GL46_R600G_FAIL;
        }
        return 0;
    }

#define ADD_GAP(id_, why_)                                            \
    do {                                                              \
        if (gaps != NULL && missing < max_gaps) {                     \
            gaps[missing].id = (id_);                                 \
            gaps[missing].why = (why_);                               \
        }                                                             \
        missing++;                                                    \
    } while (0)

    if (st->family == GL46_CHIP_UNKNOWN) {
        ADD_GAP("chip-family",
                "未知芯片代际（PCI ID 不在 r600 家族表内）");
    } else if (!gl46_chip_family_is_evergreen_plus(st->family)) {
        /* TeraScale 1：r600_pipe.c:495 明确 GL 3.3 上限，这是"合理失败"。 */
        if (verdict != NULL) {
            *verdict = GL46_R600G_GL330;
        }
        return 0;
    } else {
        /* evergreen+：逐项检查 GL 4.6 依赖（出处见 gl46_r600g.h 注释）。 */
        if (!st->cap_gl_spirv) {
            ADD_GAP("cap_gl_spirv",
                    "ARB_gl_spirv / ARB_spirv_extensions（GL 4.6 必选）");
        }
        if (!st->cap_shader_group_vote) {
            ADD_GAP("cap_shader_group_vote",
                    "ARB_shader_group_vote（GL 4.6 必选，r600: true）");
        }
        if (!st->cap_polygon_offset_clamp) {
            ADD_GAP("cap_polygon_offset_clamp",
                    "ARB_polygon_offset_clamp（GL 4.6 必选）");
        }
        if (!st->cap_query_pipeline_statistics) {
            ADD_GAP("cap_query_pipeline_statistics",
                    "ARB_pipeline_statistics_query（GL 4.6 必选）");
        }
        if (!st->cap_query_so_overflow) {
            ADD_GAP("cap_query_so_overflow",
                    "ARB_transform_feedback_overflow_query（GL 4.6 必选，"
                    "evergreen+）");
        }
        if (!st->cap_texture_barrier) {
            ADD_GAP("cap_texture_barrier",
                    "GL_ARB_texture_barrier（GL 4.5 起必选）");
        }
        if (!st->cap_anisotropic_filter) {
            ADD_GAP("cap_anisotropic_filter",
                    "ARB_texture_filter_anisotropic（GL 4.6 必选）");
        }
        if (!st->cap_draw_parameters) {
            ADD_GAP("cap_draw_parameters",
                    "ARB_shader_draw_parameters（GL 4.6 必选，evergreen+）");
        }
        /* ARB_indirect_parameters：evergreen+ 且 winsys 版本 >= 2.51.0
         * （对应 r600_pipe.c:522 的 drm_version 检查）。 */
        if (st->winsys_drm_version <
            R600_SIMPLE_VERSION(2, 51, 0)) {
            ADD_GAP("winsys_drm_version",
                    "winsys 版本 < 2.51.0：ARB_indirect_parameters 不可用"
                    "（对应 Linux radeon DRM 2.51.0）");
        }
        if (!st->ws_hw_atomic_counters) {
            ADD_GAP("ws_hw_atomic_counters",
                    "原生原子计数器缺失：ARB_shader_atomic_counter_ops/"
                    "atomic_counters 不可用（r600_pipe.c:611 has_atomics）");
        }
        if (!st->ws_streamout) {
            ADD_GAP("ws_streamout",
                    "变换反馈（streamout）缺失：GL 4.x 基础依赖");
        }
    }
#undef ADD_GAP

    if (missing == 0) {
        v = GL46_R600G_GL46_READY;
    }

    if (verdict != NULL && st->family != GL46_CHIP_UNKNOWN) {
        *verdict = v;
    } else if (verdict != NULL) {
        *verdict = GL46_R600G_FAIL;
    }
    return missing;
}
