/*
  Copyright (c), 2001-2025, Shenshu Tech. Co., Ltd.
 */
#ifdef AIDETECT_ENABLE
#include "ot_common_aidetect.h"
#include "ss_mpi_aidetect.h"
#include "algorithm.h"
#include "ss_mpi_smartae.h"
#include "ot_common_isp.h"
#include "ss_mpi_ae.h"
#include "ot_common_vpss.h"
#include "ot_common_venc.h"
#include "ss_mpi_vpss.h"
#include "ss_mpi_venc.h"
#include "sample_comm.h"

const char* AI_MODEL_PATH = "/sdcard/model/det_hvf_hor_ll_lite.bin";
ot_aidetect_chn g_aigorithm_aidetect_handle[AIDETECT_MAX_ROUTE] = {0, 1, 2, 3};
static aidetect_resolution g_aidetect_resolution[AIDETECT_MAX_ROUTE] = {0};
static ot_aidetect_result_array g_aidetect_result[AIDETECT_MAX_ROUTE] = {0};
static td_u32 g_aidetect_rect_color[AIDETECT_MAX_CLASS_NUM] = {AIDETECT_RECT_BLUE, AIDETECT_RECT_YELLOW,
                                                               AIDETECT_RECT_ORANGE, AIDETECT_RECT_GREEN,
                                                               AIDETECT_RECT_TEAL, AIDETECT_RECT_RED,
                                                               AIDETECT_RECT_RED, AIDETECT_RECT_RED};

static td_u32 g_aidetect_mask = 0;

td_void set_aidetect_mask(td_u32 mask)
{
    g_aidetect_mask = mask;
}

static td_s32 smart_ae_set_smart_exposure_attr(td_u32 route_num)
{
    td_s32 ret = TD_SUCCESS;
    for (td_u32 i = 0; i < route_num; i++) {
        if ((g_aidetect_mask & (1 << i)) == 0) {
            continue;
        }
        ot_isp_smart_exposure_attr smart_exp_attr = {0};
        ret = memset_s(&smart_exp_attr, sizeof(ot_isp_smart_exposure_attr), 0, sizeof(ot_isp_smart_exposure_attr));
        if (ret != TD_SUCCESS) {
            sample_print("memset_s failed[0x%x].\n", ret);
            return ret;
        }
        ret = ss_mpi_isp_get_smart_exposure_attr(i, &smart_exp_attr);
        if (ret != TD_SUCCESS) {
            sample_print("ss_mpi_isp_get_smart_exposure_attr failed[0x%x].\n", ret);
            return ret;
        }
        smart_exp_attr.enable = TD_TRUE;
        smart_exp_attr.ir_mode = TD_TRUE;
        smart_exp_attr.smart_exp_type = OT_OP_MODE_AUTO;
        smart_exp_attr.luma_target = 130;    /* 130 */
        smart_exp_attr.exp_coef_max = 8192;  /* 8192 */
        smart_exp_attr.exp_coef_min = 256;   /* 256 */
        smart_exp_attr.smart_interval = 2;   /* 2 */
        smart_exp_attr.smart_speed = 60;     /* 60 */
        smart_exp_attr.smart_delay_num = 60; /* 60 */
        ret = ss_mpi_isp_set_smart_exposure_attr(i, &smart_exp_attr);
        if (ret != TD_SUCCESS) {
            sample_print("ss_mpi_isp_set_smart_exposure_attr failed[0x%x].\n", ret);
            return ret;
        }
    }

    return TD_SUCCESS;
}

