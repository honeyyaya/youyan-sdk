/*
  Copyright (c), 2001-2025, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <time.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/prctl.h>

#include "securec.h"
#include "ot_common_video.h"
#include "ss_mpi_vpss.h"
#include "ss_mpi_venc.h"
#include "algorithm.h"
#include "font.h"
#include "media.h"
#include "peripheral.h"
#include "ss_mpi_awb.h"
#include "ss_mpi_isp.h"
#include "ss_mpi_ae.h"
#include "ss_mpi_syskol.h"
#include "ss_mpi_sys.h"
#include "ss_mpi_vi.h"
#include "media_venc.h"
#include "kol_timestamp.h"
#include "scene.h"
#include "region.h"
#include "state_machine_action.h"

#define FAILE_TIMES_LIMIT 2
#define KOL_TIMES_LIMIT   5
#define THREAD_NAME_LEN 16
#define KOL_CLOSE_LIGHT_ISO 1600

typedef enum {
    KOL_COLOR_MODE_ONLY = 0,
    KOL_IR_MODE_ONLY,
    KOL_COLOR_MODE_WITH_LIGHT,
    KOL_IR_MODE_WITH_LIGHT,
    KOL_IR_MODE_BUTT
} kol_ir_mode;

typedef struct {
    td_u32 exp_time;
    td_u32 a_gain;
    td_u32 d_gain;
    td_u32 isp_d_gain;
} kol_exp_attr;

typedef struct {
    pthread_t thread_id;
    td_bool is_running;
    td_bool is_pause;
    td_bool mode_change;
    kol_ir_mode last_mode;
    td_bool need_light;
    td_bool is_ir_mode;
    ot_isp_ir_auto_attr ir_auto_attr;
    td_bool has_param[KOL_IR_MODE_BUTT];
    kol_exp_attr last_exp[KOL_IR_MODE_BUTT];
} kol_exp_info;

static td_void kol_enter(td_u32 route_num, kol_start_state last_state);
static td_void kol_running(td_u32 route_num);
static td_void kol_exit(td_u32 route_num, kol_start_state next_state);
static td_void convergenc_enter(td_u32 route_num, kol_start_state last_state);
static td_void convergenc_running(td_u32 route_num);
static td_void convergenc_exit(td_u32 route_num, kol_start_state next_state);
static td_void record_enter(td_u32 route_num, kol_start_state last_state);
static td_void record_running(td_u32 route_num);
static td_void record_exit(td_u32 route_num, kol_start_state next_state);

static kol_start_state g_start_state = NORMAL_CONVERGENC_STATE;
static kol_start_state g_single_state = START_STATE_BUTT;
static kol_start_state g_current_state = NORMAL_CONVERGENC_STATE;

#define THREAD_NICE_INCR (-10)
static sem_t g_sem_sensor_standby[MAX_ROUTE_CNT];
static td_bool g_sensor_standby_flag[MAX_ROUTE_CNT];
static pthread_t g_sensor_standby_thread_id[MAX_ROUTE_CNT];
static sem_t g_sem_kol_run[MAX_ROUTE_CNT];
static sem_t g_sem_kol_done[MAX_ROUTE_CNT];
static sem_t g_sem_kol_stream[MAX_ROUTE_CNT];
static sem_t g_sem_convergenc_run[MAX_ROUTE_CNT];
static sem_t g_sem_convergenc_done[MAX_ROUTE_CNT];
static sem_t g_sem_record_run[MAX_ROUTE_CNT];
static sem_t g_sem_record_done[MAX_ROUTE_CNT];

static td_bool g_kol_run_flag[MAX_ROUTE_CNT];
static td_bool g_convergenc_run_flag[MAX_ROUTE_CNT];
static td_bool g_record_run_flag[MAX_ROUTE_CNT];

static pthread_t g_kol_proc_thread_id[MAX_ROUTE_CNT];
static pthread_t g_convergenc_proc_thread_id[MAX_ROUTE_CNT];
static pthread_t g_record_proc_thread_id[MAX_ROUTE_CNT];

static td_bool g_is_convergenc_finish[MAX_ROUTE_CNT];
static td_bool g_is_detected_target[MAX_ROUTE_CNT];

static td_u64 g_sensor_standby_pts[MAX_ROUTE_CNT];
static td_u64 g_last_stream_pts[MAX_ROUTE_CNT];
static td_bool g_sensor_enable[MAX_ROUTE_CNT] = { TD_TRUE, TD_TRUE };

static td_u32 g_normal_run_count[MAX_ROUTE_CNT] = {0};

static ot_syskol_sleep_mode g_kol_sleep_mode = OT_SYSKOL_SLEEP_MODE_LIGHT;
static td_s32 g_kol_sleep_time = 1000;
static td_bool g_mcu_enable = TD_FALSE;

#define IR_CUT_GPIO_NUM 2
td_u32 g_ir_cut_gpio[MAX_ROUTE_CNT][IR_CUT_GPIO_NUM] = { 0 };
static td_u32 g_kol_iso_threshold = 3200;
static kol_exp_info g_kol_exp_info[MAX_ROUTE_CNT] = { 0 };

static td_char *g_state_str[START_STATE_BUTT] = {
    "KOL_STATE",
    "NORMAL_CONVERGENC_STATE",
    "NORMAL_RECORD_STATE"
};

static td_bool g_convergenc_times_out = TD_FALSE;
static td_s32 g_convergenc_fail_times = 0;
static td_s32 g_kol_running_times = 0;

static td_bool g_scene_pause_state = TD_TRUE;
static td_u32 g_exp_time[MAX_ROUTE_CNT][EXPOSURE_DELAY_NUM] = {0};
static td_bool g_is_kol_running = TD_FALSE;

td_void set_kol_sleep_mode(td_u32 mode)
{
    g_kol_sleep_mode = (ot_syskol_sleep_mode)mode;
}

td_void set_kol_sleep_time(td_s32 time)
{
    g_kol_sleep_time = time;
}

td_void set_kol_iso_threshold(td_u32 threshold)
{
    g_kol_iso_threshold = threshold;
}

static td_void communicate_with_mcu(td_void)
{
    if (!g_mcu_enable) {
        return;
    }
    if (!peripheral_mode_flag1()) {
        return;
    }
    // sample_print("mode_flag1\n");
    mcu_notify_info info;
    peripheral_get_mcu_notify(&info);
    if (info.wakeup == WAKEUP_4G_MODE) {
        g_start_state = NORMAL_RECORD_STATE;
    }
    if (info.switch_state == MCU_SWITCH_STATE_ENTER_SINGLE) {
        set_single_state(info.mode == MCU_MODE_KOL ? KOL_STATE : NORMAL_RECORD_STATE);
    } else if (info.switch_state == MCU_SWITCH_STATE_EXIT_SINGLE) {
        set_single_state(START_STATE_BUTT);
    }
    if (info.has_power_test_data) {
        peripheral_get_power_test();
    }
}

static td_void wait_sensor_standby_done(td_u32 route_num)
{
    td_u64 start, end;
    ss_mpi_sys_get_cur_pts(&start);
    while (1) {
        td_bool sensor_off_flag = TD_TRUE;
        for (td_u32 i = 0; i < route_num; i++) {
            if (g_sensor_enable[i] == TD_TRUE) {
                sensor_off_flag = TD_FALSE;
                break;
            }
        }
        if (sensor_off_flag == TD_TRUE) {
            break;
        }
        ss_mpi_sys_get_cur_pts(&end);
        if ((end - start) > 1000 * 1000 * 5) { /* 1000: second to microsecond 5s */
            sample_print("sensor not off.\n");
            break;
        }
        usleep(10 * 1000); /* 1000: 10ms Allowing the CPU */
    }
    for (td_u32 i = 0; i < route_num; i++) {
        g_sensor_enable[i] = TD_FALSE;
    }
}

#define KOL_WHITE_LIGHT_PWM            1
#define KOL_WHITE_LIGHT_DEFAULT_PERIOD 1000000
#define KOL_WHITE_LIGHT_DEFAULT_DUTY   330000
#define KOL_WHITE_LIGHT_POLARITY       "normal"

#define KOL_IR_LIGHT_PWM               3
#define KOL_IR_LIGHT_DEFAULT_PERIOD    1000000
#define KOL_IR_LIGHT_DEFAULT_DUTY      280000
#define KOL_IR_LIGHT_POLARITY          "normal"

static td_void kol_white_light_controll(td_bool enable)
{
#ifdef LIGHT_ENABLE
    if (enable) {
        kol_pwm_enable(KOL_WHITE_LIGHT_PWM);
    } else {
        kol_pwm_disable(KOL_WHITE_LIGHT_PWM);
    }
#endif
}

static td_void kol_ir_light_controll(td_bool enable)
{
#ifdef LIGHT_ENABLE
    if (enable) {
        kol_pwm_enable(KOL_IR_LIGHT_PWM);
    } else {
        kol_pwm_disable(KOL_IR_LIGHT_PWM);
    }
#endif
}

