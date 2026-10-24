/*
  Copyright (c), 2001-2021, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "sensor_common.h"
#include "ot_mpi_isp.h"
#include "ot_mpi_ae.h"
#include "ot_mpi_awb.h"
#include "securec.h"
#include "vs4388_cmos_param.h"
#include "vs4388_cmos.h"

#define VS4388_ID                    0x4388
#define SENSOR_VS4388_WIDTH          2560
#define SENSOR_VS4388_HEIGHT         1440
#define VS4388_BLACK_LEVEL           1024 /*14bit*/
#define VS4388_RATIO                 64

#define higher_4bits(x) (((x) & 0xf0000) >> 16)
#define high_8bits(x) (((x) & 0xff00) >> 8)
#define low_8bits(x)  ((x) & 0x00ff)

/****************************************************************************
 * global variables                                                            *
 ****************************************************************************/
#define vs4388_sensor_set_ctx(dev, ctx)   ((g_vs4388_sns_state[dev]) = (ctx))
#define vs4388_sensor_reset_ctx(dev)      (g_vs4388_sns_state[dev] = TD_NULL)

static ot_isp_fswdr_mode g_fswdr_mode[OT_ISP_MAX_PIPE_NUM] = {
    [0 ... OT_ISP_MAX_PIPE_NUM - 1] = OT_ISP_FSWDR_NORMAL_MODE
};

static td_u32 g_max_time_get_cnt[OT_ISP_MAX_PIPE_NUM] = {0};
static td_u32 g_init_exposure[OT_ISP_MAX_PIPE_NUM]  = {0};
static td_u32 g_lines_per500ms[OT_ISP_MAX_PIPE_NUM] = {0};

static td_u16 g_init_wb_gain[OT_ISP_MAX_PIPE_NUM][OT_ISP_RGB_CHN_NUM] = {{0}};
static td_u16 g_sample_r_gain[OT_ISP_MAX_PIPE_NUM] = {0};
static td_u16 g_sample_b_gain[OT_ISP_MAX_PIPE_NUM] = {0};
static td_bool g_quick_start_en[OT_ISP_MAX_PIPE_NUM] = {TD_FALSE};

static td_bool g_ae_route_ex_valid[OT_ISP_MAX_PIPE_NUM] = {0};
static ot_isp_ae_route g_init_ae_route[OT_ISP_MAX_PIPE_NUM] = {{0}};
static ot_isp_ae_route_ex g_init_ae_route_ex[OT_ISP_MAX_PIPE_NUM] = {{0}};
static ot_isp_ae_route g_init_ae_route_sf[OT_ISP_MAX_PIPE_NUM] = {{0}};
static ot_isp_ae_route_ex g_init_ae_route_sf_ex[OT_ISP_MAX_PIPE_NUM] = {{0}};

typedef struct {
    td_u8       hcg;
    td_u32      brl;
    td_u32      rhs1_max;
    td_u32      rhs2_max;
} vs4388_state;

vs4388_state g_vs4388_state[OT_ISP_MAX_PIPE_NUM] = {{0}};
ot_isp_sns_commbus g_vs4388_bus_info[OT_ISP_MAX_PIPE_NUM] = {
    [0] = { .i2c_dev = 0},
    [1 ... OT_ISP_MAX_PIPE_NUM - 1] = { .i2c_dev = -1}
};

ot_isp_sns_state *g_vs4388_sns_state[OT_ISP_MAX_PIPE_NUM] = {TD_NULL};

static td_bool blc_clamp_info[OT_ISP_MAX_PIPE_NUM] = {[0 ...(OT_ISP_MAX_PIPE_NUM - 1)] = TD_TRUE};

ot_isp_sns_commbus *vs4388_get_bus_info(ot_vi_pipe vi_pipe)
{
    return &g_vs4388_bus_info[vi_pipe];
}

ot_isp_sns_state *vs4388_get_ctx(ot_vi_pipe vi_pipe)
{
    return g_vs4388_sns_state[vi_pipe];
}

extern const vs4388_video_mode_tbl g_vs4388_mode_tbl[6];

/****************************************************************************
 * local variables                                                            *
 ****************************************************************************/
/* Register Address */

#define VS4388_L3_ADDR_H (0x3500) 
#define VS4388_L3_ADDR_L (0x3501)
#define VS4388_L2_ADDR_H (0x3502) 
#define VS4388_L2_ADDR_L (0x3503)
#define VS4388_L1_ADDR_H (0x3504) 
#define VS4388_L1_ADDR_L (0x3505)
#define VS4388_LS_ADDR_H (0x3506) 
#define VS4388_LS_ADDR_L (0x3507)

#define VS4388_G3_ADDR_H 	(0x3508)  
#define VS4388_G3_ADDR_L 	(0x3509)
#define VS4388_G2_ADDR_H 	(0x350a)
#define VS4388_G2_ADDR_L 	(0x350b)
#define VS4388_G1_ADDR_H 	(0x350c)  
#define VS4388_G1_ADDR_L 	(0x350d)
#define VS4388_GS_ADDR_H 	(0x350e)
#define VS4388_GS_ADDR_L 	(0x350f)


#define VS4388_VTS_ADDR_H (0x3812)
#define VS4388_VTS_ADDR_L (0x3813)
#define VS4388_HTS_ADDR_H (0x3810)
#define VS4388_HTS_ADDR_L (0x3811)	


#define vs4388_err_mode_print(sensor_image_mode, sns_state) \
    do { \
        isp_err_trace("Not support! Width:%d, Height:%d, Fps:%f, WDRMode:%d\n", \
                      (sensor_image_mode)->width,  \
                      (sensor_image_mode)->height, \
                      (sensor_image_mode)->fps,    \
                      (sns_state)->wdr_mode); \
    } while (0)

static td_void cmos_get_ae_comm_default(ot_vi_pipe vi_pipe, ot_isp_ae_sensor_default *ae_sns_dft,
    const ot_isp_sns_state *sns_state)
{
    ae_sns_dft->full_lines_std = sns_state->fl_std;
    ae_sns_dft->flicker_freq = 50 * 256; /* light flicker freq: 50Hz, accuracy: 256 */
    ae_sns_dft->full_lines_max = g_vs4388_mode_tbl[sns_state->img_mode].max_ver_lines;
    ae_sns_dft->hmax_times = (1000000000) / (sns_state->fl_std * g_vs4388_mode_tbl[sns_state->img_mode].max_fps); /* 1000000000ns, 30fps */

    ae_sns_dft->again_accu.accu_type = OT_ISP_AE_ACCURACY_LINEAR;
    ae_sns_dft->again_accu.accuracy  = 1.0/16;

    ae_sns_dft->dgain_accu.accu_type = OT_ISP_AE_ACCURACY_LINEAR;
    ae_sns_dft->dgain_accu.accuracy = 1.0/256;

    ae_sns_dft->isp_dgain_shift = 8; /* accuracy: 8 */
    ae_sns_dft->min_isp_dgain_target = 1 << ae_sns_dft->isp_dgain_shift;
    ae_sns_dft->max_isp_dgain_target = 16 << ae_sns_dft->isp_dgain_shift; /* max 2 */
    if (g_lines_per500ms[vi_pipe] == 0) {
        ae_sns_dft->lines_per500ms = sns_state->fl_std * 30 / 2; /* 30fps, div 2 */
    } else {
        ae_sns_dft->lines_per500ms = g_lines_per500ms[vi_pipe];
    }
    (td_void)memcpy_s(&ae_sns_dft->piris_attr, sizeof(ot_isp_piris_attr), &g_piris, sizeof(ot_isp_piris_attr));
    ae_sns_dft->max_iris_fno = OT_ISP_IRIS_F_NO_1_0;
    ae_sns_dft->min_iris_fno = OT_ISP_IRIS_F_NO_32_0;

    ae_sns_dft->ae_route_ex_valid = TD_FALSE;
    ae_sns_dft->ae_route_attr.total_num = 0;
    ae_sns_dft->ae_route_attr_ex.total_num = 0;
    ae_sns_dft->quick_start.quick_start_enable = g_quick_start_en[vi_pipe];
    ae_sns_dft->quick_start.black_frame_num = 0;

    return;
}

