/*
  Copyright (c), 2001-2025, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/statfs.h>

#include "ot_type.h"
#include "sample_comm.h"
#include "state_machine_action.h"
#include "media_venc.h"


#define SAVE_CONVERGENC_BUFFER 1

/* one stream file max save 64M */
#define SAMPLE_VENC_FILE_MAX_SIZE (64 * 1024 * 1024)

#define WAIT_GET_STREAM_THREAD_START_SLEEP 50
#define WAIT_GET_STREAM_THREAD_START_TIMES 40
#define SAMPLE_RETURN_CONTINUE 1
#define SAMPLE_RETURN_BREAK 2
#define SAMPLE_RETURN_NULL 3
#define SAMPLE_RETURN_GOTO 4
#define SAMPLE_RETURN_FAILURE (-1)
#define CHN_NUM_MAX 2
#define MOUNT_PATH "/dev/mmcblk0p1"
#define SD_CARD_PATH "/sdcard/"

typedef struct {
    FILE *file[OT_VENC_MAX_CHN_NUM];
    td_s32 venc_fd[OT_VENC_MAX_CHN_NUM];
    td_s32 maxfd;
    td_char real_file_name[OT_VENC_MAX_CHN_NUM][FILE_NAME_LEN];
    td_char file_postfix[OT_VENC_MAX_CHN_NUM][10];
    td_s32 chn_total;
} venc_stream_proc_info;

static td_bool g_is_first_mount = TD_TRUE;
static td_bool g_is_sd_mount = TD_FALSE;

static pthread_t g_venc_pid;
static sample_venc_getstream_para g_para = {.thread_start = TD_FALSE, .cnt = 0};

static td_char *g_stream_buffer[OT_VENC_MAX_CHN_NUM];
static td_u32 g_srteam_buffer_size = 1024 * 1024;
static td_u32 g_stream_cur_offset[OT_VENC_MAX_CHN_NUM] = {0};
static td_u32 g_stream_cur_file_size[OT_VENC_MAX_CHN_NUM] = {0};

static pfn_notify_stream_finish g_notify_func = TD_NULL;

static td_s32 media_get_stream_file_name(td_s32 index, venc_stream_proc_info *proc_info)
{
    time_t time_now;
    td_char *real_path = TD_NULL;
    struct tm *tm_time = TD_NULL;

    real_path = realpath(SD_CARD_PATH, TD_NULL);
    if (real_path == TD_NULL) {
        sample_print("get chn[%d] stream file realpath error\n", index);
        return TD_FAILURE;
    }

    (td_void)time(&time_now);
    tm_time = gmtime(&time_now);
    if (snprintf_s(proc_info->real_file_name[index], FILE_NAME_LEN, FILE_NAME_LEN - 1,
        "%s/stream_chn%d_%d%02d%02d%02d%02d%02d%s", real_path, index,
        START_YEAR + tm_time->tm_year, 1 + tm_time->tm_mon, tm_time->tm_mday, tm_time->tm_hour,
        tm_time->tm_min, tm_time->tm_sec, proc_info->file_postfix[index]) < 0) {
        sample_print("get chn[%d] stream file name error\n", index);
        free(real_path);
        return TD_FAILURE;
    }
    free(real_path);
    printf("new file name is %s\n", proc_info->real_file_name[index]);
    return TD_SUCCESS;
}

static td_s32 write_buffer_to_file(venc_stream_proc_info *proc_info, td_s32 chn_num, const ot_payload_type *type)
{
    for (td_s32 i = 0; i < proc_info->chn_total; i++) {
        if (type[i] == OT_PT_JPEG) {
            continue;
        }

        if (g_stream_cur_offset[i] == 0) {
            continue;
        }

        if (g_stream_cur_file_size[i] == 0) {
            media_get_stream_file_name(i, proc_info);
        }

        FILE *file;
        file = fopen(proc_info->real_file_name[i], "ab");
        if (!file) {
            sample_print("faile to open file.\n");
            return SAMPLE_RETURN_FAILURE;
        }
        td_s32 fd = fileno(file);
        fchmod(fd, S_IRUSR | S_IWUSR);
        (td_void)fwrite(g_stream_buffer[i], g_stream_cur_offset[i], 1, file);
        (td_void)fflush(file);
        (td_void)fclose(file);
        g_stream_cur_file_size[i] += g_stream_cur_offset[i];
    }

    return TD_SUCCESS;
}

