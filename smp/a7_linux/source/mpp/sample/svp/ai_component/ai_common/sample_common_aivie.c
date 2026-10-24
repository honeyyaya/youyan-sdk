/*
  Copyright (c), 2024-2024, Shenshu Tech. Co., Ltd.
 */

#include "sample_common_aivie.h"
#include "sample_ipc.h"
#include "sample_common_ai.h"

#define OT_SAMPLE_AI_VI_GRP         0
#define OT_SAMPLE_AI_VPSS_GRP       0
#define OT_SAMPLE_AI_CHN_NUM        2

static sample_vb_param g_vb_param = {
    /* original stream, ai stream, not used */
    .vb_size = {{ 2688, 1520 },
                { 1920, 1080 },
                { 720,  480 }},
    .pixel_format = { OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420, OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420,
        OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420 },
    .compress_mode = { OT_COMPRESS_MODE_NONE, OT_COMPRESS_MODE_NONE,
        OT_COMPRESS_MODE_NONE },
    .video_format = { OT_VIDEO_FORMAT_LINEAR, OT_VIDEO_FORMAT_LINEAR,
        OT_VIDEO_FORMAT_LINEAR },
    .blk_num = { 5, 5, 0 }
};

static sampe_sys_cfg g_sys_cfg = {
    .route_num = 1,
    .mode_type = OT_VI_ONLINE_VPSS_ONLINE,
    .vpss_wrap_en = TD_FALSE,
};

static sample_vpss_cfg g_vpss_cfg = {
    .chn_en = { TD_TRUE, TD_TRUE, TD_FALSE },
};

static sample_comm_venc_chn_param g_default_venc_chn_param = {
    .frame_rate           = 30, /* 30: frame rate */
    .stats_time           = 2,  /* 2 is a number */
    .gop                  = 60, /* 60 is a number */
    .venc_size            = { 1920, 1080 },
    .size                 = -1,
    .profile              = 0,
    .is_rcn_ref_share_buf = TD_FALSE,
    .gop_attr             = {
        .gop_mode = OT_VENC_GOP_MODE_NORMAL_P,
        .normal_p = { 2 }, /* 2:diff value between I frame and P frame */
    },
    .type                 = OT_PT_H265,
    .rc_mode              = SAMPLE_RC_CBR,
};

static td_s32 sample_common_ai_sys_init(td_void)
{
    ot_vb_cfg vb_cfg;
    td_u32 supplement_config = OT_VB_SUPPLEMENT_BNR_MOT_MASK | OT_VB_SUPPLEMENT_MOTION_DATA_MASK;

    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &g_vb_param.vb_size[0]);
    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &g_vb_param.vb_size[1]);
    sample_comm_sys_get_default_vb_cfg(&g_vb_param, &vb_cfg);
    td_s32 ret = sample_comm_sys_init_with_vb_supplement(&vb_cfg, supplement_config);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, ret, "sample_comm_sys_init_with_vb_supplement failed!");

    ret = sample_comm_vi_set_vi_vpss_mode(g_sys_cfg.mode_type, OT_VI_AIISP_MODE_DEFAULT);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, sys_exit, "sample_comm_vi_set_vi_vpss_mode failed!");
    return TD_SUCCESS;
sys_exit:
    sample_comm_sys_exit();
    return TD_FAILURE;
}

static td_s32 sample_common_ai_start_and_bind_vpss(sample_vpss_cfg *vpss_cfg)
{
    sample_vpss_chn_attr vpss_chn_attr = { 0 };
    (td_void)memcpy_s(&vpss_chn_attr.chn_attr[0], sizeof(ot_vpss_chn_attr) * OT_VPSS_MAX_PHYS_CHN_NUM,
        vpss_cfg->chn_attr, sizeof(ot_vpss_chn_attr) * OT_VPSS_MAX_PHYS_CHN_NUM);
    (td_void)memcpy_s(vpss_chn_attr.chn_enable, sizeof(vpss_chn_attr.chn_enable),
        vpss_cfg->chn_en, sizeof(vpss_chn_attr.chn_enable));
    (td_void)memcpy_s(&vpss_chn_attr.wrap_attr[0], sizeof(ot_vpss_chn_buf_wrap_attr) * OT_VPSS_MAX_PHYS_CHN_NUM,
        vpss_cfg->wrap_attr, sizeof(ot_vpss_chn_buf_wrap_attr) * OT_VPSS_MAX_PHYS_CHN_NUM);
    vpss_chn_attr.chn_array_size = OT_VPSS_MAX_PHYS_CHN_NUM;
    td_s32 ret = sample_common_vpss_start(OT_SAMPLE_AI_VPSS_GRP, &vpss_cfg->grp_attr, &vpss_chn_attr);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, ret, "sample_common_vpss_start failed!");

    ret = sample_comm_vi_bind_vpss(OT_SAMPLE_AI_VI_GRP, 0, OT_SAMPLE_AI_VPSS_GRP, 0);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, bind_vi_failed, "sample_comm_vi_bind_vpss failed!");
    return TD_SUCCESS;

bind_vi_failed:
    sample_common_vpss_stop(OT_SAMPLE_AI_VPSS_GRP, g_vpss_cfg.chn_en, OT_VPSS_MAX_PHYS_CHN_NUM);
    return TD_FAILURE;
}

static td_void sample_common_ai_stop_and_unbind_vpss()
{
    sample_comm_vi_un_bind_vpss(OT_SAMPLE_AI_VI_GRP, 0, OT_SAMPLE_AI_VPSS_GRP, 0);
    sample_common_vpss_stop(OT_SAMPLE_AI_VPSS_GRP, g_vpss_cfg.chn_en, OT_VPSS_MAX_PHYS_CHN_NUM);
}