static td_void cmos_get_ae_linear_default(ot_vi_pipe vi_pipe, ot_isp_ae_sensor_default *ae_sns_dft,
    const ot_isp_sns_state *sns_state)
{
    ae_sns_dft->int_time_accu.accu_type = OT_ISP_AE_ACCURACY_LINEAR;
    ae_sns_dft->int_time_accu.accuracy = 2; /* accuracy 2 */
    ae_sns_dft->int_time_accu.offset = 0;
    ae_sns_dft->max_int_time = sns_state->fl_std - 2; /* sub 2 */
    ae_sns_dft->min_int_time = 1; /* min_int_time 6 */
    ae_sns_dft->max_int_time_target = 65535; /* max 65535 */
    ae_sns_dft->min_int_time_target = 1; /* min 1 */

    ae_sns_dft->max_again = 0x10*64; /* 16+4x */
    ae_sns_dft->min_again = 0x10; /* 1x */
    ae_sns_dft->max_again_target = ae_sns_dft->max_again;
    ae_sns_dft->min_again_target = ae_sns_dft->min_again;

    ae_sns_dft->max_dgain = 0x100; /* 8x */
    ae_sns_dft->min_dgain = 0x100; /* 1x */
    ae_sns_dft->max_dgain_target = ae_sns_dft->max_dgain;
    ae_sns_dft->min_dgain_target = ae_sns_dft->min_dgain;

    ae_sns_dft->isp_dgain_shift = 8; /* accuracy: 8 */
    ae_sns_dft->min_isp_dgain_target = 1 << ae_sns_dft->isp_dgain_shift;
    ae_sns_dft->max_isp_dgain_target = 16 << ae_sns_dft->isp_dgain_shift; /* max 255 */

    ae_sns_dft->ae_compensation = 0x38;
    ae_sns_dft->ae_exp_mode = OT_ISP_AE_EXP_HIGHLIGHT_PRIOR;
    ae_sns_dft->init_exposure = g_init_exposure[vi_pipe] ? g_init_exposure[vi_pipe] : 76151; /* init 76151 */

    ae_sns_dft->ae_route_ex_valid = g_ae_route_ex_valid[vi_pipe];
    (td_void)memcpy_s(&ae_sns_dft->ae_route_attr, sizeof(ot_isp_ae_route),
                      &g_init_ae_route[vi_pipe],  sizeof(ot_isp_ae_route));
    (td_void)memcpy_s(&ae_sns_dft->ae_route_attr_ex, sizeof(ot_isp_ae_route_ex),
                      &g_init_ae_route_ex[vi_pipe], sizeof(ot_isp_ae_route_ex));
    return;
}

static td_void cmos_get_ae_wdr_default(ot_vi_pipe vi_pipe, ot_isp_ae_sensor_default *ae_sns_dft,
    const ot_isp_sns_state *sns_state)
{
    ae_sns_dft->int_time_accu.accu_type = OT_ISP_AE_ACCURACY_LINEAR;
    ae_sns_dft->int_time_accu.accuracy = 2; /* accuracy 6 */
    ae_sns_dft->int_time_accu.offset = 0;

    ae_sns_dft->max_int_time = sns_state->fl_std - 6; /* sub 2 */
    ae_sns_dft->min_int_time = 2; /* min_int_time 6 */

    ae_sns_dft->max_int_time_target = 65535; /* max 65535 */
    ae_sns_dft->min_int_time_target = 1;

    ae_sns_dft->max_again = 0x10*64; /* 16x */
    ae_sns_dft->min_again = 0x10; /* 1x */
    ae_sns_dft->max_again_target = ae_sns_dft->max_again;
    ae_sns_dft->min_again_target = ae_sns_dft->min_again;

    ae_sns_dft->max_dgain = 0x100; /* 8x */
    ae_sns_dft->min_dgain = 0x100; /* 1x */
    ae_sns_dft->max_dgain_target = ae_sns_dft->max_dgain;
    ae_sns_dft->min_dgain_target = ae_sns_dft->min_dgain;

    ae_sns_dft->isp_dgain_shift = 8; /* accuracy: 8 */
    ae_sns_dft->min_isp_dgain_target = 1 << ae_sns_dft->isp_dgain_shift;
    ae_sns_dft->max_isp_dgain_target = 16 << ae_sns_dft->isp_dgain_shift; /* max 255 */

    ae_sns_dft->init_exposure = g_init_exposure[vi_pipe] ? g_init_exposure[vi_pipe] : 52000; /* init 52000 */

    if (g_fswdr_mode[vi_pipe] == OT_ISP_FSWDR_LONG_FRAME_MODE) {
        ae_sns_dft->ae_compensation = 64; /* ae_compensation 64 */
        ae_sns_dft->ae_exp_mode = OT_ISP_AE_EXP_HIGHLIGHT_PRIOR;
    } else {
        ae_sns_dft->ae_compensation = 32; /* ae_compensation 32 */
        ae_sns_dft->ae_exp_mode = OT_ISP_AE_EXP_LOWLIGHT_PRIOR;
        ae_sns_dft->man_ratio_enable = TD_TRUE;
        ae_sns_dft->arr_ratio[0] = 0x400;
        ae_sns_dft->arr_ratio[1] = 0x200;
        ae_sns_dft->arr_ratio[2] = 0x100; /* array index 2 */
    }
    ae_sns_dft->ae_route_ex_valid = g_ae_route_ex_valid[vi_pipe];
    (td_void)memcpy_s(&ae_sns_dft->ae_route_attr, sizeof(ot_isp_ae_route),
                      &g_init_ae_route[vi_pipe],  sizeof(ot_isp_ae_route));
    (td_void)memcpy_s(&ae_sns_dft->ae_route_attr_ex, sizeof(ot_isp_ae_route_ex),
                      &g_init_ae_route_ex[vi_pipe],  sizeof(ot_isp_ae_route_ex));
    (td_void)memcpy_s(&ae_sns_dft->ae_route_sf_attr, sizeof(ot_isp_ae_route),
                      &g_init_ae_route_sf[vi_pipe], sizeof(ot_isp_ae_route));
    (td_void)memcpy_s(&ae_sns_dft->ae_route_sf_attr_ex, sizeof(ot_isp_ae_route_ex),
                      &g_init_ae_route_sf_ex[vi_pipe],  sizeof(ot_isp_ae_route_ex));
    return;
}

static td_s32 cmos_get_ae_default(ot_vi_pipe vi_pipe, ot_isp_ae_sensor_default *ae_sns_dft)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pointer_return(ae_sns_dft);
    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_return(sns_state);

    (td_void)memset_s(&ae_sns_dft->ae_route_attr, sizeof(ot_isp_ae_route), 0, sizeof(ot_isp_ae_route));

    cmos_get_ae_comm_default(vi_pipe, ae_sns_dft, sns_state);

    switch (sns_state->wdr_mode) {
        case OT_WDR_MODE_NONE:   /* linear mode */
            cmos_get_ae_linear_default(vi_pipe, ae_sns_dft, sns_state);
            break;

        case OT_WDR_MODE_2To1_LINE:
        case OT_WDR_MODE_3To1_LINE:
            cmos_get_ae_wdr_default(vi_pipe, ae_sns_dft, sns_state);
            break;

        default:
            cmos_get_ae_linear_default(vi_pipe, ae_sns_dft, sns_state);
            break;
    }

    return TD_SUCCESS;
}

static td_void cmos_config_vmax(ot_isp_sns_state *sns_state, td_u32 vmax)
{
    sns_state->regs_info[0].i2c_data[5].data = low_8bits(vmax); /* array index 5 */
    sns_state->regs_info[0].i2c_data[4].data = high_8bits(vmax); /* array index 6 */

    return;
}



static td_void cmos_fps_set(ot_vi_pipe vi_pipe, td_float fps, ot_isp_ae_sensor_default *ae_sns_dft)
{
    td_u32 lines, lines_max, vmax;
   // td_bool achieve_fps_flag;
    td_float max_fps, min_fps;
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pointer_void_return(ae_sns_dft);
    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    lines = g_vs4388_mode_tbl[sns_state->img_mode].ver_lines;
    lines_max = g_vs4388_mode_tbl[sns_state->img_mode].max_ver_lines;
    max_fps = g_vs4388_mode_tbl[sns_state->img_mode].max_fps;
    min_fps = g_vs4388_mode_tbl[sns_state->img_mode].min_fps;

    if ((fps > max_fps) || (fps < min_fps)) {
        isp_err_trace("Not support Fps: %f\n", fps);
        return;
    }
    
    vmax = lines * max_fps / div_0_to_1_float(fps);
    vmax = (vmax > lines_max) ? lines_max : vmax;
    if (sns_state->wdr_mode == OT_WDR_MODE_NONE) {
        
    } else {
          
    }
    cmos_config_vmax(sns_state, vmax);
    sns_state->fl_std = vmax;
    ae_sns_dft->lines_per500ms = lines * max_fps / 2; /* div 2 */
    ae_sns_dft->fps = fps;
    ae_sns_dft->full_lines_std = sns_state->fl_std;
    ae_sns_dft->max_int_time = sns_state->fl_std - 2;  /* sub 2 */
    sns_state->fl[0] = sns_state->fl_std;
    ae_sns_dft->full_lines = sns_state->fl[0];
    ae_sns_dft->hmax_times =
        (1000000000) / (sns_state->fl_std * div_0_to_1_float(fps)); /* 1000000000ns */
    return;
}

static td_void cmos_slow_framerate_set(ot_vi_pipe vi_pipe, td_u32 full_lines, ot_isp_ae_sensor_default *ae_sns_dft)
{
    td_u32 lines_max, vmax;
    //td_bool achieve_fps;
    ot_isp_sns_state *sns_state = TD_NULL;
    sns_check_pointer_void_return(ae_sns_dft);
    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    lines_max = g_vs4388_mode_tbl[sns_state->img_mode].max_ver_lines;
    vmax = full_lines;
    vmax = (vmax > lines_max) ? lines_max : vmax;

    cmos_config_vmax(sns_state, vmax);

    sns_state->fl[0] = vmax;
    ae_sns_dft->full_lines = sns_state->fl[0];
    ae_sns_dft->max_int_time = sns_state->fl[0] - 4; /* max_int_time: Flstd - 10 */

    return;
}

static td_void cmos_inttime_update_linear(ot_vi_pipe vi_pipe, td_u32 int_time)
{
    ot_isp_sns_state *sns_state = TD_NULL;
    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);
    sns_state->regs_info[0].i2c_data[1].data = low_8bits(int_time);
    sns_state->regs_info[0].i2c_data[0].data = high_8bits(int_time);
 
}

