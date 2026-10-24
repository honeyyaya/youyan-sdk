/*
  Copyright (c), 2001-2025, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <arm_neon.h>
#include "sample_comm.h"
#include "sample_ipc.h"
#include "securec.h"
#include "ss_mpi_ae.h"
#include "ss_mpi_ive.h"
#include "transfer_dev.h"
#include "ot_type.h"
#include "ff_vdec.h"
#include "video_decoder.h"
#include "libavutil/log.h"
#include "libavcodec/avcodec.h"
#include "ss_mpi_vb.h"
#include "ss_mpi_sys.h"

#define VDEC_WIDTH 320
#define VDEC_HEIGHT 240
#define INBUF_SIZE VDEC_WIDTH * VDEC_HEIGHT / 2
#define VENC_MAX_CHN_NUM 8
#define VIO_THREAD_NAME_LEN 64

pthread_t g_spiThread;
pthread_t g_get_frame_thread_id;
pthread_t g_send_stream_thread_id;

static td_bool g_thread_stop_flag = TD_FALSE;
static td_void *g_vdec_hdl;
static td_char g_input_file[FILE_NAME_LEN] = "input.h265";
static td_s32 g_decode_type = 1;
static td_bool g_read_stream_running = TD_TRUE;
static td_bool g_send_stream_running = TD_TRUE;
static volatile sig_atomic_t g_sig_flag = 0;

typedef struct {
    td_u64 phys_addr;
    td_u8 *virt_addr;
    ot_vb_blk vb_blk;
    td_u64 size;
} sample_vb_info;

static sample_vb_info g_rgb888_vb_info = {
    .vb_blk = OT_VB_INVALID_HANDLE,
};
static sample_vb_info g_rgb565_vb_info[2] = {
    {
        .vb_blk = OT_VB_INVALID_HANDLE,
    },
    {
        .vb_blk = OT_VB_INVALID_HANDLE,
    }
};

static sample_vb_param g_vb_param = {
    /* raw, yuv, vpss chn1 */
    .vb_size = { { 2688, 1520 }, { 2688, 1520 }, { 720, 480 } },
    .pixel_format = { OT_PIXEL_FORMAT_RGB_BAYER_12BPP, OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420,
                      OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420 },
    .compress_mode = { OT_COMPRESS_MODE_LINE, OT_COMPRESS_MODE_NONE, OT_COMPRESS_MODE_NONE },
    .video_format = { OT_VIDEO_FORMAT_LINEAR, OT_VIDEO_FORMAT_LINEAR, OT_VIDEO_FORMAT_LINEAR },
    .blk_num = { 4, 5, 10 }
};

static sampe_sys_cfg g_spi_lcd_sys_cfg = {
    .route_num = 1,
    .mode_type = OT_VI_OFFLINE_VPSS_OFFLINE,
    .vpss_wrap_en = TD_TRUE,
    .vpss_wrap_size = 3200 * 128 * 1.5,
};

static sample_comm_venc_chn_param g_venc_chn_param = {
    .frame_rate           = 30, /* 30 is a number */
    .stats_time           = 2,  /* 2 is a number */
    .gop                  = 60, /* 60 is a number */
    .venc_size            = {1920, 1080},
    .size                 = -1,
    .profile              = 0,
    .is_rcn_ref_share_buf = TD_FALSE,
    .gop_attr             = {
        .gop_mode = OT_VENC_GOP_MODE_NORMAL_P,
        .normal_p = {2},
    },
    .type                 = OT_PT_H265,
    .rc_mode              = SAMPLE_RC_CBR,
};

#ifndef __LITEOS__
static td_void sample_vie_handle_sig(td_s32 signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        g_sig_flag = 1;
    }
}

static td_void sample_register_sig_handler(td_void (*sig_handle)(td_s32))
{
    struct sigaction sa;

    (td_void)memset_s(&sa, sizeof(struct sigaction), 0, sizeof(struct sigaction));
    sa.sa_handler = sig_handle;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, TD_NULL);
    sigaction(SIGTERM, &sa, TD_NULL);
    sigaction(SIGKILL, &sa, TD_NULL);
}
#endif

static td_void sample_get_char(td_void)
{
    if (g_sig_flag == 1) {
        return;
    }
    sample_pause();
}

#ifdef SAMPLE_MEM_SHARE_ENABLE
td_void sample_spi_lcd_init_mem_share(td_void)
{
    td_u32 i;
    ot_vb_common_pools_id pools_id = { 0 };

    if (ss_mpi_vb_get_common_pool_id(&pools_id) != TD_SUCCESS) {
        sample_print("get common pool_id failed!\n");
        return;
    }
    for (i = 0; i < pools_id.pool_cnt; ++i) {
        ss_mpi_vb_pool_share_all(pools_id.pool[i]);
    }
}
#endif

/* default sc4336p x 2 */
static ot_vi_wrap_in_param g_vi_wrap_param = {
    .is_slave = TD_FALSE,
    .pipe_num = 2,
    .wrap_param[0] = {
        .pipe_size = {2560, 1440},
        .frame_rate_100x = 3000,
        .full_lines_std = 1500,
        .slave_param = {
            .vi_vpss_online = TD_TRUE,
            .vpss_chn0_size = {2560, 1440},
            .vpss_venc_wrap = TD_TRUE,
            .large_stream_size = {2560, 1440},
            .small_stream_size = {720, 480},
        }
    },
    .wrap_param[1] = {
        .pipe_size = {2560, 1440},
        .frame_rate_100x = 3000,
        .full_lines_std = 1500,
        .slave_param.vi_vpss_online = TD_TRUE,
        .slave_param.vpss_chn0_size = {2560, 1440},
        .slave_param.vpss_venc_wrap = TD_TRUE,
        .slave_param.large_stream_size = {2560, 1440},
        .slave_param.small_stream_size = {720, 480},
    }
};

