/*
  Copyright (c), 2024-2024, Shenshu Tech. Co., Ltd.
 */

#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include "securec.h"
#include "ss_mpi_vb.h"
#include "ss_mpi_sys_mem.h"
#include "ss_mpi_sys.h"
#include "ot_math.h"
#include "ss_mpi_aidetect.h"
#include "ss_mpi_aivsr.h"
#include "sample_common_aivsr.h"
#include "sample_common_aidetect.h"
#include "sample_common_ai.h"

#define OT_MODEL_LOAD_FROM_MEMORY       0
#define OT_MODEL_LOAD_FROM_PATH         1
#define OT_SAMPLE_AIDETECT_CHN_0        0
#define OT_SAMPLE_AIVSR_CHN_0           0
#define OT_SAMPLE_STR_LEN               512
#define MAX_FR_DATABASE_NUM             10
#define BASE_FEATURE_NAME_LEN           256
#define BASE_FEATURE_FILE_PATH_LEN      512

enum case_index {
    CASE_FACE_CAPTURE = 0,
    CASE_VEHICLE_LICENSE_PLATE_RECOGNIZE,
    CASE_FACE_BASE_DATA,
    CASE_FACE_RECOGNIZE,
    CASE_BUTT
};

typedef struct {
    td_phys_addr_t phys_addr;
    td_void *virt_addr;
} memory_struct;

typedef struct {
    td_char base_feature_file[BASE_FEATURE_FILE_PATH_LEN * 2];
} fr_base_feature_file;

typedef struct {
    td_char name[BASE_FEATURE_NAME_LEN];
    td_float feature[OT_FR_FACE_FEATURE_LEN];
} fr_base_feature;

static td_s32 g_sample_index = -1;
static td_bool g_show_help = TD_FALSE;
static td_s32 g_path_or_mem = OT_MODEL_LOAD_FROM_MEMORY;
static td_char g_fr_database_name[BASE_FEATURE_NAME_LEN] = { 0 };
static td_char g_image_path[BASE_FEATURE_FILE_PATH_LEN] = { 0 };
static ot_size g_image_size = { 0 };
static td_char g_fr_database_save_path[BASE_FEATURE_FILE_PATH_LEN] = { 0 };
static td_char g_fr_database_dir_path[BASE_FEATURE_FILE_PATH_LEN] = { 0 };
static fr_base_feature_file g_fr_database_files[MAX_FR_DATABASE_NUM] = { 0 };
static fr_base_feature g_base_feature_lib[MAX_FR_DATABASE_NUM] = { 0 };
static memory_struct g_aivsr_model_mem = { 0 };

static td_char g_aidetect_model_path[] = "../../../../../out/model/aidetect/det_hvf_hor.bin";
static td_char g_face_capture_model_path[] = "../../../../../out/model/aivsr/vsr_face_cap.bin";
static td_char g_vlpr_model_path[] = "../../../../../out/model/aivsr/vsr_vlpr.bin";
static td_char g_fr_model_path[] = "../../../../../out/model/aivsr/vsr_fr.bin";

static td_s32 sample_aivsr_init_input_model(td_char *modelPath, ot_aivsr_input_model *input_model)
{
    if (g_path_or_mem == OT_MODEL_LOAD_FROM_PATH) {
        input_model->model_load_mode = OT_AIVSR_MODEL_LOAD_FROM_PATH;
        input_model->model = (td_void *)modelPath;
        input_model->size = (td_u32)strlen(modelPath);
    } else {
        input_model->model_load_mode = OT_AIVSR_MODEL_LOAD_FROM_MEMORY;
        td_u32 file_size = 0;
        td_s32 ret = sample_common_ai_get_file_size(modelPath, &file_size);
        sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "read model[%s] data error.", modelPath);

        ret = ss_mpi_sys_mmz_alloc(&g_aivsr_model_mem.phys_addr, &g_aivsr_model_mem.virt_addr,
            TD_NULL, TD_NULL, file_size);
        sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_sys_mmz_alloc.");

        ret = sample_common_ai_read_file(modelPath, file_size, g_aivsr_model_mem.virt_addr);
        sample_common_ai_log_check_goto(ret != TD_SUCCESS, free_model, "sample_aivsr_read_file failed");

        input_model->model = g_aivsr_model_mem.virt_addr;
        input_model->size = file_size;
    }

    return TD_SUCCESS;
free_model:
    (td_void)ss_mpi_sys_mmz_free(g_aivsr_model_mem.phys_addr, g_aivsr_model_mem.virt_addr);
    return TD_FAILURE;
}

static td_s32 sample_aivsr_deinit_input_model()
{
    if (g_aivsr_model_mem.phys_addr != 0 || g_aivsr_model_mem.virt_addr != TD_NULL) {
        (td_void)ss_mpi_sys_mmz_free(g_aivsr_model_mem.phys_addr, g_aivsr_model_mem.virt_addr);
        g_aivsr_model_mem.phys_addr = 0;
        g_aivsr_model_mem.virt_addr = TD_NULL;
    }
    return TD_SUCCESS;
}

static td_s32 sample_aivsr_facecap_init(td_char *model_path)
{
    // create input model
    ot_aivsr_input_model facecap_input_model;
    (td_void)memset_s(&facecap_input_model, sizeof(ot_aidetect_input_model), 0, sizeof(ot_aidetect_input_model));
    td_s32 ret = sample_aivsr_init_input_model(model_path, &facecap_input_model);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "sample init facecap model error!!");

    // create chn_attr
    ot_aivsr_chn_attr face_cap_chn_attr;
    (td_void)memset_s(&face_cap_chn_attr, sizeof(ot_aivsr_chn_attr), 0, sizeof(ot_aivsr_chn_attr));
    face_cap_chn_attr.task_type = OT_AIVSR_TASK_FACE_CAPTURE;
    face_cap_chn_attr.face_attr.face_quality_en = TD_TRUE;
    face_cap_chn_attr.face_attr.face_pose_en = TD_TRUE;
    face_cap_chn_attr.face_attr.face_mask_en = TD_TRUE;

    // create channel
    ret = ss_mpi_aivsr_create_chn(OT_SAMPLE_AIVSR_CHN_0, &facecap_input_model, &face_cap_chn_attr);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit, "ss_mpi_aivsr_create_chn error: %#x!!", ret);