static td_void cmos_inttime_update_2to1_line(ot_vi_pipe vi_pipe, td_u32 int_time)
{
    ot_isp_sns_state *sns_state = TD_NULL;
    static td_u32 count[OT_ISP_MAX_PIPE_NUM] = {[0 ...(OT_ISP_MAX_PIPE_NUM - 1)] = 0};

    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    if (count[vi_pipe] == 0) { /*  short exposure */
        sns_state->wdr_int_time[0] = int_time;
        count[vi_pipe]++;
    } 
    else /* long exposure */
    {
        sns_state->wdr_int_time[1] = int_time;
        count[vi_pipe] = 0;

        sns_state->regs_info[0].i2c_data[7].data = low_8bits(sns_state->wdr_int_time[0]);
        sns_state->regs_info[0].i2c_data[6].data = high_8bits(sns_state->wdr_int_time[0]);

        sns_state->regs_info[0].i2c_data[1].data = low_8bits(sns_state->wdr_int_time[1]);
        sns_state->regs_info[0].i2c_data[0].data = high_8bits(sns_state->wdr_int_time[1]);
        
    }
    return;
}


static td_void cmos_inttime_update_3to1_line(ot_vi_pipe vi_pipe, td_u32 int_time)
{
    ot_isp_sns_state *sns_state = TD_NULL;
    static td_u32 count[OT_ISP_MAX_PIPE_NUM] = {[0 ...(OT_ISP_MAX_PIPE_NUM - 1)] = 0};

    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    if (count[vi_pipe] == 0) { /* short short exposure */
        sns_state->wdr_int_time[0] = int_time;
        count[vi_pipe]++;
    } else if (count[vi_pipe] == 1) { /* short exposure */
        sns_state->wdr_int_time[1] = int_time;
        count[vi_pipe]++;
    } else { /* long exposure */
        sns_state->wdr_int_time[2] = int_time; /* index 2 */
        count[vi_pipe] = 0;
        sns_state->regs_info[0].i2c_data[11].data = low_8bits(sns_state->wdr_int_time[0]);
        sns_state->regs_info[0].i2c_data[10].data = high_8bits(sns_state->wdr_int_time[0]);

        sns_state->regs_info[0].i2c_data[7].data = low_8bits(sns_state->wdr_int_time[1]);
        sns_state->regs_info[0].i2c_data[6].data = high_8bits(sns_state->wdr_int_time[1]);

        sns_state->regs_info[0].i2c_data[1].data = low_8bits(sns_state->wdr_int_time[2]/2); // pixel is 2:1:1
        sns_state->regs_info[0].i2c_data[0].data = high_8bits(sns_state->wdr_int_time[2]/2);
        
    }
    return;
}
static td_void cmos_inttime_update_4to1_line(ot_vi_pipe vi_pipe, td_u32 int_time)
{
    ot_isp_sns_state *sns_state = TD_NULL;
    static td_u32 count[OT_ISP_MAX_PIPE_NUM] = {[0 ...(OT_ISP_MAX_PIPE_NUM - 1)] = 0};

    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    if (count[vi_pipe] == 0) { /* short short exposure */
        sns_state->wdr_int_time[0] = int_time;
        count[vi_pipe]++;
    } else if (count[vi_pipe] == 1) { /* short exposure */
        sns_state->wdr_int_time[1] = int_time;
        count[vi_pipe]++;
    } else if (count[vi_pipe] == 2) { /* Mid exposure */
        sns_state->wdr_int_time[2] = int_time;
        count[vi_pipe]++;
    } 
    else { /* long exposure */
        sns_state->wdr_int_time[3] = int_time; 
        count[vi_pipe] = 0;

        sns_state->regs_info[0].i2c_data[15].data = low_8bits(sns_state->wdr_int_time[0]);
        sns_state->regs_info[0].i2c_data[14].data = high_8bits(sns_state->wdr_int_time[0]);

        sns_state->regs_info[0].i2c_data[11].data = low_8bits(sns_state->wdr_int_time[1]);
        sns_state->regs_info[0].i2c_data[10].data = high_8bits(sns_state->wdr_int_time[1]);

        sns_state->regs_info[0].i2c_data[7].data = low_8bits(sns_state->wdr_int_time[2]);
        sns_state->regs_info[0].i2c_data[6].data = high_8bits(sns_state->wdr_int_time[2]);

        sns_state->regs_info[0].i2c_data[1].data = low_8bits(sns_state->wdr_int_time[3]);
        sns_state->regs_info[0].i2c_data[0].data = high_8bits(sns_state->wdr_int_time[3]);
        
    }
    return;
}
/* while isp notify ae to update sensor regs, ae call these funcs. */
static td_void cmos_inttime_update(ot_vi_pipe vi_pipe, td_u32 int_time)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);
   
    if (sns_state->wdr_mode == OT_WDR_MODE_2To1_LINE) {
        cmos_inttime_update_2to1_line(vi_pipe, int_time);
    } 
    else if (sns_state->wdr_mode == OT_WDR_MODE_3To1_LINE) {
        cmos_inttime_update_3to1_line(vi_pipe, int_time);
    } 
    else if (sns_state->wdr_mode == OT_WDR_MODE_4To1_LINE) {
        cmos_inttime_update_4to1_line(vi_pipe, int_time);
    } 
    else {
        cmos_inttime_update_linear(vi_pipe, int_time);
    }

    return;
}


static td_void cmos_again_calc_table(ot_vi_pipe vi_pipe, td_u32 *again_lin, td_u32 *again_db)
{
    
    return;
}

static td_void cmos_dgain_calc_table(ot_vi_pipe vi_pipe, td_u32 *dgain_lin, td_u32 *dgain_db)
{
    
    return;
}

static td_void cmos_gains_update(ot_vi_pipe vi_pipe, td_u32 again, td_u32 dgain)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    if (sns_state->wdr_mode == OT_WDR_MODE_NONE) {
        sns_state->regs_info[0].i2c_data[3].data = low_8bits(again);
        sns_state->regs_info[0].i2c_data[2].data = high_8bits(again);
    } else if (sns_state->wdr_mode == OT_WDR_MODE_2To1_LINE) {
        sns_state->regs_info[0].i2c_data[3].data = low_8bits(again);
        sns_state->regs_info[0].i2c_data[2].data = high_8bits(again);

        sns_state->regs_info[0].i2c_data[9].data = low_8bits(again);
        sns_state->regs_info[0].i2c_data[8].data = high_8bits(again);
    }else if (sns_state->wdr_mode == OT_WDR_MODE_3To1_LINE) {
        sns_state->regs_info[0].i2c_data[3].data = low_8bits(again);
        sns_state->regs_info[0].i2c_data[2].data = high_8bits(again);

        sns_state->regs_info[0].i2c_data[9].data = low_8bits(again);
        sns_state->regs_info[0].i2c_data[8].data = high_8bits(again);

        sns_state->regs_info[0].i2c_data[13].data = low_8bits(again);
        sns_state->regs_info[0].i2c_data[12].data = high_8bits(again);
    } else if (sns_state->wdr_mode == OT_WDR_MODE_4To1_LINE) {
        sns_state->regs_info[0].i2c_data[3].data = low_8bits(again);
        sns_state->regs_info[0].i2c_data[2].data = high_8bits(again);

        sns_state->regs_info[0].i2c_data[9].data = low_8bits(again);
        sns_state->regs_info[0].i2c_data[8].data = high_8bits(again);

        sns_state->regs_info[0].i2c_data[13].data = low_8bits(again);
        sns_state->regs_info[0].i2c_data[12].data = high_8bits(again);

        sns_state->regs_info[0].i2c_data[17].data = low_8bits(again);
        sns_state->regs_info[0].i2c_data[16].data = high_8bits(again);
    }
    return;
}