static td_void sample_spi_lcd_get_pipe_wrap_line(sample_vi_cfg vi_cfg[], td_s32 route_num)
{
    ot_vi_wrap_out_param out_param;
    ot_vi_wrap_in_param in_param;
    td_s32 i;

    if (g_spi_lcd_sys_cfg.mode_type == OT_VI_ONLINE_VPSS_ONLINE ||
        g_spi_lcd_sys_cfg.mode_type == OT_VI_ONLINE_VPSS_OFFLINE) {
        return;
    }

    (td_void)memcpy_s(&in_param, sizeof(ot_vi_wrap_in_param), &g_vi_wrap_param, sizeof(ot_vi_wrap_in_param));
    if (g_spi_lcd_sys_cfg.mode_type == OT_VI_OFFLINE_VPSS_OFFLINE) {
        in_param.wrap_param[0].slave_param.vi_vpss_online = TD_FALSE;
        in_param.wrap_param[1].slave_param.vi_vpss_online = TD_FALSE;
    }
    in_param.pipe_num = route_num;
#ifdef OT_FPGA
    in_param.wrap_param[0].frame_rate_100x = 1500; /* 1500: 15fps*100 */
    in_param.wrap_param[1].frame_rate_100x = 1500; /* 1500: 15fps*100 */
    in_param.wrap_param[0].slave_param.vpss_venc_wrap = TD_FALSE;
    in_param.wrap_param[1].slave_param.vpss_venc_wrap = TD_FALSE;
#endif
    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &in_param.wrap_param[0].pipe_size);
    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &in_param.wrap_param[0].slave_param.vpss_chn0_size);
    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &in_param.wrap_param[0].slave_param.large_stream_size);
    sample_comm_vi_get_size_by_sns_type(SENSOR1_TYPE, &in_param.wrap_param[1].pipe_size);
    sample_comm_vi_get_size_by_sns_type(SENSOR1_TYPE, &in_param.wrap_param[1].slave_param.vpss_chn0_size);
    sample_comm_vi_get_size_by_sns_type(SENSOR1_TYPE, &in_param.wrap_param[1].slave_param.large_stream_size);
    in_param.wrap_param[0].full_lines_std = in_param.wrap_param[0].pipe_size.height;
    in_param.wrap_param[1].full_lines_std = in_param.wrap_param[1].pipe_size.height;
    if (ss_mpi_sys_get_vi_wrap_buffer_line(&in_param, &out_param) != TD_SUCCESS) {
        sample_print("sample_spi_lcd get wrap line fail, pipe_wrap not enable!\n");
        return;
    }

    for (i = 0; i < route_num; i++) {
        vi_cfg[i].pipe_info[0].wrap_attr.buf_line = out_param.buf_line[i];
        vi_cfg[i].pipe_info[0].wrap_attr.enable = TD_TRUE;
    }
}

static td_s32 sample_spi_lcd_sys_init(td_void)
{
    ot_vb_cfg vb_cfg;
    td_u32 supplement_config = OT_VB_SUPPLEMENT_BNR_MOT_MASK | OT_VB_SUPPLEMENT_MOTION_DATA_MASK;

    sample_comm_sys_get_default_vb_cfg(&g_vb_param, &vb_cfg);
    /* prepare vpss wrap vb */
    if (g_spi_lcd_sys_cfg.vpss_wrap_en) {
        vb_cfg.common_pool[SAMPLE_VIE_POOL_NUM].blk_cnt = 1;
        vb_cfg.common_pool[SAMPLE_VIE_POOL_NUM].blk_size = g_spi_lcd_sys_cfg.vpss_wrap_size;
    }
    if (sample_comm_sys_init_with_vb_supplement(&vb_cfg, supplement_config) != TD_SUCCESS) {
        return TD_FAILURE;
    }

#ifdef SAMPLE_MEM_SHARE_ENABLE
    sample_spi_lcd_init_mem_share();
#endif

    if (sample_comm_vi_set_vi_vpss_mode(g_spi_lcd_sys_cfg.mode_type, OT_VI_AIISP_MODE_DEFAULT) != TD_SUCCESS) {
        goto sys_exit;
    }

    return TD_SUCCESS;
sys_exit:
    sample_comm_sys_exit();
    return TD_FAILURE;
}

static td_s32 sample_spi_lcd_vpss_set_wrap_grp_int_attr(ot_vi_vpss_mode_type mode_type, td_bool wrap_en,
    td_u32 max_height)
{
    td_s32 ret;
    if (mode_type == OT_VI_ONLINE_VPSS_ONLINE && wrap_en) {
        ot_frame_interrupt_attr frame_interrupt_attr;
        frame_interrupt_attr.interrupt_type = OT_FRAME_INTERRUPT_EARLY_END;
        frame_interrupt_attr.early_line = max_height / 2; /* 2 half */
        ret = ss_mpi_vpss_set_grp_frame_interrupt_attr(0, &frame_interrupt_attr);
        if (ret != TD_SUCCESS) {
            printf("ss_mpi_vpss_set_grp_frame_interrupt_attr failed!\n");
            return ret;
        }
    }
    return TD_SUCCESS;
}

static td_s32 sample_spi_lcd_start_vpss(ot_vpss_grp grp, sample_vpss_cfg *vpss_cfg)
{
    td_s32 ret;
    sample_vpss_chn_attr vpss_chn_attr = {0};
    ot_mpp_chn mpp_chn = {OT_ID_VPSS, 0, 1};
    ot_rotation_attr rotation_attr = {TD_TRUE, OT_ROTATION_ANG_FIXED, {OT_ROTATION_270}};

    if (grp == 0) {
        ret = sample_spi_lcd_vpss_set_wrap_grp_int_attr(g_spi_lcd_sys_cfg.mode_type, vpss_cfg->wrap_attr[0].enable,
            vpss_cfg->grp_attr.max_height);
        if (ret != TD_SUCCESS) {
            return ret;
        }
    }

    (td_void)memcpy_s(&vpss_chn_attr.chn_attr[0], sizeof(ot_vpss_chn_attr) * OT_VPSS_MAX_PHYS_CHN_NUM,
        vpss_cfg->chn_attr, sizeof(ot_vpss_chn_attr) * OT_VPSS_MAX_PHYS_CHN_NUM);
    (td_void)memcpy_s(vpss_chn_attr.chn_enable, sizeof(vpss_chn_attr.chn_enable), vpss_cfg->chn_en,
        sizeof(vpss_chn_attr.chn_enable));
    (td_void)memcpy_s(&vpss_chn_attr.wrap_attr[0], sizeof(ot_vpss_chn_buf_wrap_attr) * OT_VPSS_MAX_PHYS_CHN_NUM,
        vpss_cfg->wrap_attr, sizeof(ot_vpss_chn_buf_wrap_attr) * OT_VPSS_MAX_PHYS_CHN_NUM);
    vpss_chn_attr.chn_array_size = OT_VPSS_MAX_PHYS_CHN_NUM;
    ret = sample_common_vpss_start(grp, &vpss_cfg->grp_attr, &vpss_chn_attr);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    if (vpss_cfg->chn_en[1]) {
        const ot_low_delay_info low_delay_info = { TD_TRUE, 200, TD_FALSE }; /* 200: lowdelay line */
        if (ss_mpi_vpss_set_chn_low_delay(grp, 1, &low_delay_info) != TD_SUCCESS) {
            goto stop_vpss;
        }
    }
    /* The spi lcd screen is portrait and needs to be rotated 270° */
    ret = ss_mpi_vpss_set_chn_rotation(mpp_chn.dev_id, mpp_chn.chn_id, &rotation_attr);
    if (ret != TD_SUCCESS) {
        goto stop_vpss;
    }
    return TD_SUCCESS;
stop_vpss:
    sample_common_vpss_stop(grp, vpss_cfg->chn_en, OT_VPSS_MAX_PHYS_CHN_NUM);
    return TD_FAILURE;
}

