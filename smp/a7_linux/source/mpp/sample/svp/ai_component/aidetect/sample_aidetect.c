/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include "securec.h"
#include "ss_mpi_vb.h"
#include "ss_mpi_sys_mem.h"
#include "ss_mpi_sys.h"
#include "ot_math.h"
#include "ss_mpi_aidetect.h"
#include "sample_common_aidetect.h"
#include "sample_common_ai.h"

#define OT_SAMPLE_STR_LEN               256
#define OT_SAMPLE_AIDETECT_PATH_LEN     256
#define OT_SAMPLE_AIDETECT_CHN_0        0

typedef struct {
    td_phys_addr_t phys_addr;
    td_void *virt_addr;
} memory_struct;

static td_bool g_show_help = TD_FALSE;
static td_s32 g_path_or_mem = OT_AIDETECT_MODEL_LOAD_FROM_MEMORY; // 0: load model from memory, 1: load model from path
static td_char g_res_path[OT_SAMPLE_AIDETECT_PATH_LEN] = "";
static td_char g_model_path[OT_SAMPLE_AIDETECT_PATH_LEN] = "";
static ot_size g_image_size = { 0 };
static memory_struct g_aidetect_model_mem = { 0 };
static ot_aidetect_model_info g_model_info = { 0 };

static td_void sample_aidetect_usage(td_char **argv)
{
    (td_void)printf("usage: %s [i:m:s:hv]\n", argv[0]);
    (td_void)printf("\t  -h   show this info\n");
    (td_void)printf("\t  -i   input file path, yuv name\n");
    (td_void)printf("\t  -m   model path\n");
    (td_void)printf("\t  -s   size of image, width x height\n");
    (td_void)printf("\t  -v   input model file or memory. Reading from memory by default. Add to read from path\n");
    (td_void)printf("example:\n");
    (td_void)printf("\t  %s -h\n", argv[0]);
    (td_void)printf("\t  %s -m ./model.bin -i ./image.yuv -s 1920x1080 -v\n", argv[0]);
}

static td_s32 sample_aidetect_get_size(td_char *string)
{
    sample_common_ai_log_check_return(strlen(string) == 0, TD_FAILURE, "Image size param is an empty string");

    td_s32 base = 10;
    td_char *endptr;
    td_char *width = strtok(string, "x");
    sample_common_ai_log_check_return(width == NULL, TD_FAILURE, "input size format is not correct");
    g_image_size.width = (td_u32)strtol(width, &endptr, base);
    sample_common_ai_log_check_return((endptr == string || *endptr != '\0'), TD_FAILURE, "width is not a number");

    td_char *height = strtok(NULL, "x");
    sample_common_ai_log_check_return(height == NULL, TD_FAILURE, "input size format is not correct");
    g_image_size.height = (td_u32)strtol(height, &endptr, base);
    sample_common_ai_log_check_return((endptr == string || *endptr != '\0'), TD_FAILURE, "height is not a number");

    return TD_SUCCESS;
}

static td_s32 sample_aidetect_parse_param(td_s32 argc, td_char *argv[])
{
    td_s32 c;
    td_char tmp_str[OT_SAMPLE_STR_LEN] = { 0 };

    while ((c = getopt(argc, argv, "i:m:s:hv")) != EOF) {
        switch (c) {
            case 'i':
                sample_common_ai_log_check_return(
                    strncpy_s(tmp_str, OT_SAMPLE_STR_LEN - 1, optarg, strlen(optarg)) != EOK, TD_FAILURE,
                    "get image path failed!");
                sample_common_ai_log_check_return(realpath(tmp_str, g_res_path) == TD_NULL, TD_FAILURE,
                    "%s image path convert real path failed !!", tmp_str);
                break;
            case 'v':
                g_path_or_mem = OT_AIDETECT_MODEL_LOAD_FROM_PATH;
                break;
            case 'm':
                sample_common_ai_log_check_return(
                    strncpy_s(tmp_str, OT_SAMPLE_STR_LEN - 1, optarg, strlen(optarg)) != EOK, TD_FAILURE,
                    "get model path failed!");
                sample_common_ai_log_check_return(realpath(tmp_str, g_model_path) == TD_NULL, TD_FAILURE,
                    "%s model path convert real path failed !!", tmp_str);
                break;
            case 'h':
                g_show_help = TD_TRUE;
                break;
            case 's':
                sample_common_ai_log_check_return(
                    strncpy_s(tmp_str, OT_SAMPLE_STR_LEN - 1, optarg, strlen(optarg)) != EOK, TD_FAILURE,
                    "get image size failed!");
                sample_common_ai_log_check_return(sample_aidetect_get_size(tmp_str) != TD_SUCCESS, TD_FAILURE,
                    "sample_aidetect_get_size failed");
                break;
            default:
                sample_aidetect_usage(argv);
                return TD_FAILURE;
        }
    }
    return TD_SUCCESS;
}