static td_void cmos_get_inttime_max_2to1_line(ot_vi_pipe vi_pipe, td_u32 *ratio,
    ot_isp_ae_int_time_range *int_time, td_u32 *lf_max_int_time)
{
 
    td_u32 short_max;
    td_u32 short_max0;
    td_u32 short_time_min_limit;
    td_u32 long_time_max_limit;
    ot_isp_sns_state *sns_state = TD_NULL;

    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    short_time_min_limit = 2; /* short_time_min_limit 2 */
    long_time_max_limit = sns_state->fl[0] - 4 - short_time_min_limit;
    *lf_max_int_time = long_time_max_limit; 
    if (g_fswdr_mode[vi_pipe] == OT_ISP_FSWDR_LONG_FRAME_MODE) {
        int_time->int_time_max[0] = short_time_min_limit;
        int_time->int_time_min[0] = short_time_min_limit;
        int_time->int_time_max[1] = long_time_max_limit;
        int_time->int_time_min[1] = short_time_min_limit;
        return;
    } else {
        short_max0 = ((sns_state->fl[1] - 4 - sns_state->wdr_int_time[0]) * 0x40) / div_0_to_1(ratio[0]); /* sub 20 */
        short_max = ((sns_state->fl[0] - 4) * 0x40) / (ratio[0] + 0x40); /* sub 20 */
        short_max = short_max > (sns_state->fl[0] - 1440-28) ? (sns_state->fl[0] - 1440-28) : short_max;
        short_max = (short_max0 < short_max) ? short_max0 : short_max;
        short_max = (short_max == 0) ? 1 : short_max;
    }
    *lf_max_int_time = sns_state->fl[0] - 20; /* sub 20 */

    if (short_max >= short_time_min_limit) {
        int_time->int_time_max[0] = short_max;
        int_time->int_time_max[1] = (int_time->int_time_max[0] * ratio[0]) >> 6; /* shift 6 */
        int_time->int_time_min[0] = short_time_min_limit;
        int_time->int_time_min[1] = (int_time->int_time_min[0] * ratio[0]) >> 6; /* shift 6 */
    } else {
        short_max = short_time_min_limit;

        int_time->int_time_max[0] = short_max;
        int_time->int_time_max[1] = (int_time->int_time_max[0] * 0xFFF) >> 6; /* shift 6 */
        int_time->int_time_min[0] = int_time->int_time_max[0];
        int_time->int_time_min[1] = int_time->int_time_max[1];
    }


    return;
}
static td_void cmos_get_inttime_max_3to1_line(ot_vi_pipe vi_pipe, td_u32 *ratio,
    ot_isp_ae_int_time_range *int_time, td_u32 *lf_max_int_time)
{
 
   // td_u32 short_max;
    td_u32 short_time_min_limit;
    td_u32 long_time_max_limit;
    ot_isp_sns_state *sns_state = TD_NULL;

    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    short_time_min_limit = 2; /* short_time_min_limit 2 */
    long_time_max_limit = sns_state->fl[0] - 6;
    *lf_max_int_time = long_time_max_limit; 
    if (g_fswdr_mode[vi_pipe] == OT_ISP_FSWDR_LONG_FRAME_MODE) {
        int_time->int_time_max[0] = short_time_min_limit;
        int_time->int_time_min[0] = short_time_min_limit;
        int_time->int_time_max[1] = short_time_min_limit;
        int_time->int_time_min[1] = short_time_min_limit;
        int_time->int_time_max[2] = long_time_max_limit;
        int_time->int_time_min[2] = short_time_min_limit;
        return;
    } else {
        
        int_time->int_time_min[0] = short_time_min_limit;
        int_time->int_time_min[1] = (int_time->int_time_min[0]*ratio[0]) >> 6;
        int_time->int_time_min[2] = (int_time->int_time_min[1]*ratio[1]) >> 6;

        int_time->int_time_max[2] = long_time_max_limit;
        int_time->int_time_max[1] = (int_time->int_time_max[2]* 0x40) / div_0_to_1(ratio[1]);;
        int_time->int_time_max[0] = (int_time->int_time_max[1]* 0x40) / div_0_to_1(ratio[0]);

    }

    return;
}
static td_void cmos_get_inttime_max_4to1_line(ot_vi_pipe vi_pipe, td_u32 *ratio,
    ot_isp_ae_int_time_range *int_time, td_u32 *lf_max_int_time)
{
 
   // td_u32 short_max;
    td_u32 short_time_min_limit;
    td_u32 long_time_max_limit;
    ot_isp_sns_state *sns_state = TD_NULL;

    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    short_time_min_limit = 2; /* short_time_min_limit 2 */
    long_time_max_limit = sns_state->fl[0] - 6;
    *lf_max_int_time = long_time_max_limit; 
    if (g_fswdr_mode[vi_pipe] == OT_ISP_FSWDR_LONG_FRAME_MODE) {
        int_time->int_time_max[0] = short_time_min_limit;
        int_time->int_time_min[0] = short_time_min_limit;
        int_time->int_time_max[1] = short_time_min_limit;
        int_time->int_time_min[1] = short_time_min_limit;
        int_time->int_time_max[2] = short_time_min_limit;
        int_time->int_time_min[2] = short_time_min_limit;
        int_time->int_time_max[3] = long_time_max_limit;
        int_time->int_time_min[3] = short_time_min_limit;
        return;
    } else {
        
        int_time->int_time_min[0] = short_time_min_limit;
        int_time->int_time_min[1] = (int_time->int_time_min[0]*ratio[0]) >> 6;
        int_time->int_time_min[2] = (int_time->int_time_min[1]*ratio[1]) >> 6;
        int_time->int_time_min[3] = (int_time->int_time_min[2]*ratio[2]) >> 6;

        int_time->int_time_max[3] = long_time_max_limit;
        int_time->int_time_max[2] = (int_time->int_time_max[3]* 0x40) / div_0_to_1(ratio[1]);
        int_time->int_time_max[1] = (int_time->int_time_max[2]* 0x40) / div_0_to_1(ratio[1]);
        int_time->int_time_max[0] = (int_time->int_time_max[1]* 0x40) / div_0_to_1(ratio[0]);

    }

    return;
}
static td_void cmos_get_inttime_max(ot_vi_pipe vi_pipe, td_u16 man_ratio_enable, td_u32 *ratio,
    ot_isp_ae_int_time_range *int_time, td_u32 *lf_max_int_time)
{
    ot_unused(man_ratio_enable);
    ot_isp_sns_state *sns_state = TD_NULL;
    sns_check_pointer_void_return(ratio);
    sns_check_pointer_void_return(int_time);
    sns_check_pointer_void_return(lf_max_int_time);
    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    switch (sns_state->wdr_mode) {
        case OT_WDR_MODE_2To1_LINE:
            cmos_get_inttime_max_2to1_line(vi_pipe, ratio, int_time, lf_max_int_time);
            break;
        case OT_WDR_MODE_3To1_LINE:
            cmos_get_inttime_max_3to1_line(vi_pipe, ratio, int_time, lf_max_int_time);
            break;
        case OT_WDR_MODE_4To1_LINE:
            cmos_get_inttime_max_4to1_line(vi_pipe, ratio, int_time, lf_max_int_time);
            break;
        default:
            break;
    }

    return;
}

/* Only used in LINE_WDR mode */
static td_void cmos_ae_fswdr_attr_set(ot_vi_pipe vi_pipe, ot_isp_ae_fswdr_attr *ae_fswdr_attr)
{
    sns_check_pointer_void_return(ae_fswdr_attr);

    g_fswdr_mode[vi_pipe] = ae_fswdr_attr->fswdr_mode;
    g_max_time_get_cnt[vi_pipe] = 0;
    return;
}

static td_s32 cmos_init_ae_exp_function(ot_isp_ae_sensor_exp_func *exp_func)
{
    sns_check_pointer_return(exp_func);

    (td_void)memset_s(exp_func, sizeof(ot_isp_ae_sensor_exp_func), 0, sizeof(ot_isp_ae_sensor_exp_func));

    exp_func->pfn_cmos_get_ae_default    = cmos_get_ae_default;
    exp_func->pfn_cmos_fps_set           = cmos_fps_set;
    exp_func->pfn_cmos_slow_framerate_set = cmos_slow_framerate_set;
    exp_func->pfn_cmos_inttime_update    = cmos_inttime_update;
    exp_func->pfn_cmos_gains_update      = cmos_gains_update;
    exp_func->pfn_cmos_again_calc_table  = cmos_again_calc_table;
    exp_func->pfn_cmos_dgain_calc_table  = cmos_dgain_calc_table;
    exp_func->pfn_cmos_get_inttime_max   = cmos_get_inttime_max;
    exp_func->pfn_cmos_ae_fswdr_attr_set = cmos_ae_fswdr_attr_set;

    return TD_SUCCESS;
}

/* awb static param for Fuji Lens New IR_Cut */
#define CALIBRATE_STATIC_TEMP                         4950
#define CALIBRATE_STATIC_WB_R_GAIN                    432
#define CALIBRATE_STATIC_WB_GR_GAIN                   256
#define CALIBRATE_STATIC_WB_GB_GAIN                   256
#define CALIBRATE_STATIC_WB_B_GAIN                    479

/* Calibration results for Auto WB Planck */
#define CALIBRATE_AWB_P1                              (133)
#define CALIBRATE_AWB_P2                              (-78)
#define CALIBRATE_AWB_Q1                              (-201)
#define CALIBRATE_AWB_A1                              (207562)
#define CALIBRATE_AWB_B1                              (128)
#define CALIBRATE_AWB_C1                              (-160758)