static td_void sample_spi_lcd_stop_vpss(ot_vpss_grp grp)
{
    td_bool chn_enable[OT_VPSS_MAX_PHYS_CHN_NUM] = {TD_TRUE, TD_TRUE, TD_FALSE};

    sample_common_vpss_stop(grp, chn_enable, OT_VPSS_MAX_PHYS_CHN_NUM);
}

static td_s32 sample_spi_lcd_start_venc(ot_venc_chn venc_chn[], ot_size venc_size[], size_t size, td_u32 chn_num)
{
    td_s32 i;
    td_s32 ret;

    if (chn_num > size) {
        return TD_FAILURE;
    }

    for (i = 0; i < (td_s32)chn_num; i++) {
        g_venc_chn_param.venc_size.width = venc_size[i].width;
        g_venc_chn_param.venc_size.height = venc_size[i].height;
        g_venc_chn_param.is_rcn_ref_share_buf = 1;
        ret = sample_comm_venc_start(venc_chn[i], &g_venc_chn_param);
        if (ret != TD_SUCCESS) {
            goto exit;
        }
    }

    ret = sample_comm_venc_start_get_stream(venc_chn, chn_num);
    if (ret != TD_SUCCESS) {
        goto exit;
    }

    return TD_SUCCESS;

exit:
    for (i = i - 1; i >= 0; i--) {
        sample_comm_venc_stop(venc_chn[i]);
    }
    return TD_FAILURE;
}

static td_void sample_spi_lcd_stop_venc(ot_venc_chn venc_chn[], size_t size, td_u32 chn_num)
{
    td_u32 i;

    if (chn_num > size) {
        return;
    }

    sample_comm_venc_stop_get_stream(chn_num);

    for (i = 0; i < chn_num; i++) {
        sample_comm_venc_stop(venc_chn[i]);
    }
}

static td_s32 sample_spi_lcd_start_and_bind_venc(ot_vpss_grp vpss_grp[], sample_vpss_cfg *vpss_cfg, size_t size,
    td_u32 grp_num)
{
    td_u32 i;
    td_s32 ret;
    ot_venc_chn venc_chn[VENC_MAX_CHN_NUM];
    ot_size venc_size[VENC_MAX_CHN_NUM];
    td_u32 chn_num;

    if (grp_num > size) {
        return TD_FAILURE;
    }

    for (i = 0; i < VENC_MAX_CHN_NUM; i++) {
        venc_chn[i] = i;
        if (i % 2 == 0) { /* 2: divisor */
            venc_size[i].width = vpss_cfg->chn_attr[0].width;
            venc_size[i].height = vpss_cfg->chn_attr[0].height;
        } else {
            venc_size[i].width = vpss_cfg->chn_attr[1].width;
            venc_size[i].height = vpss_cfg->chn_attr[1].height;
        }
    }
    /* The spi lcd screen is portrait and needs to be rotated 270° */
    venc_size[1].width = vpss_cfg->chn_attr[1].height;
    venc_size[1].height = vpss_cfg->chn_attr[1].width;
    chn_num = grp_num;
    if (g_spi_lcd_sys_cfg.vpss_wrap_en) {
        chn_num *= 2; /* 2 chns */
    }
    ret = sample_spi_lcd_start_venc(venc_chn, venc_size, sizeof(venc_chn) / sizeof(venc_chn[0]), chn_num);
    if (ret != TD_SUCCESS) {
        return TD_FAILURE;
    }
    for (i = 0; i < grp_num; i++) {
        sample_comm_vpss_bind_venc(vpss_grp[i], OT_VPSS_CHN0, venc_chn[i * 2]); /* 2 chns */
        if (g_spi_lcd_sys_cfg.vpss_wrap_en) {
            sample_comm_vpss_bind_venc(vpss_grp[i], OT_VPSS_CHN1, venc_chn[i * 2 + 1]); /* 2 chns */
        }
    }

    return TD_SUCCESS;
}

static td_void sample_spi_lcd_stop_and_unbind_venc(ot_vpss_grp vpss_grp[], size_t size, td_u32 grp_num)
{
    td_u32 i;
    ot_venc_chn venc_chn[VENC_MAX_CHN_NUM];
    td_u32 chn_num;

    if (grp_num > size) {
        return;
    }

    for (i = 0; i < VENC_MAX_CHN_NUM; i++) {
        venc_chn[i] = i;
    }

    for (i = 0; i < grp_num; i++) {
        sample_comm_vpss_un_bind_venc(vpss_grp[i], OT_VPSS_CHN0, venc_chn[i * 2]); /* 2 chns */
        if (g_spi_lcd_sys_cfg.vpss_wrap_en) {
            sample_comm_vpss_un_bind_venc(vpss_grp[i], OT_VPSS_CHN1, venc_chn[i * 2 + 1]); /* 2 chns */
        }
    }

    chn_num = grp_num;
    if (g_spi_lcd_sys_cfg.vpss_wrap_en) {
        chn_num *= 2; /* 2 chns */
    }
    sample_spi_lcd_stop_venc(venc_chn, sizeof(venc_chn) / sizeof(venc_chn[0]), chn_num);
}