static td_void smart_ae_get_smart_info(ot_aidetect_result_array *result, ot_smartae_roi_info *smart_info,
    td_s32 route_id)
{
    smart_info->class_num = 0;
    td_u32 smart_info_index = 0;

    for (td_u32 i = 0; i < result->class_num; i++) {
        if (i != 0 && i != 1) { /* only support face and people */
            continue;
        }
        if (result->object_class[i].object_num <= 0) {
            continue;
        }
        td_u32 rect_num = 0;
        for (td_u32 j = 0; j < result->object_class[i].object_num && j < OT_SMARTAE_MAX_RECT_NUM; j++) {
            td_s32 rect_x = result->object_class[i].objects[j].detect_rect.x;
            td_s32 rect_y = result->object_class[i].objects[j].detect_rect.y;
            td_u32 rect_width = result->object_class[i].objects[j].detect_rect.width;
            td_u32 rect_height = result->object_class[i].objects[j].detect_rect.height;
            td_u32 width_ratio = OT_ALIGN_DOWN(rect_width * 3 / 5, EVEN_PIXEL);
            td_u32 height_ratio = i == 0 ? OT_ALIGN_DOWN(rect_height * 4 / 5, EVEN_PIXEL)
                                         : OT_ALIGN_DOWN(rect_height / 4, EVEN_PIXEL);
            if (width_ratio < 6 || height_ratio < 6 /* 6 min rect width and height */
                || (rect_x + rect_width) > g_aidetect_resolution[route_id].width
                || (rect_y + rect_height) > g_aidetect_resolution[route_id].height) {
                continue;
            }
            smart_info->obj_class[smart_info_index].objs[rect_num].rect.x = OT_ALIGN_DOWN(rect_x, EVEN_PIXEL);
            smart_info->obj_class[smart_info_index].objs[rect_num].rect.y = OT_ALIGN_DOWN(rect_y, EVEN_PIXEL);
            smart_info->obj_class[smart_info_index].objs[rect_num].rect.width = OT_ALIGN_DOWN(rect_width, EVEN_PIXEL);
            smart_info->obj_class[smart_info_index].objs[rect_num].rect.height = OT_ALIGN_DOWN(rect_height, EVEN_PIXEL);
            smart_info->obj_class[smart_info_index].objs[rect_num].score = 1.0f;
            rect_num++;
        }
        if (rect_num > 0) {
            smart_info->obj_class[smart_info_index].type = i;
            smart_info->obj_class[smart_info_index].rect_num = rect_num;
            smart_info->class_num++;
            smart_info_index++;
        }
    }
}

static td_s32 set_svc_param(ot_venc_chn chn)
{
    td_s32 ret;
    ot_venc_svc_param_ex svc_param;

    ret = ss_mpi_venc_get_svc_param_ex(chn, &svc_param);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_venc_get_svc_param_ex failed[0x%x].\n", ret);
        return ret;
    }

    svc_param.svc_version = OT_VENC_SVC_V2;
    svc_param.svc_param_v2.max_ref_num = 2;      /* 2: algn */
    svc_param.svc_param_v2.refresh_interval = 2; /* 2: algn */
    for (td_u32 m = 0; m < 16; m++) {            /* 16: algn */
        svc_param.svc_param_v2.qp_delta[m] = 0;
    }
    svc_param.svc_param_v2.qp_delta[0] = 0;
    svc_param.svc_param_v2.qp_delta[1] = 0;
    svc_param.svc_param_v2.qp_delta[15] = 3;  /* 15 3: algn */
    ret = ss_mpi_venc_set_svc_param_ex(chn, &svc_param);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_venc_set_svc_param_ex failed[0x%x].\n", ret);
        return ret;
    }

    return TD_SUCCESS;
}

td_s32 algorithm_svc_set_rect(td_u64 pts, ot_size size, aidetect_rect *corner_rect, td_u32 route_id, ot_venc_chn chn)
{
    td_s32 ret;
    ot_venc_svc_rect_info rect;
    td_u32 rect_num = 0;

    if ((g_aidetect_mask & (1 << route_id)) == 0) {
        return TD_SUCCESS;
    }

    for (td_u32 i = 0; i < AIDETECT_MAX_CLASS_NUM; i++) {
        if (i != 0 && i != 1) { /* only support face and people */
            continue;
        }
        if (corner_rect[i].rect_cnt > 0) {
            for (td_u32 j = 0; j < corner_rect[i].rect_cnt; j++) {
                td_u32 index = rect_num + j;
                rect.rect_attr[index].x = corner_rect[i].corner_rect[j].x;
                rect.rect_attr[index].y = corner_rect[i].corner_rect[j].y;
                rect.rect_attr[index].width = corner_rect[i].corner_rect[j].width;
                rect.rect_attr[index].height = corner_rect[i].corner_rect[j].height;
                rect.detect_type[index] = i;
            }
            rect_num += corner_rect[i].rect_cnt;
        }
    }

    if (rect_num <= 0) {
        return TD_SUCCESS;
    }

    rect.rect_num = rect_num;
    rect.base_resolution.width = size.width;
    rect.base_resolution.height = size.height;
    rect.pts = pts;

    ret = ss_mpi_venc_send_svc_region(chn, &rect);
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_venc_send_svc_region[%d] failed[0x%x].\n", chn, ret);
        return ret;
    }

    ret = set_svc_param(chn);
    if (ret != TD_SUCCESS) {
        sample_print("set_svc_param failed.\n");
        return ret;
    }

    return TD_SUCCESS;
}

