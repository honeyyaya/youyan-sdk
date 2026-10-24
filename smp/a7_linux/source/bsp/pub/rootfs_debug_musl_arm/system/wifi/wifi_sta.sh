#!/bin/sh
# wifi_sta.sh - WiFi station control script

# 配置文件目录
WIFI_DIR="/etc/wifi"

# 检查参数数量
if [ $# -ne 1 ]; then
    echo "Usage: $0 <1|0>"
    echo "  1 - Start WiFi STA mode"
    echo "  0 - Stop WiFi STA mode"
    exit 1
fi

# 根据参数执行操作
case "$1" in
    "1")
        echo "Starting WiFi station mode..."
        
        # 创建必要的目录（如果不存在）
        mkdir -p /var/run/wpa_supplicant
        
        # 检查wlan0接口是否存在
        if ! ifconfig wlan0 > /dev/null 2>&1; then
            echo "Error: wlan0 interface not found!"
            exit 1
        fi
        
        # 启动wpa_supplicant
        echo "Starting wpa_supplicant..."
        wpa_supplicant -i wlan0 -Dnl80211 -c${WIFI_DIR}/config/wpa_supplicant.conf &
        WPA_PID=$!
        
        # 等待wpa_supplicant初始化
        usleep 100000
        
        # 启动DHCP客户端
        echo "Starting DHCP client..."
        udhcpc -i wlan0 -s /usr/share/udhcpc/default.script -b
        DHCPC_PID=$!
        
        # 保存PID到文件以便停止时使用
        echo $WPA_PID > /var/run/wifi_sta.pid
        echo $DHCPC_PID >> /var/run/wifi_sta.pid
        
        echo "WiFi station mode started."
        ;;
        
    "0")
        echo "Stopping WiFi station mode..."
        
        # 检查是否有保存的PID文件
        if [ -f /var/run/wifi_sta.pid ]; then
            echo "Killing WiFi processes..."
            
            # 读取并杀死所有保存的进程
            while read pid; do
                if kill -0 $pid > /dev/null 2>&1; then
                    kill $pid
                    echo "Killed process $pid"
                fi
            done < /var/run/wifi_sta.pid
            
            # 删除PID文件
            rm -f /var/run/wifi_sta.pid
        else
            echo "No PID file found, trying to find and kill processes..."
            
            # 尝试查找并杀死相关进程
            pkill -f "wpa_supplicant.*wlan0" 2>/dev/null
            pkill -f "udhcpc.*wlan0" 2>/dev/null
        fi
        
        # 删除wpa_supplicant的运行时文件
        echo "Cleaning up runtime files..."
        rm -f /var/run/wpa_supplicant/wlan0
        
        # 释放wlan0 IP地址
        ifconfig wlan0 0.0.0.0 down
        
        echo "WiFi station mode stopped."
        ;;
        
    *)
        echo "Invalid parameter: $1"
        echo "Usage: $0 <1|0>"
        echo "  1 - Start WiFi STA mode"
        echo "  0 - Stop WiFi STA mode"
        exit 1
        ;;
esac

exit 0