static td_s32 sample_spi_lcd_start_route(sample_vi_cfg *vi_cfg, sample_vpss_cfg *vpss_cfg, td_s32 route_num)
{
    td_s32 i, j, ret;
    ot_vpss_grp vpss_grp[SAMPLE_VIE_MAX_ROUTE_NUM] = {0, 1, 2, 3};

    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &g_vb_param.vb_size[0]);
    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &g_vb_param.vb_size[1]);
    if (sample_spi_lcd_sys_init() != TD_SUCCESS) {
        return TD_FAILURE;
    }

    for (i = 0; i < route_num; i++) {
        ret = sample_comm_vi_start_vi(&vi_cfg[i]);
        if (ret != TD_SUCCESS) {
            goto start_vi_failed;
        }
    }

    for (i = 0; i < route_num; i++) {
        sample_comm_vi_bind_vpss(i, 0, vpss_grp[i], 0);
    }

    for (i = 0; i < route_num; i++) {
        ret = sample_spi_lcd_start_vpss(vpss_grp[i], vpss_cfg);
        if (ret != TD_SUCCESS) {
            goto start_vpss_failed;
        }
    }

    ret = sample_spi_lcd_start_and_bind_venc(vpss_grp, vpss_cfg, SAMPLE_VIE_MAX_ROUTE_NUM, route_num);
    if (ret != TD_SUCCESS) {
        goto start_vpss_failed;
    }

    return TD_SUCCESS;

start_vpss_failed:
    for (j = i - 1; j >= 0; j--) {
        sample_spi_lcd_stop_vpss(vpss_grp[j]);
    }
    for (i = 0; i < route_num; i++) {
        sample_comm_vi_un_bind_vpss(i, 0, vpss_grp[i], 0);
    }
start_vi_failed:
    for (j = i - 1; j >= 0; j--) {
        sample_comm_vi_stop_vi(&vi_cfg[j]);
    }
    sample_comm_sys_exit();
    return TD_FAILURE;
}

static td_void sample_spi_lcd_print_vi_mode_list(td_bool is_wdr_mode)
{
    printf("vi vpss mode list: \n");
    if (is_wdr_mode == TD_TRUE) {
        printf("    (0) VI_ONLINE_VPSS_ONLINE\n");
        printf("    (1) VI_ONLINE_VPSS_OFFLINE\n");
        printf("    (2) VI_OFFLINE_VPSS_ONLINE\n");
        printf("    (3) VI_OFFLINE_VPSS_OFFLINE\n");
        printf("please select mode:\n");
        return;
    }
    printf("    (0) VI_ONLINE_VPSS_ONLINE\n");
    printf("    (1) VI_ONLINE_VPSS_OFFLINE\n");
    printf("    (2) VI_OFFLINE_VPSS_ONLINE\n");
    printf("    (3) VI_OFFLINE_VPSS_OFFLINE\n");
    printf("please select mode:\n");
}

static td_void sample_spi_lcd_get_vi_vpss_mode_by_char(td_char ch, td_bool is_wdr)
{
    switch (ch) {
        case '0':
            g_spi_lcd_sys_cfg.mode_type = OT_VI_ONLINE_VPSS_ONLINE;
            g_vb_param.blk_num[0] = 0; /* raw_vb num 0 */
            g_vb_param.blk_num[1] = 0; /* yuv_vb num 0 */
            break;
        case '1':
            g_spi_lcd_sys_cfg.mode_type = OT_VI_ONLINE_VPSS_OFFLINE;
            g_vb_param.blk_num[0] = 0; /* raw_vb num 0 */
            g_vb_param.blk_num[1] = 8; /* yuv_vb num 8 */
            break;
        case '2':
            g_spi_lcd_sys_cfg.mode_type = OT_VI_OFFLINE_VPSS_ONLINE;
            g_vb_param.blk_num[0] = is_wdr ? 6 : 0; /* raw_vb num 6 or 0 */
            g_vb_param.blk_num[1] = 0;              /* yuv_vb num 0 */
            break;
        case '3':
            g_spi_lcd_sys_cfg.mode_type = OT_VI_OFFLINE_VPSS_OFFLINE;
            g_vb_param.blk_num[0] = is_wdr ? 4 : 0; /* raw_vb num 4 or 0 */
            break;
        default:
            g_spi_lcd_sys_cfg.mode_type = OT_VI_ONLINE_VPSS_ONLINE;
            g_vb_param.blk_num[0] = 0; /* raw_vb num 0 */
            g_vb_param.blk_num[1] = 0; /* yuv_vb num 0 */
            break;
    }
}

static td_void sample_spi_lcd_get_vi_vpss_mode(td_bool is_wdr_mode)
{
    td_char ch = '0';
    td_char end_ch = '3';
    td_char input[3] = {0}; /* max_len: 3 */
    td_s32 max_len = 3;       /* max_len: 3 */

    sample_spi_lcd_print_vi_mode_list(is_wdr_mode);

    while (g_sig_flag == 0) {
        if (gets_s(input, max_len) != TD_NULL && strlen(input) == 1 && input[0] >= ch && input[0] <= end_ch) {
            break;
        } else {
            printf("\nInvalid param, please enter again!\n\n");
            sample_spi_lcd_print_vi_mode_list(is_wdr_mode);
        }
        (td_void)fflush(stdin);
    }

    sample_spi_lcd_get_vi_vpss_mode_by_char(input[0], is_wdr_mode);
}

static td_s32 sample_nv12_to_rgb888_ive(const ot_video_frame_info *yuvFrame, td_u64 out_ive_phys_addr,
    td_void *out_ive_virt_addr)
{
    ot_svp_src_img src_data = { 0 };
    ot_svp_src_img dst_data = { 0 };
    ot_ive_handle td_handlen;
    td_bool is_finish, is_block;

    src_data.type = OT_SVP_IMG_TYPE_YUV420SP;
    src_data.width = yuvFrame->video_frame.width;
    src_data.height = yuvFrame->video_frame.height;
    for (int i = 0; i < OT_MAX_COLOR_COMPONENT; i++) {
        src_data.virt_addr[i] = (td_u64)(td_uintptr_t)yuvFrame->video_frame.virt_addr[i];
        src_data.phys_addr[i] = (td_u64)(td_uintptr_t)yuvFrame->video_frame.phys_addr[i];
        src_data.stride[i] = yuvFrame->video_frame.stride[i];
    }

    dst_data.type = OT_SVP_IMG_TYPE_U8C3_PACKAGE;
    dst_data.width = yuvFrame->video_frame.width;
    dst_data.height = yuvFrame->video_frame.height;

    for (int i = 0; i < OT_SVP_IMG_STRIDE_NUM; i++) {
        dst_data.stride[i] = OT_ALIGN_UP(yuvFrame->video_frame.width, 16); /** 16 */
        dst_data.virt_addr[i] = (td_u64)(td_uintptr_t)out_ive_virt_addr + dst_data.stride[i] * dst_data.height * i;
        dst_data.phys_addr[i] = out_ive_phys_addr + dst_data.stride[i] * dst_data.height * i;
    }
    ot_ive_csc_ctrl ctrl;
    ctrl.mode = OT_IVE_CSC_MODE_PIC_BT709_YUV_TO_RGB;

    td_u32 ret = ss_mpi_ive_csc(&td_handlen, &src_data, &dst_data, &ctrl, TD_SUCCESS);

    is_block = TD_TRUE;
    ret = ss_mpi_ive_query(td_handlen, &is_finish, is_block);
    if (ret != TD_SUCCESS) {
        printf("is_finish=%d\n", is_finish);
    }

    return ret;
}

