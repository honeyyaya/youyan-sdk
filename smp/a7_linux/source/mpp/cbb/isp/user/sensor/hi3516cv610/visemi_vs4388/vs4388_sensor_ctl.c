/*
  Copyright (c), 2001-2021, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef OT_GPIO_I2C
#include "gpioi2c_ex.h"
#else
#include "ot_i2c.h"
#endif
#include "securec.h"

#include "vs4388_cmos.h"
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#define I2C_DEV_FILE_NUM     16
#define I2C_BUF_NUM          8
//#define use_external_setting
static int g_fd[OT_ISP_MAX_PIPE_NUM] = {[0 ...(OT_ISP_MAX_PIPE_NUM - 1)] = -1};

extern const td_u32 vs4388_1280x720p60_linear_init_tbl[];
extern const td_u32 vs4388_2560x1440p30_linear_init_tbl[];
extern const td_u32 vs4388_2560x1440p60_linear_init_tbl[];
extern const td_u32 vs4388_2560x1440p30_2to1wdr_init_tbl[];


const vs4388_video_mode_tbl g_vs4388_mode_tbl[1] = {
    {0, 2560, 1440, 30, 1, 0x5ca, 0xFFFF, OT_WDR_MODE_NONE,&vs4388_2560x1440p30_linear_init_tbl[0]},
};

int vs4388_i2c_init(ot_vi_pipe vi_pipe)
{
    int ret;
    char dev_file[I2C_DEV_FILE_NUM] = {0};
    td_u8 dev_num;

    if (g_fd[vi_pipe] >= 0) {
        return TD_SUCCESS;
    }
#ifdef OT_GPIO_I2C
    g_fd[vi_pipe] = open("/dev/gpioi2c_ex", O_RDONLY, S_IRUSR);
    if (g_fd[vi_pipe] < 0) {
        isp_err_trace("Open gpioi2c_ex error!\n");
        return TD_FAILURE;
    }
#else
    ot_isp_sns_commbus *vs4388businfo = TD_NULL;
    vs4388businfo = vs4388_get_bus_info(vi_pipe);
    dev_num = vs4388businfo->i2c_dev;
    (td_void)snprintf_s(dev_file, sizeof(dev_file), sizeof(dev_file) - 1, "/dev/i2c-%u", dev_num);

    g_fd[vi_pipe] = open(dev_file, O_RDWR, S_IRUSR | S_IWUSR);
    if (g_fd[vi_pipe] < 0) {
        isp_err_trace("Open /dev/ot_i2c_drv-%u error!\n", dev_num);
        return TD_FAILURE;
    }

    ret = ioctl(g_fd[vi_pipe], OT_I2C_SLAVE_FORCE, (VS4388_I2C_ADDR >> 1));
    if (ret < 0) {
        isp_err_trace("I2C_SLAVE_FORCE error!\n");
        close(g_fd[vi_pipe]);
        g_fd[vi_pipe] = -1;
        return ret;
    }
#endif

    return TD_SUCCESS;
}

int vs4388_i2c_exit(ot_vi_pipe vi_pipe)
{
    if (g_fd[vi_pipe] >= 0) {
        close(g_fd[vi_pipe]);
        g_fd[vi_pipe] = -1;
        return TD_SUCCESS;
    }
    return TD_FAILURE;
}

td_s32 vs4388_read_register(ot_vi_pipe vi_pipe, td_u32 addr)
{
      if (g_fd[vi_pipe] < 0) {
        return TD_SUCCESS;
    }
    #define I2C_MSG_CNT 2
    #define BIT_CNT_8 8
	unsigned char buf[BIT_CNT_8]={0};
	static struct i2c_rdwr_ioctl_data rdwr;
	static struct i2c_msg msg[I2C_MSG_CNT]={0};
    td_u32 data;
	rdwr.msgs = &msg[0];
	rdwr.nmsgs = (__u32)I2C_MSG_CNT;
	rdwr.msgs[0].addr = (VS4388_I2C_ADDR >> 1);
	rdwr.msgs[0].flags = 0;
	rdwr.msgs[0].len = VS4388_ADDR_BYTE;
	rdwr.msgs[0].buf = buf;

	rdwr.msgs[1].addr = (VS4388_I2C_ADDR >> 1);
	rdwr.msgs[1].flags = 0;
	rdwr.msgs[1].flags |= I2C_M_RD;
	rdwr.msgs[1].len = VS4388_DATA_BYTE;
	rdwr.msgs[1].buf = buf;
	
    if (VS4388_ADDR_BYTE == 2) {
        buf[0] = (addr >> BIT_CNT_8) & 0xff;
        buf[1] = addr & 0xff;
    } else {
        buf[0] = addr & 0xff;
    }

    if (ioctl(g_fd[vi_pipe], I2C_RDWR, &rdwr) != 2) {
        printf("CMD_I2C_READ error!\n");
        return -1;
    }

    if (VS4388_DATA_BYTE == 2)
        data = buf[1] | (buf[0] << BIT_CNT_8);
    else
        data = buf[0];

   // printf("0x%x: 0x%x\n", addr, data);

    return data;
}

td_s32 vs4388_write_register(ot_vi_pipe vi_pipe, td_u32 addr, td_u32 data)
{
    if (g_fd[vi_pipe] < 0) {
        return TD_SUCCESS;
    }

#ifdef OT_GPIO_I2C
    i2c_data.dev_addr = VS4388_I2C_ADDR;
    i2c_data.reg_addr = addr;
    i2c_data.addr_byte_num = VS4388_ADDR_BYTE;
    i2c_data.data = data;
    i2c_data.data_byte_num = VS4388_DATA_BYTE;

    ret = ioctl(g_fd[vi_pipe], GPIO_I2C_WRITE, &i2c_data);
    if (ret) {
        isp_err_trace("GPIO-I2C write failed!\n");
        return ret;
    }
#else
    td_u32 idx = 0;
    td_s32 ret;
    td_u8 buf[I2C_BUF_NUM];

    if (VS4388_ADDR_BYTE == 2) {  /* 2 byte */
        buf[idx] = (addr >> 8) & 0xff;  /* shift 8 */
        idx++;
        buf[idx] = addr & 0xff;
        idx++;
    } else {
    }

    if (VS4388_DATA_BYTE == 2) {  /* 2 byte */
    } else {
        buf[idx] = data & 0xff;
        idx++;
    }

    ret = write(g_fd[vi_pipe], buf, VS4388_ADDR_BYTE + VS4388_DATA_BYTE);
    if (ret < 0) {
        isp_err_trace("I2C_WRITE error!\n");
        return TD_FAILURE;
    }

