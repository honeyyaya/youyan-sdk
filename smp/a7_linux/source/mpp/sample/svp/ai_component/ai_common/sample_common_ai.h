/*
  Copyright (c), 2024-2024, Shenshu Tech. Co., Ltd.
 */

#ifndef SAMPLE_COMMON_AI
#define SAMPLE_COMMON_AI

#include "ot_common_video.h"

#define sample_common_ai_log_check(val, fmt, ...)                                           \
    do {                                                                                    \
        if ((val)) {                                                                        \
            printf("[%s: %d]: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__);               \
        }                                                                                   \
    } while (0)

#define sample_common_ai_log_check_return(val, ret, fmt, ...)                               \
    do {                                                                                    \
        if ((val)) {                                                                        \
            printf("[%s: %d]: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__);               \
            return (ret);                                                                   \
        }                                                                                   \
    } while (0)

#define sample_common_ai_log_check_goto(val, label, fmt, ...)                               \
    do {                                                                                    \
        if ((val)) {                                                                        \
            printf("[%s: %d]: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__);               \
            goto label;                                                                     \
        }                                                                                   \
    } while (0)

td_s32 sample_common_ai_get_file_size(const td_char *path, td_u32 *len);
td_s32 sample_common_ai_read_file(const td_char *path, td_u32 len, td_void *addr);

td_s32 sample_common_ai_create_nv21_frame(const ot_size *frame_resolution, ot_video_frame *frame);
td_s32 sample_common_ai_read_nv21_frame(const td_char *image_path, ot_video_frame *frame);
td_s32 sample_common_ai_destroy_frame(ot_video_frame *frame);

#endif