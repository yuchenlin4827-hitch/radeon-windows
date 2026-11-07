/* SPDX-License-Identifier: MIT
 * vk13_promoted.h — Vulkan 1.1 / 1.2 / 1.3 promoted extension 清单。
 *
 * 数据来源（机器可验证）：Khronos 官方注册表 vk.xml 中带
 * promotedto="VK_VERSION_x_y" 属性的 <extension> 条目（main 分支，2026-11 抓取）。
 * 计数：1.1 → 23 个，1.2 → 24 个，1.3 → 23 个。
 *
 * 语义说明（供审计工具使用）：
 *   - 扩展一旦 promoted，其功能即成为对应核心版本的强制内容；
 *   - 规范允许实现不再枚举 promoted 扩展本身，因此"驱动 apiVersion 达标但
 *     扩展未列出"不算缺口，本表仅用于信息性对比（audit 报告）。
 */
#ifndef ROK_VK13_PROMOTED_H
#define ROK_VK13_PROMOTED_H

#include <stddef.h>
#include <stdint.h>

#include "vk13_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vk13_promoted_entry {
    uint32_t    level;    /* 提升到哪个核心版本（VK13_LEVEL_*） */
    const char *name;     /* 扩展名 */
} vk13_promoted_entry_t;

/* 返回指定层级（VK13_LEVEL_1_1/1_2/1_3）的 promoted 扩展名数组。
 * count 输出条数；level 非法时返回 NULL。 */
const char *const *vk13_promoted_extensions(uint32_t level, size_t *count);

/* 该层级的 promoted 扩展数量（非法层级返回 0）。 */
size_t vk13_promoted_extension_count(uint32_t level);

/* 便利接口：遍历全部三层。idx 越界时 entry 返回 NULL。 */
typedef void (*vk13_promoted_iter_fn)(uint32_t level, const char *name,
                                      void *user);
void vk13_promoted_foreach(vk13_promoted_iter_fn fn, void *user);

#ifdef __cplusplus
}
#endif

#endif /* ROK_VK13_PROMOTED_H */