static td_void kol_sns_slave_ctrl(td_bool enable)
{
    ot_slave_dev slave_dev = 0;
    ot_isp_slave_sns_sync sns_sync;
    ss_mpi_isp_get_sns_slave_attr(slave_dev, &sns_sync);
    if (enable == TD_TRUE) {
        sns_sync.cfg.bits.bit_h_enable = 1;
        sns_sync.cfg.bits.bit_v_enable = 1;
    } else {
        sns_sync.cfg.bits.bit_h_enable = 0;
        sns_sync.cfg.bits.bit_v_enable = 0;
    }
    ss_mpi_isp_set_sns_slave_attr(slave_dev, &sns_sync);
}

static td_void slave_pin_mux_ctrl(td_bool enable)
{
    if (enable == TD_TRUE) {
        sample_sys_set_reg(0x17940090, 0x1547); /* SENSOR0_HS */
    } else {
        sample_sys_set_reg(0x17940090, 0x1200); /* GPIO7_0 */
    }
}

static td_void sensor_slave_config(td_u32 route_num)
{
    for (td_u32 i = 0; i < route_num; i++) {
        kol_route_info *route_info = media_get_route_info(i);
        if (route_info == TD_NULL) {
            return;
        }
        if (route_info->sns_type != SC4336P_MIPI_2M_30FPS_10BIT) {
            continue;
        }
        ot_isp_sns_obj *sns_obj = sample_comm_isp_get_sns_obj(route_info->sns_type);
        if (sns_obj != TD_NULL && sns_obj->pfn_write_reg != TD_NULL) {
            sns_obj->pfn_write_reg(route_info->vi_pipe, 0x3222, 0x01);
        }
    }
}

static td_void sensor_master_config(td_u32 route_num)
{
    for (td_u32 i = 0; i < route_num; i++) {
        kol_route_info *route_info = media_get_route_info(i);
        if (route_info == TD_NULL) {
            return;
        }
        if (route_info->sns_type != SC4336P_MIPI_2M_30FPS_10BIT) {
            continue;
        }
        ot_isp_sns_obj *sns_obj = sample_comm_isp_get_sns_obj(route_info->sns_type);
        if (sns_obj != TD_NULL && sns_obj->pfn_write_reg != TD_NULL) {
            sns_obj->pfn_write_reg(route_info->vi_pipe, 0x3222, 0x00);
        }
    }
}

static td_void restart_sensor_by_route(td_u32 route_id)
{
    ot_isp_sns_obj *sns_obj;
    kol_route_info *route_info;
    kol_timestamp *timestamp = kol_get_timestamp();

    route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return;
    }
    sns_obj = sample_comm_isp_get_sns_obj(route_info->sns_type);
    if (sns_obj == TD_NULL) {
        sample_print("sensor %d not exist!\n", route_info->sns_type);
        return;
    }
    if (sns_obj->pfn_restart == TD_NULL) {
        sample_print("pfn_restart not register!\n");
        return;
    }

    /* turn on LED here if needed (KOL_STATE) */
    if (route_id == 0 && g_kol_exp_info[0].need_light &&
        (g_is_kol_running == TD_TRUE || g_start_state == NORMAL_CONVERGENC_STATE)) {
        kol_ir_light_controll(TD_TRUE);
    }

    sns_obj->pfn_restart(route_info->vi_pipe);
    g_sensor_enable[route_id] = TD_TRUE;
    ss_mpi_sys_get_cur_pts(&timestamp->sensor_restart[route_id]);
    if (get_statistics_switch() == TD_TRUE) {
        sample_print("route[%u] restart pts[%llu]\n", route_id, timestamp->sensor_restart[route_id]);
    }
    update_sensor_restart_times(route_id);
}

typedef struct {
    td_u32 route_id;
    td_u32 delay_time;
} align_time_info;

static td_s32 compar(const void *a, const void *b)
{
    return (td_s32)(((align_time_info *)b)->delay_time - ((align_time_info *)a)->delay_time);
}

static td_void restart_sensor_normal(td_u32 route_num)
{
    td_u32 half_frame_length = 1000000 / FRAME_RATE / 2; /* 1000000:second to microsecond 1s; 2: half */
    sensor_slave_config(route_num);

    align_time_info align_time[MAX_ROUTE_CNT + 1];
    align_time[0].route_id = 0; /* 0:sensor A */
    align_time[0].delay_time = g_exp_time[0][1] + half_frame_length; /* 0:sensor A */
    align_time[1].route_id = 1; /* 1:sensor C */
    align_time[1].delay_time = g_exp_time[1][1] + half_frame_length; /* 1:sensor C */
    align_time[2].route_id = 2; /* 2:sensor B */
    align_time[2].delay_time = g_exp_time[2][1]; /* 2:sensor B */
    align_time[3].route_id = 3; /* 3:sensor D */
    align_time[3].delay_time = g_exp_time[3][1]; /* 3:sensor D */
    align_time[4].route_id = 4; /* 4: VS */
    align_time[4].delay_time = half_frame_length; /* 4: VS */

    qsort(align_time, MAX_ROUTE_CNT + 1, sizeof(align_time[0]), compar);
    for (td_u32 i = 0; i < MAX_ROUTE_CNT + 1; i++) {
        if (i > 0) {
            usleep(align_time[i - 1].delay_time - align_time[i].delay_time);
        }
        if (align_time[i].route_id == MAX_ROUTE_CNT) {
            kol_sns_slave_ctrl(TD_TRUE);
        } else {
            restart_sensor_by_route(align_time[i].route_id);
        }
    }
}

#define GPIO7_0_NUM 56

static td_void restart_sensor_by_grp(td_u32 grp_id)
{
    td_u32 route_id_start = grp_id << 1;

    /* MIPI SWITCH */
    kol_gpio_export(GPIO7_0_NUM);
    kol_set_gpio_direction(GPIO7_0_NUM, "out");
    kol_write_gpio_value(GPIO7_0_NUM, grp_id);
    kol_gpio_unexport(GPIO7_0_NUM);

    /* set VI distrube */
    ss_mpi_vi_distribute_one_frame(0, (td_s32)route_id_start);
    ss_mpi_vi_distribute_one_frame(1, (td_s32)(route_id_start + 1));

    restart_sensor_by_route(route_id_start);
    restart_sensor_by_route(route_id_start + 1);
}

static td_void restart_sensor(td_u32 route_num)
{
    if (route_num == MAX_ROUTE_CNT) {
        if (g_is_kol_running == TD_TRUE) {
            restart_sensor_by_grp(0);
        } else {
            restart_sensor_normal(route_num);
        }
    } else {
        for (td_u32 i = 0; i < route_num; i++) {
            restart_sensor_by_route(i);
        }
    }
}

static td_void standby_sensor_by_route(td_u32 route_id)
{
    ot_isp_sns_obj *sns_obj;
    kol_route_info *route_info;

    ss_mpi_sys_get_cur_pts(&g_sensor_standby_pts[route_id]);
    if (get_statistics_switch() == TD_TRUE) {
        sample_print("route[%u] standby pts[%llu]\n", route_id, g_sensor_standby_pts[route_id]);
    }
    route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return;
    }
    sns_obj = sample_comm_isp_get_sns_obj(route_info->sns_type);
    if (sns_obj == TD_NULL) {
        sample_print("sensor %d not exist!\n", route_info->sns_type);
        return;
    }
    if (sns_obj->pfn_standby == TD_NULL) {
        sample_print("pfn_standby not register!\n");
        return;
    }
    sns_obj->pfn_standby(route_info->vi_pipe);
    /* turn off LED here if needed (KOL_STATE) */
    if (route_id == 0) {
        kol_ir_light_controll(TD_FALSE);
    }
    g_sensor_enable[route_id] = TD_FALSE;
    update_sensor_standby_times(route_id);
}

static td_void set_fast_ae_by_route(td_u32 route_id)
{
    ot_isp_sns_obj *sns_obj;
    kol_route_info *route_info;
    ot_isp_sns_fast_ae_attr fast_ae_attr;

    route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return;
    }
    sns_obj = sample_comm_isp_get_sns_obj(route_info->sns_type);
    if (sns_obj == TD_NULL) {
        printf("sensor %d not exist!\n", route_info->sns_type);
        return;
    }
    if (sns_obj->pfn_set_fast_ae == TD_NULL) {
        printf("pfn_set_fast_ae not register!\n");
        return;
    }
    fast_ae_attr.enable = TD_TRUE;
    sns_obj->pfn_set_fast_ae(route_info->vi_pipe, &fast_ae_attr);
}

static td_void set_thread_name(const td_char *name, td_u32 route_id)
{
    td_char thread_name[THREAD_NAME_LEN] = {0};

    if (snprintf_s(thread_name, sizeof(thread_name), sizeof(thread_name) - 1, "%s%u", name, route_id) == -1) {
        return;
    }
    prctl(PR_SET_NAME, thread_name, 0, 0, 0);
}

static td_void kol_cut_switch_to_ir(ot_vi_pipe vi_pipe)
{
#ifdef IR_CUT_ENABLE
    kol_write_gpio_value(g_ir_cut_gpio[vi_pipe][0], 1);
    kol_write_gpio_value(g_ir_cut_gpio[vi_pipe][1], 0);
#endif
}

static td_void kol_cut_switch_to_normal(ot_vi_pipe vi_pipe)
{
#ifdef IR_CUT_ENABLE
    kol_write_gpio_value(g_ir_cut_gpio[vi_pipe][1], 1);
    kol_write_gpio_value(g_ir_cut_gpio[vi_pipe][0], 0);
#endif
}