static td_s32 write_jpeg_to_file(td_s32 index, venc_stream_proc_info *proc_info, ot_venc_stream *stream)
{
    FILE *file = TD_NULL;

    if (media_get_stream_file_name(index, proc_info) != TD_SUCCESS) {
        return SAMPLE_RETURN_FAILURE;
    }

    file = fopen(proc_info->real_file_name[index], "wb");
    if (!file) {
        sample_print("faile to open file.\n");
        return SAMPLE_RETURN_FAILURE;
    }
    td_s32 fd = fileno(file);
    fchmod(fd, S_IRUSR | S_IWUSR);
    for (td_u32 i = 0; i < stream->pack_cnt; i++) {
        (td_void)fwrite(stream->pack[i].addr + stream->pack[i].offset,
            stream->pack[i].len - stream->pack[i].offset, 1, file);
    }
    (td_void)fflush(file);
    (td_void)fclose(file);

    return TD_SUCCESS;
}

static td_void ensure_umount_success(td_void)
{
    td_s32 i;
    td_s32 ret;

    for (i = 0; i < 5; i++) { /* 5: max umount num */
        ret = umount(SD_CARD_PATH);
        if (ret == 0) {
            return;
        }
        if (errno != EBUSY) {
            break;
        }
        sample_print("umount failed %d times\n", i + 1);
        usleep(10000); /* 10000: wiat 10ms */
    }
    umount2(SD_CARD_PATH, MNT_FORCE);
    sample_print("umount force\n");
}

static td_s32 kol_mount(td_void)
{
    td_s32 ret;

    if (g_is_first_mount) {
        struct statfs st;

        ret = access(SD_CARD_PATH, F_OK);
        if (ret != TD_SUCCESS) {
            sample_print("SD_CARD_PATH %s is not exit .\n", SD_CARD_PATH);
            return ret;
        }
        if (statfs(SD_CARD_PATH, &st) == 0) {
            if (st.f_type == 0x4d44 /* 0x4d44: vfat */) {
                g_is_sd_mount = TD_TRUE;
                printf("%s is mount already\n", SD_CARD_PATH);
                ensure_umount_success();
            }
        }
        g_is_first_mount = TD_FALSE;
    }

    ret = mount(MOUNT_PATH, SD_CARD_PATH, "vfat", 0, NULL);
    if (ret != TD_SUCCESS) {
        sample_print("faile to mount SD card %d.\n", ret);
        perror("Unable to mount sd card\n");
        return ret;
    }
    return TD_SUCCESS;
}

static td_s32 save_jpeg_to_sd(venc_stream_proc_info *proc_info, td_s32 index, ot_venc_stream *stream)
{
    td_s32 ret;

    ret = kol_mount();
    if (ret != TD_SUCCESS) {
        return ret;
    }

    write_jpeg_to_file(index, proc_info, stream);

    ensure_umount_success();

    return TD_SUCCESS;
}

static td_s32 save_stream_to_sd(venc_stream_proc_info *proc_info, td_s32 num, ot_payload_type *type)
{
    td_s32 ret;

    ret = kol_mount();
    if (ret != TD_SUCCESS) {
        return ret;
    }

    write_buffer_to_file(proc_info, num, type);

    ensure_umount_success();

    return TD_SUCCESS;
}

static td_bool media_is_need_new_stream_file(td_s32 index, ot_payload_type type, const ot_venc_stream *stream)
{
    if (g_stream_cur_file_size[index] < SAMPLE_VENC_FILE_MAX_SIZE) {
        return TD_FALSE;
    }
    switch (type) {
        case OT_PT_H264:
            return stream->h264_info.ref_type == OT_VENC_BASE_IDR_SLICE;
        case OT_PT_H265:
            return stream->h265_info.ref_type == OT_VENC_BASE_IDR_SLICE;
        default:
            return TD_FALSE;
    }
    return TD_FALSE;
}

