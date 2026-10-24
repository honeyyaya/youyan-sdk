/*
  Copyright (c), 2024-2024, Shenshu Tech. Co., Ltd.
 */

#ifndef SAMPLE_COMMON_AI_VIE
#define SAMPLE_COMMON_AI_VIE

#include "ot_type.h"
#include "sample_comm.h"

#define OT_SAMPLE_AI_CHN_ORIGINAL_STREAM    0
#define OT_SAMPLE_AI_CHN_RESULT_STREAM      1

td_s32 sample_common_aivie_init(sample_vi_cfg *vi_cfg, sample_vpss_cfg *vpss_cfg);
td_s32 sample_common_aivie_deinit(sample_vi_cfg *vi_cfg);
td_void sample_common_aivie_get_vpss_cfg(td_s32 vpss_chn, sample_vpss_cfg *vpss_cfg);

#endif