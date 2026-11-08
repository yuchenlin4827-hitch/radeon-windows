/* SPDX-License-Identifier: MIT
 * gl46_common.h — umd/gl46 模块共享类型（缺口描述）。
 */
#ifndef ROK_GL46_COMMON_H
#define ROK_GL46_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/* 单条缺口：id 为短标识（cap 名 / 服务名 / 硬件条件），why 为中文说明。 */
typedef struct gl46_gap {
    const char *id;
    const char *why;
} gl46_gap_t;

#ifdef __cplusplus
}
#endif

#endif /* ROK_GL46_COMMON_H */