static td_void media_save_stream_to_buffer(td_s32 index, venc_stream_proc_info *proc_info,
    ot_venc_stream *stream, ot_payload_type *type)
{
    td_bool new_file = TD_FALSE;
    td_u32 curr_size = 0;
    td_u32 i;
    td_u64 start, end;

    for (i = 0; i < stream->pack_cnt; i++) {
        curr_size += (stream->pack[i].len - stream->pack[i].offset);
    }

    if (media_is_need_new_stream_file(index, type[index], stream)) {
        new_file = TD_TRUE;
    }
    if ((g_stream_cur_offset[index] + curr_size) > g_srteam_buffer_size || new_file) {
        printf("flush to SD card\n");
        ss_mpi_sys_get_cur_pts(&start);
        td_s32 ret = save_stream_to_sd(proc_info, index, type);
        if (ret != TD_SUCCESS) {
            sample_print("failed to save to SD card.\n");
        }
        ss_mpi_sys_get_cur_pts(&end);
        printf("flush to SD card over, cost time:%llu\n", end - start);
        for (td_s32 j = 0; j < proc_info->chn_total; j++) {
            if (type[j] == OT_PT_JPEG) {
                continue;
            }
            g_stream_cur_offset[j] = 0;
        }
    }
    if (new_file) {
        g_stream_cur_file_size[index] = 0;
    }
    for (i = 0; i < stream->pack_cnt; i++) {
        (td_void)memcpy_s(g_stream_buffer[index] + g_stream_cur_offset[index],
            stream->pack[i].len - stream->pack[i].offset,
            stream->pack[i].addr + stream->pack[i].offset,
            stream->pack[i].len - stream->pack[i].offset);
        g_stream_cur_offset[index] += (stream->pack[i].len - stream->pack[i].offset);
    }
}

static td_s32 media_query_stream_stat(ot_venc_chn chn, ot_venc_chn_status *stat)
{
    td_s32 ret;

    ret = ss_mpi_venc_query_status(chn, stat);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_venc_query_status chn[%d] failed with %#x!\n", chn, ret);
        return SAMPLE_RETURN_BREAK;
    }

    if (stat->cur_packs == 0) {
        sample_print("NOTE: current  frame is TD_NULL!\n");
        return SAMPLE_RETURN_CONTINUE;
    }
    return TD_SUCCESS;
}

static td_s32 media_get_stream_from_one_channl(venc_stream_proc_info *proc_info, td_s32 index,
    ot_venc_stream_buf_info *buf_info, ot_payload_type *type)
{
    td_s32 ret;
    ot_venc_stream stream = { 0 };
    ot_venc_chn_status stat;

    ret = media_query_stream_stat(index, &stat);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    stream.pack = (ot_venc_pack *)malloc(sizeof(ot_venc_pack) * stat.cur_packs);
    if (stream.pack == TD_NULL) {
        sample_print("malloc stream pack failed!\n");
        return SAMPLE_RETURN_BREAK;
    }

    stream.pack_cnt = stat.cur_packs;
    ret = ss_mpi_venc_get_stream(index, &stream, TD_TRUE);
    if (ret != TD_SUCCESS) {
        ret = SAMPLE_RETURN_BREAK;
        sample_print("ss_mpi_venc_get_stream failed with %#x!\n", ret);
        goto end;
    }

    if (type[index] != OT_PT_JPEG) {
        if (get_curr_state() == NORMAL_CONVERGENC_STATE && SAVE_CONVERGENC_BUFFER == 0) {
            sample_print("cur state is convergenc, not save stream.\n");
        } else {
            media_save_stream_to_buffer(index, proc_info, &stream, type);
        }
    } else {
        printf("flush jpeg to SD card. \n");
        save_jpeg_to_sd(proc_info, index, &stream);
    }

    if (g_notify_func != TD_NULL) {
        g_notify_func(index, stream.pack[0].pts);
    }

    if (ret != TD_SUCCESS) {
        sample_print("save stream failed!\n");
    }

    ret = ss_mpi_venc_release_stream(index, &stream);
    if (ret != TD_SUCCESS) {
        ret = SAMPLE_RETURN_BREAK;
        sample_print("ss_mpi_venc_release_stream failed!\n");
        goto end;
    }
end:
    free(stream.pack);
    stream.pack = TD_NULL;
    return ret;
}