deinit:
    (td_void)sample_aivsr_deinit_input_model();
    return ret;
}

static td_s32 sample_aivsr_facecap_get_set_param()
{
    ot_aivsr_chn_param get_chn_param;
    (td_void)memset_s(&get_chn_param, sizeof(ot_aivsr_chn_param), 0, sizeof(ot_aivsr_chn_param));

    td_s32 ret = ss_mpi_aivsr_get_chn_param(OT_SAMPLE_AIVSR_CHN_0, &get_chn_param);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_aivsr_get_chn_param error: %#x!!", ret);

    (td_void)printf("==================== Default Chn Param ====================\n");
    (td_void)printf("task_type is %d\n", get_chn_param.task_type);
    (td_void)printf("preemp_en: %d, priority: %u, priority_up_step_timeout: %u, priority_up_top_timeout: %u\n",
        get_chn_param.model_priority.preemp_en, get_chn_param.model_priority.priority,
        get_chn_param.model_priority.priority_up_step_timeout, get_chn_param.model_priority.priority_up_top_timeout);
    (td_void)printf("face_param min_face_quality: %u, max_face_pose.roll: %d, max_face_pose.yaw: %d, "
                    "max_face_pose.pitch: %d, min_pupil_pixel_dist is %u\n",
        get_chn_param.face_param.min_face_quality, get_chn_param.face_param.max_face_pose.roll,
        get_chn_param.face_param.max_face_pose.yaw, get_chn_param.face_param.max_face_pose.pitch,
        get_chn_param.face_param.min_pupil_pixel_dist);

    ot_aivsr_chn_param set_chn_param;
    (td_void)memset_s(&set_chn_param, sizeof(ot_aivsr_chn_param), 0, sizeof(ot_aivsr_chn_param));
    set_chn_param.task_type = get_chn_param.task_type;
    set_chn_param.model_priority.preemp_en = get_chn_param.model_priority.preemp_en;
    set_chn_param.model_priority.priority = get_chn_param.model_priority.priority;
    set_chn_param.model_priority.priority_up_top_timeout = get_chn_param.model_priority.priority_up_top_timeout;
    set_chn_param.model_priority.priority_up_step_timeout = get_chn_param.model_priority.priority_up_step_timeout;
    set_chn_param.face_param.min_face_quality = get_chn_param.face_param.min_face_quality;
    set_chn_param.face_param.min_pupil_pixel_dist = get_chn_param.face_param.min_pupil_pixel_dist;
    set_chn_param.face_param.max_face_pose = get_chn_param.face_param.max_face_pose;
    ret = ss_mpi_aivsr_set_chn_param(OT_SAMPLE_AIVSR_CHN_0, &set_chn_param);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_aivsr_set_chn_param error: %#x!!", ret);

    return TD_SUCCESS;
}

static td_s32 sample_aivsr_process_face_capture(ot_video_frame *frame, ot_aidetect_result_array *result)
{
    ot_aivsr_result_array faceCapResult = { 0 };
    (td_void)printf("==================== Face Capture ====================\n");
    for (td_u32 i = 0; i < (*result).class_num; ++i) {
        if ((*result).object_class[i].class_type != OT_AIDETECT_CLASS_FACE) {
            continue;
        }

        if ((*result).object_class[i].object_num == 0) {
            (td_void)printf("WARNING:the detect result is 0!\n");
            continue;
        }
        td_s32 ret = ss_mpi_aivsr_process(OT_SAMPLE_AIVSR_CHN_0, frame, &(*result).object_class[i],
            &faceCapResult);
        sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_aivsr_process error: %#x!!", ret);
        (td_void)printf("ss_mpi_aivsr_process the task_type is %d, the face capture num is %u\n",
            faceCapResult.task_type, faceCapResult.face_result.object_num);
        for (td_u32 j = 0; j < faceCapResult.face_result.object_num; ++j) {
            (td_void)printf("face capture id:%-2u [x:%4d, y:%4d, width:%4u, height:%4u], [quality is %u, "
                            "face_mask is %d, face_pose.roll is %d, face_pose.yaw is %d, face_pose.pitch is %d]\n",
                faceCapResult.face_result.face_quality_result[j].detect_object.track_id,
                faceCapResult.face_result.face_quality_result[j].detect_object.detect_rect.x,
                faceCapResult.face_result.face_quality_result[j].detect_object.detect_rect.y,
                faceCapResult.face_result.face_quality_result[j].detect_object.detect_rect.width,
                faceCapResult.face_result.face_quality_result[j].detect_object.detect_rect.height,
                faceCapResult.face_result.face_quality_result[j].quality,
                faceCapResult.face_result.face_quality_result[j].mask,
                faceCapResult.face_result.face_quality_result[j].pose.roll,
                faceCapResult.face_result.face_quality_result[j].pose.yaw,
                faceCapResult.face_result.face_quality_result[j].pose.pitch);
        }
    }
    return TD_SUCCESS;
}

static td_s32 sample_aivsr_facecap_process()
{
    // prepare frame
    ot_video_frame frame;
    td_s32 ret = sample_common_ai_create_nv21_frame(&g_image_size, &frame);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "sample_common_ai_create_nv21_frame fail!!");

    ret = sample_common_ai_read_nv21_frame(g_image_path, &frame);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, destroy_frame,
        "sample_common_ai_read_nv21_frame error,yuv file is %s", g_image_path);

    // result
    ot_aidetect_result_array result;
    ret = sample_common_aidetect_init_result(OT_SAMPLE_AIDETECT_CHN_0, &result);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, destroy_frame, "sample_common_aidetect_init_result error: %#x!!",
        ret);

    ret = sample_common_aidetect_process(OT_SAMPLE_AIDETECT_CHN_0, &frame, &result);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit_result, "sample_common_aidetect_process error: %#x!!",
        ret);

    // face capture process
    ret = sample_aivsr_process_face_capture(&frame, &result);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit_result, "sample_process_face_capture error: %#x!!", ret);