static td_void kol_exp_io_init(td_void)
{
#ifdef IR_CUT_ENABLE
    /* depend on ir_auto */
    if (BOARD_TYPE == DMEB_BGA) {
        g_ir_cut_gpio[0][0] = 9;  /* 9: GPIO1_1 */
        g_ir_cut_gpio[0][1] = 62; /* 62: GPIO7_6 */
        g_ir_cut_gpio[1][0] = 63; /* 63: GPIO7_7 */
        g_ir_cut_gpio[1][1] = 8;  /* 8: GPIO1_0 */
    } else {
        g_ir_cut_gpio[0][0] = 63; /* 63: GPIO7_7 */
        g_ir_cut_gpio[0][1] = 8;  /* 8: GPIO1_0 */
        g_ir_cut_gpio[1][0] = 10; /* 10: GPIO1_2 */
        g_ir_cut_gpio[1][1] = 11; /* 11: GPIO1_3 */
    }

    for (int i = 0; i < MAX_ROUTE_CNT; i++) {
        for (int j = 0; j < IR_CUT_GPIO_NUM; j++) {
            kol_gpio_export(g_ir_cut_gpio[i][j]);
            kol_set_gpio_direction(g_ir_cut_gpio[i][j], "out");
            kol_write_gpio_value(g_ir_cut_gpio[i][j], j == 0 ? 1 : 0);
        }
    }
#endif
    /* need pwm driver open and set pin mux first */
#ifdef LIGHT_ENABLE
    kol_pwm_export(KOL_WHITE_LIGHT_PWM);
    kol_pwm_set_period(KOL_WHITE_LIGHT_PWM, KOL_WHITE_LIGHT_DEFAULT_PERIOD);
    kol_pwm_set_duty_cycle(KOL_WHITE_LIGHT_PWM, KOL_WHITE_LIGHT_DEFAULT_DUTY);
    kol_pwm_set_polarity(KOL_WHITE_LIGHT_PWM, KOL_WHITE_LIGHT_POLARITY);

    kol_pwm_export(KOL_IR_LIGHT_PWM);
    kol_pwm_set_period(KOL_IR_LIGHT_PWM, KOL_IR_LIGHT_DEFAULT_PERIOD);
    kol_pwm_set_duty_cycle(KOL_IR_LIGHT_PWM, KOL_IR_LIGHT_DEFAULT_DUTY);
    kol_pwm_set_polarity(KOL_IR_LIGHT_PWM, KOL_IR_LIGHT_POLARITY);
#endif
    kol_white_light_controll(TD_FALSE);
    kol_ir_light_controll(TD_FALSE);
}

static td_void kol_exp_io_deinit(td_void)
{
    kol_white_light_controll(TD_FALSE);
    kol_ir_light_controll(TD_FALSE);
#ifdef LIGHT_ENABLE
    kol_pwm_unexport(KOL_WHITE_LIGHT_PWM);
    kol_pwm_unexport(KOL_IR_LIGHT_PWM);
#endif
#ifdef IR_CUT_ENABLE
    for (int i = 0; i < MAX_ROUTE_CNT; i++) {
        for (int j = 0; j < IR_CUT_GPIO_NUM; j++) {
            kol_gpio_unexport(g_ir_cut_gpio[i][j]);
        }
    }
#endif
}

static td_void kol_exp_switch_scene_mode(td_u32 route_id, td_u32 is_ir_mode)
{
    static td_u32 last_ir_mode = -1;

    if (route_id != 0) {
        return;
    }

    if (last_ir_mode != is_ir_mode) {
        td_bool need_pause = !g_scene_pause_state;
        if (need_pause) {
            scene_pause();
        }
        scene_set_scene_mode(is_ir_mode > 0 ? 1 : 0);
        if (need_pause) {
            scene_resume();
        }
    }
    last_ir_mode = is_ir_mode;
}

static td_s32 kol_exp_switch_to_normal(ot_vi_pipe vi_pipe)
{
    td_s32 ret;
    ot_isp_saturation_attr isp_saturation_attr;
    ot_isp_wb_attr isp_wb_attr;
    ot_isp_color_matrix_attr isp_ccm_attr;

    /* switch ir-cut to normal */
    kol_cut_switch_to_normal(vi_pipe);
    /* switch isp config to normal */
    ret = ss_mpi_isp_get_saturation_attr(vi_pipe, &isp_saturation_attr);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_isp_get_saturation_attr failed\n");
        return ret;
    }
    isp_saturation_attr.op_type = OT_OP_MODE_AUTO;
    ret = ss_mpi_isp_set_saturation_attr(vi_pipe, &isp_saturation_attr);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_isp_set_saturation_attr failed\n");
        return ret;
    }

    ret = ss_mpi_isp_get_wb_attr(vi_pipe, &isp_wb_attr);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_isp_get_wb_attr failed\n");
        return ret;
    }
    isp_wb_attr.op_type = OT_OP_MODE_AUTO;
    ret = ss_mpi_isp_set_wb_attr(vi_pipe, &isp_wb_attr);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_isp_set_wb_attr failed\n");
        return ret;
    }

    ret = ss_mpi_isp_get_ccm_attr(vi_pipe, &isp_ccm_attr);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_isp_get_ccm_attr failed\n");
        return ret;
    }
    isp_ccm_attr.op_type = OT_OP_MODE_AUTO;
    ret = ss_mpi_isp_set_ccm_attr(vi_pipe, &isp_ccm_attr);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_isp_set_ccm_attr failed\n");
        return ret;
    }

    return TD_SUCCESS;
}

static td_s32 kol_ir_change_saturation_attr(ot_vi_pipe vi_pipe)
{
    td_s32 ret;
    ot_isp_saturation_attr isp_saturation_attr;

    ret = ss_mpi_isp_get_saturation_attr(vi_pipe, &isp_saturation_attr);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_isp_get_saturation_attr failed\n");
        return ret;
    }
    isp_saturation_attr.op_type = OT_OP_MODE_MANUAL;
    isp_saturation_attr.manual_attr.saturation = 0;
    ret = ss_mpi_isp_set_saturation_attr(vi_pipe, &isp_saturation_attr);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_isp_set_saturation_attr failed\n");
        return ret;
    }

    return TD_SUCCESS;
}

static td_s32 kol_ir_change_wb_attr(ot_vi_pipe vi_pipe)
{
    td_s32 ret;
    ot_isp_wb_attr isp_wb_attr;

    ret = ss_mpi_isp_get_wb_attr(vi_pipe, &isp_wb_attr);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_isp_get_wb_attr failed\n");
        return ret;
    }
    isp_wb_attr.op_type = OT_OP_MODE_MANUAL;
    isp_wb_attr.manual_attr.b_gain  = 0x100;
    isp_wb_attr.manual_attr.gb_gain = 0x100;
    isp_wb_attr.manual_attr.gr_gain = 0x100;
    isp_wb_attr.manual_attr.r_gain  = 0x100;
    ret = ss_mpi_isp_set_wb_attr(vi_pipe, &isp_wb_attr);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_isp_set_wb_attr failed\n");
        return ret;
    }

    return TD_SUCCESS;
}

static td_s32 kol_ir_change_ccm_attr(ot_vi_pipe vi_pipe)
{
    td_s32 ret;
    ot_isp_color_matrix_attr isp_ccm_attr;
    ret = ss_mpi_isp_get_ccm_attr(vi_pipe, &isp_ccm_attr);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_isp_get_ccm_attr failed\n");
        return ret;
    }
    isp_ccm_attr.op_type = OT_OP_MODE_MANUAL;
    isp_ccm_attr.manual_attr.ccm[0] = 0x100;
    isp_ccm_attr.manual_attr.ccm[1] = 0x0;
    isp_ccm_attr.manual_attr.ccm[2] = 0x0; // idx 2
    isp_ccm_attr.manual_attr.ccm[3] = 0x0; // idx 3
    isp_ccm_attr.manual_attr.ccm[4] = 0x100; // idx 4
    isp_ccm_attr.manual_attr.ccm[5] = 0x0; // idx 5
    isp_ccm_attr.manual_attr.ccm[6] = 0x0; // idx 6
    isp_ccm_attr.manual_attr.ccm[7] = 0x0; // idx 7
    isp_ccm_attr.manual_attr.ccm[8] = 0x100; // idx 8
    ret = ss_mpi_isp_set_ccm_attr(vi_pipe, &isp_ccm_attr);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_isp_set_ccm_attr failed\n");
        return ret;
    }
    return TD_SUCCESS;
}

static td_s32 kol_exp_switch_to_ir_part1(ot_vi_pipe vi_pipe)
{
    td_s32 ret;

    ret = kol_ir_change_saturation_attr(vi_pipe);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    ret = kol_ir_change_wb_attr(vi_pipe);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    ret = kol_ir_change_ccm_attr(vi_pipe);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    return TD_SUCCESS;
}

static td_s32 kol_exp_switch_to_ir(ot_vi_pipe vi_pipe)
{
    td_s32 ret;

    ret = kol_exp_switch_to_ir_part1(vi_pipe);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    usleep(1000000); /* sleep 1000000 us */

    /* switch ir-cut to ir */
    kol_cut_switch_to_ir(vi_pipe);

    return TD_SUCCESS;
}