static td_void media_fd_isset(venc_stream_proc_info *proc_info, const fd_set *read_fds,
    ot_venc_stream_buf_info *buf_info, ot_payload_type *type, sample_venc_getstream_para *para)
{
    td_s32 i, ret;

    for (i = 0; (i < proc_info->chn_total) && (i < OT_VENC_MAX_CHN_NUM); i++) {
        if (FD_ISSET(proc_info->venc_fd[i], read_fds)) {
            ret = media_get_stream_from_one_channl(proc_info, i, buf_info, type);
            if (ret == SAMPLE_RETURN_CONTINUE) {
                continue;
            } else if (ret == SAMPLE_RETURN_BREAK) {
                break;
            }
        }
    }
}

static td_s32 media_prepare_to_save_stream(venc_stream_proc_info *proc_info,
    ot_venc_stream_buf_info *buf_info, ot_payload_type *type, const sample_venc_getstream_para *para)
{
    td_s32 i, ret;
    ot_venc_chn_attr chn_attr;

    for (i = 0; (i < proc_info->chn_total) && (i < OT_VENC_MAX_CHN_NUM); i++) {
        ot_venc_chn venc_chn = para->venc_chn[i];
        ret = ss_mpi_venc_get_chn_attr(venc_chn, &chn_attr);
        if (ret != TD_SUCCESS) {
            sample_print("ss_mpi_venc_get_chn_attr chn[%d] failed with %#x!\n", venc_chn, ret);
            return SAMPLE_RETURN_NULL;
        }
        type[i] = chn_attr.venc_attr.type;

        ret = sample_comm_venc_get_file_postfix(type[i],
            proc_info->file_postfix[i], sizeof(proc_info->file_postfix[i]));
        if (ret != TD_SUCCESS) {
            sample_print("venc_get_file_postfix [%d] failed with %#x!\n", chn_attr.venc_attr.type, ret);
            return SAMPLE_RETURN_NULL;
        }

        /* set venc fd. */
        proc_info->venc_fd[i] = ss_mpi_venc_get_fd(i);
        if (proc_info->venc_fd[i] < 0) {
            sample_print("ss_mpi_venc_get_fd failed with %#x!\n", proc_info->venc_fd[i]);
            return SAMPLE_RETURN_NULL;
        }

        if (proc_info->maxfd <= proc_info->venc_fd[i]) {
            proc_info->maxfd = proc_info->venc_fd[i];
        }

        ret = ss_mpi_venc_get_stream_buf_info(i, &buf_info[i]);
        if (ret != TD_SUCCESS) {
            sample_print("ss_mpi_venc_get_stream_buf_info failed with %#x!\n", ret);
            return SAMPLE_RETURN_FAILURE;
        }
    }

    return TD_SUCCESS;
}