deinit_result:
    (td_void)sample_common_aidetect_deinit_result(&result);
destroy_frame:
    (td_void)sample_common_ai_destroy_frame(&frame);
    return ret;
}

static td_s32 sample_aivsr_deinit()
{
    td_s32 ret = ss_mpi_aivsr_destroy_chn(OT_SAMPLE_AIVSR_CHN_0);
    sample_common_ai_log_check(ret != TD_SUCCESS, "ss_mpi_aivsr_destroy_chn failed!");
    return ret;
}

static td_s32 sample_aivsr_run_face_capture()
{
    td_s32 ret = sample_common_aidetect_create(OT_SAMPLE_AIDETECT_CHN_0, g_aidetect_model_path);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "sample_common_aidetect_create failed");

    ret = sample_aivsr_facecap_init(g_face_capture_model_path);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit_aidetect, "sample_aivsr_facecap_init failed");

    // set & get facecapture param
    ret = sample_aivsr_facecap_get_set_param();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit_facecap, "sample_aivsr_facecap_get_set_param error !!");

    // process
    ret = sample_aivsr_facecap_process();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit_facecap, "sample_aivsr_facecap_process error !!");

    ret = sample_aivsr_deinit();
    sample_common_ai_log_check(ret != TD_SUCCESS, "sample_aivsr_facecap_deinit error: %#x!!", ret);

    ret = sample_common_aidetect_destroy(OT_SAMPLE_AIDETECT_CHN_0);
    sample_common_ai_log_check(ret != TD_SUCCESS, "sample_common_aidetect_destroy error: %#x!!", ret);

    return TD_SUCCESS;

deinit_facecap:
    (td_void)sample_aivsr_deinit();
deinit_aidetect:
    (td_void)sample_common_aidetect_destroy(OT_SAMPLE_AIDETECT_CHN_0);
    return TD_FAILURE;
}

static td_s32 sample_aivsr_vlpr_init(td_char *model_path)
{
    // create input model
    ot_aivsr_input_model vlpr_model;
    (td_void)memset_s(&vlpr_model, sizeof(ot_aidetect_input_model), 0, sizeof(ot_aidetect_input_model));
    td_s32 ret = sample_aivsr_init_input_model(model_path, &vlpr_model);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "sample_aivsr_init_input_model error!!");

    // create chn_attr
    ot_aivsr_chn_attr vlpr_chn_attr;
    (td_void)memset_s(&vlpr_chn_attr, sizeof(ot_aivsr_chn_attr), 0, sizeof(ot_aivsr_chn_attr));
    vlpr_chn_attr.task_type = OT_AIVSR_TASK_VLPR;
    vlpr_chn_attr.vlpr_attr.vehicle_property_en = TD_TRUE;
    vlpr_chn_attr.vlpr_attr.license_plate_detect_en = TD_TRUE;
    vlpr_chn_attr.vlpr_attr.license_plate_property_en = TD_TRUE;
    vlpr_chn_attr.vlpr_attr.license_plate_recognize_en = TD_TRUE;

    // create channel
    ret = ss_mpi_aivsr_create_chn(OT_SAMPLE_AIVSR_CHN_0, &vlpr_model, &vlpr_chn_attr);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit, "ss_mpi_aivsr_create_chn error: %#x!!", ret);

deinit:
    (td_void)sample_aivsr_deinit_input_model();
    return ret;
}

static td_s32 sample_aivsr_vlpr_get_set_param()
{
    ot_aivsr_chn_param get_param;
    (td_void)memset_s(&get_param, sizeof(ot_aivsr_chn_param), 0, sizeof(ot_aivsr_chn_param));

    td_s32 ret = ss_mpi_aivsr_get_chn_param(OT_SAMPLE_AIVSR_CHN_0, &get_param);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_aivsr_get_chn_param error: %#x!!", ret);

    (td_void)printf("==================== Default Chn Param ====================\n");
    (td_void)printf("task_type is %d\n", get_param.task_type);
    (td_void)printf("model_priority\n"
                    "  preemp_en: %d\n"
                    "  priority: %u\n"
                    "  priority_up_step_timeout: %u\n"
                    "  priority_up_top_timeout: %u\n",
        get_param.model_priority.preemp_en, get_param.model_priority.priority,
        get_param.model_priority.priority_up_step_timeout, get_param.model_priority.priority_up_top_timeout);
    (td_void)printf("vlp_param \n"
                    "  vehicle_min_width: %u\n"
                    "  license_plate_min_width: %u\n"
                    "  license_plate_detect_threshold: %f\n"
                    "  license_plate_recognize_threshold: %f\n",
        get_param.vlpr_param.vehicle_min_width, get_param.vlpr_param.license_plate_min_width,
        get_param.vlpr_param.license_plate_detect_threshold,
        get_param.vlpr_param.license_plate_recognize_threshold);

    ot_aivsr_chn_param set_param;
    (td_void)memset_s(&set_param, sizeof(ot_aivsr_chn_param), 0, sizeof(ot_aivsr_chn_param));
    set_param.task_type = get_param.task_type;
    set_param.model_priority.preemp_en = get_param.model_priority.preemp_en;
    set_param.model_priority.priority = get_param.model_priority.priority;
    set_param.model_priority.priority_up_top_timeout = get_param.model_priority.priority_up_top_timeout;
    set_param.model_priority.priority_up_step_timeout = get_param.model_priority.priority_up_step_timeout;
    set_param.vlpr_param.vehicle_min_width = get_param.vlpr_param.vehicle_min_width;
    set_param.vlpr_param.license_plate_min_width = get_param.vlpr_param.license_plate_min_width;
    set_param.vlpr_param.license_plate_detect_threshold = get_param.vlpr_param.license_plate_detect_threshold;
    set_param.vlpr_param.license_plate_recognize_threshold = get_param.vlpr_param.license_plate_recognize_threshold;
    ret = ss_mpi_aivsr_set_chn_param(OT_SAMPLE_AIVSR_CHN_0, &get_param);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_aivsr_set_chn_param error: %#x!!", ret);

    return TD_SUCCESS;
}