static td_s32 sample_aidetect_check_param()
{
    if ((access(g_model_path, F_OK) != 0) || (access(g_res_path, F_OK) != 0)) {
        (td_void)printf("model file path[%s] or res file path[%s] not exist.\n", g_model_path, g_res_path);
        return TD_FAILURE;
    }
    if (g_image_size.width == 0 || g_image_size.height == 0) {
        (td_void)printf("image width(%u) or height(%u) is 0\n", g_image_size.width, g_image_size.height);
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

static td_void sample_aidetect_print_param()
{
    (td_void)printf("==================== Input Param ====================\n");
    if (g_path_or_mem == OT_AIDETECT_MODEL_LOAD_FROM_MEMORY) {
        (td_void)printf("Read model from memory\n");
    } else {
        (td_void)printf("Read model from path\n");
    }
    (td_void)printf("Model path: %s\n", g_model_path);
    (td_void)printf("Image path: %s\n", g_res_path);
    (td_void)printf("Image size: %ux%u\n", g_image_size.width, g_image_size.height);
}

static td_s32 sample_aidetect_init_input_model(ot_aidetect_input_model *input_model)
{
    if (g_path_or_mem == OT_AIDETECT_MODEL_LOAD_FROM_PATH) {
        input_model->model_load_mode = OT_AIDETECT_MODEL_LOAD_FROM_PATH;
        input_model->model = (td_void *)g_model_path;
        input_model->size = (td_u32)strlen(g_model_path);
    } else {
        input_model->model_load_mode = OT_AIDETECT_MODEL_LOAD_FROM_MEMORY;
        td_u32 file_size = 0;
        td_s32 ret = sample_common_ai_get_file_size(g_model_path, &file_size);
        sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "read model[%s] data error.", g_model_path);

        ret = ss_mpi_sys_mmz_alloc(&g_aidetect_model_mem.phys_addr, &g_aidetect_model_mem.virt_addr,
            TD_NULL, TD_NULL, file_size);
        sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_sys_mmz_alloc failed");

        ret = sample_common_ai_read_file(g_model_path, file_size, g_aidetect_model_mem.virt_addr);
        sample_common_ai_log_check_goto(ret != TD_SUCCESS, free_model, "sample_aidetect_read_file failed");

        input_model->model = g_aidetect_model_mem.virt_addr;
        input_model->size = file_size;
    }

    return TD_SUCCESS;
free_model:
    (td_void)ss_mpi_sys_mmz_free(g_aidetect_model_mem.phys_addr, g_aidetect_model_mem.virt_addr);
    return TD_FAILURE;
}

static td_s32 sample_aidetect_deinit_input_model()
{
    if (g_aidetect_model_mem.phys_addr != 0 || g_aidetect_model_mem.virt_addr != NULL) {
        (td_void)ss_mpi_sys_mmz_free(g_aidetect_model_mem.phys_addr, g_aidetect_model_mem.virt_addr);
        g_aidetect_model_mem.phys_addr = 0;
        g_aidetect_model_mem.virt_addr = NULL;
    }
    return TD_SUCCESS;
}

static td_s32 sample_aidetect_init()
{
    // create ot_aidetect_input_model
    ot_aidetect_input_model input_model;
    (td_void)memset_s(&input_model, sizeof(ot_aidetect_input_model), 0, sizeof(ot_aidetect_input_model));
    td_s32 ret = sample_aidetect_init_input_model(&input_model);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "sample init aidetect input model error!!");

    // crate chn_attr
    ot_aidetect_chn_attr chn_attr;
    (td_void)memset_s(&chn_attr, sizeof(ot_aidetect_chn_attr), 0, sizeof(ot_aidetect_chn_attr));

    // create channel
    ret = ss_mpi_aidetect_create_chn(OT_SAMPLE_AIDETECT_CHN_0, &input_model, &chn_attr);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit, "ss_mpi_aidetect_create_chn error: %#x!!", ret);

deinit:
    (td_void)sample_aidetect_deinit_input_model();
    return ret;
}

