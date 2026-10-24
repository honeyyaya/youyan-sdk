/*
  Copyright (c), 2001-2025, Shenshu Tech. Co., Ltd.
 */

#include "sensor_common.h"
#include "gc8613_cfg.h"

static void gc8613_default_reg_init(cis_info *cis)
{
    td_u32 i;
    td_s32 ret = TD_SUCCESS;
    ot_isp_sns_state *past_sensor = TD_NULL;

    past_sensor = cis->sns_state;
    for (i = 0; i < past_sensor->regs_info[0].reg_num; i++) {
        ret += cis_write_reg(&cis->i2c,
            past_sensor->regs_info[0].i2c_data[i].reg_addr,
            past_sensor->regs_info[0].i2c_data[i].data);
    }

    if (ret != TD_SUCCESS) {
        isp_err_trace("write register failed!\n");
    }
    return;
}

static td_s32 gc8613_reg_init(cis_info *cis, cis_reg_cfg *cfg, td_u32 len)
{
    td_u32 i;

    for (i = 0; i < len; i++) {
        sns_check_return(cis_write_reg(&cis->i2c, cfg->addr, cfg->data));
        cfg++;
    }
    gc8613_default_reg_init(cis);
    return TD_SUCCESS;
}

td_s32 gc8613_linear_8m30_10bit_init(cis_info *cis)
{
    td_s32 ret;
    td_u32 len;
    cis_reg_cfg *cfg1 = gc8613_linear_8m30_10bit_part1;
    cis_reg_cfg *cfg2 = gc8613_linear_8m30_10bit_part2;

    sns_check_pointer_return(cis);

    len = (td_u32)(sizeof(gc8613_linear_8m30_10bit_part1) / sizeof(gc8613_linear_8m30_10bit_part1[0]));
    ret = gc8613_reg_init(cis, cfg1, len);
    if (ret != TD_SUCCESS) {
        isp_err_trace("gc8613_reg_init failed!\n");
        return ret;
    } 
	
		cis_delay_ms(0x14);
		
    len = (td_u32)(sizeof(gc8613_linear_8m30_10bit_part2) / sizeof(gc8613_linear_8m30_10bit_part2[0]));
    ret = gc8613_reg_init(cis, cfg2, len);
    if (ret != TD_SUCCESS) {
        isp_err_trace("gc8613_reg_init failed!\n");
        return ret;
    } 

    printf("===================================================================================\n");
    printf("vi_pipe:%d,== gc8613 27Mclk 8M30fps(MIPI) 4LANE_1003.5Mbps 10bit linear Init OK! ==\n", cis->pipe);
    printf("===================================================================================\n");

    return TD_SUCCESS;
}