static td_void sample_bgr888_to_rgb565_neon(td_u32 width, td_u32 height, td_u8 *bgr888_addr, td_u8 *rgb565_addr)
{
    uint8_t *src = bgr888_addr;
    uint16_t *dst = (uint16_t *)rgb565_addr;
    int count = height * width;

    while (count >= 8) { /** 8 */
        uint8x8x3_t vsrc;
        uint16x8_t vdst;

        vsrc = vld3_u8(src);
        vdst = vshll_n_u8(vsrc.val[0], 8); /** 8 */
        vdst = vsriq_n_u16(vdst, vshll_n_u8(vsrc.val[1], 8), 5); /** 8 5 */
        vdst = vsriq_n_u16(vdst, vshll_n_u8(vsrc.val[2], 8), 11); /** 2 8 11 */
        vst1q_u16(dst, vdst);

        dst += 8; /** 8 */
        src += 8 * 3; /** 8 3 */
        count -= 8; /** 8 */
    }
}

static td_s32 sample_spi_get_vb(sample_vb_info *vb_info)
{
    td_char *mmz_name = TD_NULL;

    vb_info->vb_blk = ss_mpi_vb_get_blk(OT_VB_INVALID_POOL_ID, vb_info->size, mmz_name);
    if (vb_info->vb_blk == OT_VB_INVALID_HANDLE) {
        sample_print("Info:mpi_vb_get_blk(size:%llu) fail\n", vb_info->size);
        return TD_FAILURE;
    }

    vb_info->phys_addr = ss_mpi_vb_handle_to_phys_addr(vb_info->vb_blk);
    if (vb_info->phys_addr == 0) {
        sample_print("Info:mpi_vb_handle_to_phys_addr fail, u32OutPhyAddr:0x%llx\n", vb_info->phys_addr);
        ss_mpi_vb_release_blk(vb_info->vb_blk);
        vb_info->vb_blk = OT_VB_INVALID_HANDLE;
        return TD_FAILURE;
    }

    vb_info->virt_addr = (td_u8 *)ss_mpi_sys_mmap(vb_info->phys_addr, vb_info->size);
    if (vb_info->virt_addr == TD_NULL) {
        sample_print("Info:mpi_sys_mmap fail\n");
        ss_mpi_vb_release_blk(vb_info->vb_blk);
        vb_info->vb_blk = OT_VB_INVALID_HANDLE;
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

static td_void sample_spi_release_vb(sample_vb_info *vb_info)
{
    if (vb_info->virt_addr != TD_NULL) {
        ss_mpi_sys_munmap(vb_info->virt_addr, vb_info->size);
        vb_info->virt_addr = TD_NULL;
    }
    if (vb_info->vb_blk != OT_VB_INVALID_HANDLE) {
        ss_mpi_vb_release_blk(vb_info->vb_blk);
        vb_info->vb_blk = OT_VB_INVALID_HANDLE;
    }
    vb_info->phys_addr = 0;
}

static td_s32 data_transfer(td_u8 *out_phy_addr, td_u8 *virt_addr, td_u32 size)
{
    td_s32 ret;
    static int fd = -1;
    if (fd == -1) {
        fd = open("/dev/transfer", O_RDWR);
        if (fd == -1) {
            sample_print("open transfer fail errno[%d]\n", errno);
            return -1;
        }
    }
    struct transfer_frame_info frame_info;
    frame_info.frame_phys_addr = (unsigned long long)(uintptr_t)out_phy_addr;
    frame_info.size = size;
    frame_info.timeout = -1;
    frame_info.frame_virt_addr = (unsigned long long)(uintptr_t)virt_addr;
    printf("data_transfer ioctl start\r\n");
    ret = ioctl(fd, TRANSFER_SEND_FRAME, &frame_info);
    if (ret != TD_SUCCESS) {
        printf("[Func]:%s [Line]:%d [Info]:%s\n", __FUNCTION__, __LINE__, "transfer send failed");
        return ret;
    }
    printf("data_transfer ioctl end\r\n");
    return 0;
}

static td_s32 get_rgb565_vb(td_void)
{
    td_s32 ret;
    td_s32 i, j;

    for (i = 0; i < 2; i++) { /* 2 */
        if (g_rgb565_vb_info[i].phys_addr == 0) {
            ret = sample_spi_get_vb(&g_rgb565_vb_info[i]);
            if (ret != TD_SUCCESS) {
                sample_print("get_vb fail\n");
                goto fail;
            }
        }
    }
    return TD_SUCCESS;
fail:
    for (j = i - 1; j >= 0; j--) {
        sample_spi_release_vb(&g_rgb565_vb_info[j]);
    }
    return TD_FAILURE;
}

static td_void release_rgb565_vb(td_void)
{
    td_s32 i;

    for (i = 0; i < 2; i++) { /* 2 */
        sample_spi_release_vb(&g_rgb565_vb_info[i]);
    }
}

static td_s32 save_first_frame(td_u8 *rgb565_virt_addr, td_u32 rgb565_size)
{
    FILE *fp = fopen("rgb565", "wb");
    if (fp != NULL) {
        fwrite(rgb565_virt_addr, rgb565_size, 1, fp);
        fclose(fp);
        return TD_SUCCESS;
    }
    return TD_FAILURE;
}

static td_void print_performance_stats(td_u64 pix_time_start, td_u64 pix_time_ive_end, td_u64 pix_time_end,
    td_s32 timeCnt)
{
    if (timeCnt % 100 == 0) { /** 100 */
        td_u64 pix_transfer_end;
        ss_mpi_sys_get_cur_pts(&pix_transfer_end);
        sample_print("ive cost:%llu, all pix cost:%llu\n", pix_time_ive_end - pix_time_start,
            pix_time_end - pix_time_start);
        sample_print("every frame cost:%llu, transfer cost:%llu\n", pix_transfer_end - pix_time_start,
            pix_transfer_end - pix_time_end);
    }
}

static td_s32 sample_yuv_frame_proc(ot_video_frame_info *frame)
{
    td_s32 ret;

    g_rgb888_vb_info.size = frame->video_frame.stride[0] * frame->video_frame.height * 3; /* 3 */
    if (g_rgb888_vb_info.phys_addr == 0) {
        ret = sample_spi_get_vb(&g_rgb888_vb_info);
        if (ret != TD_SUCCESS) {
            sample_print("get_vb fail\n");
            return TD_FAILURE;
        }
    }

    static td_s32 rgb565_index = 0;

    g_rgb565_vb_info[0].size = frame->video_frame.width * frame->video_frame.height * 2; /* 2 */
    g_rgb565_vb_info[1].size = g_rgb565_vb_info[0].size;
    ret = get_rgb565_vb();
    if (ret != TD_SUCCESS) {
        sample_spi_release_vb(&g_rgb888_vb_info);
        return TD_FAILURE;
    }

    td_u64 pix_time_start, pix_time_ive_end, pix_time_end;
    static td_s32 timeCnt = 0;
    ss_mpi_sys_get_cur_pts(&pix_time_start);
    ret = sample_nv12_to_rgb888_ive(frame, g_rgb888_vb_info.phys_addr, g_rgb888_vb_info.virt_addr);
    if (ret != TD_SUCCESS) {
        sample_print("ive_csc fail\n");
        sample_spi_release_vb(&g_rgb888_vb_info);
        release_rgb565_vb();
        return TD_FAILURE;
    }
    ss_mpi_sys_get_cur_pts(&pix_time_ive_end);

    sample_bgr888_to_rgb565_neon(frame->video_frame.width, frame->video_frame.height, g_rgb888_vb_info.virt_addr,
        g_rgb565_vb_info[rgb565_index].virt_addr);
    ss_mpi_sys_get_cur_pts(&pix_time_end);

    static td_s32 save_first_picture = 1;
    if (save_first_picture == 1) {
        save_first_frame(g_rgb565_vb_info[rgb565_index].virt_addr, g_rgb565_vb_info[0].size);
        save_first_picture = 0;
    }

	printf("-> %d ",g_rgb565_vb_info[0].size);
	
    ret = data_transfer((td_u8 *)(uintptr_t)g_rgb565_vb_info[rgb565_index].phys_addr,
        g_rgb565_vb_info[rgb565_index].virt_addr, g_rgb565_vb_info[0].size);
    if (ret == 0) {
        rgb565_index = (rgb565_index == 0 ? 1 : 0);
        print_performance_stats(pix_time_start, pix_time_ive_end, pix_time_end, timeCnt);
        timeCnt++;
    }

    return TD_SUCCESS;
}

static td_void *sample_spi_frame_proc(td_void *args)
{
    td_char thread_name[VIO_THREAD_NAME_LEN] = {0};
    ot_mpp_chn mppChn = *(ot_mpp_chn *)args;
    ot_video_frame_info frame;
    td_s32 ret;
    td_s32 milli_sec = 1000; /* 1000ms timeout time. */

    if (snprintf_s(thread_name, sizeof(thread_name), sizeof(thread_name) - 1, "spi_Frame_proc") == -1) {
        return TD_NULL;
    }
    prctl(PR_SET_NAME, thread_name, 0, 0, 0);

    td_u64 spi_timeCnt = 0;
    td_u64 spi_send_time_start = 0, spi_send_time_end = 0;

    ss_mpi_sys_get_cur_pts(&spi_send_time_start);
    while (g_thread_stop_flag == TD_FALSE && g_sig_flag == 0) {
        ret = ss_mpi_vpss_get_chn_frame(mppChn.dev_id, mppChn.chn_id, &frame, milli_sec);
        if (ret != TD_SUCCESS) {
            printf("vpss_grp%d chn[%d] get_chn_frame ret = 0x%x failed!\n", mppChn.dev_id, mppChn.chn_id, ret);
            continue;
        }
        printf("get a new frame %d \r\n",mppChn.dev_id);
        
        ret = sample_yuv_frame_proc(&frame);
        if (ret != TD_SUCCESS) {
            sample_print("yuv_frame_proc failed!\n");
        }
        
        ret = ss_mpi_vpss_release_chn_frame(mppChn.dev_id, mppChn.chn_id, &frame);
        if (ret != TD_SUCCESS) {
            printf("vpss_grp%d chn[%d] release_chn_frame ret = 0x%x failed!\n", mppChn.dev_id, mppChn.chn_id, ret);
        }
        ss_mpi_sys_get_cur_pts(&spi_send_time_end);
        spi_timeCnt = spi_timeCnt + 1;
        td_u64 time_diff = spi_send_time_end - spi_send_time_start;
        td_u64 every_frame_cost;
        if ((spi_timeCnt % 50) == 0) { /* * 50 */
            every_frame_cost = time_diff / spi_timeCnt;
            sample_print("sum cost %llu, spi_timeCnt:%llu, every frame cost %llu, average frame rate %llufps\n",
                time_diff, spi_timeCnt, every_frame_cost, 1000000 / every_frame_cost); /* 1000000 1s */
        }
    }
    printf("%s:%d spi_Frame_proc finish\n", __FUNCTION__, __LINE__);

    return TD_NULL;
}

static td_void sample_stop_spi_frame_proc()
{
    g_thread_stop_flag = 1;

    (td_void)pthread_join(g_spiThread, TD_NULL);
}

static td_void sample_spi_lcd_vpss_get_wrap_cfg(sampe_sys_cfg *g_spi_lcd_sys_cfg, sample_vpss_cfg *vpss_cfg)
{
    td_u32 full_lines_std;
    if (g_spi_lcd_sys_cfg->vpss_wrap_en) {
        if (SENSOR0_TYPE == SC4336P_MIPI_4M_30FPS_10BIT || SENSOR0_TYPE == OS04D10_MIPI_4M_30FPS_10BIT ||
            SENSOR0_TYPE == GC4023_MIPI_4M_30FPS_10BIT || SENSOR0_TYPE == SC431HAI_MIPI_4M_30FPS_10BIT ||
            SENSOR0_TYPE == SC431HAI_MIPI_4M_30FPS_10BIT_WDR2TO1 || SENSOR0_TYPE == SC4336P_MIPI_3M_30FPS_10BIT) {
            full_lines_std = 1500; /* full_lines_std: 1500 */
        } else if (SENSOR0_TYPE == SC450AI_MIPI_4M_30FPS_10BIT || SENSOR0_TYPE == SC450AI_MIPI_4M_30FPS_10BIT_WDR2TO1) {
            full_lines_std = 1585; /* full_lines_std: 1585 */
        } else if (SENSOR0_TYPE == SC500AI_MIPI_5M_30FPS_10BIT || SENSOR0_TYPE == SC500AI_MIPI_5M_30FPS_10BIT_WDR2TO1) {
            full_lines_std = 1700; /* full_lines_std: 1700 */
        } else {
            g_spi_lcd_sys_cfg->vpss_wrap_en = TD_FALSE;
            vpss_cfg->wrap_attr[0].enable = TD_FALSE;
            return;
        }
        (td_void)sample_comm_vpss_get_wrap_cfg(vpss_cfg->chn_attr, g_spi_lcd_sys_cfg->mode_type, full_lines_std,
            &vpss_cfg->wrap_attr[0]);
        g_spi_lcd_sys_cfg->vpss_wrap_size = vpss_cfg->wrap_attr[0].buf_size;
    }
}

static td_void sample_spi_lcd_stop_route(sample_vi_cfg *vi_cfg, td_s32 route_num)
{
    td_s32 i;
    ot_vpss_grp vpss_grp[SAMPLE_VIE_MAX_ROUTE_NUM] = {0, 1, 2, 3};

    sample_spi_lcd_stop_and_unbind_venc(vpss_grp, SAMPLE_VIE_MAX_ROUTE_NUM, route_num);
    for (i = route_num - 1; i >= 0; i--) {
        sample_spi_lcd_stop_vpss(vpss_grp[i]);
        sample_comm_vi_un_bind_vpss(i, 0, vpss_grp[i], 0);
        sample_comm_vi_stop_vi(&vi_cfg[i]);
    }
    sample_spi_release_vb(&g_rgb888_vb_info);
    release_rgb565_vb();
    sample_comm_sys_exit();
}

static td_s32 sample_spi_lcd_sensor_in(td_void)
{
    sample_vi_cfg vi_cfg[1];
    sample_vpss_cfg vpss_cfg;
    sample_sns_type sns_type = SENSOR0_TYPE;

    sample_spi_lcd_get_vi_vpss_mode(TD_FALSE);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg[0]);
    sample_spi_lcd_get_pipe_wrap_line(vi_cfg, 1);
    sample_comm_vpss_get_default_vpss_cfg(&vpss_cfg, g_spi_lcd_sys_cfg.vpss_wrap_en);
    if (g_spi_lcd_sys_cfg.vpss_wrap_en) {
        vpss_cfg.chn_attr[1].width = VDEC_WIDTH;
        vpss_cfg.chn_attr[1].height = VDEC_HEIGHT;
        vpss_cfg.chn_attr[1].pixel_format = OT_PIXEL_FORMAT_YUV_SEMIPLANAR_420;
        vpss_cfg.chn_attr[1].depth = 1;
    }
    sample_spi_lcd_vpss_get_wrap_cfg(&g_spi_lcd_sys_cfg, &vpss_cfg);

    if (sample_spi_lcd_start_route(vi_cfg, &vpss_cfg, g_spi_lcd_sys_cfg.route_num) != TD_SUCCESS) {
        return TD_FAILURE;
    }
    if (SENSOR0_TYPE == SC4336P_MIPI_4M_30FPS_10BIT) {
        td_s32 ret;
        ot_mpp_chn mppChn = { OT_ID_VPSS, 0, 1 };
        ret = pthread_create(&g_spiThread, 0, sample_spi_frame_proc, &mppChn);
        if (ret != TD_SUCCESS) {
            printf("%s:%d  spi_Frame_proc ptread create error\n", __FUNCTION__, __LINE__);
        }
        sample_print("Saving video stream .....\n");
        sample_get_char();
        sample_stop_spi_frame_proc();
    } else {
        sample_get_char();
    }

    sample_spi_lcd_stop_route(vi_cfg, g_spi_lcd_sys_cfg.route_num);
    return TD_SUCCESS;
}

static td_void *send_stream_thread(td_void *param)
{
    FILE *file = TD_NULL;
    size_t data_size;
    td_bool *send_stream_running = (td_bool *)param;

    /* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
    td_s32 buff_size = INBUF_SIZE;
    td_u8 *inbuf = malloc(buff_size);

    file = fopen(g_input_file, "rb");
    if (!file) {
        sample_print("open file %s failed \n", g_input_file);
        return TD_NULL;
    }

    while (*send_stream_running && g_sig_flag == 0 && !feof(file)) {
        /* read raw data from the input file */
        data_size = fread(inbuf, 1, INBUF_SIZE, file);
        if (!data_size) {
            break;
        }

        /* use the parser to split the data into frames */
        av_vdec_stream stream;
        stream.pts = 0;
        stream.addr = inbuf;
        stream.len = data_size;
        stream.end_of_frame = TD_TRUE;
        stream.end_of_stream = 0;
        stream.display = 1;

        td_bool send_frame = TD_TRUE;
        while (send_frame && g_sig_flag == 0) {
            td_s32 ret = av_vdec_send_stream(g_vdec_hdl, &stream);
            if (ret != TD_SUCCESS) {
                send_frame = TD_TRUE;
                usleep(20000); /** 20000 : wait time*/
            } else {
                send_frame = TD_FALSE;
            }
        }
        usleep(200000); /** 200000 : wait time*/
    }
    (td_void) fclose(file);
    return TD_NULL;
}

