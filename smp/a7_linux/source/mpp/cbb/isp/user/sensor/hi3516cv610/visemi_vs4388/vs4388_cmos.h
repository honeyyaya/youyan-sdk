/*
  Copyright (c), 2001-2021, Shenshu Tech. Co., Ltd.
 */

#ifndef VS4388_CMOS_H
#define VS4388_CMOS_H

#include "ot_common.h"
#include "ot_common_isp.h"
#include "ot_common_video.h"
#include "ot_sns_ctrl.h"
#include "ot_type.h"
#include "ot_math.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

#define VS4388_I2C_ADDR    0x6c
#define VS4388_ADDR_BYTE   2
#define VS4388_DATA_BYTE   1
#define vs4388_sensor_get_ctx(dev, ctx)   ((ctx) = vs4388_get_ctx(dev))


typedef struct {
    td_u8       sns_mode;
    td_u32      width;
    td_u32      height;
    td_float    max_fps;
    td_float    min_fps;
    td_u32      ver_lines;
    td_u32      max_ver_lines;
    ot_wdr_mode wdr_mode;
    const td_u32   *init_reg_tbl;
    //const char *mode_name;
} vs4388_video_mode_tbl;

typedef struct {
    td_u32 dec[OT_ISP_WDR_MAX_FRAME_NUM];
    td_u32 inc[OT_ISP_WDR_MAX_FRAME_NUM];
} time_step;

ot_isp_sns_state *vs4388_get_ctx(ot_vi_pipe vi_pipe);

ot_isp_sns_commbus *vs4388_get_bus_info(ot_vi_pipe vi_pipe);

td_void vs4388_init(ot_vi_pipe vi_pipe);
td_void vs4388_exit(ot_vi_pipe vi_pipe);
td_void vs4388_standby(ot_vi_pipe vi_pipe);
td_void vs4388_restart(ot_vi_pipe vi_pipe);
td_void vs4388_set_blc_clamp_value(ot_vi_pipe vi_pipe, td_bool clamp_en);
td_void vs4388_blc_clamp(ot_vi_pipe vi_pipe, ot_isp_sns_blc_clamp blc_clamp);
td_void vs4388_mirror_flip(ot_vi_pipe vi_pipe, ot_isp_sns_mirrorflip_type e_sns_mirror_flip);
td_s32  vs4388_write_register(ot_vi_pipe vi_pipe, td_u32 addr, td_u32 data);
td_s32  vs4388_read_register(ot_vi_pipe vi_pipe, td_u32 addr);
#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
#endif /* VS4388_CMOS_H */