static td_s32 sample_aivsr_process_vlpr(ot_video_frame *frame, ot_aidetect_result_array *result)
{
    ot_aivsr_result_array plateRecResult;
    (td_void)printf("==================== VLPR ====================\n");
    for (td_u32 i = 0; i < (*result).class_num; ++i) {
        if ((*result).object_class[i].class_type != OT_AIDETECT_CLASS_VEHICLE) {
            continue;
        }

        if ((*result).object_class[i].object_num == 0) {
            (td_void)printf("WARNING:the detect result is 0!\n");
            continue;
        }
        td_s32 ret = ss_mpi_aivsr_process(OT_SAMPLE_AIVSR_CHN_0, frame, &(*result).object_class[i], &plateRecResult);
        sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_aivsr_process error: %#x!!", ret);
        (td_void)printf("ss_mpi_aivsr_process the task_type is %d, the plate recognized num is %u\n",
            plateRecResult.task_type, plateRecResult.vlpr_result.object_num);
        for (td_u32 j = 0; j < plateRecResult.vlpr_result.object_num; ++j) {
            sample_common_aivsr_print_vehicle_info(plateRecResult.vlpr_result.result_info[j].vehicle_info);
            for (td_u32 k = 0; k < plateRecResult.vlpr_result.result_info[j].license_plate_num; ++k) {
                sample_common_aivsr_print_plate_info(plateRecResult.vlpr_result.result_info[j].license_plate_info[k]);
            }
        }
    }
    return TD_SUCCESS;
}

static td_s32 sample_aivsr_vlpr_process()
{
    // prepare frame
    ot_video_frame frame;
    td_s32 ret = sample_common_ai_create_nv21_frame(&g_image_size, &frame);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "sample_common_ai_create_nv21_frame fail!!");

    ret = sample_common_ai_read_nv21_frame(g_image_path, &frame);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, destroy_frame,
        "sample_common_ai_read_nv21_frame error,yuv file is %s", g_image_path);

    // result
    ot_aidetect_result_array result;
    ret = sample_common_aidetect_init_result(OT_SAMPLE_AIDETECT_CHN_0, &result);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, destroy_frame, "sample_common_aidetect_init_result error: %#x!!",
        ret);

    ret = sample_common_aidetect_process(OT_SAMPLE_AIDETECT_CHN_0, &frame, &result);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit_result, "sample_common_aidetect_process error: %#x!!",
        ret);

    // vlpr process
    ret = sample_aivsr_process_vlpr(&frame, &result);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit_result, "sample_aivsr_process_vlpr error: %#x!!", ret);

deinit_result:
    (td_void)sample_common_aidetect_deinit_result(&result);
destroy_frame:
    (td_void)sample_common_ai_destroy_frame(&frame);
    return ret;
}

static td_s32 sample_aivsr_run_vlpr()
{
    td_s32 ret = sample_common_aidetect_create(OT_SAMPLE_AIDETECT_CHN_0, g_aidetect_model_path);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "sample_common_aidetect_create failed");

    ret = sample_aivsr_vlpr_init(g_vlpr_model_path);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, DEINIT_AIDETECT, "sample_aivsr_vlpr_init failed");

    // set & get vlpr param
    ret = sample_aivsr_vlpr_get_set_param();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, DEINIT_VLPR, "sample_aivsr_vlpr_get_set_param error !!");

    // process
    ret = sample_aivsr_vlpr_process();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, DEINIT_VLPR, "sample_aivsr_vlpr_process error !!");

    ret = sample_aivsr_deinit();
    sample_common_ai_log_check(ret != TD_SUCCESS, "sample_aivsr_deinit error: %#x!!", ret);

    ret = sample_common_aidetect_destroy(OT_SAMPLE_AIDETECT_CHN_0);
    sample_common_ai_log_check(ret != TD_SUCCESS, "sample_common_aidetect_destroy error: %#x!!", ret);

    return TD_SUCCESS;

DEINIT_VLPR:
    (td_void)sample_aivsr_deinit();
DEINIT_AIDETECT:
    (td_void)sample_common_aidetect_destroy(OT_SAMPLE_AIDETECT_CHN_0);
    return TD_FAILURE;
}

static td_s32 sample_aivsr_fr_init(td_char *model_path)
{
    // create input model
    ot_aivsr_input_model fr_model;
    (td_void)memset_s(&fr_model, sizeof(ot_aivsr_input_model), 0, sizeof(ot_aivsr_input_model));
    td_s32 ret = sample_aivsr_init_input_model(model_path, &fr_model);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "sample_aivsr_init_input_model error!!");

    // create chn_attr
    ot_aivsr_chn_attr fr_chn_attr;
    (td_void)memset_s(&fr_chn_attr, sizeof(ot_aivsr_chn_attr), 0, sizeof(ot_aivsr_chn_attr));
    fr_chn_attr.task_type = OT_AIVSR_TASK_FR;

    // create channel
    ret = ss_mpi_aivsr_create_chn(OT_SAMPLE_AIVSR_CHN_0, &fr_model, &fr_chn_attr);
    sample_common_ai_log_check(ret != TD_SUCCESS, "ss_mpi_aivsr_create_chn error: %#x!!", ret);

    (td_void)sample_aivsr_deinit_input_model();
    return ret;
}