static td_s32 algorithm_aidetect_init_global_param(td_u32 route_num)
{
    td_s32 ret;
    ot_aidetect_model_info aidetect_info;

    for (td_u32 i = 0; i < route_num; i++) {
        if ((g_aidetect_mask & (1 << i)) == 0) {
            continue;
        }
        (td_void)memset_s(&aidetect_info, sizeof(ot_aidetect_model_info), 0, sizeof(ot_aidetect_model_info));
        ret = ss_mpi_aidetect_get_model_info(g_aigorithm_aidetect_handle[i], &aidetect_info);
        if (ret != TD_SUCCESS) {
            sample_print("aidetect get model info failed[0x%x].\n", ret);
            return ret;
        }

        g_aidetect_resolution[i].width = aidetect_info.size.width;
        g_aidetect_resolution[i].height = aidetect_info.size.height;
        g_aidetect_result[i].class_num = aidetect_info.class_num > AIDETECT_MAX_CLASS_NUM ?
                                    AIDETECT_MAX_CLASS_NUM : aidetect_info.class_num;
        for (td_u32 j = 0; j < g_aidetect_result[i].class_num; j++) {
            if (g_aidetect_result[i].object_class[j].objects != TD_NULL) {
                continue;
            }
            g_aidetect_result[i].object_class[j].class_type = aidetect_info.classes[j];
            g_aidetect_result[i].object_class[j].object_capacity = AIDETECT_MAX_OBJECT_CNT;
            g_aidetect_result[i].object_class[j].objects = (ot_aidetect_object *)malloc(sizeof(ot_aidetect_object) *
                g_aidetect_result[i].object_class[j].object_capacity);
            if (g_aidetect_result[i].object_class[j].objects == TD_NULL) {
                continue;
            }
            (td_void)memset_s(g_aidetect_result[i].object_class[j].objects,
                sizeof(ot_aidetect_object) * g_aidetect_result[i].object_class[j].object_capacity,
                0, sizeof(ot_aidetect_object) * g_aidetect_result[i].object_class[j].object_capacity);
        }
    }
    return TD_SUCCESS;
}

td_s32 algorithm_aidetect_deinit(td_u32 route_num)
{
    td_s32 ret = TD_SUCCESS;

    for (td_u32 i = 0; i < route_num; i++) {
        if ((g_aidetect_mask & (1 << i)) == 0) {
            continue;
        }
        for (td_u32 j = 0; j < g_aidetect_result[i].class_num; j++) {
            if (g_aidetect_result[i].object_class[j].objects != TD_NULL) {
                free(g_aidetect_result[i].object_class[j].objects);
                g_aidetect_result[i].object_class[j].objects = TD_NULL;
            }
        }
        g_aidetect_result[i].class_num = 0;

        ret = ss_mpi_aidetect_destroy_chn(g_aigorithm_aidetect_handle[i]);
        if (ret != TD_SUCCESS) {
            sample_print("aidetect destroy chn[%u] failed.[0x%x].\n", i, ret);
        }
    }

    return TD_SUCCESS;
}

td_s32 algorithm_aidetect_init(td_u32 route_num)
{
    td_s32 ret = TD_SUCCESS;

    ot_aidetect_input_model model_info;
    model_info.model_load_mode = OT_AIDETECT_MODEL_LOAD_FROM_PATH;
    model_info.model = (td_void *)AI_MODEL_PATH;
    model_info.size = (td_u32)strlen(AI_MODEL_PATH);

    ot_aidetect_chn_attr chn_attr;
    for (td_u32 i = 0; i < route_num; i++) {
        if ((g_aidetect_mask & (1 << i)) == 0) {
            continue;
        }
        (td_void)memset_s(&chn_attr, sizeof(ot_aidetect_chn_attr), 0, sizeof(ot_aidetect_chn_attr));
        ret = ss_mpi_aidetect_create_chn(g_aigorithm_aidetect_handle[i], &model_info, &chn_attr);
        if (ret != TD_SUCCESS) {
            sample_print("aidetect create chn[%u] failed[0x%x].\n", i, ret);
            return ret;
        }
    }

    ret = algorithm_aidetect_init_global_param(route_num);
    if (ret != TD_SUCCESS) {
        algorithm_aidetect_deinit(route_num);
        return ret;
    }

    ret = smart_ae_set_smart_exposure_attr(route_num);
    if (ret != TD_SUCCESS) {
        sample_print("set_smart_exposure_attr failed.\n");
    }

    return TD_SUCCESS;
}