static td_s32 sample_common_ai_start_and_bind_venc(sample_vpss_cfg *vpss_cfg)
{
    td_s32 i;
    td_s32 ret;
    ot_venc_chn venc_chn[OT_SAMPLE_AI_CHN_NUM];
    for (i = 0; i < OT_SAMPLE_AI_CHN_NUM; i++) {
        venc_chn[i] = (ot_venc_chn)i;

        sample_comm_venc_chn_param venc_chn_param;
        (td_void)memcpy_s(&venc_chn_param, sizeof(sample_comm_venc_chn_param),
            &g_default_venc_chn_param, sizeof(sample_comm_venc_chn_param));
        venc_chn_param.venc_size.width = vpss_cfg->chn_attr[i].width;
        venc_chn_param.venc_size.height = vpss_cfg->chn_attr[i].height;
        venc_chn_param.is_rcn_ref_share_buf = 1;

        ret = sample_comm_venc_start(venc_chn[i], &venc_chn_param);
        sample_common_ai_log_check_goto(ret != TD_SUCCESS, venc_start_failed, "sample_comm_venc_start failed!");
    }

    ret = sample_comm_venc_start_get_stream(venc_chn, OT_SAMPLE_AI_CHN_NUM);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, venc_start_failed, "sample_comm_venc_start_get_stream failed!");

    sample_comm_vpss_bind_venc(OT_SAMPLE_AI_VPSS_GRP, OT_SAMPLE_AI_CHN_ORIGINAL_STREAM,
        OT_SAMPLE_AI_CHN_ORIGINAL_STREAM);

    return TD_SUCCESS;

venc_start_failed:
    for (i = i - 1; i >= 0; i--) {
        sample_comm_venc_stop(venc_chn[i]);
    }

    return TD_FAILURE;
}

static td_void sample_common_ai_stop_and_unbind_venc()
{
    sample_comm_vpss_un_bind_venc(OT_SAMPLE_AI_VPSS_GRP, OT_SAMPLE_AI_CHN_ORIGINAL_STREAM,
        OT_SAMPLE_AI_CHN_ORIGINAL_STREAM);
    sample_comm_venc_stop_get_stream(OT_SAMPLE_AI_CHN_NUM);

    ot_venc_chn venc_chn[OT_SAMPLE_AI_CHN_NUM];
    for (td_s32 i = 0; i < OT_SAMPLE_AI_CHN_NUM; i++) {
        venc_chn[i] = (ot_venc_chn)i;
        sample_comm_venc_stop(venc_chn[i]);
    }
}

td_s32 sample_common_aivie_init(sample_vi_cfg *vi_cfg, sample_vpss_cfg *vpss_cfg)
{
    td_s32 ret = sample_common_ai_sys_init();
    sample_common_ai_log_check_return(ret != TD_SUCCESS, ret, "sample_common_ai_sys_init failed!");

    ret = sample_comm_vi_start_vi(vi_cfg);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, start_vi_failed, "sample_comm_vi_start_vi failed!");

    ret = sample_common_ai_start_and_bind_vpss(vpss_cfg);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, start_vpss_failed,
        "sample_common_ai_start_and_bind_vpss failed!");

    ret = sample_common_ai_start_and_bind_venc(vpss_cfg);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, start_venc_failed,
        "sample_common_ai_start_and_bind_venc failed!");

    return TD_SUCCESS;

start_venc_failed:
    sample_common_ai_stop_and_unbind_vpss();
start_vpss_failed:
    sample_comm_vi_stop_vi(vi_cfg);
start_vi_failed:
    sample_comm_sys_exit();
    return TD_FAILURE;
}

td_s32 sample_common_aivie_deinit(sample_vi_cfg *vi_cfg)
{
    sample_common_ai_stop_and_unbind_venc();
    sample_common_ai_stop_and_unbind_vpss();
    sample_comm_vi_stop_vi(vi_cfg);
    sample_comm_sys_exit();
    return TD_SUCCESS;
}

td_void sample_common_aivie_get_vpss_cfg(td_s32 vpss_chn, sample_vpss_cfg *vpss_cfg)
{
    ot_size in_size;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_comm_vi_get_size_by_sns_type(sns_type, &in_size);
    sample_comm_vpss_get_default_grp_attr(&vpss_cfg->grp_attr);
    sample_comm_vpss_get_default_3dnr_attr(&vpss_cfg->nr_attr);
    vpss_cfg->vpss_grp = 0;
    vpss_cfg->grp_attr.max_width = in_size.width;
    vpss_cfg->grp_attr.max_height = in_size.height;

    for (ot_vpss_chn chn = 0; chn < OT_VPSS_MAX_PHYS_CHN_NUM; chn++) {
        vpss_cfg->chn_en[chn] = g_vpss_cfg.chn_en[chn];
        sample_comm_vpss_get_default_chn_attr(&vpss_cfg->chn_attr[chn]);
        vpss_cfg->chn_attr[chn].compress_mode = OT_COMPRESS_MODE_NONE;
        vpss_cfg->chn_attr[chn].width = in_size.width;
        vpss_cfg->chn_attr[chn].height = in_size.height;
        if (chn == vpss_chn) {
            vpss_cfg->chn_attr[chn].depth = 1;
            vpss_cfg->chn_attr[chn].width = 1920; // 1920: default image width
            vpss_cfg->chn_attr[chn].height = 1080; // 1080: default image height
            vpss_cfg->chn_attr[chn].frame_rate.src_frame_rate = 30; // 30: default src frame rate
            vpss_cfg->chn_attr[chn].frame_rate.dst_frame_rate = 15; // 15: default dts frame rate
        }
    }
}