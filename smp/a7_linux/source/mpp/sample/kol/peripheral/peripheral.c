/*
  Copyright (c), 2001-2025, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <stddef.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <linux/input.h>
#include "securec.h"
#include "sample_comm.h"
#include "ss_mpi_syskol.h"
#include "peripheral.h"

#define GPIO_BIT_NUM 8
#define UART2_BAUDRATE B115200
/* GPIO8_5 8 * 8 + 5 */
#define STAUS_GPIO_NUM 69
/* GPIO0_0 0 * 8 + 0 only test for update switch */
#define WAKEUP_GPIO_KEY_NUM "0"
#define UART_CMD_LEN 6
#define KOL_MAX_FILE_NAME_LEN 48
#define GPIO_BUF_LEN 10
#define CMD_MAP_IDX 3
#define MODE_VALUE_IDX 4
#define CHECK_SUM_IDX (UART_CMD_LEN - 1)
#define POWER_TEST_AD_NUM 6

#define WAKEUP_TIMER_NAME "11000000.bsp_timer"

#define DEFAULT_PWMCHIP "3"

typedef enum {
    CMD_MAP_ID_WAKEUP_BY_KEY,
    CMD_MAP_ID_ENTER_SINGLE_STATE,
    CMD_MAP_ID_EXIT_SINGLE_STATE,
    CMD_MAP_ID_POWER_TEST_RESULT,
} mode_flag_cmd_map_id;

static td_s32 config_port(td_s32 fd, speed_t baud);
static ssize_t write_port(td_s32 fd, td_u8 *buf, size_t len);
static ssize_t read_port(td_s32 fd, td_u8 *buf, size_t len, cc_t sec, cc_t min);

static td_s32 g_uart2_fd = -1;

td_s32 kol_pwm_export(td_u32 pwm_num)
{
    FILE *fp = TD_NULL;

    /* open pwm */
    fp = fopen("/sys/class/pwm/pwmchip"DEFAULT_PWMCHIP"/export", "w");
    if (fp == NULL) {
        sample_print("Cannot open /sys/class/pwm/pwmchip"DEFAULT_PWMCHIP"/export.\n");
        return -1;
    }
    (td_void)fprintf(fp, "%u", pwm_num);
    (td_void)fclose(fp);
    return 0;
}

td_s32 kol_pwm_unexport(td_u32 pwm_num)
{
    FILE *fp = TD_NULL;

    /* open pwm */
    fp = fopen("/sys/class/pwm/pwmchip"DEFAULT_PWMCHIP"/unexport", "w");
    if (fp == NULL) {
        sample_print("Cannot open /sys/class/pwm/pwmchip"DEFAULT_PWMCHIP"/unexport.\n");
        return -1;
    }
    (td_void)fprintf(fp, "%u", pwm_num);
    (td_void)fclose(fp);
    return 0;
}

td_s32 kol_pwm_set_period(td_u32 pwm_num, td_u32 period)
{
    FILE *fp = TD_NULL;
    td_char file_name[KOL_MAX_FILE_NAME_LEN];

    (td_void)sprintf_s(file_name, sizeof(file_name), "/sys/class/pwm/pwmchip"DEFAULT_PWMCHIP"/pwm%u/period", pwm_num);
    fp = fopen(file_name, "w");
    if (fp == NULL) {
        sample_print("Cannot open %s.\n", file_name);
        return TD_NULL;
    }
    (td_void)fprintf(fp, "%u", period);
    (td_void)fclose(fp);
    return 0;
}

td_s32 kol_pwm_set_duty_cycle(td_u32 pwm_num, td_u32 duty_cycle)
{
    FILE *fp = TD_NULL;
    td_char file_name[KOL_MAX_FILE_NAME_LEN];

    (td_void)sprintf_s(file_name, sizeof(file_name),
        "/sys/class/pwm/pwmchip"DEFAULT_PWMCHIP"/pwm%u/duty_cycle", pwm_num);
    fp = fopen(file_name, "w");
    if (fp == NULL) {
        sample_print("Cannot open %s.\n", file_name);
        return TD_NULL;
    }
    (td_void)fprintf(fp, "%u", duty_cycle);
    (td_void)fclose(fp);
    return 0;
}

