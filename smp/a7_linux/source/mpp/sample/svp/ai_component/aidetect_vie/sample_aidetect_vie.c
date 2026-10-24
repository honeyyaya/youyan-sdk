/*
  Copyright (c), 2024-2024, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "securec.h"
#include "ss_mpi_aidetect.h"
#include "ot_math.h"
#include "sample_common_aivie.h"
#include "sample_common_aidetect.h"
#include "sample_common_ai.h"

#define OT_SAMPLE_AIDETECT_PATH_LEN         256
#define OT_AIDETECT_CHN_0                   0
#define OT_SAMPLE_AIDETECT_MAX_COVER_NUM    30
#define OT_SAMPLE_AIDETECT_COLOR_NUM        3

typedef struct {
    ot_vpss_grp vpss_grp;
    ot_vpss_chn vpss_chn;
    ot_venc_chn venc_chn;
    td_bool thread_start;
    td_char model_path[OT_SAMPLE_AIDETECT_PATH_LEN];
} aidetect_proc_param;

static volatile sig_atomic_t g_sig_flag = 0;

pthread_t g_aidetect_pid;

static aidetect_proc_param g_detect_proc_param = {
    .vpss_grp = 0,
    .vpss_chn = OT_SAMPLE_AI_CHN_RESULT_STREAM,
    .venc_chn = OT_SAMPLE_AI_CHN_RESULT_STREAM,
    .thread_start = TD_FALSE,
    .model_path = { 0 },
};

static td_void sample_aidetect_get_char(td_void)
{
    if (g_sig_flag == 1) {
        return;
    }
    sample_pause();
}

static td_u32 sample_aidetect_get_draw_color(td_u32 class_idx)
{
    const td_u32 color[OT_SAMPLE_AIDETECT_COLOR_NUM] = {
        0xFF0000,
        0x00FF00,
        0x0000FF,
    };
    return color[class_idx % OT_SAMPLE_AIDETECT_COLOR_NUM];
}

static td_s32 sample_aidetect_draw_result(const ot_aidetect_result_array *detect_result,
    ot_video_frame_info *frame_info)
{
    ot_vgs_handle handle;
    td_s32 ret = ss_mpi_vgs_begin_job(&handle);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, ret, "ss_mpi_vgs_begin_job failed.");

    ot_vgs_task_attr task = { 0 };
    task.img_in = *frame_info;
    task.img_out = *frame_info;

    ot_cover cover[OT_SAMPLE_AIDETECT_MAX_COVER_NUM] = { 0 };
    td_u32 cover_cnt = 0;
    const td_u32 ALIGN_2 = 2;
    for (td_u32 i = 0; i < detect_result->class_num; i++) {
        for (td_u32 j = 0; j < detect_result->object_class[i].object_num; j++) {
            cover[cover_cnt].type = OT_COVER_RECT;
            cover[cover_cnt].rect_attr.is_solid = TD_FALSE;
            cover[cover_cnt].rect_attr.thick = 2; // 2 : Thick of the hollow rectangle is 2
            cover[cover_cnt].rect_attr.rect.x =
                OT_ALIGN_DOWN(detect_result->object_class[i].objects[j].detect_rect.x, ALIGN_2);
            cover[cover_cnt].rect_attr.rect.y =
                OT_ALIGN_DOWN(detect_result->object_class[i].objects[j].detect_rect.y, ALIGN_2);
            cover[cover_cnt].rect_attr.rect.width =
                OT_ALIGN_DOWN(detect_result->object_class[i].objects[j].detect_rect.width, ALIGN_2);
            cover[cover_cnt].rect_attr.rect.height =
                OT_ALIGN_DOWN(detect_result->object_class[i].objects[j].detect_rect.height, ALIGN_2);
            cover[cover_cnt].color = sample_aidetect_get_draw_color(detect_result->object_class[i].class_type);
            cover_cnt++;
        }
    }
    if (cover_cnt == 0) {
        ss_mpi_vgs_cancel_job(handle);
        return TD_SUCCESS;
    }

    ret = ss_mpi_vgs_add_cover_task(handle, &task, cover, cover_cnt);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, exit, "ss_mpi_vgs_add_cover_task failed.");

    ret = ss_mpi_vgs_end_job(handle);
    sample_common_ai_log_check(ret != TD_SUCCESS, "ss_mpi_vgs_end_job failed.");
    return TD_SUCCESS;
exit:
    ss_mpi_vgs_cancel_job(handle);
    return TD_FAILURE;
}

static td_void *sample_aidetect_detect_proc(td_void *p)
{
    aidetect_proc_param *para = (aidetect_proc_param *)p;
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

        ret = sample_aidetect_draw_result(&detect_result, &frame_info);
        sample_common_ai_log_check_goto(ret != TD_SUCCESS, release_frame, "sample_aidetect_draw_result failed.");

        ret = ss_mpi_venc_send_frame(para->venc_chn, &frame_info, wait_time_ms);
        sample_common_ai_log_check(ret != TD_SUCCESS, "venc send frame failed.");

release_frame:
        ss_mpi_vpss_release_chn_frame(para->vpss_grp, para->vpss_chn, &frame_info);
    }

    sample_common_aidetect_deinit_result(&detect_result);
    return TD_NULL;
}

static td_s32 sample_aidetect_start_detect()
{
    td_s32 ret = sample_common_aidetect_create(OT_AIDETECT_CHN_0, g_detect_proc_param.model_path);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, ret, "sample_common_aidetect_create failed.");

    g_detect_proc_param.thread_start = TD_TRUE;
    ret = pthread_create(&g_aidetect_pid, 0, sample_aidetect_detect_proc, (td_void *)&g_detect_proc_param);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, exit, "pthread create failed.");
    return TD_SUCCESS;

exit:
    sample_common_aidetect_destroy(OT_AIDETECT_CHN_0);
    return TD_FAILURE;
}

static td_s32 sample_aidetect_stop_detect()
{
    if (g_detect_proc_param.thread_start == TD_TRUE) {
        g_detect_proc_param.thread_start = TD_FALSE;
        pthread_join(g_aidetect_pid, TD_NULL);
    }

    sample_common_aidetect_destroy(OT_AIDETECT_CHN_0);

    return TD_SUCCESS;
}

static td_s32 sample_aidetect_start_route(sample_vi_cfg *vi_cfg, sample_vpss_cfg *vpss_cfg)
{
    td_s32 ret = sample_common_aivie_init(vi_cfg, vpss_cfg);
    if (ret != TD_SUCCESS) {
        return TD_FAILURE;
    }
    ret = sample_aidetect_start_detect();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, start_aidetect_failed, "sample_aidetect_start_detect failed.");
    return TD_SUCCESS;

start_aidetect_failed:
    sample_common_aivie_deinit(vi_cfg);
    return TD_FAILURE;
}

static td_void sample_aidetect_stop_route(sample_vi_cfg *vi_cfg)
{
    sample_aidetect_stop_detect();
    sample_common_aivie_deinit(vi_cfg);
}

static td_s32 sample_aidetect_run(td_char *model_path)
{
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg;
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);

    sample_vpss_cfg vpss_cfg;
    sample_common_aivie_get_vpss_cfg(OT_SAMPLE_AI_CHN_RESULT_STREAM, &vpss_cfg);

    (td_void)strncpy_s(g_detect_proc_param.model_path, OT_SAMPLE_AIDETECT_PATH_LEN, model_path, strlen(model_path));

    if (sample_aidetect_start_route(&vi_cfg, &vpss_cfg) != TD_SUCCESS) {
        return TD_FAILURE;
    }

    sample_aidetect_get_char();

    sample_aidetect_stop_route(&vi_cfg);
    return TD_SUCCESS;
}

static td_void sample_aidetect_usage(const char *prg_name)
{
    printf("usage : %s -h \n", prg_name);
    printf("        %s model_path \n", prg_name);
}

static td_void sample_aidetect_handle_sig(td_s32 signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        g_sig_flag = 1;
    }
}

static td_void sample_aidetect_register_sig_handler(td_void (*sig_handle)(td_s32))
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
    if (argc != 2) { /* 2:arg num */
        sample_aidetect_usage(argv[0]);
        return TD_FAILURE;
    }

    if (strncmp(argv[1], "-h", 2) == 0) { /* 2:arg num */
        sample_aidetect_usage(argv[0]);
        return TD_SUCCESS;
    }

#ifndef __LITEOS__
    sample_aidetect_register_sig_handler(sample_aidetect_handle_sig);
#endif

    td_s32 ret = sample_aidetect_run(argv[1]);
    if ((ret == TD_SUCCESS) && (g_sig_flag == 0)) {
        printf("\033[0;32mprogram exit normally!\033[0;39m\n");
    } else {
        printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
    }

    return ret;
}