static td_s32 sample_aivsr_read_feature(FILE *feature_file, td_float *feature, td_s32 feature_size, td_char *name)
{
    td_s32 index = 0;
    td_char line[BASE_FEATURE_NAME_LEN];
    td_s32 ret;
    while (fgets(line, sizeof(line), feature_file) != TD_NULL) {
        if (index > feature_size) {
            return TD_FAILURE;
        }
        if (index == 0) {
            ret = strncpy_s(name, BASE_FEATURE_NAME_LEN, line, BASE_FEATURE_NAME_LEN - 1);
            sample_common_ai_log_check_return(ret == TD_FAILURE, TD_FAILURE, "strncpy_s base name failed!!");
        } else {
            feature[index - 1] = strtof(line, NULL);
        }
        index++;
    }
    return TD_SUCCESS;
}

static td_float sample_aivsr_get_similarity(const td_float *detect_feature, td_s32 detect_feature_size,
    const td_float *base_feature, td_s32 base_feature_size)
{
    td_float similarity = 0.0f;
    if (detect_feature_size != base_feature_size) {
        return similarity;
    }
    for (td_s32 i = 0; i < detect_feature_size; i++) {
        similarity += detect_feature[i] * base_feature[i];
    }
    return similarity;
}

static td_s32 split_path(const char *full_path, char *folder, char *filename)
{
    sample_common_ai_log_check_return((full_path == TD_NULL || folder == TD_NULL || filename == TD_NULL), TD_FAILURE,
        "Invalid input parameters.");
    const td_s32 max_path_len = 1024;
    const char *last_separator = strrchr(full_path, '/');
    if (last_separator != NULL) {
        size_t folder_len = last_separator - full_path;
        sample_common_ai_log_check_return(strncpy_s(folder, max_path_len, full_path, folder_len) != EOK, TD_FAILURE,
            "strncpy_s failed!");
        folder[folder_len] = '\0';
        sample_common_ai_log_check_return(strcpy_s(filename, max_path_len, last_separator + 1) != EOK, TD_FAILURE,
            "strcpy_s failed!");
    } else {
        sample_common_ai_log_check_return(strcpy_s(folder, max_path_len, ".") != EOK, TD_FAILURE, "strcpy_s failed!");
        sample_common_ai_log_check_return(strcpy_s(filename, max_path_len, full_path) != EOK, TD_FAILURE,
            "strcpy_s failed!");
    }
    return TD_SUCCESS;
}

static td_s32 get_database_save_realpath(char *real_path, int path_len)
{
    td_char database_save_dir[BASE_FEATURE_NAME_LEN] = { 0 };
    td_char database_save_file[BASE_FEATURE_NAME_LEN] = { 0 };
    sample_common_ai_log_check_return(
        split_path(g_fr_database_save_path, database_save_dir, database_save_file) != TD_SUCCESS, TD_FAILURE,
        "split_path failed!");
    td_char real_dir_path[PATH_MAX] = { 0 };
    sample_common_ai_log_check_return(realpath(database_save_dir, real_dir_path) == TD_NULL, TD_FAILURE,
        "%s database save path convert real path failed !!", g_fr_database_save_path);
    sample_common_ai_log_check_return(
        snprintf_s(real_path, path_len, path_len - 1, "%s/%s", real_dir_path, database_save_file) == TD_FAILURE,
        TD_FAILURE, "snprintf_s save path failed!!");
    return TD_SUCCESS;
}

static td_s32 sample_aivsr_fr_process_save_database(ot_aivsr_result_array *frRecResult)
{
    (td_void)printf("==================== save face database====================\n");
    sample_common_ai_log_check_return(frRecResult->fr_result.object_num != 1, TD_FAILURE,
        "only one face is supported when save fr database!!");
    td_char real_path[PATH_MAX] = { 0 };
    sample_common_ai_log_check_return(get_database_save_realpath(real_path, PATH_MAX) != TD_SUCCESS,
        TD_FAILURE, "get_database_save_realpath failed!");
    FILE *file = fopen(real_path, "w");
    sample_common_ai_log_check_return(file == NULL, TD_FAILURE, "Unable to open input fr database save path!! %s",
        real_path);
    td_s32 write_len = fprintf(file, "%s\n", g_fr_database_name);
    td_s32 ret = (write_len < 0) ? TD_FAILURE : TD_SUCCESS;
    sample_common_ai_log_check_goto(ret == TD_FAILURE, CLOSE_FILE, "fprintf Error! data base name is %s",
        g_fr_database_name);
    for (td_s32 i = 0; i < OT_FR_FACE_FEATURE_LEN; ++i) {
        write_len = fprintf(file, "%f\n", frRecResult->fr_result.fr_info_result[0].face_feature.feature[i]);
        ret = (write_len < 0) ? TD_FAILURE : TD_SUCCESS;
        sample_common_ai_log_check_goto(ret == TD_FAILURE, CLOSE_FILE, "fprintf Error! feature is %f",
            frRecResult->fr_result.fr_info_result[0].face_feature.feature[i]);
    }
    printf("save the fr database name(%s) and feature into file(%s) success!\n", g_fr_database_name, real_path);
CLOSE_FILE:
    (td_void)fclose(file);
    file = TD_NULL;
    return ret;
}