td_void algorithm_detect_result_clear(ot_aidetect_result_array *result)
{
    for (td_u32 i = 0; i < result->class_num; i++) {
        result->object_class[i].object_num = 0;
        (td_void)memset_s(result->object_class[i].objects,
                          sizeof(ot_aidetect_object) * result->object_class[i].object_capacity, 0,
                          sizeof(ot_aidetect_object) * result->object_class[i].object_capacity);
    }
}

td_void algorithm_detect_result_to_corner_rect(ot_aidetect_result_array *result, td_u32 route_id,
    aidetect_rect *corner_rect, td_s32 in_width, td_s32 in_height)
{
    td_s32 x;
    td_s32 y;
    td_u32 width;
    td_u32 height;

    if ((result == TD_NULL) || (corner_rect == TD_NULL)) {
        sample_print("Input param is null.\n");
        return;
    }

    for (td_u32 i = 0; i < result->class_num; i++) {
        ot_aidetect_class class_type = result->object_class[i].class_type;
        td_u32 object_num = (result->object_class[i].object_num > AIDETECT_MAX_OBJECT_CNT) ?
                             AIDETECT_MAX_OBJECT_CNT : result->object_class[i].object_num;
        td_u32 rect_count = 0;
        for (td_u32 j = 0; j < object_num; j++) {
            x = result->object_class[i].objects[j].detect_rect.x * in_width /
                g_aidetect_resolution[route_id].width / EVEN_PIXEL * EVEN_PIXEL;
            y = result->object_class[i].objects[j].detect_rect.y * in_height /
                g_aidetect_resolution[route_id].height / EVEN_PIXEL * EVEN_PIXEL;
            width = result->object_class[i].objects[j].detect_rect.width * in_width /
                    g_aidetect_resolution[route_id].width / EVEN_PIXEL * EVEN_PIXEL;
            height = result->object_class[i].objects[j].detect_rect.height * in_height /
                     g_aidetect_resolution[route_id].height / EVEN_PIXEL * EVEN_PIXEL;
            if ((width == 0) || (height == 0)) {
                continue;
            }
            corner_rect[class_type].corner_rect[rect_count].x = x;
            corner_rect[class_type].corner_rect[rect_count].y = y;
            corner_rect[class_type].corner_rect[rect_count].width = width;
            corner_rect[class_type].corner_rect[rect_count].height = height;
            rect_count++;
        }
        corner_rect[class_type].rect_cnt = rect_count;
    }

    algorithm_detect_result_clear(result);
}

td_s32 algorithm_add_vgs_rect_task(ot_vgs_handle vgs_handle, const ot_vgs_task_attr *task_attr,
    const aidetect_rect *rect, td_u32 rect_size)
{
    td_s32 ret;
    ot_corner_rect_attr rect_attr = {0};

    rect_attr.corner_rect_type = OT_CORNER_RECT_TYPE_FULL_LINE;
    for (td_u32 i = 0; i < rect_size; i++) {
        if (rect[i].rect_cnt == 0) {
            continue;
        }

        ot_corner_rect corner_rect[AIDETECT_MAX_RECT_CNT] = {0};
        for (td_u32 j = 0; j < rect[i].rect_cnt; j++) {
            corner_rect[j].thick = CORNER_RECT_LINE_THICK;
            corner_rect[j].hor_len = CORNER_RECT_HOR_LEN;
            corner_rect[j].ver_len = CORNER_RECT_VER_LEN;
            corner_rect[j].rect.x = rect[i].corner_rect[j].x;
            corner_rect[j].rect.y = rect[i].corner_rect[j].y;
            corner_rect[j].rect.width = rect[i].corner_rect[j].width;
            corner_rect[j].rect.height = rect[i].corner_rect[j].height;
        }

        rect_attr.color = g_aidetect_rect_color[i];

        ret = ss_mpi_vgs_add_corner_rect_task(vgs_handle, task_attr, corner_rect, &rect_attr, rect[i].rect_cnt);
        if (ret != TD_SUCCESS) {
            sample_print("draw rect vgs add corner rect task failed.[0x%x].\n", ret);
            return ret;
        }
    }
    return TD_SUCCESS;
}

