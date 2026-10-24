#!/bin/sh

# 脚本名称: youyan_tf.sh
# 功能:
# 1. 若 /mnt/sdcard/audio 存在，删除其内所有 .g711a 文件
# 2. 若 /mnt/sdcard/youyan_app 存在，则执行它

AUDIO_DIR="/mnt/sdcard/audio"
APP_FILE="/mnt/sdcard/youyan_app"

# 1. 清理 .g711a 文件
if [ -d "$AUDIO_DIR" ]; then
    # 使用 find 避免文件名含空格或特殊字符的问题（若系统支持 find）
    if command -v find >/dev/null 2>&1; then
        find "$AUDIO_DIR" -type f -name "*.g711a" -delete 2>/dev/null
    else
        # 备用方案：使用 glob（但无法处理隐藏文件或极端命名）
        rm -f "$AUDIO_DIR"/*.g711a 2>/dev/null
    fi
fi

# 2. 执行 youyan_app（如果存在且可执行）
if [ -f "$APP_FILE" ]; then
    if [ -x "$APP_FILE" ]; then
        exec "$APP_FILE"
    else
        # 尝试用 sh 执行（如果是脚本但未加执行权限）
        exec sh "$APP_FILE"
    fi
else
    # youyan_app 不存在，正常退出
    exit 0
fi