static td_s32 sample_aivsr_process_print_match_result(ot_aivsr_result_array *frRecResult,
    fr_base_feature *base_feature_lib, td_s32 file_count)
{
    td_s32 ret;
    for (td_u32 i = 0; i < frRecResult->fr_result.object_num; ++i) {
        td_float score = 0.0f;
        td_char match_name[BASE_FEATURE_NAME_LEN] = { 0 };
        for (td_s32 j = 0; j < file_count; ++j) {
            td_float similarity = sample_aivsr_get_similarity(
                frRecResult->fr_result.fr_info_result[i].face_feature.feature,
                OT_FR_FACE_FEATURE_LEN, base_feature_lib[j].feature, OT_FR_FACE_FEATURE_LEN);
            (td_void)printf("basedata name is %s, similarity is %f\n", base_feature_lib[j].name, similarity);
            if (similarity > score && similarity >= 0.3) { // 0.3 : the threshold of similarity
                ret = strcpy_s(match_name, BASE_FEATURE_NAME_LEN - 1, base_feature_lib[j].name);
                sample_common_ai_log_check_return(ret == TD_FAILURE, TD_FAILURE, "strcpy_s match name failed!!");
                score = similarity;
            }
        }
        (td_void)printf("FR match name is: %s, [x:%4d, y:%4d, width:%4u, height:%4u]\n",
            match_name,
            frRecResult->fr_result.fr_info_result[i].detect_object.detect_rect.x,
            frRecResult->fr_result.fr_info_result[i].detect_object.detect_rect.y,
            frRecResult->fr_result.fr_info_result[i].detect_object.detect_rect.width,
            frRecResult->fr_result.fr_info_result[i].detect_object.detect_rect.height);
    }
    return TD_SUCCESS;
}

static td_s32 sample_aivsr_process_match_base_feature(ot_aivsr_result_array *frRecResult)
{
    (td_void)printf("==================== start match fr database similarity ====================\n");
    td_char real_path[PATH_MAX] = { 0 };
    sample_common_ai_log_check_return(realpath(g_fr_database_dir_path, real_path) == TD_NULL, TD_FAILURE,
        "%s database dir path convert real path failed !!", g_fr_database_dir_path);
    DIR *dir = opendir(real_path);
    sample_common_ai_log_check_return(dir == NULL, TD_FAILURE, "Unable to open input fr database dir path %s!!",
        real_path);
    struct dirent *entry;
    td_s32 ret;
    td_s32 file_count = 0;
    (td_void)memset_s(g_fr_database_files, sizeof(fr_base_feature_file) * MAX_FR_DATABASE_NUM, 0,
        sizeof(fr_base_feature_file) * MAX_FR_DATABASE_NUM);
    while ((entry = readdir(dir)) != NULL && file_count < MAX_FR_DATABASE_NUM) {
        if (strstr(entry->d_name, ".txt") != NULL) {
            ret = snprintf_s(g_fr_database_files[file_count].base_feature_file,
                sizeof(g_fr_database_files[file_count].base_feature_file) - 1,
                sizeof(g_fr_database_files[file_count].base_feature_file) - 1, "%s/%s", real_path, entry->d_name);
            sample_common_ai_log_check_goto(ret == TD_FAILURE, CLOSE_DIR, "snprintf_s feature file path failed!!");
            file_count++;
        }
    }
    (void)memset_s(g_base_feature_lib, sizeof(fr_base_feature) * MAX_FR_DATABASE_NUM, 0,
        sizeof(fr_base_feature) * MAX_FR_DATABASE_NUM);
    for (td_s32 i = 0; i < file_count; ++i) {
        FILE *feature_library = fopen(g_fr_database_files[i].base_feature_file, "rb");
        if (feature_library == TD_NULL) {
            (td_void)printf("base_feature_file file:%s read fail!!\n", g_fr_database_files[i].base_feature_file);
            continue;
        }
        ret = sample_aivsr_read_feature(feature_library, g_base_feature_lib[i].feature, OT_FR_FACE_FEATURE_LEN,
            g_base_feature_lib[i].name);
        (td_void)fclose(feature_library);
        sample_common_ai_log_check_goto(ret != TD_SUCCESS, CLOSE_DIR, "sample_aivsr_read_feature error!!");
    }
    ret = sample_aivsr_process_print_match_result(frRecResult, g_base_feature_lib, file_count);
    sample_common_ai_log_check(ret != TD_SUCCESS, "sample_aivsr_process_print_match_result failed!");
CLOSE_DIR:
    closedir(dir);
    return ret;
}

static td_s32 sample_aivsr_process_fr(ot_video_frame *frame, ot_aidetect_result_array *result,
    ot_aivsr_result_array *frRecResult)
{
    (td_void)printf("==================== FR ====================\n");
    for (td_u32 i = 0; i < (*result).class_num; ++i) {
        if ((*result).object_class[i].class_type != OT_AIDETECT_CLASS_FACE) {
            continue;
        }

        if ((*result).object_class[i].object_num == 0) {
            (td_void)printf("WARNING:the detect result is 0!\n");
            continue;
        }
        td_s32 ret = ss_mpi_aivsr_process(OT_SAMPLE_AIVSR_CHN_0, frame, &(*result).object_class[i],
            frRecResult);
        sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_aivsr_process error: %#x!!", ret);
        (td_void)printf("ss_mpi_aivsr_process the task_type is %d, the face recognized num is %u\n",
            frRecResult->task_type, frRecResult->fr_result.object_num);
        for (td_u32 j = 0; j < frRecResult->fr_result.object_num; ++j) {
            (td_void)printf("FR [x:%4d, y:%4d, width:%4u, height:%4u], feture is:\n",
                frRecResult->fr_result.fr_info_result[j].detect_object.detect_rect.x,
                frRecResult->fr_result.fr_info_result[j].detect_object.detect_rect.y,
                frRecResult->fr_result.fr_info_result[j].detect_object.detect_rect.width,
                frRecResult->fr_result.fr_info_result[j].detect_object.detect_rect.height);
            for (int k = 0; k < OT_FR_FACE_FEATURE_LEN; ++k) {
                (td_void)printf("%f ", frRecResult->fr_result.fr_info_result[j].face_feature.feature[k]);
            }
            (td_void)printf("\n");
        }
    }
    return TD_SUCCESS;
}