static td_void kol_exp_controll(td_u32 route_id, ot_vi_pipe vi_pipe, td_bool is_ir_mode)
{
    kol_exp_switch_scene_mode(route_id, is_ir_mode);
    if (is_ir_mode == TD_TRUE) {
        kol_exp_switch_to_ir(vi_pipe);
    } else {
        kol_exp_switch_to_normal(vi_pipe);
    }
}

static td_void kol_update_ir_info(ot_isp_ir_auto_attr *ir_attr, kol_exp_info *info)
{
    ot_isp_ir_switch_status ir_switch_status;

    ir_switch_status = ir_attr->ir_switch;
    if (ir_switch_status == OT_ISP_IR_SWITCH_TO_IR) {
        ir_attr->ir_status = OT_ISP_IR_STATUS_IR;
        info->is_ir_mode = TD_TRUE;
    } else if (ir_switch_status == OT_ISP_IR_SWITCH_TO_NORMAL) {
        ir_attr->ir_status = OT_ISP_IR_STATUS_NORMAL;
        info->is_ir_mode = TD_FALSE;
    }
    if (g_start_state == NORMAL_RECORD_STATE) {
        ir_attr->ir_status = OT_ISP_IR_STATUS_NORMAL;
        info->is_ir_mode = TD_FALSE;
    }
    if (g_start_state == KOL_STATE && info->need_light) {
        ir_attr->ir_status = OT_ISP_IR_STATUS_IR;
        info->is_ir_mode = TD_TRUE;
    }
}

static td_void kol_backup_exp_info(const ot_isp_exp_info *exp_info, kol_ir_mode mode, kol_exp_info *info)
{
    info->last_exp[mode].exp_time = exp_info->exp_time;
    info->last_exp[mode].a_gain = exp_info->a_gain;
    info->last_exp[mode].d_gain = exp_info->d_gain;
    info->last_exp[mode].isp_d_gain = exp_info->isp_d_gain;
    info->has_param[mode] = TD_TRUE;
}

static td_void kol_update_manual_exp_attr(ot_vi_pipe vi_pipe, kol_ir_mode cur_mode, kol_exp_info *info)
{
    ot_isp_exposure_attr exp_attr;

    if (info->has_param[cur_mode]) {
        info->mode_change = TD_TRUE;
        ss_mpi_isp_get_exposure_attr(vi_pipe, &exp_attr);
        exp_attr.op_type = OT_OP_MODE_MANUAL;
        exp_attr.manual_attr.exp_time_op_type = OT_OP_MODE_MANUAL;
        exp_attr.manual_attr.a_gain_op_type = OT_OP_MODE_MANUAL;
        exp_attr.manual_attr.d_gain_op_type = OT_OP_MODE_MANUAL;
        exp_attr.manual_attr.ispd_gain_op_type = OT_OP_MODE_MANUAL;
        exp_attr.manual_attr.exp_time = info->last_exp[cur_mode].exp_time;
        exp_attr.manual_attr.a_gain = info->last_exp[cur_mode].a_gain;
        exp_attr.manual_attr.d_gain = info->last_exp[cur_mode].d_gain;
        exp_attr.manual_attr.isp_d_gain = info->last_exp[cur_mode].isp_d_gain;
        ss_mpi_isp_set_exposure_attr(vi_pipe, &exp_attr);
    }
}

static td_void kol_update_auto_exp_attr(ot_vi_pipe vi_pipe, kol_exp_info *info)
{
    ot_isp_exposure_attr exp_attr;

    if (info->mode_change == TD_TRUE) {
        info->mode_change = TD_FALSE;
        ss_mpi_isp_get_exposure_attr(vi_pipe, &exp_attr);
        exp_attr.op_type = OT_OP_MODE_AUTO;
        ss_mpi_isp_set_exposure_attr(vi_pipe, &exp_attr);
    }
}

static ot_isp_ir_auto_attr g_ir_auto_attr = {
    .enable = TD_TRUE,
    .normal_to_ir_iso_threshold = 3200,
    .ir_to_normal_iso_threshold = 400,
    .rg_max = 280,
    .rg_min = 190,
    .bg_max = 280,
    .bg_min = 190,
    .ir_status = OT_ISP_IR_STATUS_IR
};

static kol_ir_mode get_ir_mode(const kol_exp_info *info)
{
    if (info->need_light == TD_TRUE) {
        if (info->is_ir_mode == TD_TRUE) {
            return KOL_IR_MODE_WITH_LIGHT;
        } else {
            return KOL_COLOR_MODE_WITH_LIGHT;
        }
    } else {
        if (info->is_ir_mode == TD_TRUE) {
            return KOL_IR_MODE_ONLY;
        } else {
            return KOL_COLOR_MODE_ONLY;
        }
    }
}

static td_void kol_exp_process(td_u32 route_id, kol_exp_info *info)
{
    kol_route_info *route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return;
    }
    ot_vi_pipe vi_pipe = route_info->vi_pipe;
    kol_ir_mode cur_mode;
    ot_isp_exp_info exp_info;

    if (ss_mpi_isp_ir_auto(vi_pipe, &info->ir_auto_attr) != TD_SUCCESS) {
        return;
    }
    if (ss_mpi_isp_query_exposure_info(vi_pipe, &exp_info) != TD_SUCCESS) {
        return;
    }
    /* change exp info to auto mode when next mode */
    kol_update_auto_exp_attr(vi_pipe, info);
    if (exp_info.iso > g_kol_iso_threshold) {
        info->need_light = TD_TRUE;
    } else if (exp_info.iso < KOL_CLOSE_LIGHT_ISO) {
        info->need_light = TD_FALSE;
    }
    kol_update_ir_info(&info->ir_auto_attr, info);
    cur_mode = get_ir_mode(info);
    if (info->last_mode != cur_mode) {
        kol_update_manual_exp_attr(vi_pipe, cur_mode, info);
        kol_exp_controll(route_id, vi_pipe, info->is_ir_mode);
    } else {
        /* backup exp info for next time convergence quickly when the same mode reappears */
        kol_backup_exp_info(&exp_info, cur_mode, info);
    }
    info->last_mode = cur_mode;
}

static td_void *kol_exp_thread(td_void *param)
{
    td_u32 route_id = *(td_u32 *)param;
    kol_exp_info *info = &g_kol_exp_info[route_id];
    kol_route_info *route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return TD_NULL;
    }
    ot_vi_pipe vi_pipe = route_info->vi_pipe;

    set_thread_name("exp_thread", route_id);
    kol_exp_io_init();
    info->is_pause = TD_FALSE;
    info->last_mode = KOL_IR_MODE_BUTT;
    (td_void)memcpy_s(&info->ir_auto_attr, sizeof(ot_isp_ir_auto_attr), &g_ir_auto_attr, sizeof(ot_isp_ir_auto_attr));
    while (info->is_running) {
        if (g_start_state == NORMAL_CONVERGENC_STATE || info->is_pause == TD_TRUE) {
            usleep(300 * 1000); /* 300 * 1000: sleep time in normal fps */
            continue;
        }
        if (ss_mpi_isp_get_vd_time_out(vi_pipe, OT_ISP_VD_FE_START, EXP_VD_TIMEOUT) == TD_SUCCESS) {
            kol_exp_process(route_id, info);
        }
    }
    kol_exp_io_deinit();

    return TD_NULL;
}

static td_void kol_exp_process_record_exit_part1(td_u32 route_id)
{
    kol_route_info *route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return;
    }
    ot_vi_pipe vi_pipe = route_info->vi_pipe;
    kol_exp_info *info = &g_kol_exp_info[route_id];

    if (info->need_light == TD_TRUE) {
        info->ir_auto_attr.ir_status = OT_ISP_IR_STATUS_IR;
        info->is_ir_mode = TD_TRUE;
    }

    kol_ir_mode cur_mode = get_ir_mode(info);
    if (info->last_mode != cur_mode) {
        kol_update_manual_exp_attr(vi_pipe, cur_mode, info);
        kol_exp_switch_scene_mode(route_id, info->is_ir_mode);
        if (info->is_ir_mode == TD_TRUE) {
            kol_exp_switch_to_ir_part1(vi_pipe);
        }
    }
}

static td_void kol_exp_process_record_exit_part2(td_u32 route_id)
{
    kol_route_info *route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return;
    }
    ot_vi_pipe vi_pipe = route_info->vi_pipe;
    kol_exp_info *info = &g_kol_exp_info[route_id];

    kol_ir_mode cur_mode = get_ir_mode(info);
    if (info->last_mode != cur_mode) {
        if (info->is_ir_mode == TD_TRUE) {
            kol_cut_switch_to_ir(vi_pipe);
        }
        kol_update_auto_exp_attr(vi_pipe, info);
    }
    info->last_mode = cur_mode;
    info->is_pause = TD_FALSE;
}

