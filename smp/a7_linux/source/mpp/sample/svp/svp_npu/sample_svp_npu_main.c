/*
  Copyright (c), 2001-2025, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "securec.h"
#include "sample_svp_npu_process.h"
#include "rtsp_demo.h"

#define SAMPLE_SVP_NPU_ARG_NUM_TWO 2
#define SAMPLE_SVP_NPU_ARG_NUM_THREE 3
#define SAMPLE_SVP_NPU_ARG_NUM_FOUR 4
#define SAMPLE_SVP_NPU_ARG_IDX_TWO 2
#define SAMPLE_SVP_NPU_CMP_STR_NUM 2
#define SAMPLE_SVP_NPU_ARG_STR_LEN 2
#define SAMPLE_SVP_NPU_ARG_IDX_BASE 10

static char **g_npu_cmd_argv = TD_NULL;

/*
 * function : to process abnormal case
 */
#ifndef __LITEOS__
static td_void sample_svp_npu_handle_sig(td_s32 signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        sample_svp_npu_acl_handle_sig();
    }
}
#endif

/*
 * function : show usage
 */
static td_void sample_svp_npu_usage(const td_char *prg_name)
{
    printf("Usage : %s <index> <rtsp_enable>\n", prg_name);
    printf("index:\n");
    printf("\t a) [8] yolov8.(VI->VPSS->SVP_NPU->VGS->VENC).\n");

    printf("rtsp_enable:\n");
    printf("\t  0) rtsp disabled\n");
    printf("\t  1) rtsp enabled\n");
}

static td_s32 sample_svp_npu_case_with_one_arg(td_char idx)
{
    td_s32 ret = TD_SUCCESS;

    switch (idx) {
        case '0':
            sample_svp_npu_acl_resnet50();
            break;
        case '1':
            sample_svp_npu_acl_resnet50_multi_thread();
            break;
        case '2':
            sample_svp_npu_acl_resnet50_dynamic_batch();
            break;
        case '3':
            sample_svp_npu_acl_lstm();
            break;
        case '4':
            sample_svp_npu_acl_preemption();
            break;
        default:
            ret = TD_FAILURE;
            break;
    }
    return ret;
}

static td_s32 sample_svp_npu_case_with_two_args(td_char idx1, td_char idx2)
{
    td_s32 ret = TD_SUCCESS;
    if ((idx1 == 'a' || idx1 == 'A') && !(idx2 == '8')) {
        return TD_FAILURE;
    }
    switch (idx1) {
        case 'A':
        case 'a':
            sample_svp_npu_acl_e2e_yolo(idx2 - '0');
            ret = TD_SUCCESS;
            break;
        default:
            ret = TD_FAILURE;
            break;
    }
    return ret;
}

/*
 * function : svp npu sample
 */
#ifdef __LITEOS__
int app_main(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
    td_s32 ret;
    size_t argv1_len, argv2_len;
    char *end_ptr;
#ifndef __LITEOS__
    struct sigaction sa;
#endif
    if (argc < SAMPLE_SVP_NPU_ARG_NUM_TWO || argc > SAMPLE_SVP_NPU_ARG_NUM_FOUR) {
        sample_svp_npu_usage(argv[0]);
        return TD_FAILURE;
    }

    if (!strncmp(argv[1], "-h", SAMPLE_SVP_NPU_CMP_STR_NUM)) {
        sample_svp_npu_usage(argv[0]);
        return TD_SUCCESS;
    }

    g_npu_cmd_argv = argv;
#ifndef __LITEOS__
    (td_void)memset_s(&sa, sizeof(struct sigaction), 0, sizeof(struct sigaction));
    sa.sa_handler = sample_svp_npu_handle_sig;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
#endif
    argv1_len = strlen(argv[1]);
    if (argv1_len != 1) {
        sample_svp_npu_usage(argv[0]);
        return TD_FAILURE;
    }
    if (argc == SAMPLE_SVP_NPU_ARG_NUM_THREE) {
        argv2_len = strlen(argv[SAMPLE_SVP_NPU_ARG_IDX_TWO]);
        if (argv2_len != 1) {
            sample_svp_npu_usage(argv[0]);
            return TD_FAILURE;
        }
    }
    if (argc == SAMPLE_SVP_NPU_ARG_NUM_TWO) {
        ret = sample_svp_npu_case_with_one_arg(*argv[1]);
    } else if (argc == SAMPLE_SVP_NPU_ARG_NUM_THREE){
        ret = sample_svp_npu_case_with_two_args(*argv[1], *argv[SAMPLE_SVP_NPU_ARG_IDX_TWO]);
    }else {
        g_rtsp_enable = strtoul(argv[3], &end_ptr, 10); /* 10: numberbase */
        if (end_ptr == argv[3] || *end_ptr !='\0') {
            sample_svp_npu_usage(argv[0]);
            return TD_FAILURE;
        }
        ret = sample_svp_npu_case_with_two_args(*argv[1], *argv[SAMPLE_SVP_NPU_ARG_IDX_TWO]);
    }
    if (ret != TD_SUCCESS) {
        sample_svp_npu_usage(argv[0]);
        return TD_FAILURE;
    }

    return 0;
}