static td_s32 sample_aidetect_deinit()
{
    td_s32 ret = ss_mpi_aidetect_destroy_chn(OT_SAMPLE_AIDETECT_CHN_0);
    sample_common_ai_log_check(ret != TD_SUCCESS, "ss_mpi_aidetect_destroy_chn error: %#x!!", ret);
    return ret;
}

static td_s32 sample_aidetect_show_model_info()
{
    (td_void)memset_s(&g_model_info, sizeof(ot_aidetect_model_info), 0, sizeof(ot_aidetect_model_info));
    td_s32 ret = ss_mpi_aidetect_get_model_info(OT_SAMPLE_AIDETECT_CHN_0, &g_model_info);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_aidetect_get_model_info error: %#x!!",
        ret);

    (td_void)printf("==================== Model Info ====================\n");
    (td_void)printf("model support frame width: %u, model support frame height: %u\n",
        g_model_info.size.width, g_model_info.size.height);
    (td_void)printf("model support class num: %u\n", g_model_info.class_num);
    (td_void)printf("model support class type:|");
    for (td_u32 i = 0; i < g_model_info.class_num; ++i) {
        (td_void)printf(" %d |", g_model_info.classes[i]);
    }
    (td_void)printf("\n");
    return TD_SUCCESS;
}

static td_s32 sample_aidetect_get_set_attr()
{
    ot_aidetect_chn_attr get_chn_attr;
    (td_void)memset_s(&get_chn_attr, sizeof(ot_aidetect_chn_attr), 0, sizeof(ot_aidetect_chn_attr));
    td_s32 ret = ss_mpi_aidetect_get_chn_attr(OT_SAMPLE_AIDETECT_CHN_0, &get_chn_attr);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_aidetect_get_chn_attr error: %#x!!", ret);

    (td_void)printf("==================== Default Chn Attr ====================\n");
    (td_void)printf("track num: %u\n", get_chn_attr.track_class_num);
    for (td_u32 i = 0; i < get_chn_attr.track_class_num; ++i) {
        printf("track type:%d, is_tracking: %d\n",
            get_chn_attr.track_class_attr[i].class_type, get_chn_attr.track_class_attr[i].track_en);
    }

    ot_aidetect_chn_attr set_chn_attr;
    (td_void)memset_s(&set_chn_attr, sizeof(ot_aidetect_chn_attr), 0, sizeof(ot_aidetect_chn_attr));

    set_chn_attr.track_class_num = get_chn_attr.track_class_num;
    for (td_u32 i = 0; i < get_chn_attr.track_class_num; ++i) {
        set_chn_attr.track_class_attr[i].class_type = get_chn_attr.track_class_attr[i].class_type;
        set_chn_attr.track_class_attr[i].track_en = get_chn_attr.track_class_attr[i].track_en;
    }
    ret = ss_mpi_aidetect_set_chn_attr(OT_SAMPLE_AIDETECT_CHN_0, &set_chn_attr);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_aidetect_set_chn_attr error: %#x!!", ret);

    return TD_SUCCESS;
}

static td_s32 sample_aidetect_get_set_param()
{
    ot_aidetect_chn_param get_chn_param;
    (td_void)memset_s(&get_chn_param, sizeof(ot_aidetect_chn_param), 0, sizeof(ot_aidetect_chn_param));

    td_s32 ret = ss_mpi_aidetect_get_chn_param(OT_SAMPLE_AIDETECT_CHN_0, &get_chn_param);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_aidetect_get_chn_param error: %#x!!", ret);

    (td_void)printf("==================== Default Chn Param ====================\n");
    (td_void)printf("preemp_en: %d, priority: %u, priority_up_step_timeout: %u, priority_up_top_timeout: %u\n",
        get_chn_param.model_priority.preemp_en, get_chn_param.model_priority.priority,
        get_chn_param.model_priority.priority_up_step_timeout, get_chn_param.model_priority.priority_up_top_timeout);
    (td_void)printf("detect num: %u\n", get_chn_param.detect_threshold_num);
    for (td_u32 i = 0; i < get_chn_param.detect_threshold_num; ++i) {
        printf("detect type:%d, threshold: %f, track_missing_frame_num: %u\n",
            get_chn_param.detect_threshold[i].class_type, get_chn_param.detect_threshold[i].detect_threshold,
            get_chn_param.detect_threshold[i].track_miss_frame_num);
    }

    ot_aidetect_chn_param set_chn_param;
    (td_void)memset_s(&set_chn_param, sizeof(ot_aidetect_chn_param), 0, sizeof(ot_aidetect_chn_param));
    set_chn_param.model_priority.preemp_en = get_chn_param.model_priority.preemp_en;
    set_chn_param.model_priority.priority = get_chn_param.model_priority.priority;
    set_chn_param.model_priority.priority_up_top_timeout = get_chn_param.model_priority.priority_up_top_timeout;
    set_chn_param.model_priority.priority_up_step_timeout = get_chn_param.model_priority.priority_up_step_timeout;
    set_chn_param.detect_threshold_num = get_chn_param.detect_threshold_num;
    for (td_u32 i = 0; i < get_chn_param.detect_threshold_num; ++i) {
        set_chn_param.detect_threshold[i].class_type = get_chn_param.detect_threshold[i].class_type;
        set_chn_param.detect_threshold[i].track_miss_frame_num = get_chn_param.detect_threshold[i].track_miss_frame_num;
        set_chn_param.detect_threshold[i].detect_threshold = get_chn_param.detect_threshold[i].detect_threshold;
    }
    ret = ss_mpi_aidetect_set_chn_param(OT_SAMPLE_AIDETECT_CHN_0, &set_chn_param);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_aidetect_get_chn_param error: %#x!!", ret);

    return TD_SUCCESS;
}