static td_bool check_ae_result_finished(td_u32 route_id)
{
    td_s32 ret = TD_SUCCESS;
    kol_route_info *route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return TD_FALSE;
    }
    kol_timestamp *timestamp = kol_get_timestamp();

    ret = ss_mpi_isp_get_vd_time_out(route_info->vi_pipe, OT_ISP_VD_RUN_END, AE_VD_TIMEOUT);
    if (ret != TD_SUCCESS) {
        sample_print("pipe[%d] get vd RUN_END failed.[0x%x]\n", route_info->vi_pipe, ret);
    }
    ss_mpi_sys_get_cur_pts(&timestamp->run_end_pts[route_id]);

    ot_isp_exp_info isp_exp_info;
    ret = ss_mpi_isp_query_exposure_info(route_info->vi_pipe, &isp_exp_info);
    if (ret != TD_SUCCESS) {
        sample_print("pipe[%d] query exp info failed.[0x%x]\n", route_info->vi_pipe, ret);
        return TD_FALSE;
    }

    for (td_s32 i = EXPOSURE_DELAY_NUM - 1; i > 0; i--) {
        g_exp_time[route_id][i] = g_exp_time[route_id][i - 1];
    }
    g_exp_time[route_id][0] = isp_exp_info.exp_time;

    if (isp_exp_info.first_stable_time == 0) {
        return TD_FALSE;
    }

    return TD_TRUE;
}

static td_bool kol_check_target_detected(td_u32 route_id)
{
    td_s32 ret;
    ot_video_frame_info video_frame_info = {0};
    kol_route_info *route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return TD_FALSE;
    }
    kol_timestamp *timestamp = kol_get_timestamp();
    td_bool detected_target = TD_FALSE;

    if ((media_get_aidetect_mask() & (1 << route_id)) == 0) {
        return TD_FALSE;
    }

    ret = ss_mpi_vpss_get_chn_frame(route_info->vpss_grp, 1, &video_frame_info, GET_VPSS_CHN_FRAME_TIMEOUT);
    if (ret != TD_SUCCESS) {
        sample_print("vpss[%d] get chn[1] frame failed[0x%x].\n", route_info->vpss_grp, ret);
        return TD_FALSE;
    }

    ss_mpi_sys_get_cur_pts(&timestamp->before_aidetect[route_id]);
    aidetect_rect corner_rect[AIDETECT_MAX_CLASS_NUM] = {0};
    ot_size size;
    sample_comm_vi_get_size_by_sns_type(route_info->sns_type, &size);
    ret = algorithm_aidetect_proc(&video_frame_info, route_id, corner_rect, (td_s32)size.width, (td_s32)size.height);
    if (ret != TD_SUCCESS) {
        sample_print("route[%d] aidetect proc failed.[0x%x].\n", route_info->vpss_grp, ret);
    }
    detected_target = algorithm_detected_target(corner_rect, AIDETECT_MAX_CLASS_NUM);
    ss_mpi_sys_get_cur_pts(&timestamp->after_aidetect[route_id]);

    ret = ss_mpi_vpss_release_chn_frame(route_info->vpss_grp, 1, &video_frame_info);
    if (ret != TD_SUCCESS) {
        sample_print("vpss[%d] release chn frame failed.[0x%x].\n", route_info->vpss_grp, ret);
    }
    return detected_target;
}

static td_s32 time_state_to_region(td_u32 route_id, kol_start_state state)
{
    td_char font_str[MAX_FONT_STR_LEN] = {0};
    td_char exp_str[MAX_FONT_STR_LEN] = {0};
    td_char time_str[MAX_FONT_STR_LEN] = {0};
    kol_route_info *route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return TD_FAILURE;
    }

    time_t time_now;
    struct tm *tm_time;
    (td_void) time(&time_now);
    tm_time = gmtime(&time_now);
    td_s32 ret = snprintf_s(time_str, MAX_FONT_STR_LEN, MAX_FONT_STR_LEN - 1, "%d-%d-%d %d:%d:%d",
        OSD_START_YEAR + tm_time->tm_year, 1 + tm_time->tm_mon, tm_time->tm_mday, tm_time->tm_hour, tm_time->tm_min,
        tm_time->tm_sec);
    if (ret <= 0) {
        sample_print("snprintf_s failed\n");
        return TD_FAILURE;
    }

    td_u32 exp_idx = (state == KOL_STATE) ? 1 : EXPOSURE_DELAY_NUM - 1;
    if (g_exp_time[route_id][exp_idx] > LONG_EXPOSURE_TIME) {
        if (snprintf_s(exp_str, MAX_FONT_STR_LEN, MAX_FONT_STR_LEN - 1, "exposure_time:%u",
            g_exp_time[route_id][exp_idx])  < 0) {
            sample_print("snprintf_s exp_time failed\n");
            return TD_FAILURE;
        }
    }

    ret = snprintf_s(font_str, MAX_FONT_STR_LEN, MAX_FONT_STR_LEN - 1,
        "%s %s %s", time_str, g_state_str[state], exp_str);
    if (ret <= 0) {
        sample_print("snprintf_s failed\n");
        return TD_FAILURE;
    }

    ret = region_draw_font(route_id, route_info->venc_chn[0], font_str);
    if (ret != TD_SUCCESS) {
        sample_print("region_draw_font route_id[%u] failed \n", route_id);
        return ret;
    }

    return TD_SUCCESS;
}

static td_s32 kol_frame_proc(td_u32 route_id)
{
    td_s32 ret = TD_SUCCESS;
    kol_timestamp *timestamp = kol_get_timestamp();

    ss_mpi_sys_get_cur_pts(&timestamp->before_osd[route_id]);
    ret = time_state_to_region(route_id, KOL_STATE);
    if (ret != TD_SUCCESS) {
        sample_print("time state to region route_id[%u] failed.[0x%x].\n", route_id, ret);
    }
    ss_mpi_sys_get_cur_pts(&timestamp->after_osd[route_id]);

    return TD_SUCCESS;
}

static td_void svc_set_rect(td_u64 pts, aidetect_rect *corner_rect, td_u32 route_id, kol_route_info *route_info)
{
    ot_size size;
    sample_comm_vi_get_size_by_sns_type(route_info->sns_type, &size);
    td_s32 ret = algorithm_svc_set_rect(pts, size, corner_rect, route_id, route_info->venc_chn[0]);
    if (ret != TD_SUCCESS) {
        sample_print("algorithm_svc_set_rect failed.[0x%x].\n", ret);
    }
}

static td_void update_exp_info(td_u32 route_id)
{
    ot_isp_exp_info isp_exp_info;
    kol_route_info *route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return;
    }

    td_s32 ret = ss_mpi_isp_query_exposure_info(route_info->vi_pipe, &isp_exp_info);
    if (ret != TD_SUCCESS) {
        sample_print("pipe[%d] query exp info failed.[0x%x]\n", route_info->vi_pipe, ret);
        return;
    }

    for (td_s32 i = EXPOSURE_DELAY_NUM - 1; i > 0; i--) {
        g_exp_time[route_id][i] = g_exp_time[route_id][i - 1];
    }
    g_exp_time[route_id][0] = isp_exp_info.exp_time;
}

static td_s32 record_frame_proc(td_u32 route_id)
{
    td_s32 ret = TD_SUCCESS;
    ot_video_frame_info video_frame_info = {0};
    kol_route_info *route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return TD_FAILURE;
    }
    ot_vpss_chn vpss_chn = 1;
    aidetect_rect corner_rect[AIDETECT_MAX_CLASS_NUM] = {0};
    ot_size size;

    if ((media_get_aidetect_mask() & (1 << route_id)) == 0) {
        ss_mpi_isp_get_vd_time_out(route_info->vi_pipe, OT_ISP_VD_RUN_END, AE_VD_TIMEOUT);
        update_exp_info(route_id);
        goto region_proc;
    }

    ret = ss_mpi_vpss_get_chn_frame(route_info->vpss_grp, vpss_chn, &video_frame_info, GET_VPSS_CHN_FRAME_TIMEOUT);
    if (ret != TD_SUCCESS) {
        sample_print("vpss get chn[1] frame failed[0x%x].\n", ret);
        return ret;
    }
    update_exp_info(route_id);

    sample_comm_vi_get_size_by_sns_type(route_info->sns_type, &size);
    ret = algorithm_aidetect_proc(&video_frame_info, route_id, corner_rect, (td_s32)size.width, (td_s32)size.height);
    if (ret != TD_SUCCESS) {
        sample_print("aidetect proc failed.[0x%x].\n", ret);
    }

    ret = ss_mpi_vpss_release_chn_frame(route_info->vpss_grp, vpss_chn, &video_frame_info);
    if (ret != TD_SUCCESS) {
        sample_print("vpss release chn frame failed.[0x%x].\n", ret);
    }

region_proc:
    ret = time_state_to_region(route_id, NORMAL_RECORD_STATE);
    if (ret != TD_SUCCESS) {
        sample_print("time state to region route_id[%u] failed.[0x%x].\n", route_id, ret);
    }

    ret = region_draw_rect(corner_rect, AIDETECT_MAX_CLASS_NUM, (td_s32)route_id);
    if (ret != TD_SUCCESS) {
        sample_print("algorithm_draw_rect route_id[%u] failed\n", route_id);
    }

    svc_set_rect(video_frame_info.video_frame.pts, corner_rect, route_id, route_info);

    return TD_SUCCESS;
}

static td_void kol_stream_finish_notify(ot_venc_chn venc_chn, td_u64 pts)
{
    td_u32 route_id = media_get_route_by_venc_chn(venc_chn);
    sem_post(&g_sem_kol_stream[route_id]);
    kol_get_timestamp()->frame_pts[route_id] = pts;
}