/* Rgain and Bgain of the golden sample */
#define GOLDEN_RGAIN                                  0
#define GOLDEN_BGAIN                                  0
static td_s32 cmos_get_awb_default(ot_vi_pipe vi_pipe, ot_isp_awb_sensor_default *awb_sns_dft)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pointer_return(awb_sns_dft);
    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_return(sns_state);

    (td_void)memset_s(awb_sns_dft, sizeof(ot_isp_awb_sensor_default), 0, sizeof(ot_isp_awb_sensor_default));
    awb_sns_dft->wb_ref_temp = CALIBRATE_STATIC_TEMP; /* wb_ref_temp 4950 */

    awb_sns_dft->gain_offset[0] = CALIBRATE_STATIC_WB_R_GAIN;
    awb_sns_dft->gain_offset[1] = CALIBRATE_STATIC_WB_GR_GAIN;
    awb_sns_dft->gain_offset[2] = CALIBRATE_STATIC_WB_GB_GAIN; /* index 2 */
    awb_sns_dft->gain_offset[3] = CALIBRATE_STATIC_WB_B_GAIN; /* index 3 */

    awb_sns_dft->wb_para[0] = CALIBRATE_AWB_P1;
    awb_sns_dft->wb_para[1] = CALIBRATE_AWB_P2;
    awb_sns_dft->wb_para[2] = CALIBRATE_AWB_Q1; /* index 2 */
    awb_sns_dft->wb_para[3] = CALIBRATE_AWB_A1; /* index 3 */
    awb_sns_dft->wb_para[4] = CALIBRATE_AWB_B1; /* index 4 */
    awb_sns_dft->wb_para[5] = CALIBRATE_AWB_C1; /* index 5 */

    awb_sns_dft->golden_rgain = GOLDEN_RGAIN;
    awb_sns_dft->golden_bgain = GOLDEN_BGAIN;

    switch (sns_state->wdr_mode) {
        default:
        case OT_WDR_MODE_NONE:
            (td_void)memcpy_s(&awb_sns_dft->ccm, sizeof(ot_isp_awb_ccm), &g_awb_ccm, sizeof(ot_isp_awb_ccm));
            (td_void)memcpy_s(&awb_sns_dft->agc_tbl, sizeof(ot_isp_awb_agc_table),
                              &g_awb_agc_table, sizeof(ot_isp_awb_agc_table));
            (td_void)memcpy_s(&awb_sns_dft->sector, sizeof(ot_isp_color_sector),
                &g_color_sector, sizeof(ot_isp_color_sector));
            break;

        case OT_WDR_MODE_2To1_LINE:
            (td_void)memcpy_s(&awb_sns_dft->ccm, sizeof(ot_isp_awb_ccm), &g_awb_ccm_wdr, sizeof(ot_isp_awb_ccm));
            (td_void)memcpy_s(&awb_sns_dft->agc_tbl, sizeof(ot_isp_awb_agc_table),
                              &g_awb_agc_table_wdr, sizeof(ot_isp_awb_agc_table));
            (td_void)memcpy_s(&awb_sns_dft->sector, sizeof(ot_isp_color_sector),
                &g_color_sector_wdr, sizeof(ot_isp_color_sector));
            break;

    }

    awb_sns_dft->init_rgain = g_init_wb_gain[vi_pipe][0]; /* 0: Rgain */
    awb_sns_dft->init_ggain = g_init_wb_gain[vi_pipe][1]; /* 1: Ggain */
    awb_sns_dft->init_bgain = g_init_wb_gain[vi_pipe][2]; /* 2: Bgain */
    awb_sns_dft->sample_rgain = g_sample_r_gain[vi_pipe];
    awb_sns_dft->sample_bgain = g_sample_b_gain[vi_pipe];

    return TD_SUCCESS;
}

static td_s32 cmos_init_awb_exp_function(ot_isp_awb_sensor_exp_func *exp_func)
{
    sns_check_pointer_return(exp_func);

    (td_void)memset_s(exp_func, sizeof(ot_isp_awb_sensor_exp_func), 0, sizeof(ot_isp_awb_sensor_exp_func));

    exp_func->pfn_cmos_get_awb_default = cmos_get_awb_default;

    return TD_SUCCESS;
}

static ot_isp_cmos_dng_color_param g_dng_color_param = {{ 378, 256, 430 }, { 439, 256, 439 }};

static td_void cmos_get_isp_dng_default(const ot_isp_sns_state *sns_state, ot_isp_cmos_default *isp_def)
{
    (td_void)memcpy_s(&isp_def->dng_color_param, sizeof(ot_isp_cmos_dng_color_param), &g_dng_color_param,
                      sizeof(ot_isp_cmos_dng_color_param));

    switch (sns_state->img_mode) {

        default:
            isp_def->sns_mode.dng_raw_format.bits_per_sample = 10; /* 12bit */
            isp_def->sns_mode.dng_raw_format.white_level = 1023; /* max 4095 */
            break;
    }

    isp_def->sns_mode.dng_raw_format.default_scale.default_scale_hor.denominator = 1;
    isp_def->sns_mode.dng_raw_format.default_scale.default_scale_hor.numerator = 1;
    isp_def->sns_mode.dng_raw_format.default_scale.default_scale_ver.denominator = 1;
    isp_def->sns_mode.dng_raw_format.default_scale.default_scale_ver.numerator = 1;
    isp_def->sns_mode.dng_raw_format.cfa_repeat_pattern_dim.repeat_pattern_dim_row = 2; /* pattern 2 */
    isp_def->sns_mode.dng_raw_format.cfa_repeat_pattern_dim.repeat_pattern_dim_col = 2; /* pattern 2 */
    isp_def->sns_mode.dng_raw_format.black_level_repeat_dim.repeat_row = 2; /* pattern 2 */
    isp_def->sns_mode.dng_raw_format.black_level_repeat_dim.repeat_col = 2; /* pattern 2 */
    isp_def->sns_mode.dng_raw_format.cfa_layout = OT_ISP_CFALAYOUT_TYPE_RECTANGULAR;
    isp_def->sns_mode.dng_raw_format.cfa_plane_color[0] = 0;
    isp_def->sns_mode.dng_raw_format.cfa_plane_color[1] = 1;
    isp_def->sns_mode.dng_raw_format.cfa_plane_color[2] = 2; /* index 2, cfa_plane_color 2 */
    isp_def->sns_mode.dng_raw_format.cfa_pattern[0] = 0;
    isp_def->sns_mode.dng_raw_format.cfa_pattern[1] = 1;
    isp_def->sns_mode.dng_raw_format.cfa_pattern[2] = 1; /* index 2, cfa_pattern 1 */
    isp_def->sns_mode.dng_raw_format.cfa_pattern[3] = 2; /* index 3, cfa_pattern 2 */
    isp_def->sns_mode.valid_dng_raw_format = TD_TRUE;
    return;
}

static void cmos_get_isp_linear_default(ot_isp_cmos_default *isp_def)
{
    isp_def->key.bit1_demosaic         = 1;
    isp_def->demosaic                  = &g_cmos_demosaic;
    isp_def->key.bit1_sharpen          = 1;
    isp_def->sharpen                   = &g_cmos_yuv_sharpen;
    isp_def->key.bit1_drc              = 1;
    isp_def->drc                       = &g_cmos_drc;
    isp_def->key.bit1_bayer_nr         = 1;
    isp_def->bayer_nr                  = &g_cmos_bayer_nr;
    isp_def->key.bit1_anti_false_color = 1;
    isp_def->anti_false_color          = &g_cmos_anti_false_color;
    isp_def->key.bit1_cac              = 1;
    isp_def->cac                       = &g_cmos_cac;
    isp_def->key.bit1_ldci             = 1;
    isp_def->ldci                      = &g_cmos_ldci;
    isp_def->key.bit1_gamma            = 1;
    isp_def->gamma                     = &g_cmos_gamma;
    isp_def->key.bit1_clut             = 1;
    isp_def->clut                      = &g_cmos_clut;
#ifdef CONFIG_OT_ISP_CR_SUPPORT
    isp_def->key.bit1_ge               = 1;
    isp_def->ge                        = &g_cmos_ge;
#endif
    isp_def->key.bit1_dehaze = 1;
    isp_def->dehaze = &g_cmos_dehaze;
    isp_def->key.bit1_ca = 1;
    isp_def->ca = &g_cmos_ca;
    (td_void)memcpy_s(&isp_def->noise_calibration, sizeof(ot_isp_noise_calibration),
                      &g_cmos_noise_calibration, sizeof(ot_isp_noise_calibration));
    return;
}

static void cmos_get_isp_wdr_default(ot_isp_cmos_default *isp_def)
{
    isp_def->key.bit1_dpc            = 1;
    isp_def->dpc                     = &g_cmos_dpc_wdr;
    isp_def->key.bit1_demosaic       = 1;
    isp_def->demosaic                = &g_cmos_demosaic_wdr;
    isp_def->key.bit1_sharpen        = 1;
    isp_def->sharpen                 = &g_cmos_yuv_sharpen_wdr;
    isp_def->key.bit1_drc            = 1;
    isp_def->drc                     = &g_cmos_drc_wdr;
    isp_def->key.bit1_gamma          = 1;
    isp_def->gamma                   = &g_cmos_gamma_wdr;
    isp_def->key.bit1_clut           = 1;
    isp_def->clut                    = &g_cmos_clut_wdr;
#ifdef CONFIG_OT_ISP_PREGAMMA_SUPPORT
    isp_def->key.bit1_pregamma       = 1;
    isp_def->pregamma                = &g_cmos_pregamma;
#endif
    isp_def->key.bit1_bayer_nr       = 1;
    isp_def->bayer_nr                = &g_cmos_bayer_nr_wdr;
#ifdef CONFIG_OT_ISP_CR_SUPPORT
    isp_def->key.bit1_ge             = 1;
    isp_def->ge                      = &g_cmos_ge_wdr;
#endif
    isp_def->key.bit1_anti_false_color = 1;
    isp_def->anti_false_color = &g_cmos_anti_false_color_wdr;
    isp_def->key.bit1_cac = 1;
    isp_def->cac = &g_cmos_cac_wdr;
    isp_def->key.bit1_ldci = 1;
    isp_def->ldci = &g_cmos_ldci_wdr;
    isp_def->key.bit1_dehaze = 1;
    isp_def->dehaze = &g_cmos_dehaze_wdr;

    (td_void)memcpy_s(&isp_def->noise_calibration, sizeof(ot_isp_noise_calibration),
                      &g_cmos_noise_calibration, sizeof(ot_isp_noise_calibration));
    return;
}