static td_s32 sample_aidetect_init_result(ot_aidetect_result_array *result)
{
    (td_void)memset_s(result, sizeof(ot_aidetect_result_array), 0, sizeof(ot_aidetect_result_array));
    result->class_num = g_model_info.class_num;

    td_u32 i = 0;
    for (i = 0; i < result->class_num; ++i) {
        result->object_class[i].class_type = g_model_info.classes[i];
        result->object_class[i].object_capacity = OT_SAMPLE_AIDETECT_MAX_OUTPUT_RECT_NUM;
        result->object_class[i].objects = (ot_aidetect_object *)malloc(sizeof(ot_aidetect_object) *
            result->object_class[i].object_capacity);
        sample_common_ai_log_check_goto(result->object_class[i].objects == NULL, free_obj, "malloc result failed\n");
        (td_void)memset_s(result->object_class[i].objects,
            sizeof(ot_aidetect_object) * result->object_class[i].object_capacity, 0,
            sizeof(ot_aidetect_object) * result->object_class[i].object_capacity);
    }
    return TD_SUCCESS;
free_obj:
    for (td_u32 j = 0; j < i; ++j) {
        free(result->object_class[j].objects);
    }
    return TD_FAILURE;
}

static td_void sample_aidetect_deinit_result(ot_aidetect_result_array *result)
{
    if (result == TD_NULL) {
        return;
    }

    for (td_u32 i = 0; i < result->class_num; ++i) {
        if (result->object_class[i].objects != TD_NULL) {
            free(result->object_class[i].objects);
            result->object_class[i].objects = TD_NULL;
        }
    }
}

void print_video_frame(const ot_video_frame *frame) {
    if (!frame) {
        printf("Error: NULL frame pointer\n");
        return;
    }

    // 基础属性
    printf("width: %u\n", frame->width);
    printf("height: %u\n", frame->height);
    printf("field: %d\n", (int)frame->field);
    printf("pixel_format: %d\n", (int)frame->pixel_format);
    printf("video_format: %d\n", (int)frame->video_format);
    printf("compress_mode: %d\n", (int)frame->compress_mode);
    printf("dynamic_range: %d\n", (int)frame->dynamic_range);
    printf("color_gamut: %d\n", (int)frame->color_gamut);

    // 数组类型成员
    for (int i = 0; i < OT_MAX_COLOR_COMPONENT; i++) {
        printf("header_stride[%d]: %u\n", i, frame->header_stride[i]);
        printf("stride[%d]: %u\n", i, frame->stride[i]);
        printf("header_phys_addr[%d]: 0x%lx\n", i, (unsigned long)frame->header_phys_addr[i]);
        printf("phys_addr[%d]: 0x%lx\n", i, (unsigned long)frame->phys_addr[i]);
        printf("header_virt_addr[%d]: %p\n", i, frame->header_virt_addr[i]);
        printf("virt_addr[%d]: %p\n", i, frame->virt_addr[i]);
    }

    // 时间相关
    printf("time_ref: %u\n", frame->time_ref);
    printf("pts: %lu\n", frame->pts);

    // 用户数据
    for (int i = 0; i < OT_MAX_USER_DATA_NUM; i++) {
        printf("user_data[%d]: %lu\n", i, frame->user_data[i]);
    }

    printf("frame_flag: 0x%x\n", frame->frame_flag);
  //  printf("supplement: (需补充ot_video_supplement的打印实现)\n");
}