static td_void normal_stream_finish_notify(ot_venc_chn venc_chn, td_u64 pts)
{
    td_u32 route_id = media_get_route_by_venc_chn(venc_chn);
    g_last_stream_pts[route_id] = pts;
}

static td_void kol_suspend(td_s32 time)
{
    if (g_kol_sleep_mode == OT_SYSKOL_SLEEP_MODE_DEEP) {
        wakeup_timer_start(time);
    }
    ss_mpi_syskol_sleep(time);
    if (g_kol_sleep_mode == OT_SYSKOL_SLEEP_MODE_DEEP) {
        wakeup_timer_stop();
    }
}

static td_void kol_update_param(ot_syskol_capture_mode capture_mode)
{
    ot_syskol_param kol_param;
    kol_param.capture_mode = capture_mode;
    kol_param.sleep_mode = g_kol_sleep_mode;
    if (g_kol_sleep_mode == OT_SYSKOL_SLEEP_MODE_USER) {
        kol_param.user_param.bits.audio_cken = 1;
    }
    ss_mpi_syskol_set_param(&kol_param);
}

static td_void kol_sem_clear(td_u32 route_num)
{
    td_s32 ret;

    for (td_u32 i = 0; i < route_num; i++) {
        do {
            ret = sem_trywait(&g_sem_kol_stream[i]);
            if (ret == 0) {
                printf("kol sem trywait\n");
            }
        } while (ret == 0);
    }
}

static td_void kol_enter(td_u32 route_num, kol_start_state last_state)
{
    // printf("kol_enter last_state[%s].\n", g_state_str[last_state]);
    g_current_state = KOL_STATE;
    if (last_state == NORMAL_CONVERGENC_STATE && g_convergenc_times_out) {
        // printf("convergenc fail.\n");
        g_convergenc_fail_times++;
    } else {
        g_convergenc_fail_times = 0;
    }
    if (last_state == NORMAL_CONVERGENC_STATE) {
        for (td_u32 i = 0; i < route_num; i++) {
            kol_route_info *route_info = media_get_route_info(i);
            if (route_info == TD_NULL) {
                sample_print("route_id[%d] get route info is null!\n", i);
                continue;
            }
            td_s32 ret = ss_mpi_venc_request_idr(route_info->venc_chn[0], TD_TRUE);
            if (ret != TD_SUCCESS) {
                sample_print("venc[%d] ss_mpi_venc_request_idr failed.[0x%x].\n", route_info->venc_chn[0], ret);
            }
        }
    }

    g_kol_running_times = 0;

    kol_update_param(OT_SYSKOL_CAPTURE_MODE_SINGLE);
    if (route_num == MAX_ROUTE_CNT) {
        kol_sns_slave_ctrl(TD_FALSE);
        slave_pin_mux_ctrl(TD_FALSE);
    }
    if (last_state == NORMAL_CONVERGENC_STATE) {
        kol_suspend(100); // 100: sleep 100ms when convertgenc turn to kol.
    } else {
        kol_suspend(g_kol_sleep_time);
    }
    kol_sem_clear(route_num);
    if (route_num == MAX_ROUTE_CNT) {
        sensor_master_config(route_num);
    }
    media_venc_register_notify(kol_stream_finish_notify);

    // clear the  AI detection rects in the KOL_STATE
    for (td_u32 i = 0; i < route_num; i++) {
        td_s32 ret = region_detach_rect((td_s32)i);
        if (ret != TD_SUCCESS) {
            sample_print("region_detach_rect route_id[%u] failed.[0x%x].\n", i, ret);
        }
    }
}

static td_void kol_running(td_u32 route_num)
{
    printf("kol_running.\n");
    kol_timestamp *timestamp = kol_get_timestamp();

    if (g_kol_running_times == KOL_TIMES_LIMIT) {
        g_convergenc_fail_times = 0;
        g_kol_running_times = 0;
    }

    /* kol proc start */
    for (td_u32 i = 0; i < route_num; i++) {
        sem_post(&g_sem_sensor_standby[i]);
        sem_post(&g_sem_kol_run[i]);
    }
    g_is_kol_running = TD_TRUE;
    for (td_u32 i = 0; i < route_num; i++) {
        kol_frame_proc(i);
    }

    restart_sensor(route_num);

    /* kol processing by kol_proc_thread */

    for (td_u32 i = 0; i < route_num; i++) {
        if (g_kol_run_flag[i] == TD_TRUE) {
            sem_wait(&g_sem_kol_done[i]);
        }
    }

    /* kol proc end */

    for (td_u32 i = 0; i < route_num; i++) {
        if (!g_is_convergenc_finish[i]) {
            // printf("do not ae finish.\n");
            g_start_state = NORMAL_CONVERGENC_STATE;
            goto exit;
        }
    }

    for (td_u32 i = 0; i < route_num; i++) {
        if (g_is_detected_target[i] || kol_get_wakeup_record()) {
            // printf("need record, wakeup or detected target[%u]:%d.\n", i, g_is_detected_target[i]);
            g_start_state = NORMAL_RECORD_STATE;
            goto exit;
        }
    }
exit:
    communicate_with_mcu();
    if (g_single_state == KOL_STATE || (g_single_state == START_STATE_BUTT && g_start_state == KOL_STATE)) {
        ss_mpi_sys_get_cur_pts(&timestamp->before_suspend);
        kol_show_timestamp(route_num);
        wait_sensor_standby_done(route_num);
        kol_suspend(g_kol_sleep_time);
        kol_sem_clear(route_num);
        ss_mpi_sys_get_cur_pts(&timestamp->after_suspend);
    } else {
        g_is_kol_running = TD_FALSE;
    }

    g_kol_running_times++;
}

static td_void kol_exit(td_u32 route_num, kol_start_state next_state)
{
    // printf("kol_exit next_state[%s].\n", g_state_str[next_state]);
    wait_sensor_standby_done(route_num);
    media_venc_unregister_notify();
    if (route_num == MAX_ROUTE_CNT) {
        slave_pin_mux_ctrl(TD_TRUE);
    }
    g_current_state = START_STATE_BUTT;
}

static td_void *kol_proc_thread(td_void *param)
{
    td_u32 route_id = *(td_u32 *)param;
    kol_timestamp *timestamp = kol_get_timestamp();

    set_thread_name("kol_proc", route_id);
    while (g_kol_run_flag[route_id]) {
        sem_wait(&g_sem_kol_run[route_id]);

        if (g_kol_run_flag[route_id] == TD_FALSE) {
            break;
        }

        td_bool ae_finished = check_ae_result_finished(route_id);
        if (!(ae_finished || g_single_state == KOL_STATE || g_convergenc_fail_times >= FAILE_TIMES_LIMIT)) {
            // printf("route[%u] ae do not finish.\n", route_id);
            g_is_convergenc_finish[route_id] = TD_FALSE;
            sem_wait(&g_sem_kol_stream[route_id]);
            sem_post(&g_sem_kol_done[route_id]);
            continue;
        }
        g_is_convergenc_finish[route_id] = TD_TRUE;
        if (route_id == 0) {
            scene_run_once();
        }

        td_bool detected_target = kol_check_target_detected(route_id);
        if (detected_target) {
            // printf("route[%u] detect target.\n", route_id);
        }

        g_is_detected_target[route_id] = detected_target;

        ss_mpi_sys_get_cur_pts(&timestamp->before_wait_venc[route_id]);
        sem_wait(&g_sem_kol_stream[route_id]);
        ss_mpi_sys_get_cur_pts(&timestamp->after_wait_venc[route_id]);
        sem_post(&g_sem_kol_done[route_id]);
    }

    return TD_NULL;
}

static td_void convergenc_enter(td_u32 route_num, kol_start_state last_state)
{
    // printf("convergenc_enter last_state[%s].\n", g_state_str[last_state]);
    g_convergenc_times_out = TD_FALSE;
    g_current_state = NORMAL_CONVERGENC_STATE;
    media_venc_register_notify(normal_stream_finish_notify);
    kol_update_param(OT_SYSKOL_CAPTURE_MODE_NORMAL);
    restart_sensor(route_num);
}

static td_void convergenc_running(td_u32 route_num)
{
    // printf("convergenc_running.\n");

    peripheral_set_uart_mode(MCU_MODE_NORMAL);

    for (td_u32 i = 0; i < route_num; i++) {
        if (g_is_convergenc_finish[i]) {
            g_normal_run_count[i] = route_num == MAX_ROUTE_CNT ? 2 : 1; /* 2:slave mode */
        } else {
            set_fast_ae_by_route(i);
            g_normal_run_count[i] = CONVERGENC_RUN_FRAME_COUNT;
        }
    }

    /* convergenc proc start */

    for (td_u32 i = 0; i < route_num; i++) {
        sem_post(&g_sem_convergenc_run[i]);
    }

    /* convergenc processing by convergenc_proc_thread */

    for (td_u32 i = 0; i < route_num; i++) {
        sem_wait(&g_sem_convergenc_done[i]);
    }

    /* convergenc proc end */

    for (td_u32 i = 0; i < route_num; i++) {
        if (!g_is_convergenc_finish[i]) {
            g_convergenc_times_out = TD_TRUE;
        }
    }
    g_start_state = KOL_STATE;
}