static td_void *media_venc_get_venc_stream_proc(td_void *p)
{
    td_s32 i, ret;
    fd_set read_fds;
    struct timeval timeout_val = {0};
    ot_payload_type type[OT_VENC_MAX_CHN_NUM] = {0};
    ot_venc_stream_buf_info stream_buf_info[OT_VENC_MAX_CHN_NUM];
    sample_venc_getstream_para *para = (sample_venc_getstream_para *)p;
    venc_stream_proc_info *proc_info = malloc(sizeof(venc_stream_proc_info));
    if (proc_info == TD_NULL) {
        goto FREE;
    }

    (td_void)memset_s(proc_info, sizeof(venc_stream_proc_info), 0, sizeof(venc_stream_proc_info));

    prctl(PR_SET_NAME, "get_venc_stream", 0, 0, 0);

    proc_info->chn_total = para->cnt;

    ret = media_prepare_to_save_stream(proc_info, stream_buf_info, type, para);
    if (ret != TD_SUCCESS) {
        goto FREE;
    }

    while (para->thread_start == TD_TRUE) {
        FD_ZERO(&read_fds);
        for (i = 0; (i < proc_info->chn_total) && (i < OT_VENC_MAX_CHN_NUM); i++) {
            FD_SET(proc_info->venc_fd[i], &read_fds);
        }

        timeout_val.tv_sec = 2; /* 2 is a number */
        timeout_val.tv_usec = 0;
        ret = select(proc_info->maxfd + 1, &read_fds, TD_NULL, TD_NULL, &timeout_val);
        if (ret < 0) {
            sample_print("select failed!\n");
            break;
        } else if (ret == 0) {
            sample_print("get venc stream time out, exit thread\n");
            continue;
        } else {
            media_fd_isset(proc_info, &read_fds, stream_buf_info, type, para);
        }
    }
    save_stream_to_sd(proc_info, 0, type);
    if (g_is_sd_mount) {
        kol_mount();
    }
FREE:
    if (proc_info != TD_NULL) {
        free(proc_info);
    }

    para->thread_start = TD_FALSE;
    return TD_NULL;
}

td_s32 media_venc_start_get_stream(ot_venc_chn *venc_chn, td_s32 cnt)
{
    td_s32 i, ret;
    td_s32 wait_times = WAIT_GET_STREAM_THREAD_START_TIMES;

    g_para.thread_start = TD_TRUE;
    g_para.cnt = cnt;
    if (cnt >= OT_VENC_MAX_CHN_NUM) {
        sample_print("input count invalid\n");
        return TD_FAILURE;
    }

    ot_venc_chn_attr venc_chn_attr;
    for (i = 0; (i < cnt) && (i < OT_VENC_MAX_CHN_NUM); i++) {
        g_para.venc_chn[i] = venc_chn[i];

        ret = ss_mpi_venc_get_chn_attr(venc_chn[i], &venc_chn_attr);
        if (ret != TD_SUCCESS) {
            sample_print("ss_mpi_venc_get_chn_attr chn[%d] failed with %#x!\n", i, ret);
            return SAMPLE_RETURN_NULL;
        }

        if (venc_chn_attr.venc_attr.type != OT_PT_JPEG) {
            g_stream_buffer[i] = (td_char *)malloc(g_srteam_buffer_size);
        }
    }

    ret = pthread_create(&g_venc_pid, 0, media_venc_get_venc_stream_proc, (td_void *)&g_para);
    if (ret < 0) {
        sample_print("create threa failed !!! ret %d\n", ret);
        return TD_FAILURE;
    }

    while ((g_para.thread_start != 0) && wait_times-- > 0) {
        usleep(WAIT_GET_STREAM_THREAD_START_SLEEP);
        if (g_para.thread_start == 0) {
            sample_print("start thread failed !!!\n");
            pthread_join(g_venc_pid, 0);
            return TD_FAILURE;
        }
    }

    return ret;
}

td_void media_venc_register_notify(pfn_notify_stream_finish notify_stream_finish)
{
    if (notify_stream_finish != TD_NULL) {
        g_notify_func = notify_stream_finish;
    }
}

td_void media_venc_unregister_notify(td_void)
{
    g_notify_func = TD_NULL;
}

td_void media_stop_venc(ot_venc_chn *venc_chn, td_s32 cnt)
{
    if (g_para.thread_start == TD_TRUE) {
        g_para.thread_start = TD_FALSE;
        pthread_join(g_venc_pid, 0);
    }

    for (td_s32 i = 0; i < cnt; i++) {
        sample_comm_venc_stop(venc_chn[i]);
        if (g_stream_buffer[i] != TD_NULL) {
            free(g_stream_buffer[i]);
            g_stream_buffer[i] = TD_NULL;
        }
    }
}