static td_s32 cmos_get_isp_default(ot_vi_pipe vi_pipe, ot_isp_cmos_default *isp_def)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pointer_return(isp_def);
    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_return(sns_state);

    (td_void)memset_s(isp_def, sizeof(ot_isp_cmos_default), 0, sizeof(ot_isp_cmos_default));
#ifdef CONFIG_OT_ISP_CA_SUPPORT
    isp_def->key.bit1_ca      = 1;
    isp_def->ca               = &g_cmos_ca;
#endif
    isp_def->key.bit1_dpc     = 1;
    isp_def->dpc              = &g_cmos_dpc;

    isp_def->key.bit1_wdr     = 1;
    isp_def->wdr              = &g_cmos_wdr;

    isp_def->key.bit1_lsc      = 0;
    isp_def->lsc               = &g_cmos_lsc;

    isp_def->key.bit1_acs      = 0;
    isp_def->acs               = &g_cmos_acs;

#ifdef CONFIG_OT_ISP_PREGAMMA_SUPPORT
    isp_def->key.bit1_pregamma = 0;
    isp_def->pregamma          = &g_cmos_pregamma;
#endif
    switch (sns_state->wdr_mode) {
        case OT_WDR_MODE_NONE:
            cmos_get_isp_linear_default(isp_def);
            break;
        case OT_WDR_MODE_2To1_LINE:
            cmos_get_isp_wdr_default(isp_def);
            break;
        default:
            cmos_get_isp_linear_default(isp_def);
            break;
    }
    isp_def->wdr_switch_attr.exp_ratio[0] = 0x40;

    if (sns_state->wdr_mode == OT_WDR_MODE_2To1_LINE) {
        isp_def->wdr_switch_attr.exp_ratio[0] = 0x400;
    }
    else if (sns_state->wdr_mode == OT_WDR_MODE_3To1_LINE) {
        isp_def->wdr_switch_attr.exp_ratio[0] = 0x200;
        isp_def->wdr_switch_attr.exp_ratio[1] = 0x200;
    }
    else if (sns_state->wdr_mode == OT_WDR_MODE_4To1_LINE) {
        isp_def->wdr_switch_attr.exp_ratio[0] = 0x200;
        isp_def->wdr_switch_attr.exp_ratio[1] = 0x200;
        isp_def->wdr_switch_attr.exp_ratio[2] = 0x200;
    }
    isp_def->sns_mode.sns_id = VS4388_ID;
    isp_def->sns_mode.sns_mode = sns_state->img_mode;
    cmos_get_isp_dng_default(sns_state, isp_def);

    return TD_SUCCESS;
}

static td_s32 cmos_get_isp_black_level(ot_vi_pipe vi_pipe, ot_isp_cmos_black_level *black_level)
{
    td_s32  i;
    ot_isp_sns_state *sns_state = TD_NULL;
    const ot_isp_cmos_black_level *cmos_blc_def = TD_NULL;

    sns_check_pointer_return(black_level);
    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_return(sns_state);
    if ((sns_state->wdr_mode == OT_WDR_MODE_NONE) && (is_virt_pipe(vi_pipe) == TD_FALSE)) {
        cmos_blc_def = &g_cmos_blc;
    } else {
        cmos_blc_def = &g_cmos_blc_wdr;
    }
    (td_void)memcpy_s(black_level, sizeof(ot_isp_cmos_black_level), cmos_blc_def, sizeof(ot_isp_cmos_black_level));

    /* Don't need to update black level when iso change */
    black_level->auto_attr.update = TD_FALSE;

    /* black level of linear mode */
    if (sns_state->wdr_mode == OT_WDR_MODE_NONE) {
        for (i = 0; i < OT_ISP_BAYER_CHN_NUM; i++) {
            black_level->auto_attr.black_level[0][i] = VS4388_BLACK_LEVEL;
        }
    } else { /* black level of DOL mode */
        for (i = 0; i < OT_ISP_WDR_MAX_FRAME_NUM; i++) {
            black_level->auto_attr.black_level[i][0] = VS4388_BLACK_LEVEL;
            black_level->auto_attr.black_level[i][1] = VS4388_BLACK_LEVEL;
            black_level->auto_attr.black_level[i][2] = VS4388_BLACK_LEVEL; /* index 2 */
            black_level->auto_attr.black_level[i][3] = VS4388_BLACK_LEVEL; /* index 3 */
        }
    }

    return TD_SUCCESS;
}
static td_s32 cmos_get_isp_blc_clamp_info(ot_vi_pipe vi_pipe, td_bool *blc_clamp_en)
{
    sns_check_pointer_return(blc_clamp_en);

    *blc_clamp_en = blc_clamp_info[vi_pipe];

    return TD_SUCCESS;
}
static td_void cmos_set_pixel_detect(ot_vi_pipe vi_pipe, td_bool enable)
{
    td_u32 full_lines_5fps, max_int_time_5fps;
    ot_isp_sns_state *sns_state = TD_NULL;

    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    if (sns_state->wdr_mode != OT_WDR_MODE_NONE) {
        return;
    } else {
        if (sns_state->img_mode == 1)  {
            full_lines_5fps = (g_vs4388_mode_tbl[sns_state->img_mode].ver_lines * 30) / 5; /* 30fps, 5fps */
        } else {
            return;
        }
    }

    max_int_time_5fps = 6; /* max_int_time_5fps 6 */

    if (enable) { /* setup for ISP pixel calibration mode */
        vs4388_write_register(vi_pipe, VS4388_G3_ADDR_H, 0x00);
        vs4388_write_register(vi_pipe, VS4388_G3_ADDR_L, 0x10);
        vs4388_write_register(vi_pipe, VS4388_VTS_ADDR_L, low_8bits(full_lines_5fps));
        vs4388_write_register(vi_pipe, VS4388_VTS_ADDR_H, high_8bits(full_lines_5fps));

        vs4388_write_register(vi_pipe, VS4388_L3_ADDR_L, low_8bits(max_int_time_5fps));
        vs4388_write_register(vi_pipe, VS4388_L3_ADDR_H, high_8bits(max_int_time_5fps));
    } else { /* setup for ISP 'normal mode' */
        sns_state->fl_std = (sns_state->fl_std > 0xFFFFF) ? 0xFFFFF : sns_state->fl_std;
        vs4388_write_register(vi_pipe, VS4388_VTS_ADDR_L, low_8bits(sns_state->fl_std));
        vs4388_write_register(vi_pipe, VS4388_VTS_ADDR_H, high_8bits(sns_state->fl_std));
        sns_state->sync_init = TD_FALSE;
    }

    return;
}

static td_s32 cmos_set_wdr_mode(ot_vi_pipe vi_pipe, td_u8 mode)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_return(sns_state);

    sns_state->sync_init    = TD_FALSE;

    switch (mode & 0x3F) {
        case OT_WDR_MODE_NONE:
            sns_state->wdr_mode = OT_WDR_MODE_NONE;
            printf("linear mode\n");
            break;
        case OT_WDR_MODE_2To1_LINE:
            sns_state->wdr_mode = OT_WDR_MODE_2To1_LINE;
            printf("2to1 line WDR mode\n");
            break;
        case OT_WDR_MODE_3To1_LINE:
            sns_state->wdr_mode = OT_WDR_MODE_3To1_LINE;
            printf("3to1 line WDR mode\n");
            break;
        case OT_WDR_MODE_4To1_LINE:
            sns_state->wdr_mode = OT_WDR_MODE_4To1_LINE;
            printf("4to1 line WDR mode\n");
            break;

        default:
            isp_err_trace("NOT support this mode!\n");
            return TD_FAILURE;
    }

    (td_void)memset_s(sns_state->wdr_int_time, sizeof(sns_state->wdr_int_time), 0, sizeof(sns_state->wdr_int_time));

    return TD_SUCCESS;
}

static td_void cmos_comm_sns_reg_info_init(ot_vi_pipe vi_pipe, ot_isp_sns_state *sns_state)
{
    td_u32 i;
    sns_state->regs_info[0].sns_type = OT_ISP_SNS_TYPE_I2C;
    sns_state->regs_info[0].com_bus.i2c_dev = g_vs4388_bus_info[vi_pipe].i2c_dev;
    sns_state->regs_info[0].cfg2_valid_delay_max = 2; /* delay_max 2 */
    sns_state->regs_info[0].reg_num = 6; /* reg_num 8 */

    if (sns_state->wdr_mode == OT_WDR_MODE_2To1_LINE) {
        sns_state->regs_info[0].reg_num += 4; /* reg_num 10 */
        sns_state->regs_info[0].cfg2_valid_delay_max = 2; /* delay_max 2 */
    }
    else if (sns_state->wdr_mode == OT_WDR_MODE_3To1_LINE) {
        sns_state->regs_info[0].reg_num += 8; /* reg_num 12 */
        sns_state->regs_info[0].cfg2_valid_delay_max = 2; /* delay_max 2 */
    }
    else if (sns_state->wdr_mode == OT_WDR_MODE_4To1_LINE) {
        sns_state->regs_info[0].reg_num += 16; /* reg_num 16 */
        sns_state->regs_info[0].cfg2_valid_delay_max = 2; /* delay_max 2 */
    }
    for (i = 0; i < sns_state->regs_info[0].reg_num; i++) {
        sns_state->regs_info[0].i2c_data[i].update = TD_TRUE;
        sns_state->regs_info[0].i2c_data[i].dev_addr = VS4388_I2C_ADDR;
        sns_state->regs_info[0].i2c_data[i].addr_byte_num = VS4388_ADDR_BYTE;
        sns_state->regs_info[0].i2c_data[i].data_byte_num = VS4388_DATA_BYTE;
    }

    /* Linear Mode Regs */
    sns_state->regs_info[0].i2c_data[0].delay_frame_num = 0;
    sns_state->regs_info[0].i2c_data[0].reg_addr = VS4388_L3_ADDR_H;
    sns_state->regs_info[0].i2c_data[1].delay_frame_num = 0;
    sns_state->regs_info[0].i2c_data[1].reg_addr = VS4388_L3_ADDR_L;

    sns_state->regs_info[0].i2c_data[2].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[2].reg_addr = VS4388_G3_ADDR_H; 
    sns_state->regs_info[0].i2c_data[3].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[3].reg_addr = VS4388_G3_ADDR_L; 
    
    sns_state->regs_info[0].i2c_data[4].delay_frame_num = 0;  
    sns_state->regs_info[0].i2c_data[4].reg_addr = VS4388_VTS_ADDR_H; 
    sns_state->regs_info[0].i2c_data[5].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[5].reg_addr = VS4388_VTS_ADDR_L; 

    return;
}