static td_void wait_normal_stream_finish(td_u32 route_num)
{
    td_u64 start, end;
    ss_mpi_sys_get_cur_pts(&start);
    td_bool finish = TD_TRUE;
    while (1) {
        finish = TD_TRUE;
        for (td_u32 i = 0; i < route_num; i++) {
            if ((g_sensor_standby_pts[i] - g_last_stream_pts[i]) > 1000000 / FRAME_RATE) { /* 1000000: 1s */
                finish = TD_FALSE;
                break;
            }
        }

        if (finish == TD_TRUE) {
            // printf("stream finish exit.\n");
            break;
        }

        ss_mpi_sys_get_cur_pts(&end);
        if ((end - start) > 1000 * 1000 * 5) { /* 1000: second to microsecond 5s */
            printf("time out exit.\n");
            break;
        }
        usleep(10 * 1000); /* 1000: 10ms Allowing the CPU */
    }

    if (finish == TD_FALSE) {
        for (td_u32 i = 0; i < route_num; i++) {
            sample_print("sensor_standby_pts[%u] %llu\n", i, g_sensor_standby_pts[i]);
            sample_print("last_stream_pts[%u] %llu\n", i, g_last_stream_pts[i]);
        }
    }
}

static td_void convergenc_exit(td_u32 route_num, kol_start_state next_state)
{
    // printf("convergenc_exit next_state[%s].\n", g_state_str[next_state]);
    wait_sensor_standby_done(route_num);
    wait_normal_stream_finish(route_num);
    media_venc_unregister_notify();
    g_current_state = START_STATE_BUTT;
}

static td_s32 convergenc_frame_proc(td_u32 route_id, td_bool ae_finished)
{
    td_s32 ret = TD_SUCCESS;
    ot_video_frame_info video_frame_info = {0};
    td_s32 milli_sec = GET_VPSS_CHN_FRAME_TIMEOUT;
    kol_route_info *route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return TD_FAILURE;
    }
    ot_vpss_chn vpss_chn = 1;

    ret = time_state_to_region(route_id, NORMAL_CONVERGENC_STATE);
    if (ret != TD_SUCCESS) {
        sample_print("time state to region route_id[%u] failed.[0x%x].\n", route_id, ret);
    }

    if ((media_get_aidetect_mask() & (1 << route_id)) != 0) {
        if (ss_mpi_vpss_get_chn_frame(route_info->vpss_grp, vpss_chn, &video_frame_info, milli_sec) == TD_SUCCESS) {
            ss_mpi_vpss_release_chn_frame(route_info->vpss_grp, vpss_chn, &video_frame_info);
        }
    }

    return TD_SUCCESS;
}

static td_void *convergenc_proc_thread(td_void *param)
{
    td_u32 route_id = *(td_u32 *)param;

    set_thread_name("converge_proc", route_id);
    while (g_convergenc_run_flag[route_id]) {
        sem_wait(&g_sem_convergenc_run[route_id]);

        if (g_convergenc_run_flag[route_id] == TD_FALSE) {
            break;
        }

        td_bool ae_finished = TD_FALSE;

        while (g_normal_run_count[route_id] > 0 && g_convergenc_run_flag[route_id]) {
            g_normal_run_count[route_id]--;
            ae_finished = check_ae_result_finished(route_id);
            if (ae_finished || g_normal_run_count[route_id] == 0) {
                sem_post(&g_sem_sensor_standby[route_id]);
            }
            if (route_id == 0) {
                scene_run_once();
            }
            convergenc_frame_proc(route_id, ae_finished);
            if (ae_finished) {
                break;
            } else {
                // printf("route[%u] ae do not finish.\n", route_id);
            }
        }
        g_is_convergenc_finish[route_id] = ae_finished;
        sem_post(&g_sem_convergenc_done[route_id]);
    }

    return TD_NULL;
}

static td_void record_enter(td_u32 route_num, kol_start_state last_state)
{
    // printf("record_enter last_state[%s].\n", g_state_str[last_state]);
    g_current_state = NORMAL_RECORD_STATE;
    for (td_u32 i = 0; i < route_num; i++) {
        kol_exp_process(i, &g_kol_exp_info[i]);
    }
    media_venc_register_notify(normal_stream_finish_notify);
    scene_resume();
    g_scene_pause_state = TD_FALSE;
    algorithm_set_track_attr(route_num, TD_TRUE);

    if (g_kol_exp_info[0].need_light) {
        kol_white_light_controll(TD_TRUE);
    }

    kol_update_param(OT_SYSKOL_CAPTURE_MODE_NORMAL);
    restart_sensor(route_num);
    media_trigger_jpeg();
}

static td_void record_running(td_u32 route_num)
{
    /* record proc start */

    for (td_u32 i = 0; i < route_num; i++) {
        g_normal_run_count[i] = RECORD_RUN_FRAME_COUNT;
        sem_post(&g_sem_record_run[i]);
    }

    peripheral_set_uart_mode(MCU_MODE_NORMAL);

    for (td_u32 j = 0; j < RECORD_STREAM_TIME; j++) {
        communicate_with_mcu();
        if (g_single_state == KOL_STATE || g_start_state == START_STATE_BUTT) {
            for (td_u32 i = 0; i < route_num; i++) {
                g_normal_run_count[i] = 1;
            }
            break;
        }
        sleep(1);
        for (td_u32 i = 0; i < route_num; i++) {
            g_normal_run_count[i] = RECORD_RUN_FRAME_COUNT;
        }
    }

    /* record processing by record_proc_thread */

    for (td_u32 i = 0; i < route_num; i++) {
        sem_wait(&g_sem_record_done[i]);
    }

    /* record proc end */

    g_start_state = KOL_STATE;
}

static td_void record_exit(td_u32 route_num, kol_start_state next_state)
{
    // printf("record_exit next_state[%s].\n", g_state_str[next_state]);
    kol_white_light_controll(TD_FALSE);
    scene_pause();
    g_scene_pause_state = TD_TRUE;
    algorithm_set_track_attr(route_num, TD_FALSE);
    wait_sensor_standby_done(route_num);
    wait_normal_stream_finish(route_num);
    media_venc_unregister_notify();
    kol_set_wakeup_record(TD_FALSE);
    if (next_state == KOL_STATE) {
        for (td_u32 i = 0; i < route_num; i++) {
            kol_exp_process_record_exit_part2(i);
        }
    }
    g_current_state = START_STATE_BUTT;
}

static td_void *record_proc_thread(td_void *param)
{
    td_u32 route_id = *(td_u32 *)param;

    set_thread_name("record_proc", route_id);
    while (g_record_run_flag[route_id]) {
        sem_wait(&g_sem_record_run[route_id]);
        if (g_record_run_flag[route_id] == TD_FALSE) {
            break;
        }
        while (g_normal_run_count[route_id] > 0 && g_record_run_flag[route_id]) {
            g_normal_run_count[route_id]--;
            if (g_normal_run_count[route_id] == 1 && g_single_state != NORMAL_RECORD_STATE) {
                g_kol_exp_info[route_id].is_pause = TD_TRUE;
            }
            if (g_normal_run_count[route_id] == 0 && g_single_state != NORMAL_RECORD_STATE) {
                sem_post(&g_sem_sensor_standby[route_id]);
                kol_exp_process_record_exit_part1(route_id);
            }
            record_frame_proc(route_id);
        }
        sem_post(&g_sem_record_done[route_id]);
    }

    return TD_NULL;
}

static td_bool is_need_trigger_grp1(td_u32 route_id)
{
    if (g_is_kol_running != TD_TRUE) {
        return TD_FALSE;
    }
    if (media_get_route_num() != MAX_ROUTE_CNT) {
        return TD_FALSE;
    }
    if (route_id != 0 && route_id != 1) {
        return TD_FALSE;
    }
    if (g_sensor_enable[0] != TD_FALSE || g_sensor_enable[1] != TD_FALSE) {
        return TD_FALSE;
    }
    return TD_TRUE;
}

static td_void *sensor_standby_thread(td_void *param)
{
    td_s32 ret;
    td_u32 route_id = *(td_u32 *)param;
    kol_route_info *route_info = media_get_route_info(route_id);
    if (route_info == TD_NULL) {
        return TD_NULL;
    }

    set_thread_name("sns_standby", route_id);
    nice(THREAD_NICE_INCR);
    while (g_sensor_standby_flag[route_id]) {
        sem_wait(&g_sem_sensor_standby[route_id]);

        if (g_sensor_standby_flag[route_id] == TD_FALSE) {
            break;
        }
        ret = ss_mpi_isp_get_vd_time_out(route_info->vi_pipe, OT_ISP_VD_STANDBY_FE_END, AE_VD_TIMEOUT);
        if (ret != TD_SUCCESS) {
            sample_print("pipe[%d] get vd STANDBY_FE_END failed.[0x%x]\n", route_info->vi_pipe, ret);
        }
        standby_sensor_by_route(route_id);
        if (is_need_trigger_grp1(route_id) == TD_TRUE) {
            restart_sensor_by_grp(1);
        }
    }

    return TD_NULL;
}

