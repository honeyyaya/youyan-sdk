/*
  Copyright (c), 2024-2024, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "securec.h"
#include "ot_math.h"
#include "ss_mpi_aidetect.h"
#include "ss_mpi_aivsr.h"
#include "sample_common_aivie.h"
#include "sample_common_aidetect.h"
#include "sample_common_aivsr.h"
#include "sample_common_ai.h"

#define OT_SAMPLE_AIVSR_PATH_LEN        256
#define OT_AIDETECT_CHN_0               0
#define OT_AIVSR_CHN_0                  0

typedef struct {
    ot_vpss_grp vpss_grp;
    ot_vpss_chn vpss_chn;
    ot_venc_chn venc_chn;
    td_bool thread_start;
    td_char det_model_path[OT_SAMPLE_AIVSR_PATH_LEN];
    td_char aivsr_model_path[OT_SAMPLE_AIVSR_PATH_LEN];
} aivsr_process_param;

static volatile sig_atomic_t g_sig_flag = 0;

pthread_t g_aivsr_pid;

static aivsr_process_param g_aivsr_proc_param = {
    .vpss_grp = 0,
    .vpss_chn = OT_SAMPLE_AI_CHN_RESULT_STREAM,
    .venc_chn = OT_SAMPLE_AI_CHN_RESULT_STREAM,
    .thread_start = TD_FALSE,
    .det_model_path = { 0 },
    .aivsr_model_path = { 0 },
};

static td_void sample_aivsr_get_char(td_void)
{
    if (g_sig_flag == 1) {
        return;
    }
    sample_pause();
}

static void sample_aivsr_get_vlpr_result(td_s32 retcode, const ot_aivsr_result_array *result)
{
    if (retcode != TD_SUCCESS) {
        printf("vlpr process failed with %x\n", retcode);
        return;
    }
    (td_void)printf("ss_mpi_aivsr_process the task_type is %d, the plate recognized num is %u\n",
        result->task_type, result->vlpr_result.object_num);
    for (td_u32 j = 0; j < result->vlpr_result.object_num; ++j) {
        sample_common_aivsr_print_vehicle_info(result->vlpr_result.result_info[j].vehicle_info);
        for (td_u32 k = 0; k < result->vlpr_result.result_info[j].license_plate_num; ++k) {
            sample_common_aivsr_print_plate_info(result->vlpr_result.result_info[j].license_plate_info[k]);
        }
    }
}

static td_s32 sample_aivsr_create_vlpr(ot_aivsr_chn chn, td_char *model_path)
{
    ot_aivsr_input_model input_model = {
        .model_load_mode= OT_AIVSR_MODEL_LOAD_FROM_PATH,
        .size = strlen(model_path),
        .model = model_path
    };
    ot_aivsr_chn_attr chn_attr = {
        .task_type = OT_AIVSR_TASK_VLPR,
        .depth =  1,
        .vlpr_attr = { TD_TRUE, TD_TRUE, TD_TRUE, TD_TRUE }
    };
    td_s32 ret = ss_mpi_aivsr_create_chn(chn, &input_model, &chn_attr);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, ret, "ss_mpi_aidetect_create_chn failed!");
    ret = ss_mpi_aivsr_register_result_callback(chn, sample_aivsr_get_vlpr_result);
    if (ret != TD_SUCCESS) {
        printf("ss_mpi_aivsr_register_result_callback failed\n");
        (td_void)ss_mpi_aivsr_destroy_chn(chn);
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

static td_void sample_aivsr_destroy_vlpr(ot_aivsr_chn chn)
{
    ss_mpi_aivsr_unregister_result_callback(chn);
    (td_void)ss_mpi_aivsr_destroy_chn(chn);
}

static td_s32 sample_aivsr_async_process_vlpr(ot_video_frame *frame, ot_aidetect_result_array *result)
{
    (td_void)printf("==================== VLPR ====================\n");
    for (td_u32 i = 0; i < result->class_num; ++i) {
        if (result->object_class[i].class_type != OT_AIDETECT_CLASS_VEHICLE) {
            continue;
        }

        if (result->object_class[i].object_num == 0) {
            (td_void)printf("WARNING:the detect result is 0!\n");
            continue;
        }
        td_s32 ret = ss_mpi_aivsr_async_process(OT_AIVSR_CHN_0, frame, &result->object_class[i], -1);
        sample_common_ai_log_check_return(ret != TD_SUCCESS, ret, "ss_mpi_aivsr_async_process failed!");
    }

    return TD_SUCCESS;
}

static td_void *sample_aivsr_vlpr_proc(td_void *p)
{
    aivsr_process_param *para = (aivsr_process_param *)p;
    const td_s32 wait_time_ms = 100;
    ot_aidetect_result_array detect_result;
    td_s32 ret = sample_common_aidetect_init_result(OT_AIDETECT_CHN_0, &detect_result);
    if (ret != TD_SUCCESS) {
        printf("sample_common_aidetect_init_result failed.\n");
        return TD_NULL;
    }

    while (para->thread_start == TD_TRUE) {
        ot_video_frame_info frame_info;
        ret = ss_mpi_vpss_get_chn_frame(para->vpss_grp, para->vpss_chn, &frame_info, wait_time_ms);
        if (ret != TD_SUCCESS) {
            printf("vpss get frame failed.\n");
            continue;
        }

        ret = ss_mpi_aidetect_process(OT_AIDETECT_CHN_0, &frame_info.video_frame, &detect_result);
        sample_common_ai_log_check_goto(ret != TD_SUCCESS, release_frame, "ss_mpi_aidetect_process failed.");

        ret = sample_aivsr_async_process_vlpr(&frame_info.video_frame, &detect_result);
        sample_common_ai_log_check_goto(ret != TD_SUCCESS, release_frame, "sample_aivsr_async_process_vlpr failed.");

        ret = ss_mpi_venc_send_frame(para->venc_chn, &frame_info, wait_time_ms);
        if (ret != TD_SUCCESS) {
            printf("venc send frame failed.\n");
        }

release_frame:
        ss_mpi_vpss_release_chn_frame(para->vpss_grp, para->vpss_chn, &frame_info);
    }

    sample_common_aidetect_deinit_result(&detect_result);
    return TD_NULL;
}

static td_s32 sample_aivsr_start_vlpr()
{
    td_s32 ret = sample_common_aidetect_create(OT_AIDETECT_CHN_0, g_aivsr_proc_param.det_model_path);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, ret, "sample_common_aidetect_create failed!");
    ret = sample_aivsr_create_vlpr(OT_AIVSR_CHN_0, g_aivsr_proc_param.aivsr_model_path);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, destroy_aidetect, "sample_aivsr_create_vlpr failed.");

    g_aivsr_proc_param.thread_start = TD_TRUE;
    ret = pthread_create(&g_aivsr_pid, 0, sample_aivsr_vlpr_proc, (td_void *)&g_aivsr_proc_param);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, destroy_aivsr, "pthread create failed.");
    return TD_SUCCESS;

destroy_aivsr:
    sample_aivsr_destroy_vlpr(OT_AIVSR_CHN_0);
destroy_aidetect:
    sample_common_aidetect_destroy(OT_AIDETECT_CHN_0);
    return TD_FAILURE;
}

static td_s32 sample_aivsr_stop_vlpr()
{
    if (g_aivsr_proc_param.thread_start == TD_TRUE) {
        g_aivsr_proc_param.thread_start = TD_FALSE;
        pthread_join(g_aivsr_pid, TD_NULL);
    }
    sample_aivsr_destroy_vlpr(OT_AIVSR_CHN_0);
    sample_common_aidetect_destroy(OT_AIDETECT_CHN_0);
    return TD_SUCCESS;
}

static td_s32 sample_aivsr_start_route(sample_vi_cfg *vi_cfg, sample_vpss_cfg *vpss_cfg)
{
    td_s32 ret = sample_common_aivie_init(vi_cfg, vpss_cfg);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "sample_common_aivie_init failed!");

    ret = sample_aivsr_start_vlpr();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, start_aidetect_failed, "sample_aivsr_start_vlpr failed.");
    return TD_SUCCESS;

start_aidetect_failed:
    sample_common_aivie_deinit(vi_cfg);
    return TD_FAILURE;
}

static td_void sample_aivsr_stop_route(sample_vi_cfg *vi_cfg)
{
    sample_aivsr_stop_vlpr();
    sample_common_aivie_deinit(vi_cfg);
}

static td_s32 sample_aivsr_run(td_char *det_model_path, td_char *aivsr_model_path)
{
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg;
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);

    sample_vpss_cfg vpss_cfg;
    sample_common_aivie_get_vpss_cfg(OT_SAMPLE_AI_CHN_RESULT_STREAM, &vpss_cfg);

    (td_void)strncpy_s(g_aivsr_proc_param.det_model_path, OT_SAMPLE_AIVSR_PATH_LEN, det_model_path,
        strlen(det_model_path));
    (td_void)strncpy_s(g_aivsr_proc_param.aivsr_model_path, OT_SAMPLE_AIVSR_PATH_LEN, aivsr_model_path,
        strlen(aivsr_model_path));

    if (sample_aivsr_start_route(&vi_cfg, &vpss_cfg) != TD_SUCCESS) {
        return TD_FAILURE;
    }

    sample_aivsr_get_char();

    sample_aivsr_stop_route(&vi_cfg);
    return TD_SUCCESS;
}

static td_void sample_aivsr_usage(const char *prg_name)
{
    printf("usage : %s -h \n", prg_name);
    printf("        %s aidetect_model_path vlpr_model_path\n", prg_name);
}

static td_void sample_aivsr_handle_sig(td_s32 signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        g_sig_flag = 1;
    }
}

static td_void sample_aivsr_register_sig_handler(td_void (*sig_handle)(td_s32))
{
    struct sigaction sa;

    (td_void)memset_s(&sa, sizeof(struct sigaction), 0, sizeof(struct sigaction));
    sa.sa_handler = sig_handle;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, TD_NULL);
    sigaction(SIGTERM, &sa, TD_NULL);
}

#ifdef __LITEOS__
td_s32 app_main(td_s32 argc, td_char *argv[])
#else

td_s32 main(td_s32 argc, td_char *argv[])
#endif
{
    if (argc == 2 && strncmp(argv[1], "-h", 2) == 0) { /* 2:input arg num */
        sample_aivsr_usage(argv[0]);
        return TD_SUCCESS;
    }
    if (argc != 3) { /* 3:arg num */
        sample_aivsr_usage(argv[0]);
        return TD_FAILURE;
    }

#ifndef __LITEOS__
    sample_aivsr_register_sig_handler(sample_aivsr_handle_sig);
#endif

    td_s32 ret = sample_aivsr_run(argv[1], argv[2]);
    if ((ret == TD_SUCCESS) && (g_sig_flag == 0)) {
        printf("\033[0;32mprogram exit normally!\033[0;39m\n");
    } else {
        printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
    }

    return ret;
}