static td_void cmos_2to1_line_wdr_sns_reg_info_init(ot_vi_pipe vi_pipe, ot_isp_sns_state *sns_state)
{
    ot_unused(vi_pipe);
   
    sns_state->regs_info[0].i2c_data[6].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[6].reg_addr = VS4388_L2_ADDR_H;
    sns_state->regs_info[0].i2c_data[7].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[7].reg_addr = VS4388_L2_ADDR_L; 

    sns_state->regs_info[0].i2c_data[8].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[8].reg_addr = VS4388_G2_ADDR_H; 
    sns_state->regs_info[0].i2c_data[9].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[9].reg_addr = VS4388_G2_ADDR_L; 

    return;
}
static td_void cmos_3to1_line_wdr_sns_reg_info_init(ot_vi_pipe vi_pipe, ot_isp_sns_state *sns_state)
{
    ot_unused(vi_pipe);
   
    sns_state->regs_info[0].i2c_data[6].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[6].reg_addr = VS4388_L2_ADDR_H;
    sns_state->regs_info[0].i2c_data[7].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[7].reg_addr = VS4388_L2_ADDR_L; 

    sns_state->regs_info[0].i2c_data[8].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[8].reg_addr = VS4388_G2_ADDR_H; 
    sns_state->regs_info[0].i2c_data[9].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[9].reg_addr = VS4388_G2_ADDR_L; 

    sns_state->regs_info[0].i2c_data[10].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[10].reg_addr = VS4388_L1_ADDR_H;
    sns_state->regs_info[0].i2c_data[11].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[11].reg_addr = VS4388_L1_ADDR_L; 

    sns_state->regs_info[0].i2c_data[12].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[12].reg_addr = VS4388_G1_ADDR_H; 
    sns_state->regs_info[0].i2c_data[13].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[13].reg_addr = VS4388_G1_ADDR_L; 

    return;
}
static td_void cmos_4to1_line_wdr_sns_reg_info_init(ot_vi_pipe vi_pipe, ot_isp_sns_state *sns_state)
{
    ot_unused(vi_pipe);
   
    sns_state->regs_info[0].i2c_data[6].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[6].reg_addr = VS4388_L2_ADDR_H;
    sns_state->regs_info[0].i2c_data[7].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[7].reg_addr = VS4388_L2_ADDR_L; 

    sns_state->regs_info[0].i2c_data[8].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[8].reg_addr = VS4388_G2_ADDR_H; 
    sns_state->regs_info[0].i2c_data[9].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[9].reg_addr = VS4388_G2_ADDR_L; 

    sns_state->regs_info[0].i2c_data[10].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[10].reg_addr = VS4388_L1_ADDR_H;
    sns_state->regs_info[0].i2c_data[11].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[11].reg_addr = VS4388_L1_ADDR_L; 

    sns_state->regs_info[0].i2c_data[12].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[12].reg_addr = VS4388_G1_ADDR_H; 
    sns_state->regs_info[0].i2c_data[13].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[13].reg_addr = VS4388_G1_ADDR_L; 

    sns_state->regs_info[0].i2c_data[14].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[14].reg_addr = VS4388_LS_ADDR_H;
    sns_state->regs_info[0].i2c_data[15].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[15].reg_addr = VS4388_LS_ADDR_L; 

    sns_state->regs_info[0].i2c_data[16].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[16].reg_addr = VS4388_GS_ADDR_H; 
    sns_state->regs_info[0].i2c_data[17].delay_frame_num = 0; 
    sns_state->regs_info[0].i2c_data[17].reg_addr = VS4388_GS_ADDR_L; 

    return;
}

static td_void cmos_sns_reg_info_update(ot_vi_pipe vi_pipe, ot_isp_sns_state *sns_state)
{
    td_u32 i;
    ot_unused(vi_pipe);

    for (i = 0; i < sns_state->regs_info[0].reg_num; i++) {
        if (sns_state->regs_info[0].i2c_data[i].data ==
            sns_state->regs_info[1].i2c_data[i].data) {
            sns_state->regs_info[0].i2c_data[i].update = TD_FALSE;
        } else {
            sns_state->regs_info[0].i2c_data[i].update = TD_TRUE;
        }
    }
    return;
}

static td_s32 cmos_get_sns_regs_info(ot_vi_pipe vi_pipe, ot_isp_sns_regs_info *sns_regs_info)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pointer_return(sns_regs_info);
    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_return(sns_state);

    if ((sns_state->sync_init == TD_FALSE) || (sns_regs_info->config == TD_FALSE)) {
        cmos_comm_sns_reg_info_init(vi_pipe, sns_state);

        if (sns_state->wdr_mode == OT_WDR_MODE_2To1_LINE) {
            /* DOL 3t1 Mode Regs */
            cmos_2to1_line_wdr_sns_reg_info_init(vi_pipe, sns_state);
        }
        else if (sns_state->wdr_mode == OT_WDR_MODE_3To1_LINE) {
            /* DOL 3t1 Mode Regs */
            cmos_3to1_line_wdr_sns_reg_info_init(vi_pipe, sns_state);
        }
        else if (sns_state->wdr_mode == OT_WDR_MODE_4To1_LINE) {
            /* DOL 3t1 Mode Regs */
            cmos_4to1_line_wdr_sns_reg_info_init(vi_pipe, sns_state);
        }
        sns_state->sync_init = TD_TRUE;
    } else {
        cmos_sns_reg_info_update(vi_pipe, sns_state);
    }

    sns_regs_info->config = TD_FALSE;
    (td_void)memcpy_s(sns_regs_info, sizeof(ot_isp_sns_regs_info),
                      &sns_state->regs_info[0], sizeof(ot_isp_sns_regs_info));
    (td_void)memcpy_s(&sns_state->regs_info[1], sizeof(ot_isp_sns_regs_info),
                      &sns_state->regs_info[0], sizeof(ot_isp_sns_regs_info));
    sns_state->fl[1] = sns_state->fl[0];

    return TD_SUCCESS;
}


static td_s32 cmos_set_image_mode(ot_vi_pipe vi_pipe, const ot_isp_cmos_sns_image_mode *sensor_image_mode)
{
    td_u32 i;
    td_u8 image_mode;
    ot_isp_sns_state *sns_state = TD_NULL;
    sns_check_pointer_return(sensor_image_mode);
    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_return(sns_state);

    image_mode = sns_state->img_mode;
    for (i = 0; i < sizeof(g_vs4388_mode_tbl)/sizeof(g_vs4388_mode_tbl[0]); i++) {
        if (sensor_image_mode->fps <= g_vs4388_mode_tbl[i].max_fps &&
            sensor_image_mode->width <= g_vs4388_mode_tbl[i].width &&
            sensor_image_mode->height <= g_vs4388_mode_tbl[i].height &&
            sns_state->wdr_mode == g_vs4388_mode_tbl[i].wdr_mode) {
            image_mode = g_vs4388_mode_tbl[i].sns_mode;
            printf("image mode = %d\n",image_mode);
            break;
        }
    }

    if (i >= sizeof(g_vs4388_mode_tbl)/sizeof(g_vs4388_mode_tbl[0])) {
        vs4388_err_mode_print(sensor_image_mode, sns_state);
        return TD_FAILURE;
    }

    sns_state->fl_std = g_vs4388_mode_tbl[image_mode].ver_lines;

    if ((sns_state->init == TD_TRUE) && (image_mode == sns_state->img_mode)) {
        return OT_ISP_DO_NOT_NEED_SWITCH_IMAGEMODE; /* Don't need to switch image_mode */
    }

    sns_state->sync_init = TD_FALSE;
    sns_state->img_mode = image_mode;
    sns_state->fl[0] = sns_state->fl_std;
    sns_state->fl[1] = sns_state->fl[0];
    return TD_SUCCESS;
}