td_s32 kol_pwm_set_polarity(td_u32 pwm_num, const td_char *polarity)
{
    FILE *fp = TD_NULL;
    td_char file_name[KOL_MAX_FILE_NAME_LEN];

    (td_void)sprintf_s(file_name, sizeof(file_name),
        "/sys/class/pwm/pwmchip"DEFAULT_PWMCHIP"/pwm%u/polarity", pwm_num);
    fp = fopen(file_name, "w");
    if (fp == NULL) {
        sample_print("Cannot open %s.\n", file_name);
        return TD_NULL;
    }
    (td_void)fprintf(fp, "%s", polarity);
    (td_void)fclose(fp);
    return 0;
}

td_s32 kol_pwm_enable(td_u32 pwm_num)
{
    FILE *fp = TD_NULL;
    td_char file_name[KOL_MAX_FILE_NAME_LEN];

    (td_void)sprintf_s(file_name, sizeof(file_name),
        "/sys/class/pwm/pwmchip"DEFAULT_PWMCHIP"/pwm%u/enable", pwm_num);
    fp = fopen(file_name, "w");
    if (fp == NULL) {
        sample_print("Cannot open %s.\n", file_name);
        return TD_NULL;
    }
    (td_void)fprintf(fp, "%u", 1);
    (td_void)fclose(fp);
    return 0;
}

td_s32 kol_pwm_disable(td_u32 pwm_num)
{
    FILE *fp = TD_NULL;
    td_char file_name[KOL_MAX_FILE_NAME_LEN];

    (td_void)sprintf_s(file_name, sizeof(file_name),
        "/sys/class/pwm/pwmchip"DEFAULT_PWMCHIP"/pwm%u/enable", pwm_num);
    fp = fopen(file_name, "w");
    if (fp == NULL) {
        sample_print("Cannot open %s.\n", file_name);
        return TD_NULL;
    }
    (td_void)fprintf(fp, "%u", 0);
    (td_void)fclose(fp);
    return 0;
}

td_s32 kol_gpio_export(td_u32 gpio_num)
{
    FILE *fp = TD_NULL;

    /* open gpio */
    fp = fopen("/sys/class/gpio/export", "w");
    if (fp == NULL) {
        sample_print("Cannot open /sys/class/gpio/export.\n");
        return -1;
    }
    (td_void)fprintf(fp, "%u", gpio_num);
    (td_void)fclose(fp);
    return 0;
}

td_s32 kol_gpio_unexport(td_u32 gpio_num)
{
    FILE *fp = TD_NULL;

    /* open gpio */
    fp = fopen("/sys/class/gpio/unexport", "w");
    if (fp == NULL) {
        sample_print("Cannot open /sys/class/gpio/unexport.\n");
        return -1;
    }
    (td_void)fprintf(fp, "%u", gpio_num);
    (td_void)fclose(fp);
    return 0;
}

td_s32 kol_set_gpio_direction(td_u32 gpio_num, const td_char *direction)
{
    FILE *fp = TD_NULL;
    td_char file_name[KOL_MAX_FILE_NAME_LEN];

    (td_void)sprintf_s(file_name, sizeof(file_name), "/sys/class/gpio/gpio%u/direction", gpio_num);
    fp = fopen(file_name, "w");
    if (fp == NULL) {
        sample_print("Cannot open %s.\n", file_name);
        return TD_NULL;
    }
    (td_void)fprintf(fp, "%s", direction);
    (td_void)fclose(fp);
    return 0;
}

