/*
  Copyright (c), 2024-2024, Shenshu Tech. Co., Ltd.
 */

#ifndef SAMPLE_COMMON_AIDETECT
#define SAMPLE_COMMON_AIDETECT

#include "ss_mpi_aidetect.h"

#define OT_SAMPLE_AIDETECT_MAX_OUTPUT_RECT_NUM 10

td_s32 sample_common_aidetect_create(ot_aidetect_chn chn, td_char *model_path);
td_s32 sample_common_aidetect_destroy(ot_aidetect_chn chn);
td_s32 sample_common_aidetect_init_result(ot_aidetect_chn chn, ot_aidetect_result_array *result);
td_s32 sample_common_aidetect_deinit_result(ot_aidetect_result_array *result);
td_s32 sample_common_aidetect_process(ot_aidetect_chn chn, const ot_video_frame *frame,
    ot_aidetect_result_array *result);
#endif