static td_s32 sample_sys_init(td_void)
{
    ot_vb_cfg vb_cfg;
    td_s32 ret;

    (td_void)memset_s(&vb_cfg, sizeof(ot_vb_cfg), 0, sizeof(ot_vb_cfg));
    vb_cfg.max_pool_cnt = 1;
    vb_cfg.common_pool[0].remap_mode = OT_VB_REMAP_MODE_NONE;
    vb_cfg.common_pool[0].blk_cnt = 15; /** 15 blk num */
    vb_cfg.common_pool[0].blk_size = VDEC_WIDTH * VDEC_HEIGHT * 4; /** 4 : blk size multiple*/
    ret = sample_comm_sys_vb_init(&vb_cfg);
    if (ret != TD_SUCCESS) {
        sample_print("init sys fail for %#x!\n", ret);
        sample_comm_sys_exit();
        return ret;
    }

    return TD_SUCCESS;
}

static td_void *get_frame_thread(td_void *param)
{
    td_bool *read_stream_running = (td_bool *)param;
    while (*read_stream_running && g_sig_flag == 0) {
        ot_video_frame_info video_frame_info;
        if (memset_s(&video_frame_info, sizeof(video_frame_info), 0x00, sizeof(video_frame_info)) != EOK) {
            sample_print("set m_vid_frame failed\n");
        }
        td_s32 ret = av_vdec_get_frame(g_vdec_hdl, &video_frame_info);
        if (ret == TD_SUCCESS) {
            ret = sample_yuv_frame_proc(&video_frame_info);
            if (ret != TD_SUCCESS) {
                sample_print("yuv_frame_proc failed!\n");
            }
            ret = av_vdec_release_frame(g_vdec_hdl, &video_frame_info);
            if (ret != TD_SUCCESS) {
                sample_print("av_vdec_release_frame00 failed \n");
            }
        }
        usleep(20000); /** 20000 : allow cpu time */
    }
    return TD_NULL;
}

