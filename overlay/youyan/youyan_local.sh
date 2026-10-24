#!/bin/sh
# 文件名：youyan_pre_init.sh

# 1. 清理.g711a文件
clean_audio_files() {
    DIR="/mnt/sdcard/audio"
    if [ -d "$DIR" ]; then
        echo "清理 $DIR 目录中的.g711a文件"
        cd "$DIR" && rm -f *.g711a 2>/dev/null
        echo "清理完成"
    fi
}

# 执行清理
clean_audio_files

cd /youyan

./youyan_app &