static td_s32 sample_aivsr_fr_process()
{
    // prepare frame
    ot_video_frame frame;
    td_s32 ret = sample_common_ai_create_nv21_frame(&g_image_size, &frame);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "sample_common_ai_create_nv21_frame fail!!");

    ret = sample_common_ai_read_nv21_frame(g_image_path, &frame);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, destroy_frame,
        "sample_common_ai_read_nv21_frame error,yuv file is %s", g_image_path);

    // result
    ot_aidetect_result_array result;
    ret = sample_common_aidetect_init_result(OT_SAMPLE_AIDETECT_CHN_0, &result);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, destroy_frame, "sample_common_aidetect_init_result error: %#x!!",
        ret);

    ret = sample_common_aidetect_process(OT_SAMPLE_AIDETECT_CHN_0, &frame, &result);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit_result, "sample_common_aidetect_process error: %#x!!",
        ret);

    // fr process
    ot_aivsr_result_array frRecResult = { 0 };
    frRecResult.fr_result.object_capacity = OT_FR_MAX_OUTPUT_NUM;
    frRecResult.fr_result.fr_info_result = (ot_fr_result *)malloc(
        sizeof(ot_fr_result) * frRecResult.fr_result.object_capacity);
    sample_common_ai_log_check_goto(frRecResult.fr_result.fr_info_result == NULL, deinit_result, "malloc failed!!");
    ret = sample_aivsr_process_fr(&frame, &result, &frRecResult);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit_fr_result, "sample_aivsr_process_fr error: %#x!!", ret);

    switch (g_sample_index) {
        case CASE_FACE_BASE_DATA:
            ret = sample_aivsr_fr_process_save_database(&frRecResult);
            sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit_fr_result,
                "sample_aivsr_fr_process_save_database error: %#x!!", ret);
            break;
        case CASE_FACE_RECOGNIZE:
            ret = sample_aivsr_process_match_base_feature(&frRecResult);
            sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit_fr_result,
                "sample_aivsr_process_match_base_feature error: %#x!!", ret);
            break;
        default:
            (td_void)printf("case index is invalid!\n");
            break;
    }

deinit_fr_result:
    free(frRecResult.fr_result.fr_info_result);
deinit_result:
    (td_void)sample_common_aidetect_deinit_result(&result);
destroy_frame:
    (td_void)sample_common_ai_destroy_frame(&frame);
    return ret;
}

static td_s32 sample_aivsr_run_fr()
{
    td_s32 ret = sample_common_aidetect_create(OT_SAMPLE_AIDETECT_CHN_0, g_aidetect_model_path);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "sample_common_aidetect_create failed");

    ret = sample_aivsr_fr_init(g_fr_model_path);
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, deinit_aidetect, "sample_aivsr_fr_init failed");

    // process
    ret = sample_aivsr_fr_process();
    sample_common_ai_log_check(ret != TD_SUCCESS, "sample_aivsr_fr_process error !!");

    ret = sample_aivsr_deinit();
    sample_common_ai_log_check(ret != TD_SUCCESS, "sample_aivsr_deinit error: %#x!!", ret);

deinit_aidetect:
    (td_void)sample_common_aidetect_destroy(OT_SAMPLE_AIDETECT_CHN_0);
    return ret;
}

static td_s32 sample_get_image_size(td_char *string)
{
    sample_common_ai_log_check_return(strlen(string) == 0, TD_FAILURE, "Image size param is an empty string");

    td_u32 base = 10;
    td_char *endptr;
    td_char *width = strtok(string, "x");
    sample_common_ai_log_check_return(width == NULL, TD_FAILURE, "input size format is not correct");
    g_image_size.width = strtoul(width, &endptr, base);
    sample_common_ai_log_check_return((endptr == string || *endptr != '\0'), TD_FAILURE, "width is not a number");

    td_char *height = strtok(NULL, "x");
    sample_common_ai_log_check_return(height == NULL, TD_FAILURE, "input size format is not correct");
    g_image_size.height = strtoul(height, &endptr, base);
    sample_common_ai_log_check_return((endptr == string || *endptr != '\0'), TD_FAILURE, "height is not a number");

    return TD_SUCCESS;
}

static td_void sample_aivsr_usage(td_char **argv)
{
    (td_void)printf("usage: %s [c:m:f:i:vhn:p:o:d:]\n", argv[0]);
    (td_void)printf("\t  -c:   sample case\n");
    (td_void)printf("\t\t  0) face_capture\n");
    (td_void)printf("\t\t  1) vehicle_plate_recognize\n");
    (td_void)printf("\t\t  2) face_data_base_create\n");
    (td_void)printf("\t\t  3) face_recognize\n");
    (td_void)printf("\t  -h:   show this info\n");
    (td_void)printf("\t  -p:   input nv21 image file path\n");
    (td_void)printf("\t  -s:   input size of image, width x height\n");
    (td_void)printf("\t  -v:   input model file or memory. Reading from memory by default. Add to read from path\n");
    (td_void)printf("\t  -n:   input fr database name.\n");
    (td_void)printf("\t  -o:   input save the fr database file name\n");
    (td_void)printf("\t  -d:   input databases dir path\n");
    (td_void)printf("example:\n");
    (td_void)printf("\t  %s -h\n", argv[0]);
    (td_void)printf("\t  %s -c 0 -p /xxx/xxx/xxx.yuv -s 1920x1080\n", argv[0]);
    (td_void)printf("\t  %s -c 1 -p /xxx/xxxx/xxx.yuv -s 1920x1080 -v\n", argv[0]);
    (td_void)printf("\t  %s -c 2 -n xxx -p /xxxx/xxxx/xxx.yuv -s 1920x1080 -o /xxxx/xxxx/xxx.txt \n", argv[0]);
    (td_void)printf("\t  %s -c 3 -p /xxxx/xxxx/xxx.yuv -s 1920x1080 -d /xxxx/xxxx/database \n", argv[0]);
}