static td_s32 create_thread_by_route(td_u32 i)
{
    td_s32 ret;
    static td_u32 route_id[MAX_ROUTE_CNT];

    route_id[i] = i;

    g_sensor_standby_flag[i] = TD_TRUE;
    ret = pthread_create(&g_sensor_standby_thread_id[i], TD_NULL, sensor_standby_thread, &route_id[i]);
    if (ret != TD_SUCCESS) {
        sample_print("create sensor standby thread failed.[0x%x], route_id[%u].\n", ret, route_id[i]);
        return ret;
    }

    g_kol_run_flag[i] = TD_TRUE;
    ret = pthread_create(&g_kol_proc_thread_id[i], TD_NULL, kol_proc_thread, &route_id[i]);
    if (ret != TD_SUCCESS) {
        sample_print("create kol proc thread failed.[0x%x], route_id[%u].\n", ret, route_id[i]);
        return ret;
    }

    g_convergenc_run_flag[i] = TD_TRUE;
    ret = pthread_create(&g_convergenc_proc_thread_id[i], TD_NULL, convergenc_proc_thread, &route_id[i]);
    if (ret != TD_SUCCESS) {
        sample_print("create convergenc proc thread failed.[0x%x], route_id[%u].\n", ret, route_id[i]);
        return ret;
    }

    g_record_run_flag[i] = TD_TRUE;
    ret = pthread_create(&g_record_proc_thread_id[i], TD_NULL, record_proc_thread, &route_id[i]);
    if (ret != TD_SUCCESS) {
        sample_print("create record proc thread failed.[0x%x], route_id[%u].\n", ret, route_id[i]);
        return ret;
    }

    g_kol_exp_info[i].is_running = TD_TRUE;
    g_kol_exp_info[i].last_mode = KOL_IR_MODE_BUTT;
    scene_pause();
    g_scene_pause_state = TD_TRUE;
    ret = pthread_create(&g_kol_exp_info[i].thread_id, TD_NULL, kol_exp_thread, &route_id[i]);
    if (ret != TD_SUCCESS) {
        sample_print("create state machine thread failed.[0x%x], route_id[%u].\n", ret, route_id[i]);
        return ret;
    }
    return TD_SUCCESS;
}

td_s32 state_machine_resource_init(td_u32 route_num)
{
    td_s32 ret = TD_SUCCESS;

    register_wakeup_trigger();

    for (td_u32 i = 0; i < route_num; i++) {
        sem_init(&g_sem_kol_run[i], 0, 0);
        sem_init(&g_sem_kol_done[i], 0, 0);
        sem_init(&g_sem_convergenc_run[i], 0, 0);
        sem_init(&g_sem_convergenc_done[i], 0, 0);
        sem_init(&g_sem_sensor_standby[i], 0, 0);
        sem_init(&g_sem_kol_stream[i], 0, 0);
        sem_init(&g_sem_record_run[i], 0, 0);
        sem_init(&g_sem_record_done[i], 0, 0);
    }

    media_venc_register_notify(normal_stream_finish_notify);
    if (route_num == MAX_ROUTE_CNT) {
        slave_pin_mux_ctrl(TD_TRUE);
    }

    for (td_u32 i = 0; i < route_num; i++) {
        ret = create_thread_by_route(i);
        if (ret != TD_SUCCESS) {
            return ret;
        }
    }

    if (g_mcu_enable == TD_TRUE) {
        ret = peripheral_mcu_init();
        if (ret != TD_SUCCESS) {
            sample_print("peripheral init fialed!\n");
        }
        ret = peripheral_set_sleep_time((td_u16)g_kol_sleep_time);
        if (ret != TD_SUCCESS) {
            sample_print("peripheral set sleep time fialed!\n");
        }
    }

    return ret;
}

td_void state_machine_resource_deinit(td_u32 route_num)
{
    peripheral_mcu_deinit();

    for (td_u32 i = 0; i < route_num; i++) {
        g_kol_run_flag[i] = TD_FALSE;
        g_convergenc_run_flag[i] = TD_FALSE;
        g_kol_exp_info[i].is_running = TD_FALSE;
    }
    for (td_u32 i = 0; i < route_num; i++) {
        sem_post(&g_sem_kol_run[i]);
        sem_post(&g_sem_kol_stream[i]);
        sem_post(&g_sem_convergenc_run[i]);
    }
    for (td_u32 i = 0; i < route_num; i++) {
        pthread_join(g_kol_exp_info[i].thread_id, TD_NULL);
        pthread_join(g_kol_proc_thread_id[i], TD_NULL);
        pthread_join(g_convergenc_proc_thread_id[i], TD_NULL);
        g_record_run_flag[i] = TD_FALSE;
        sem_post(&g_sem_record_run[i]);
        pthread_join(g_record_proc_thread_id[i], TD_NULL);
        g_sensor_standby_flag[i] = TD_FALSE;
        sem_post(&g_sem_sensor_standby[i]);
        pthread_join(g_sensor_standby_thread_id[i], TD_NULL);
    }

    for (td_u32 i = 0; i < route_num; i++) {
        sem_destroy(&g_sem_sensor_standby[i]);
        sem_destroy(&g_sem_kol_stream[i]);
        sem_destroy(&g_sem_kol_run[i]);
        sem_destroy(&g_sem_kol_done[i]);
        sem_destroy(&g_sem_convergenc_run[i]);
        sem_destroy(&g_sem_convergenc_done[i]);
        sem_destroy(&g_sem_record_run[i]);
        sem_destroy(&g_sem_record_done[i]);
    }
    unregister_wakeup_trigger();
}

td_void set_mcu_switch(td_bool enable)
{
    g_mcu_enable = enable;
}

kol_action_map *get_action_map(td_void)
{
    /* kol transfer to convergenc: kol_running -> kol_exit -> convergenc_enter -> convergenc_running */
    /* convergenc transfer to kol: convergenc_running -> convergenc_exit -> kol_enter -> kol_running */
    /* kol transfer to record: kol_running -> kol_exit -> record_enter -> record_running */
    /* record transfer to kol: record_running -> record_exit -> kol_enter -> kol_running */

    static kol_action_map action_map[MAX_ACTION_MAP_CNT] = {
        {KOL_STATE, kol_enter, kol_running, kol_exit},
        {NORMAL_CONVERGENC_STATE, convergenc_enter, convergenc_running, convergenc_exit},
        {NORMAL_RECORD_STATE, record_enter, record_running, record_exit}
    };
    return action_map;
}

kol_event_map *get_event_map(td_void)
{
    static kol_event_map event_map[MAX_EVENT_MAP_CNT] = {
        {KOL_TO_CONVERGENC_EVENT, KOL_STATE, NORMAL_CONVERGENC_STATE},
        {KOL_TO_RECORD_EVENT, KOL_STATE, NORMAL_RECORD_STATE},
        {CONVERGENC_TO_KOL_EVENT, NORMAL_CONVERGENC_STATE, KOL_STATE},
        {CONVERGENC_TO_RECORD_EVENT, NORMAL_CONVERGENC_STATE, NORMAL_RECORD_STATE},
        {RECORD_TO_KOL_EVENT, NORMAL_RECORD_STATE, KOL_STATE},
        {RECORD_TO_CONVERGENC_EVENT, NORMAL_RECORD_STATE, NORMAL_CONVERGENC_STATE},
        {START_EVENT_BUTT, 0, 0}
    };
    return event_map;
}

td_void set_start_state(kol_start_state state)
{
    g_start_state = state;
}

kol_start_state get_start_state(td_void)
{
    return g_start_state;
}

td_void set_single_state(kol_start_state state)
{
    g_single_state = state;
}

kol_start_state get_single_state(td_void)
{
    return g_single_state;
}

kol_start_state get_curr_state(td_void)
{
    return g_current_state;
}

td_bool kol_is_need_state_transfer(const state_machine_cfg *state_machine, kol_start_state state)
{
    if (state_machine == NULL) {
        sample_print("check state need transfer input param is null.\n");
        return TD_FALSE;
    }

    return (state_machine->cur_state != state);
}

kol_start_event kol_get_start_event(const state_machine_cfg *state_machine, kol_start_state state)
{
    if (state_machine == NULL) {
        sample_print("get start event input param is null.\n");
        return START_EVENT_BUTT;
    }

    if ((state_machine->cur_state == KOL_STATE) && (state == NORMAL_CONVERGENC_STATE)) {
        return KOL_TO_CONVERGENC_EVENT;
    } else if ((state_machine->cur_state == KOL_STATE) && (state == NORMAL_RECORD_STATE)) {
        return KOL_TO_RECORD_EVENT;
    } else if ((state_machine->cur_state == NORMAL_CONVERGENC_STATE) && (state == KOL_STATE)) {
        return CONVERGENC_TO_KOL_EVENT;
    } else if ((state_machine->cur_state == NORMAL_CONVERGENC_STATE) && (state == NORMAL_RECORD_STATE)) {
        return CONVERGENC_TO_RECORD_EVENT;
    } else if ((state_machine->cur_state == NORMAL_RECORD_STATE) && (state == KOL_STATE)) {
        return RECORD_TO_KOL_EVENT;
    } else if ((state_machine->cur_state == NORMAL_RECORD_STATE) && (state == NORMAL_CONVERGENC_STATE)) {
        return RECORD_TO_CONVERGENC_EVENT;
    } else {
        sample_print("Invalid start state[%d], cur_state[%d].\n", state, state_machine->cur_state);
        return START_EVENT_BUTT;
    }
}