static td_s32 sample_aidetect_process()
{
    // prepare frame
    ot_video_frame frame;
    td_s32 ret = sample_common_ai_create_nv21_frame(&g_image_size, &frame);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "sample_common_ai_create_nv21_frame fail!!");

    ret = sample_common_ai_read_nv21_frame(g_res_path, &frame);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, destroy_frame, "sample_aidetect_read_frame error");
	print_video_frame(&frame);
    // result
    ot_aidetect_result_array result;
    ret = sample_aidetect_init_result(&result);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, destroy_frame, "sample_aidetect_init_result error");

    ret = ss_mpi_aidetect_process(OT_SAMPLE_AIDETECT_CHN_0, &frame, &result);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit_result, "ss_mpi_aidetect_process error: %#x!!", ret);

    (td_void)printf("==================== Frame ====================\n");
    for (td_u32 i = 0; i < result.class_num; ++i) {
        for (td_u32 j = 0; j < result.object_class[i].object_num; ++j) {
            (td_void)printf("type:%-2d id:%-2u conf:%-2f [x:%4d, y:%4d, width:%4u, height:%4u] \n",
                result.object_class[i].class_type,
                result.object_class[i].objects[j].track_id,
                result.object_class[i].objects[j].detect_confidence,
                result.object_class[i].objects[j].detect_rect.x, result.object_class[i].objects[j].detect_rect.y,
                result.object_class[i].objects[j].detect_rect.width,
                result.object_class[i].objects[j].detect_rect.height);
        }
    }

deinit_result:
    (td_void)sample_aidetect_deinit_result(&result);
destroy_frame:
    (td_void)sample_common_ai_destroy_frame(&frame);
    return ret;
}

static td_s32 sample_aidetect_sys_init()
{
    // exit before init, to make sure the success of init
    (td_void)ss_mpi_sys_exit();
    (td_void)ss_mpi_vb_exit();

    // aidetect do not use vb, so an empty config is sufficient for system init
    ot_vb_cfg vb_cfg;
    (td_void)memset_s(&vb_cfg, sizeof(vb_cfg), 0, sizeof(vb_cfg));

    td_s32 ret = ss_mpi_vb_set_cfg(&vb_cfg);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_vb_set_cfg error, please check system");
    ret = ss_mpi_vb_init();
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_vb_init error, please check system");
    ss_mpi_sys_init();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, vb_deinit, "ss_mpi_sys_init failed!");

    return TD_SUCCESS;
vb_deinit:
    (td_void)ss_mpi_vb_exit();
    return TD_FAILURE;
}

static td_s32 sample_aidetect_sys_exit()
{
    td_s32 ret = ss_mpi_sys_exit();
    sample_common_ai_log_check(ret != TD_SUCCESS, "ss_mpi_sys_exit error, please check system");
    ret = ss_mpi_vb_exit();
    sample_common_ai_log_check(ret != TD_SUCCESS, "ss_mpi_vb_exit error, please check system");
    return TD_SUCCESS;
}

int main(int argc, char *argv[])
{
    td_s32 ret = sample_aidetect_parse_param(argc, argv);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE,
        "sample_aidetect_parse_param error, please check your param!");

    if (g_show_help == TD_TRUE) {
        sample_aidetect_usage(argv);
        return TD_SUCCESS;
    }

    ret = sample_aidetect_check_param();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, show_help, "aidetect check arguments failed!");

    sample_aidetect_print_param();

    ret = sample_aidetect_sys_init();
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_vb_init failed!");

    ret = sample_aidetect_init();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, sys_exit, "sample_aidetect_init error!!");

    // show model info
    ret = sample_aidetect_show_model_info();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, aidetect_deinit, "sample_aidetect_show_model_info error !!");

    // set & get param
    ret = sample_aidetect_get_set_param();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, aidetect_deinit, "sample_aidetect_get_param_attr error !!");
    ret = sample_aidetect_get_set_attr();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, aidetect_deinit, "sample_aidetect_set_param_attr error !!");

    // process
    while(1)
    {
    	ret = sample_aidetect_process();
    	sample_common_ai_log_check_goto(ret != TD_SUCCESS, aidetect_deinit, "sample_aidetect_process error !!");
    	usleep(10000);
    	//sleep(1);
    }
aidetect_deinit:
    (td_void)sample_aidetect_deinit();
sys_exit:
    (td_void)sample_aidetect_sys_exit();
    return ret;
show_help:
    sample_aidetect_usage(argv);
    return TD_FAILURE;
}