static td_s32 sample_aivsr_parse_argv(td_s32 argc, td_char *argv[])
{
    td_s32 c;
    td_char *endptr = NULL;
    td_s32 base = 10;
    td_char tmp_str[OT_SAMPLE_STR_LEN] = { 0 };
    while ((c = getopt(argc, argv, "c:vhn:p:s:o:d:")) != EOF) {
        switch (c) {
            case 'c':
                g_sample_index = strtol(optarg, &endptr, base);
                break;
            case 'h':
                g_show_help = TD_TRUE;
                return TD_SUCCESS;
            case 'v':
                g_path_or_mem = OT_MODEL_LOAD_FROM_PATH;
                break;
            case 'n':
                sample_common_ai_log_check_return(
                    strncpy_s(g_fr_database_name, sizeof(g_fr_database_name) - 1, optarg, strlen(optarg)) != EOK,
                    TD_FAILURE, "strncpy_s face database_name failed!");
                break;
            case 'p':
                sample_common_ai_log_check_return(
                    strncpy_s(g_image_path, BASE_FEATURE_FILE_PATH_LEN - 1, optarg, strlen(optarg)) != EOK, TD_FAILURE,
                    "strncpy_s failed!");
                break;
            case 's':
                sample_common_ai_log_check_return(
                    strncpy_s(tmp_str, OT_SAMPLE_STR_LEN - 1, optarg, strlen(optarg)) != EOK, TD_FAILURE,
                    "strncpy_s face image size failed!");
                sample_common_ai_log_check_return(sample_get_image_size(tmp_str) != TD_SUCCESS, TD_FAILURE,
                    "sample_get_size failed");
                break;
            case 'o':
                sample_common_ai_log_check_return(
                    strncpy_s(g_fr_database_save_path, BASE_FEATURE_FILE_PATH_LEN - 1, optarg, strlen(optarg)) != EOK,
                    TD_FAILURE, "strncpy_s database feature file path failed!");
                break;
            case 'd':
                sample_common_ai_log_check_return(
                    strncpy_s(g_fr_database_dir_path, BASE_FEATURE_FILE_PATH_LEN - 1, optarg, strlen(optarg)) != EOK,
                    TD_FAILURE, "strncpy_s database direstory path failed!");
                break;
            default:
                sample_aivsr_usage(argv);
                return TD_FAILURE;
        }
    }

    return TD_SUCCESS;
}

static td_s32 sample_aivsr_check_argv()
{
    if (g_sample_index < 0 || g_sample_index >= CASE_BUTT) {
        (td_void)printf("index should be between 0 ~ %d\n", CASE_BUTT - 1);
        return TD_FAILURE;
    }
    if ((access(g_image_path, F_OK) != 0)) {
        (td_void)printf("image file path[%s] not exist.\n", g_image_path);
        return TD_FAILURE;
    }
    if (g_image_size.width == 0 || g_image_size.height == 0) {
        (td_void)printf("image width(%u) or height(%u) is 0\n", g_image_size.width, g_image_size.height);
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

static td_void sample_aivsr_print_argv()
{
    (td_void)printf("==================== Input Param ====================\n");
    if (g_path_or_mem == OT_MODEL_LOAD_FROM_MEMORY) {
        (td_void)printf("Read model from memory\n");
    } else {
        (td_void)printf("Read model from path\n");
    }
    td_char case_name[CASE_BUTT][32] = {        // 32: string length
        "FACE_CAPTURE",
        "VEHICLE_LICENSE_PLATE_RECOGNIZE",
        "FACE_BASE_DATA",
        "FACE_RECOGNIZE"
    };
    (td_void)printf("Case: %s\n", case_name[g_sample_index]);
    (td_void)printf("Image: %s\n", g_image_path);
    (td_void)printf("Image Resolution: %ux%u\n", g_image_size.width, g_image_size.height);

    if (g_sample_index == CASE_FACE_BASE_DATA) {
        (td_void)printf("Database save name: %s\n", g_fr_database_name);
        (td_void)printf("Database save path: %s\n", g_fr_database_save_path);
    }
    if (g_sample_index == CASE_FACE_RECOGNIZE) {
        (td_void)printf("Database dir path name: %s\n", g_fr_database_dir_path);
    }
}

static td_s32 sample_aivsr_run_case()
{
    switch (g_sample_index) {
        case CASE_FACE_CAPTURE:
            return sample_aivsr_run_face_capture();
        case CASE_VEHICLE_LICENSE_PLATE_RECOGNIZE:
            return sample_aivsr_run_vlpr();
        case CASE_FACE_BASE_DATA:
        case CASE_FACE_RECOGNIZE:
            return sample_aivsr_run_fr();
        default:
            (td_void)printf("case index is invalid!\n");
            return TD_FAILURE;
    }
}

static td_s32 sample_aivsr_sys_init()
{
    // exit before init, to make sure the success of init
    (td_void)ss_mpi_sys_exit();
    (td_void)ss_mpi_vb_exit();

    // aivsr do not use vb, so an empty config is sufficient for system init
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
    return ret;
}

static td_s32 sample_aivsr_sys_exit()
{
    td_s32 ret = ss_mpi_sys_exit();
    sample_common_ai_log_check(ret != TD_SUCCESS, "ss_mpi_sys_exit error, please check system");
    ret = ss_mpi_vb_exit();
    sample_common_ai_log_check(ret != TD_SUCCESS, "ss_mpi_vb_exit error, please check system");
    return TD_SUCCESS;
}

int main(td_s32 argc, td_char *argv[])
{
    td_s32 ret = sample_aivsr_parse_argv(argc, argv);
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "sample_aivsr_parse_argv failed!");
    if (g_show_help == TD_TRUE) {
        sample_aivsr_usage(argv);
        return TD_SUCCESS;
    }

    ret = sample_aivsr_check_argv();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, show_help, "aivsr check arguments failed!");

    sample_aivsr_print_argv();

    ret = sample_aivsr_sys_init();
    sample_common_ai_log_check_return(ret != TD_SUCCESS, TD_FAILURE, "ss_mpi_vb_init failed!");

    ret = sample_aivsr_run_case();
    sample_common_ai_log_check_goto(ret != TD_SUCCESS, sys_deinit, "sample sample_aivsr_run_case error!!");

sys_deinit:
    (td_void)sample_aivsr_sys_exit();
    return ret;
show_help:
    sample_aivsr_usage(argv);
    return TD_FAILURE;
}