td_s32 algorithm_aidetect_proc(ot_video_frame_info *frame_info, td_u32 route_id, aidetect_rect *corner_rect,
    td_s32 width, td_s32 height)
{
    td_s32 ret = TD_SUCCESS;

    if ((g_aidetect_mask & (1 << route_id)) == 0) {
        return TD_SUCCESS;
    }

    ot_video_frame frame = {0};
    td_u32 blk_size = frame_info->video_frame.width * frame_info->video_frame.height * 3 / 2;
    frame.width = frame_info->video_frame.width;
    frame.height = frame_info->video_frame.height;
    frame.phys_addr[0] = frame_info->video_frame.phys_addr[0];
    frame.phys_addr[1] = frame_info->video_frame.phys_addr[1];
    td_void *vir_addr = ss_mpi_sys_mmap(frame_info->video_frame.phys_addr[0], blk_size);
    frame.virt_addr[0] = vir_addr;
    frame.virt_addr[1] = vir_addr + frame_info->video_frame.width * frame_info->video_frame.height;
    frame.stride[0] = frame_info->video_frame.stride[0];
    frame.stride[1] = frame_info->video_frame.stride[1];
    frame.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    ret = ss_mpi_aidetect_process(g_aigorithm_aidetect_handle[route_id], &frame, &g_aidetect_result[route_id]);
    if (ret != TD_SUCCESS) {
        sample_print("aidetect process failed.[0x%x].\n", ret);
        ss_mpi_sys_munmap(vir_addr, blk_size);
        return ret;
    }

    ss_mpi_sys_munmap(vir_addr, blk_size);

    ot_smartae_roi_info smart_info = {0};
    smart_ae_get_smart_info(&g_aidetect_result[route_id], &smart_info, route_id);
    if (smart_info.class_num > 0) {
        ret = ss_mpi_smartae_update_roi_info(route_id, frame_info, &smart_info);
        if (ret != TD_SUCCESS) {
            sample_print("ss_mpi_smartae_update_roi_info failed.\n");
        }
    }

    algorithm_detect_result_to_corner_rect(&g_aidetect_result[route_id], route_id, corner_rect, width, height);

    return ret;
}

td_bool algorithm_detected_target(aidetect_rect *corner_rect, td_u32 rect_size)
{
    for (td_u32 i = 0; i < rect_size; i++) {
        if (corner_rect[i].rect_cnt != 0) {
            printf("============Target detected!==========\n");
            return TD_TRUE;
        }
    }
    printf("============No target detected!==========\n");
    return TD_FALSE;
}

td_s32 algorithm_set_track_attr(td_u32 route_num, td_bool track_en)
{
    ot_aidetect_chn_attr get_chn_attr[route_num];

    for (td_u32 i = 0; i < route_num; ++i) {
        if ((g_aidetect_mask & (1 << i)) == 0) {
            continue;
        }
        (td_void) memset_s(&get_chn_attr[i], sizeof(ot_aidetect_chn_attr), 0, sizeof(ot_aidetect_chn_attr));
        td_s32 ret = ss_mpi_aidetect_get_chn_attr(i, &get_chn_attr[i]);
        if (ret != TD_SUCCESS) {
            sample_print("ss_mpi_aidetect_get_chn_attr get failed %#x \n", ret);
            return ret;
        }

        ot_aidetect_chn_attr set_chn_attr;
        (td_void) memset_s(&set_chn_attr, sizeof(ot_aidetect_chn_attr), 0, sizeof(ot_aidetect_chn_attr));

        set_chn_attr.track_class_num = get_chn_attr[i].track_class_num;
        for (td_u32 j = 0; j < get_chn_attr[i].track_class_num; ++j) {
            set_chn_attr.track_class_attr[j].class_type = get_chn_attr[i].track_class_attr[j].class_type;
            set_chn_attr.track_class_attr[j].track_en = track_en;
        }

        ret = ss_mpi_aidetect_set_chn_attr(i, &set_chn_attr);
        if (ret != TD_SUCCESS) {
            sample_print("ss_mpi_aidetect_set_chn_attr set failed %#x \n", ret);
            return ret;
        }
    }
    return TD_SUCCESS;
}

#endif