td_s32 kol_set_gpio_edge(td_u32 gpio_num, const td_char *edge)
{
    FILE *fp = TD_NULL;
    td_char file_name[KOL_MAX_FILE_NAME_LEN];

    (td_void)sprintf_s(file_name, sizeof(file_name), "/sys/class/gpio/gpio%u/edge", gpio_num);
    fp = fopen(file_name, "w");
    if (fp == NULL) {
        sample_print("Cannot open %s.\n", file_name);
        return TD_NULL;
    }
    (td_void)fprintf(fp, "%s", edge);
    (td_void)fclose(fp);
    return 0;
}

td_s32 kol_read_gpio_value(td_u32 gpio_num)
{
    td_u8 buf[GPIO_BUF_LEN];
    FILE *fp = TD_NULL;
    td_char file_name[KOL_MAX_FILE_NAME_LEN];

    (td_void)sprintf_s(file_name, sizeof(file_name), "/sys/class/gpio/gpio%u/value", gpio_num);
    fp = fopen(file_name, "rb+");
    if (fp == NULL) {
        sample_print("Cannot open %s.\n", file_name);
        return -1;
    }
    memset_s(buf, sizeof(buf), 0, sizeof(buf));
    (td_void)fread(buf, sizeof(td_char), sizeof(buf) - 1, fp);
    (td_void)fclose(fp);
    return (td_s32)(buf[0] - '0');
}

td_s32 kol_write_gpio_value(td_u32 gpio_num, td_u32 value)
{
    FILE *fp = TD_NULL;
    td_char file_name[KOL_MAX_FILE_NAME_LEN];

    (td_void)sprintf_s(file_name, sizeof(file_name), "/sys/class/gpio/gpio%u/value", gpio_num);
    fp = fopen(file_name, "w");
    if (fp == NULL) {
        sample_print("Cannot open %s.\n", file_name);
        return -1;
    }
    (td_void)fprintf(fp, "%d", value > 0 ? 1 : 0);
    (td_void)fclose(fp);
    return 0;
}

static td_s32 kol_get_gpio_value(td_u32 gpio_num)
{
    td_s32 value;

    (td_void)kol_gpio_export(gpio_num);
    (td_void)kol_set_gpio_direction(gpio_num, "in");
    value = kol_read_gpio_value(gpio_num);
    (td_void)kol_gpio_unexport(gpio_num);
    return value;
}

td_bool peripheral_mode_flag1(td_void)
{
    if (kol_get_gpio_value(STAUS_GPIO_NUM) != 0) {
        return TD_TRUE;
    }
    return TD_FALSE;
}

td_void peripheral_get_mcu_notify(mcu_notify_info *info)
{
    if (info == TD_NULL) {
        return;
    }
    memset_s(info, sizeof(mcu_notify_info), 0, sizeof(mcu_notify_info));
    if (g_uart2_fd == -1) {
        return;
    }
    td_u8 cmd[UART_CMD_LEN] = { 0xEB, 0x90, 0xCC, 0x00, 0x00, 0x00 };
    for (td_s32 i = 0; i < CHECK_SUM_IDX; i++) {
        cmd[CHECK_SUM_IDX] += cmd[i];
    }
    tcflush(g_uart2_fd, TCIOFLUSH);
    if (write_port(g_uart2_fd, cmd, UART_CMD_LEN) < 0) {
        return;
    }
    if (read_port(g_uart2_fd, cmd, UART_CMD_LEN, 1, UART_CMD_LEN) < 0) {
        return;
    }
    printf("read from mcu : ");
    for (td_s32 i = 0; i < UART_CMD_LEN; i++) {
        printf("0x%02X ", cmd[i]);
    }
    printf("\n");
    if ((cmd[CMD_MAP_IDX] & (1 << CMD_MAP_ID_WAKEUP_BY_KEY)) != 0) {
        info->wakeup = WAKEUP_4G_MODE;  /* key instead 4G */
    }
    if ((cmd[CMD_MAP_IDX] & (1 << CMD_MAP_ID_ENTER_SINGLE_STATE)) != 0) {
        info->switch_state = MCU_SWITCH_STATE_ENTER_SINGLE;
    }
    if ((cmd[CMD_MAP_IDX] & (1 << CMD_MAP_ID_EXIT_SINGLE_STATE)) != 0) {
        info->switch_state = MCU_SWITCH_STATE_EXIT_SINGLE;
    }
    if ((cmd[CMD_MAP_IDX] & (1 << CMD_MAP_ID_POWER_TEST_RESULT)) != 0) {
        info->has_power_test_data = TD_TRUE;
    }
    info->mode = cmd[MODE_VALUE_IDX];
}