static td_void sensor_global_init(ot_vi_pipe vi_pipe)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    vs4388_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    sns_state->init = TD_FALSE;
    sns_state->sync_init = TD_FALSE;
    sns_state->img_mode = 1;
    sns_state->wdr_mode = OT_WDR_MODE_NONE;
    sns_state->fl_std  = g_vs4388_mode_tbl[sns_state->img_mode].ver_lines;
    sns_state->fl[0] = g_vs4388_mode_tbl[sns_state->img_mode].ver_lines;
    sns_state->fl[1] = g_vs4388_mode_tbl[sns_state->img_mode].ver_lines;

    (td_void)memset_s(&sns_state->regs_info[0], sizeof(ot_isp_sns_regs_info), 0, sizeof(ot_isp_sns_regs_info));
    (td_void)memset_s(&sns_state->regs_info[1], sizeof(ot_isp_sns_regs_info), 0, sizeof(ot_isp_sns_regs_info));
    return;
}

static td_s32 cmos_init_sensor_exp_function(ot_isp_sns_exp_func *sensor_exp_func)
{
    sns_check_pointer_return(sensor_exp_func);

    (td_void)memset_s(sensor_exp_func, sizeof(ot_isp_sns_exp_func), 0, sizeof(ot_isp_sns_exp_func));

    sensor_exp_func->pfn_cmos_sns_init         = vs4388_init;
    sensor_exp_func->pfn_cmos_sns_exit         = vs4388_exit;
    sensor_exp_func->pfn_cmos_sns_global_init  = sensor_global_init;
    sensor_exp_func->pfn_cmos_set_image_mode      = cmos_set_image_mode;
    sensor_exp_func->pfn_cmos_set_wdr_mode        = cmos_set_wdr_mode;
    sensor_exp_func->pfn_cmos_get_isp_default     = cmos_get_isp_default;
    sensor_exp_func->pfn_cmos_get_isp_black_level = cmos_get_isp_black_level;
    sensor_exp_func->pfn_cmos_get_blc_clamp_info  = cmos_get_isp_blc_clamp_info;
    sensor_exp_func->pfn_cmos_set_pixel_detect    = cmos_set_pixel_detect;
    sensor_exp_func->pfn_cmos_get_sns_reg_info    = cmos_get_sns_regs_info;

    return TD_SUCCESS;
}

static td_s32 vs4388_set_bus_info(ot_vi_pipe vi_pipe, ot_isp_sns_commbus sns_bus_info)
{
    g_vs4388_bus_info[vi_pipe].i2c_dev = sns_bus_info.i2c_dev;

    return TD_SUCCESS;
}

static td_s32 sensor_ctx_init(ot_vi_pipe vi_pipe)
{
    ot_isp_sns_state *sns_state_ctx = TD_NULL;

    vs4388_sensor_get_ctx(vi_pipe, sns_state_ctx);

    if (sns_state_ctx == TD_NULL) {
        sns_state_ctx = (ot_isp_sns_state *)malloc(sizeof(ot_isp_sns_state));
        if (sns_state_ctx == TD_NULL) {
            isp_err_trace("Isp[%d] SnsCtx malloc memory failed!\n", vi_pipe);
            return OT_ERR_ISP_NOMEM;
        }
    }

    (td_void)memset_s(sns_state_ctx, sizeof(ot_isp_sns_state), 0, sizeof(ot_isp_sns_state));

    vs4388_sensor_set_ctx(vi_pipe, sns_state_ctx);

    return TD_SUCCESS;
}

static td_void sensor_ctx_exit(ot_vi_pipe vi_pipe)
{
    ot_isp_sns_state *sns_state_ctx = TD_NULL;

    vs4388_sensor_get_ctx(vi_pipe, sns_state_ctx);
    sns_free(sns_state_ctx);
    vs4388_sensor_reset_ctx(vi_pipe);
    return;
}

static td_s32 sensor_register_callback(ot_vi_pipe vi_pipe, ot_isp_3a_alg_lib *ae_lib, ot_isp_3a_alg_lib *awb_lib)
{
    td_s32 ret;
    ot_isp_sns_register isp_register;
    ot_isp_ae_sensor_register ae_register;
    ot_isp_awb_sensor_register awb_register;
    ot_isp_sns_attr_info sns_attr_info;

    sns_check_pointer_return(ae_lib);
    sns_check_pointer_return(awb_lib);

    ret = sensor_ctx_init(vi_pipe);
    if (ret != TD_SUCCESS) {
        return TD_FAILURE;
    }

    sns_attr_info.sns_id = VS4388_ID;
    ret = cmos_init_sensor_exp_function(&isp_register.sns_exp);
    if (ret != TD_SUCCESS) {
        isp_err_trace("cmos init exp function failed!\n");
        return TD_FAILURE;
    }
    ret = ot_mpi_isp_sensor_reg_callback(vi_pipe, &sns_attr_info, &isp_register);
    if (ret != TD_SUCCESS) {
        isp_err_trace("sensor register callback function failed!\n");
        return ret;
    }

    ret = cmos_init_ae_exp_function(&ae_register.sns_exp);
    if (ret != TD_SUCCESS) {
        isp_err_trace("cmos init ae exp function failed!\n");
        return TD_FAILURE;
    }
    ret = ot_mpi_ae_sensor_reg_callback(vi_pipe, ae_lib, &sns_attr_info, &ae_register);
    if (ret != TD_SUCCESS) {
        isp_err_trace("sensor register callback function to ae lib failed!\n");
        return ret;
    }

    ret = cmos_init_awb_exp_function(&awb_register.sns_exp);
    if (ret != TD_SUCCESS) {
        isp_err_trace("cmos init awb exp function failed!\n");
        return TD_FAILURE;
    }
    ret = ot_mpi_awb_sensor_reg_callback(vi_pipe, awb_lib, &sns_attr_info, &awb_register);
    if (ret != TD_SUCCESS) {
        isp_err_trace("sensor register callback function to awb lib failed!\n");
        return ret;
    }

    return TD_SUCCESS;
}

static td_s32 sensor_unregister_callback(ot_vi_pipe vi_pipe, ot_isp_3a_alg_lib *ae_lib, ot_isp_3a_alg_lib *awb_lib)
{
    td_s32 ret;

    sns_check_pointer_return(ae_lib);
    sns_check_pointer_return(awb_lib);

    ret = ot_mpi_isp_sensor_unreg_callback(vi_pipe, VS4388_ID);
    if (ret != TD_SUCCESS) {
        isp_err_trace("sensor unregister callback function failed!\n");
        return ret;
    }

    ret = ot_mpi_ae_sensor_unreg_callback(vi_pipe, ae_lib, VS4388_ID);
    if (ret != TD_SUCCESS) {
        isp_err_trace("sensor unregister callback function to ae lib failed!\n");
        return ret;
    }

    ret = ot_mpi_awb_sensor_unreg_callback(vi_pipe, awb_lib, VS4388_ID);
    if (ret != TD_SUCCESS) {
        isp_err_trace("sensor unregister callback function to awb lib failed!\n");
        return ret;
    }

    sensor_ctx_exit(vi_pipe);
    return TD_SUCCESS;
}

static td_s32 sensor_set_init(ot_vi_pipe vi_pipe, ot_isp_init_attr *init_attr)
{
    sns_check_pointer_return(init_attr);

    g_init_exposure[vi_pipe]  = init_attr->exposure;
    g_lines_per500ms[vi_pipe] = init_attr->lines_per500ms;
    g_init_wb_gain[vi_pipe][0] = init_attr->wb_r_gain; /* 0: rgain */
    g_init_wb_gain[vi_pipe][1] = init_attr->wb_g_gain; /* 1: ggain */
    g_init_wb_gain[vi_pipe][2] = init_attr->wb_b_gain; /* 2: bgain */
    g_sample_r_gain[vi_pipe] = init_attr->sample_r_gain;
    g_sample_b_gain[vi_pipe] = init_attr->sample_b_gain;
    g_quick_start_en[vi_pipe] = init_attr->quick_start_en;

    g_ae_route_ex_valid[vi_pipe] = init_attr->ae_route_ex_valid;
    (td_void)memcpy_s(&g_init_ae_route[vi_pipe], sizeof(ot_isp_ae_route),
                      &init_attr->ae_route, sizeof(ot_isp_ae_route));
    (td_void)memcpy_s(&g_init_ae_route_ex[vi_pipe], sizeof(ot_isp_ae_route_ex),
                      &init_attr->ae_route_ex, sizeof(ot_isp_ae_route_ex));
    (td_void)memcpy_s(&g_init_ae_route_sf[vi_pipe], sizeof(ot_isp_ae_route),
                      &init_attr->ae_route_sf, sizeof(ot_isp_ae_route));
    (td_void)memcpy_s(&g_init_ae_route_sf_ex[vi_pipe], sizeof(ot_isp_ae_route_ex),
                      &init_attr->ae_route_sf_ex, sizeof(ot_isp_ae_route_ex));

    return TD_SUCCESS;
}

ot_isp_sns_obj g_sns_vs4388_obj = {
    .pfn_register_callback     = sensor_register_callback,
    .pfn_un_register_callback  = sensor_unregister_callback,
    .pfn_standby               = vs4388_standby,
    .pfn_restart               = vs4388_restart,
    .pfn_mirror_flip           = vs4388_mirror_flip,
    .pfn_set_blc_clamp         = TD_NULL,
    .pfn_write_reg             = vs4388_write_register,
    .pfn_read_reg              = vs4388_read_register,
    .pfn_set_bus_info          = vs4388_set_bus_info,
    .pfn_set_init              = sensor_set_init
};
