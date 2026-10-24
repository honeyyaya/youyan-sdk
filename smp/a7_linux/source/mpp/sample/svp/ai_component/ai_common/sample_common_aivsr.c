/*
  Copyright (c), 2024-2024, Shenshu Tech. Co., Ltd.
 */

#include "sample_common_aivsr.h"

td_void sample_common_aivsr_print_vehicle_info(ot_vehicle_info vehicleInfo)
{
    td_char vehicleCategory[][10] = { "客车", "轿车", "面包车", "SUV", "皮卡车", "货车", "未知" };
    td_char vehicleColor[][10] = { "黑", "蓝", "棕", "灰", "黄", "绿", "紫", "红", "白", "未知" };
    td_char vehicleOrient[][10] = { "车头", "侧面", "车尾", "未知" };

    (td_void)printf("vehicle:\n"
                    "  [id: %-2u]\n"
                    "  [x:%4d, y:%4d, width:%4u, height:%4u]\n"
                    "  [category:%d(%s), color: %d(%s), orient: %d(%s)]\n",
        vehicleInfo.detect_object.track_id,
        vehicleInfo.detect_object.detect_rect.x, vehicleInfo.detect_object.detect_rect.y,
        vehicleInfo.detect_object.detect_rect.width, vehicleInfo.detect_object.detect_rect.height,
        vehicleInfo.property.category, vehicleCategory[vehicleInfo.property.category],
        vehicleInfo.property.color, vehicleColor[vehicleInfo.property.color],
        vehicleInfo.property.orient, vehicleOrient[vehicleInfo.property.orient]);
    (td_void)printf("\n");
}

td_void sample_common_aivsr_print_plate_info(ot_license_plate_info plateInfo)
{
    td_char plateCategory[][10] = { "大型", "小型", "使馆", "领馆", "挂车", "教练车", "警车", "香港", "澳门", "武警",
        "军车",
        "新能源", "其他" };
    td_char plateColor[][10] = { "蓝", "黄", "绿", "黑", "白", "未知" };
    td_char plateCode[][10] = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
        "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
        "U", "V", "W", "X", "Y", "Z",
        "皖", "沪", "津", "渝", "冀", "晋", "蒙", "辽", "吉",
        "黑", "苏", "浙", "京", "闽", "赣", "鲁", "豫", "鄂",
        "湘", "粤", "桂", "琼", "川", "贵", "云", "藏", "陕",
        "甘", "青", "宁", "新", "港", "澳", "挂", "使", "军",
        "学", "警", "领", "字", "电"
    };
    (td_void)printf("  plate: \n");
    (td_void)printf("    [code: ");
    for (td_u32 word = 0; word < plateInfo.license_plate_len; ++word) {
        (td_void)printf("%u(%s) ",
            plateInfo.license_plate_code[word],
            plateCode[plateInfo.license_plate_code[word]]);
    }
    (td_void)printf("]\n");
    (td_void)printf("    [x:%4d, y:%4d, width:%4u, height:%4u]\n"
                    "    [category:%d(%s), color: %d(%s)]\n",
        plateInfo.license_plate_rect.x, plateInfo.license_plate_rect.y,
        plateInfo.license_plate_rect.width, plateInfo.license_plate_rect.height,
        plateInfo.property.category, plateCategory[plateInfo.property.category],
        plateInfo.property.color, plateColor[plateInfo.property.color]);
}