#endif
    return TD_SUCCESS;
}

static void delay_ms(int ms)
{
    usleep(ms * 1000); /* 1ms: 1000us */
    return;
}

td_s32 vs4388_prog(ot_vi_pipe vi_pipe, const td_u32 *rom)
{
    td_u32 i = 0;
	td_u32 addr;
	td_u32 data;
    td_s32 ret = TD_SUCCESS;
    while (1) {
       
        addr = *(rom+i);
        data = *(rom+i+1);
		
        if (addr == 0xFFFE) {
            delay_ms(data);
        } else if (addr == 0xFFFF && data == 0xFF) {
            return ret;
        } else {
            ret += vs4388_write_register(vi_pipe,addr, data);
        }
		i+=2;
    }
    return ret;
}

void vs4388_standby(ot_vi_pipe vi_pipe)
{
    td_s32 ret = TD_SUCCESS;
    ret += vs4388_write_register(vi_pipe, 0x100, 0x00);  /* STANDBY */
    if (ret != TD_SUCCESS) {
        isp_err_trace("write register failed!\n");
    }
    return;
}

void vs4388_restart(ot_vi_pipe vi_pipe)
{
    td_s32 ret = TD_SUCCESS;
    ret += vs4388_write_register(vi_pipe, 0x100, 0x1); 
    delay_ms(24); /* 24ms */
    if (ret != TD_SUCCESS) {
        isp_err_trace("write register failed!\n");
    }
    return;
}
void vs4388_mirror_flip(ot_vi_pipe vi_pipe, ot_isp_sns_mirrorflip_type e_sns_mirror_flip)
{
    switch (e_sns_mirror_flip) {
        default:
        case ISP_SNS_NORMAL:
            vs4388_write_register(vi_pipe, 0x3816, 0x10);
            vs4388_write_register(vi_pipe, 0x3809, 0x06);
            vs4388_write_register(vi_pipe, 0x380b, 0x05);
            vs4388_write_register(vi_pipe, 0x4101, 0x0c);
            break;
        case ISP_SNS_FLIP:
            vs4388_write_register(vi_pipe, 0x3816, 0x00);
            vs4388_write_register(vi_pipe, 0x3809, 0x06);
            vs4388_write_register(vi_pipe, 0x380b, 0x06);
            vs4388_write_register(vi_pipe, 0x4101, 0x0c);
        break;
        case ISP_SNS_MIRROR:
            vs4388_write_register(vi_pipe, 0x3816, 0x30);
            vs4388_write_register(vi_pipe, 0x3809, 0x05);
            vs4388_write_register(vi_pipe, 0x380b, 0x05);
            vs4388_write_register(vi_pipe, 0x4101, 0x0e);
            break;

        case ISP_SNS_MIRROR_FLIP:
            vs4388_write_register(vi_pipe, 0x3816, 0x20);
            vs4388_write_register(vi_pipe, 0x3809, 0x05);
            vs4388_write_register(vi_pipe, 0x380b, 0x06);
            vs4388_write_register(vi_pipe, 0x4101, 0x0e);
            break;
    }
    return;
}