td_s32 peripheral_set_uart_mode(mcu_mode mode)
{
    td_s32 ret = TD_SUCCESS;

    return ret;
}

td_s32 peripheral_set_sleep_time(td_u16 milli_sec)
{
    td_u8 cmd[UART_CMD_LEN] = { 0xEB, 0x90, 0xEE, 0x00, 0x00, 0x00 };
    if (g_uart2_fd == -1) {
        return TD_FAILURE;
    }

    cmd[3] = milli_sec & 0xff;          /* byte3: low 8 bits */
    cmd[4] = (milli_sec >> 8) & 0xff;   /* byte4: high 8 bits */
    for (td_s32 i = 0; i < CHECK_SUM_IDX; i++) {
        cmd[CHECK_SUM_IDX] += cmd[i];
    }
    if (write_port(g_uart2_fd, cmd, UART_CMD_LEN) < 0) {
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

td_void peripheral_get_power_test(td_void)
{
    td_u16 voltage, current, power, total_power;
    td_char *ad_name[POWER_TEST_AD_NUM] = { "1v8_soc", "3v7", "ddr_ram", "3v3_soc", "ddr_soc", "dvdd" };
    if (g_uart2_fd == -1) {
        return;
    }
    td_u8 cmd[UART_CMD_LEN] = { 0xEB, 0x90, 0xDD, 0x00, 0x00, 0x00 };
    for (td_s32 i = 0; i < CHECK_SUM_IDX; i++) {
        cmd[CHECK_SUM_IDX] += cmd[i];
    }
    tcflush(g_uart2_fd, TCIOFLUSH);
    total_power = 0;
    for (td_s32 i = 0; i < POWER_TEST_AD_NUM; i++) {
        if (write_port(g_uart2_fd, cmd, UART_CMD_LEN) < 0) {
            return;
        }
        if (read_port(g_uart2_fd, (td_u8 *)&voltage, sizeof(voltage), 1, sizeof(voltage)) < 0) {
            return;
        }
        if (read_port(g_uart2_fd, (td_u8 *)&current, sizeof(current), 1, sizeof(current)) < 0) {
            return;
        }
        if (read_port(g_uart2_fd, (td_u8 *)&power, sizeof(power), 1, sizeof(power)) < 0) {
            return;
        }
        printf("[%s] vol avg %umV cur avg %umA power avg %umW\n", ad_name[i], voltage, current, power);
        cmd[CMD_MAP_IDX]++;
        cmd[CHECK_SUM_IDX]++;
        if (i != 1) { /* skip 3v7 */
            total_power += power;
        }
    }
    printf("total power avg %umW\n", total_power);
}

td_s32 peripheral_mcu_init(td_void)
{
    if (g_uart2_fd != -1) {
        return TD_SUCCESS;
    }
    g_uart2_fd = open("/dev/ttyAMA2", O_RDWR | O_NOCTTY | O_NDELAY);
    if (g_uart2_fd < 0) {
        perror("open");
        return TD_FAILURE;
    }

    if (config_port(g_uart2_fd, UART2_BAUDRATE) < 0) {
        close(g_uart2_fd);
        g_uart2_fd = -1;
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

td_void peripheral_mcu_deinit(td_void)
{
    if (g_uart2_fd == -1) {
        return;
    }
    close(g_uart2_fd);
    g_uart2_fd = -1;
}

static td_s32 config_port(td_s32 fd, speed_t baud)
{
    struct termios term;

    /* read serial port configure */
    if (tcgetattr(fd, &term) != 0) {
        perror("config_port: Unable to read configure - ");
        return -1;
    }

    /* set baudrate */
    cfsetspeed(&term, baud);

    /* 8N1 mode */
    term.c_cflag &= ~PARENB;
    term.c_cflag &= ~CSTOPB;
    term.c_cflag &= ~CSIZE;
    term.c_cflag |= CS8;

    /* enbale raw mode */
    cfmakeraw(&term);

    /* write serial port configure */
    if (tcsetattr(fd, TCSADRAIN, &term) != 0) {
        perror("config_port: Unable to write configure - ");
        return -1;
    }
    return 0;
}

static ssize_t write_port(td_s32 fd, td_u8 *buf, size_t len)
{
    ssize_t actual_write;

    actual_write = write(fd, buf, len);
    if (actual_write == -1) {
        /* write failure */
        perror("write_port: Unable to write port - ");
    }
    return actual_write;
}

/* set timeout and minimum number of bytes to read */
static td_s32 read_port_setup(td_s32 fd, cc_t timeout_sec, cc_t min_bytes)
{
    struct termios term;

    /* read serial port configure */
    if (tcgetattr(fd, &term) != 0) {
        perror("read_port_setup: Unable to read configure - ");
        return -1;
    }

    /* set timeout in deciseconds for noncanonical read */
    term.c_cc[VTIME] = timeout_sec * 10;    /* VTIME 1/10 sec */

    /* set minimum number of bytes to read */
    term.c_cc[VMIN] = min_bytes;

    /* write serial port configure */
    if (tcsetattr(fd, TCSADRAIN, &term) != 0) {
        perror("read_port_setup: Unable to write configure - ");
        return -1;
    }
    return 0;
}

static ssize_t read_port(td_s32 fd, td_u8 *buf, size_t len, cc_t sec, cc_t min)
{
    ssize_t actual_read;

    /* setup serial port */
    if (read_port_setup(fd, sec, min) == -1) {
        return -1;
    }

    /* read bytes from serial port */
    actual_read = read(fd, buf, len);
    if (actual_read == -1) {
        /* read failure */
        // perror("read_port: Unable to read port - ");
    }

    return actual_read;
}

static pthread_t g_wakeup_thread_id = 0;
static td_bool   g_is_need_wakeup = TD_FALSE;
static td_bool   g_is_wakeup_record = TD_FALSE;

td_void kol_set_wakeup_record(td_bool is_record)
{
    g_is_wakeup_record = is_record;
}

td_bool kol_get_wakeup_record(td_void)
{
    return g_is_wakeup_record;
}

static td_void *kol_wakeup_thread(td_void *param)
{
    struct input_event event = { 0 };
    struct pollfd fds[1] = { 0 };
    FILE *fp = TD_NULL;
    td_char *file_name = "/dev/input/event"WAKEUP_GPIO_KEY_NUM;

    fp = fopen(file_name, "r");
    if (fp == TD_NULL) {
        sample_print("Cannot open %s .\n", file_name);
        return TD_NULL;
    }
    fds[0].fd = fileno(fp);
    fds[0].events = POLLIN;
    while (g_is_need_wakeup) {
        if (poll(fds, (sizeof(fds) / sizeof(struct pollfd)), 1000) < 0) { /* 1000: timeout 1000 ms */
            perror("wakeup poll failed\n");
            break;
        }

        if (((td_u16)(fds[0].revents) & POLLIN) != 0) {
            td_s32 ret = read(fds[0].fd, &event, sizeof(event));
            if (ret < 0) {
                perror("read event failed\n");
                break;
            }
            if (ret != sizeof(struct input_event)) {
                perror("wakeup read failed\n");
                break;
            }
            if (event.type != EV_KEY || event.value != 1) {
                continue;
            }
            ss_mpi_syskol_wakeup();
            kol_set_wakeup_record(TD_TRUE);
            printf("=========gpio wakeup===========\n");
        }
    }
    (td_void)fclose(fp);
    return TD_NULL;
}

static td_s32 kol_enable_timer_wakeup(td_void)
{
    FILE *fp = TD_NULL;

    fp = fopen("/sys/devices/platform/soc/soc:amba/"WAKEUP_TIMER_NAME"/power/wakeup", "w");
    if (fp == NULL) {
        sample_print("Cannot open /sys/devices/platform/soc/soc:amba/"WAKEUP_TIMER_NAME"/power/wakeup\n");
        return -1;
    }
    (td_void)fprintf(fp, "%s", "enabled");
    (td_void)fclose(fp);
    return 0;
}

static td_s32 kol_disable_timer_wakeup(td_void)
{
    FILE *fp = TD_NULL;

    fp = fopen("/sys/devices/platform/soc/soc:amba/"WAKEUP_TIMER_NAME"/power/wakeup", "w");
    if (fp == NULL) {
        sample_print("Cannot open /sys/devices/platform/soc/soc:amba/"WAKEUP_TIMER_NAME"/power/wakeup\n");
        return -1;
    }
    (td_void)fprintf(fp, "%s", "disabled");
    (td_void)fclose(fp);
    return 0;
}

static td_s32 kol_set_wakeup_timer_ms(td_u32 ms)
{
    FILE *fp = TD_NULL;

    fp = fopen("/sys/devices/platform/soc/soc:amba/"WAKEUP_TIMER_NAME"/time_ms", "w");
    if (fp == NULL) {
        sample_print("Cannot open /sys/devices/platform/soc/soc:amba/"WAKEUP_TIMER_NAME"/time_ms\n");
        return -1;
    }
    (td_void)fprintf(fp, "%u", ms);
    (td_void)fclose(fp);
    return 0;
}

static td_s32 kol_start_wakeup_timer(td_void)
{
    FILE *fp = TD_NULL;

    fp = fopen("/sys/devices/platform/soc/soc:amba/"WAKEUP_TIMER_NAME"/timer_start", "w");
    if (fp == NULL) {
        sample_print("Cannot open /sys/devices/platform/soc/soc:amba/"WAKEUP_TIMER_NAME"/timer_start\n");
        return -1;
    }
    (td_void)fprintf(fp, "%s", "1");
    (td_void)fclose(fp);
    return 0;
}

static td_s32 kol_stop_wakeup_timer(td_void)
{
    FILE *fp = TD_NULL;

    fp = fopen("/sys/devices/platform/soc/soc:amba/"WAKEUP_TIMER_NAME"/timer_start", "w");
    if (fp == NULL) {
        sample_print("Cannot open /sys/devices/platform/soc/soc:amba/"WAKEUP_TIMER_NAME"/timer_start\n");
        return -1;
    }
    (td_void)fprintf(fp, "%s", "0");
    (td_void)fclose(fp);
    return 0;
}

td_void register_wakeup_trigger(td_void)
{
    (td_void)kol_enable_timer_wakeup();
    g_is_need_wakeup = TD_TRUE;
    (td_void)pthread_create(&g_wakeup_thread_id, TD_NULL, kol_wakeup_thread, TD_NULL);
}

td_void unregister_wakeup_trigger(td_void)
{
    g_is_need_wakeup = TD_FALSE;
    if (g_wakeup_thread_id) {
        pthread_join(g_wakeup_thread_id, TD_NULL);
        g_wakeup_thread_id = 0;
    }
    (td_void)kol_disable_timer_wakeup();
}

td_void wakeup_timer_start(td_s32 ms)
{
    if (ms < 0) {
        return;
    }
    (td_void)kol_set_wakeup_timer_ms(ms);
    (td_void)kol_start_wakeup_timer();
}

td_void wakeup_timer_stop(td_void)
{
    (td_void)kol_stop_wakeup_timer();
}