static td_void sample_spi_lcd_stream_in_process(td_void)
{
    td_s32 ret;

    ret = pthread_create(&g_get_frame_thread_id, TD_NULL, get_frame_thread, &g_read_stream_running);
    if (ret != TD_SUCCESS) {
        return;
    }

    ret = pthread_create(&g_send_stream_thread_id, TD_NULL, send_stream_thread, &g_send_stream_running);
    if (ret != TD_SUCCESS) {
        g_read_stream_running = TD_FALSE;
        pthread_join(g_get_frame_thread_id, 0);
        return;
    }

    sample_get_char();

    g_read_stream_running = TD_FALSE;
    g_send_stream_running = TD_FALSE;
    pthread_join(g_send_stream_thread_id, 0);
    pthread_join(g_get_frame_thread_id, 0);
}

static td_s32 sample_spi_lcd_stream_in(td_void)
{
    td_s32 ret = TD_SUCCESS;

    ret = sample_sys_init();
    if (ret != TD_SUCCESS) {
        return TD_FAILURE;
    }

    av_log_set_level(AV_LOG_ERROR);
    ret = av_vdec_init();
    if (ret != TD_SUCCESS) {
        sample_print("av_vdec_init failed \n");
        goto exit;
    }

    av_vdec_attr vdec_attr;
    vdec_attr.buf_size = VDEC_WIDTH * VDEC_HEIGHT * 4; /** 4 : buff multiples*/
    ot_payload_type vdec_type = g_decode_type == 0 ? OT_PT_H264 : OT_PT_H265;
    ret = av_vdec_create(&g_vdec_hdl, vdec_type, &vdec_attr);
    if (ret != TD_SUCCESS) {
        sample_print("av_vdec_create failed \n");
        goto deinit;
    }

    ret = av_vdec_start(g_vdec_hdl);
    if (ret != TD_SUCCESS) {
        sample_print("av_vdec_start failed \n");
        goto destroy;
    }

    sample_spi_lcd_stream_in_process();

    ret = av_vdec_stop(g_vdec_hdl);
    if (ret != TD_SUCCESS) {
        sample_print("av_vdec_stop failed \n");
    }
destroy:
    ret = av_vdec_destroy(g_vdec_hdl);
    if (ret != TD_SUCCESS) {
        sample_print("av_vdec_destroy failed \n");
    }
deinit:
    ret = av_vdec_deinit();
    if (ret != TD_SUCCESS) {
        sample_print("av_vdec_deinit failed \n");
    }
exit:
    sample_spi_release_vb(&g_rgb888_vb_info);
    release_rgb565_vb();
    sample_comm_sys_exit();
    return 0;
}

