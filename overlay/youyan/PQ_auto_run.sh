#!/bin/sh

# 脚本名称: PQ_auto_run.sh
# 功能: 检查 SD 卡中指定目录是否存在，若存在则进入并执行 PQTools.sh

SD_DIR="/mnt/sdcard/Hi3516CV610_PQ_V1.0.2.0"
PQ_SCRIPT="./PQTools.sh"

if [ -d "$SD_DIR" ]; then
    cd "$SD_DIR" || exit 1
    if [ -x "$PQ_SCRIPT" ]; then
    	ifconfig eth0 192.168.31.200
        exec "$PQ_SCRIPT" -s sc4336p
    else
        echo "Error: $PQ_SCRIPT not found or not executable." >&2
        exit 1
    fi
else
    echo "Info: Directory $SD_DIR does not exist. Skipping PQ run." >&2
    exit 0
fi
