#!/bin/sh
if [ -e "/dev/$MDEV" ]; then
    mkdir -p /mnt/sdcard
    mount -t vfat /dev/$MDEV /mnt/sdcard  # 根据实际文件系统调整（如 ext4）
fi
