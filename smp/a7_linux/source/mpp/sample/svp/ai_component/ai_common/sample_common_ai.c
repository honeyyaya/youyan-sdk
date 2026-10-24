/*
  Copyright (c), 2024-2024, Shenshu Tech. Co., Ltd.
 */

#include "sample_common_ai.h"
#include <securec.h>
#include <limits.h>
#include "ot_math.h"
#include "ss_mpi_sys_mem.h"

#define NUMBER_TWO          2
#define ALIGN_NUM           16

#define sample_common_ai_get_nv21_height(h) ((h) * 3 / 2)

td_s32 sample_common_ai_get_file_size(const td_char *path, td_u32 *len)
{
    *len = 0;
    FILE *fd = fopen(path, "rb");
    sample_common_ai_log_check_return(fd == TD_NULL, TD_FAILURE, "open file[%s] failed", path);

    (td_void)fseek(fd, 0, SEEK_END);
    *len = (td_u32)ftell(fd);
    (td_void)fclose(fd);

    return TD_SUCCESS;
}

td_s32 sample_common_ai_read_file(const td_char *path, td_u32 len, td_void *addr)
{
    td_char real_path[PATH_MAX] = {0};
    sample_common_ai_log_check_return(realpath(path, real_path) == TD_NULL, TD_FAILURE,
        "%s image path convert real path failed !!", path);
    FILE *fd = fopen(real_path, "rb");
    sample_common_ai_log_check_return(fd == TD_NULL, TD_FAILURE, "open file[%s] failed", real_path);

    (td_void)fseek(fd, 0, SEEK_SET);
    (td_void)memset_s(addr, len, 0, len);
    size_t read_len = fread(addr, 1, len, fd);
    td_s32 ret = (read_len == len) ? TD_SUCCESS : TD_FAILURE;
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, close_file, "read file[%s] failed", real_path);

close_file:
    (td_void)fclose(fd);
    fd = TD_NULL;
    return ret;
}

td_s32 sample_common_ai_create_nv21_frame(const ot_size *frame_resolution, ot_video_frame *frame)
{
    sample_common_ai_log_check_return(frame_resolution->width % NUMBER_TWO != 0, TD_FAILURE, "width must be even");
    sample_common_ai_log_check_return(frame_resolution->height % NUMBER_TWO != 0, TD_FAILURE, "height must be even");

    td_u32 stride = OT_ALIGN_UP(frame_resolution->width, ALIGN_NUM);

    td_phys_addr_t phy_addr = 0;
    td_void *vir_addr = TD_NULL;
    td_s32 ret = ss_mpi_sys_mmz_alloc(&phy_addr, &vir_addr, TD_NULL, TD_NULL,
        stride * sample_common_ai_get_nv21_height(frame_resolution->height));
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_sys_mmz_alloc error: %#x!!", ret);

    (td_void)memset_s(frame, sizeof(ot_video_frame), 0, sizeof(ot_video_frame));
    frame->width = frame_resolution->width;
    frame->height = frame_resolution->height;
    frame->pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    frame->phys_addr[0] = phy_addr;
    frame->phys_addr[1] = phy_addr + stride * frame->height;
    frame->virt_addr[0] = vir_addr;
    frame->virt_addr[1] = vir_addr + stride * frame->height;
    frame->stride[0] = stride;
    frame->stride[1] = stride;

    return TD_SUCCESS;
}

td_s32 sample_common_ai_read_nv21_frame(const td_char *image_path, ot_video_frame *frame)
{
    sample_common_ai_log_check_return(frame->width == 0, TD_FAILURE, "frame width can't be 0");
    sample_common_ai_log_check_return(frame->height == 0, TD_FAILURE, "frame height can't be 0");
    sample_common_ai_log_check_return(frame->width % NUMBER_TWO != 0, TD_FAILURE, "frame width must be even");
    sample_common_ai_log_check_return(frame->height % NUMBER_TWO != 0, TD_FAILURE, "frame height must be even");
    sample_common_ai_log_check_return(frame->stride[0] == 0, TD_FAILURE, "frame stride[0] can't be 0");
    sample_common_ai_log_check_return(frame->stride[0] != OT_ALIGN_UP(frame->width, ALIGN_NUM), TD_FAILURE,
        "frame stride[0](%u) is not equal to width(%u) after align to 16(%u)",
        frame->stride[0], frame->width, OT_ALIGN_UP(frame->width, ALIGN_NUM));
    sample_common_ai_log_check_return(frame->phys_addr[0] == 0, TD_FAILURE, "frame phys address can't be 0");
    sample_common_ai_log_check_return(frame->virt_addr[0] == TD_NULL, TD_FAILURE, "frame virt address can't be NULL");
    sample_common_ai_log_check_return(frame->pixel_format != OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420, TD_FAILURE,
        "frame is not in OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420 pixel format");

    td_u32 image_size = 0;
    td_char real_path[PATH_MAX] = {0};
    sample_common_ai_log_check_return(realpath(image_path, real_path) == TD_NULL, TD_FAILURE,
        "%s image path convert real path failed !!", image_path);
    td_s32 ret = sample_common_ai_get_file_size(real_path, &image_size);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "get file[%s] size failed", real_path);

    td_u32 frame_resolution_size = frame->width * sample_common_ai_get_nv21_height(frame->height);
    sample_common_ai_log_check_return(image_size != frame_resolution_size, TD_FAILURE,
        "image file size(%u) does not match frame resolution size(%u)", image_size, frame_resolution_size);

    FILE *fd = fopen(real_path, "rb");
    sample_common_ai_log_check_return(fd == TD_NULL, TD_FAILURE, "open file[%s] failed", real_path);

    td_void *row_ptr = frame->virt_addr[0];
    for (size_t row = 0; row < sample_common_ai_get_nv21_height(frame->height); ++row) {
        td_u32 read_size = fread(row_ptr, 1, frame->width, fd);
        ret = (read_size == frame->width) ? TD_SUCCESS : TD_FAILURE;
        sample_common_ai_log_check_goto(ret != TD_SUCCESS, close_file, "read file[%s] failed", real_path);
        row_ptr += frame->stride[0];
    }

close_file:
    (td_void)fclose(fd);
    fd = TD_NULL;
    return ret;
}

td_s32 sample_common_ai_destroy_frame(ot_video_frame *frame)
{
    if (frame->phys_addr[0] == 0 || frame->virt_addr[0] == TD_NULL) {
        return TD_SUCCESS;
    }

    td_s32 ret = ss_mpi_sys_mmz_free(frame->phys_addr[0], frame->virt_addr[0]);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_sys_mmz_free error: %#x!!", ret);

    frame->phys_addr[0] = 0;
    frame->phys_addr[1] = 0;
    frame->virt_addr[0] = TD_NULL;
    frame->virt_addr[1] = TD_NULL;

    return TD_SUCCESS;
}