static td_void sample_spi_lcd_usage(const char *prg_name)
{
    printf("usage : %s <index> \n", prg_name);
    printf("index:\n");
    printf("    (0) sensor in  -> spi lcd            \n");
    printf("    (1) stream in  -> spi lcd            \n");
}

static td_s32 sample_spi_lcd_execute_case(td_u32 case_index)
{
    td_s32 ret;

    switch (case_index) {
        case 0:
            ret = sample_spi_lcd_sensor_in();
            break;
        case 1:
            ret = sample_spi_lcd_stream_in();
            break;
        default:
            ret = TD_FAILURE;
            break;
    }

    return ret;
}

#ifdef __LITEOS__
td_s32 app_main(td_s32 argc, td_char *argv[])
#else
td_s32 main(td_s32 argc, td_char *argv[])
#endif
{
    td_s32 ret;
    td_u32 index;
    td_char *end_ptr = TD_NULL;

    if (argc != 2) { /* 2:arg num */
        sample_spi_lcd_usage(argv[0]);
        return TD_FAILURE;
    }

    if (!strncmp(argv[1], "-h", 2)) { /* 2:arg num */
        sample_spi_lcd_usage(argv[0]);
        return TD_FAILURE;
    }

    if (strlen(argv[1]) >= 2 || strlen(argv[1]) == 0 || !check_digit(argv[1][0])) { /* 2:arg len */
        sample_spi_lcd_usage(argv[0]);
        return TD_FAILURE;
    }

    index = (td_u32)strtol(argv[1], &end_ptr, 2); /* 2:arg len */
    if ((end_ptr == argv[1]) || (*end_ptr) != '\0') {
        sample_spi_lcd_usage(argv[0]);
        return TD_FAILURE;
    }

#ifndef __LITEOS__
    sample_register_sig_handler(sample_vie_handle_sig);
#endif

    ret = sample_spi_lcd_execute_case(index);
    if ((ret == TD_SUCCESS) && (g_sig_flag == 0)) {
        printf("\033[0;32mprogram exit normally!\033[0;39m\n");
    } else {
        printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
    }

    return ret;
}
