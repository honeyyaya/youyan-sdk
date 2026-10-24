/*
  Copyright (c), 2024-2024, Shenshu Tech. Co., Ltd.
 */

#include "sample_common_aidetect.h"
#include <securec.h>
#include <string.h>
#include "sample_common_ai.h"

td_s32 sample_common_aidetect_create(ot_aidetect_chn chn, td_char *model_path)
{
    ot_aidetect_input_model input_model = {
        .model_load_mode= OT_AIDETECT_MODEL_LOAD_FROM_PATH,
        .size = strlen(model_path),
        .model = model_path
    };
    ot_aidetect_chn_attr chn_attr = { 0 };
    td_s32 ret = ss_mpi_aidetect_create_chn(chn, &input_model, &chn_attr);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, ret, "ss_mpi_aidetect_create_chn failed!");
    return TD_SUCCESS;
}

td_s32 sample_common_aidetect_destroy(ot_aidetect_chn chn)
{
    return ss_mpi_aidetect_destroy_chn(chn);
}

td_s32 sample_common_aidetect_init_result(ot_aidetect_chn chn, ot_aidetect_result_array *result)
{
    td_u32 i;
    ot_aidetect_model_info model_info = { 0 };
    td_s32 ret = ss_mpi_aidetect_get_model_info(chn, &model_info);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, ret, "ss_mpi_aidetect_get_model_info[chn:%d] failed!", chn);

    (td_void)memset_s(result, sizeof(ot_aidetect_result_array), 0, sizeof(ot_aidetect_result_array));
    result->class_num = model_info.class_num;
    for (i = 0; i < result->class_num; ++i) {
        result->object_class[i].class_type = model_info.classes[i];
        result->object_class[i].object_capacity = OT_SAMPLE_AIDETECT_MAX_OUTPUT_RECT_NUM;
        result->object_class[i].objects = (ot_aidetect_object *)malloc(sizeof(ot_aidetect_object) *
            result->object_class[i].object_capacity);
        sample_common_ai_log_check_goto(result->object_class[i].objects == TD_NULL, free_obj, "malloc result failed");
        (td_void)memset_s(result->object_class[i].objects,
            sizeof(ot_aidetect_object) * result->object_class[i].object_capacity, 0,
            sizeof(ot_aidetect_object) * result->object_class[i].object_capacity);
    }
    return TD_SUCCESS;

free_obj:
    for (td_u32 j = 0; j < i; ++j) {
        free(result->object_class[j].objects);
        result->object_class[j].objects = TD_NULL;
    }
    return TD_FAILURE;
}

td_s32 sample_common_aidetect_deinit_result(ot_aidetect_result_array *result)
{
    for (td_u32 i = 0; i < result->class_num; ++i) {
        if (result->object_class[i].objects != TD_NULL) {
            free(result->object_class[i].objects);
            result->object_class[i].objects = TD_NULL;
        }
    }
    return TD_SUCCESS;
}

td_s32 sample_common_aidetect_process(ot_aidetect_chn chn, const ot_video_frame *frame,
    ot_aidetect_result_array *result)
{
    return ss_mpi_aidetect_process(chn, frame, result);
}