static void vs4388_default_reg_init(ot_vi_pipe vi_pipe)
{
    td_u32 i;
    td_s32 ret = TD_SUCCESS;
    ot_isp_sns_state *pastvs4388 = TD_NULL;
    pastvs4388 = vs4388_get_ctx(vi_pipe);
    for (i = 0; i < pastvs4388->regs_info[0].reg_num; i++) {
        ret += vs4388_write_register(vi_pipe,
                                     pastvs4388->regs_info[0].i2c_data[i].reg_addr,
                                     pastvs4388->regs_info[0].i2c_data[i].data);
    }
    if (ret != TD_SUCCESS) {
        isp_err_trace("write register failed!\n");
    }
    return;
}

void vs4388_init(ot_vi_pipe vi_pipe)
{
  
    td_u8            img_mode;
    td_s32 ret;
    ot_isp_sns_state *pastvs4388 = TD_NULL;
    pastvs4388 = vs4388_get_ctx(vi_pipe);
   
    img_mode   = pastvs4388->img_mode;

    ret = vs4388_i2c_init(vi_pipe);
    if (ret != TD_SUCCESS) {
        isp_err_trace("i2c init failed!\n");
        return;
    }
    td_u32 dataH,dataL;
    
    ret = vs4388_prog(vi_pipe, g_vs4388_mode_tbl[img_mode].init_reg_tbl);
    delay_ms(20); /* 20ms */
    if (ret != TD_SUCCESS) {
        isp_err_trace("write register failed!\n");
        return;
    }
    dataH = vs4388_read_register(vi_pipe,0x3812);
    dataL = vs4388_read_register(vi_pipe,0x3813);
    printf("=============img_mode = %d, VTS = 0x%x\n",img_mode,(dataH<<8) | dataL);
    pastvs4388->init = TD_TRUE;
    return;
}

void vs4388_exit(ot_vi_pipe vi_pipe)
{
    td_s32 ret;
    ret = vs4388_i2c_exit(vi_pipe);
    if (ret != TD_SUCCESS) {
        isp_err_trace("vs4388 exit failed!\n");
    }
    return;
}

const td_u32 vs4388_1280x720p60_linear_init_tbl[]={
    
    0xFFFF,0xFF, //end flag,must
};

