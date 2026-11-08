/* SPDX-License-Identifier: MIT
 * vk13_api.h — Vulkan API 版本号打包/比较辅助（纯 C11，无外部依赖）。
 *
 * 打包布局与 Vulkan 官方 VK_API_VERSION_* 宏一致（vk.xml / vulkan_core.h）：
 *   | variant (7b) | major (10b) | minor (10b) | patch (12b) |
 * 本项目目标层级：1.0 → 1.1 → 1.2 → 1.3。
 */
#ifndef ROK_VK13_API_H
#define ROK_VK13_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VK13_MAKE_VERSION(major, minor, patch)                        \
    (((uint32_t)(major) << 22u) | ((uint32_t)(minor) << 12u) |        \
     ((uint32_t)(patch) & 0xFFFu))

#define VK13_API_VERSION_1_0 VK13_MAKE_VERSION(1, 0, 0)
#define VK13_API_VERSION_1_1 VK13_MAKE_VERSION(1, 1, 0)
#define VK13_API_VERSION_1_2 VK13_MAKE_VERSION(1, 2, 0)
#define VK13_API_VERSION_1_3 VK13_MAKE_VERSION(1, 3, 0)
#define VK13_API_VERSION_1_4 VK13_MAKE_VERSION(1, 4, 0)

static inline uint32_t vk13_version_major(uint32_t v)
{
    return v >> 22u;
}

static inline uint32_t vk13_version_minor(uint32_t v)
{
    return (v >> 12u) & 0x3FFu;
}

static inline uint32_t vk13_version_patch(uint32_t v)
{
    return v & 0xFFFu;
}

/* 常用聚合位段（与 vk.xml 的 promotedto="VK_VERSION_x_y" 对应）。 */
#define VK13_LEVEL_1_0 VK13_API_VERSION_1_0
#define VK13_LEVEL_1_1 VK13_API_VERSION_1_1
#define VK13_LEVEL_1_2 VK13_API_VERSION_1_2
#define VK13_LEVEL_1_3 VK13_API_VERSION_1_3

#ifdef __cplusplus
}
#endif

#endif /* ROK_VK13_API_H */