const td_u32 vs4388_2560x1440p30_linear_init_tbl[]={
    0x0103, 0x01,
	0x4601, 0x40,
	0x5001, 0xc1,
	0x5000, 0x69,
	0x3809, 0x06,
	0x380b, 0x06,
	0x4003, 0x40,
	0x4004, 0x03,
	0x4000, 0x0b,
	0x4001, 0x40,
	0x4007, 0x72,
	0x400c, 0x02,
	0x400d, 0x07,
	0x400e, 0x32,
	0x2050, 0x00,
	0x2051, 0x05,
	0x2052, 0x06,
	0x4104, 0x01,
	0x4101, 0x0c,
	0x2148, 0xf0,
	0x2149, 0x7f,
	0x2150, 0xe0,
	0x2151, 0x3f,
	0x2153, 0x80,
	0x2154, 0xff,
	0x2155, 0x01,
	0x2158, 0xc0,
	0x2159, 0x1f,
	0x215a, 0xf8,
	0x215b, 0x0f,
	0x215c, 0x1f,
	0x215d, 0xfc,
	0x215e, 0x00,
	0x1111, 0xc8,
	0x1112, 0x97,
	0x2138, 0x7c,
	0x2168, 0x1f,
	0x2169, 0x00,
	0x103a, 0x10,
	0x103b, 0x10,
	0x103c, 0x10,
	0x1020, 0x18,
	0x21d4, 0xff,
	0x21d5, 0x07,
	0x21b8, 0xff,
	0x21b9, 0x0f,
	0x202d, 0x00,
	0x202e, 0x00,
	0x202f, 0x00,
	0x219f, 0x7f,
	0x2199, 0x7f,
	0x103f, 0x06,
	0x103e, 0x05,
	0x103d, 0x05,
	0x1021, 0x05,
	0x2030, 0x27,
	0x2031, 0x0f,
	0x1101, 0x02,
	0x1100, 0x28,
	0x201f, 0x2a,
	0x202c, 0x03,
	0x1018, 0x00,
	0x1019, 0x08,
	0x101a, 0x0b,
	0x101b, 0x0c,
	0x1025, 0xf7,
	0x1024, 0x4e,
	0x2012, 0x0b,
	0x2013, 0x0d,
	0x2016, 0x0f,
	0x2017, 0x10,
	0x201c, 0x13,
	0x201d, 0x31,
	0x201a, 0x2b,
	0x2023, 0x36,
	0x2024, 0x20,
	0x2025, 0x06,
	0x2027, 0x06,
	0x2028, 0x0f,
	0x202a, 0xbd,
	0x202b, 0xaa,
	0x2026, 0x3a,
	0x2021, 0xb0,
	0x2003, 0x10,
	0x2006, 0x00,
	0x2007, 0x20,
	0x2008, 0x00,
	0x2009, 0x00,
	0x200a, 0x11,
	0x3511, 0x0c,
	0x3520, 0x06,
	0x3521, 0x03,
	0x3522, 0x03,
	0x3512, 0x12,
	0x3513, 0x30,
	0x3514, 0x54,
	0x3515, 0x06,
	0x1113, 0x01,
	0x1028, 0x32,
	0x1141, 0x40,
	0x1010, 0x0b,
	0x1011, 0x16,
	0x1012, 0x29,
	0x1013, 0x3f,
	0x1014, 0x06,
	0x1015, 0x04,
	0x1016, 0x02,
	0x1017, 0x00,
	0x101c, 0x8c,
	0x101d, 0x8c,
	0x101e, 0x0c,
	0x101f, 0x0c,
	0x2032, 0x1f,
	0x2033, 0x11,
	0x2034, 0x13,
	0x2035, 0x1d,
	0x2036, 0x2a,
	0x2037, 0x31,
	0x1110, 0x1a,
	0x1221, 0xe3,
	0x1200, 0x1b,
	0x1201, 0x5d,
	0x1202, 0x15,
	0x1203, 0x44,
	0x1204, 0x01,
	0x1205, 0x1b,
	0x1206, 0x5d,
	0x1207, 0x05,
	0x3810, 0x05,
	0x3811, 0xa0,
	0x3812, 0x05,
	0x3813, 0xdc,
	0x3500, 0x02,
	0x3501, 0xc0,
	0x3509, 0x10,
	0x0100, 0x01,

    0xFFFF,0xFF, //end flag,must
};

const td_u32 vs4388_2560x1440p60_linear_init_tbl[]={
    
    0xFFFF,0xFF, //end flag,must
};
const td_u32 vs4388_2560x1440p30_2to1wdr_init_tbl[]={
    
    0xFFFF,0xFF, //end flag,must
};
