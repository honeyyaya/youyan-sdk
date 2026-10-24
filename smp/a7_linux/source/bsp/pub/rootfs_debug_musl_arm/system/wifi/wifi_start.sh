#!/bin/sh

WLAN_IF="wlan0"
WPAS_PID_FILE="/var/run/wpa_supplicant/$WLAN_IF"
WPAS_CONF="/system/wifi/config/wpa_supplicant.conf"
STATIC_IP="192.168.31.200"
NETMASK="255.255.255.0"
GATEWAY="192.168.31.1"

echo "==================== wifi ==================="

# 1. 确保 wlan0 接口 up（某些驱动需要先 up 才能扫描）
ifconfig "$WLAN_IF" up >/dev/null 2>&1

# 2. 如果 wpa_supplicant 已在运行，先 kill 掉
if [ -e "$WPAS_PID_FILE" ] || pidof wpa_supplicant >/dev/null 2>&1; then
    echo "Killing existing wpa_supplicant..."
    killall wpa_supplicant >/dev/null 2>&1
    usleep 200000  # 等待 200ms 确保退出
fi

# 3. 启动 wpa_supplicant
echo "Starting wpa_supplicant..."
mkdir -p /var/run/wpa_supplicant
wpa_supplicant -i "$WLAN_IF" -Dnl80211 -c"$WPAS_CONF" -B

# 等待 supplicant 初始化完成（可选，但建议）
usleep 500000

# 4. 尝试配置静态 IP 并测试网关连通性
echo "Trying static IP: $STATIC_IP"
ifconfig "$WLAN_IF" "$STATIC_IP" netmask "$NETMASK"
route add default gw "$GATEWAY" dev "$WLAN_IF"

# 测试网关是否可达（ping 1 次，超时 2 秒）
if ping -c 1 -W 2 "$GATEWAY" >/dev/null 2>&1; then
    echo "Static IP configured successfully. Gateway is reachable."
else
    echo "Static IP failed: gateway $GATEWAY not reachable. Falling back to DHCP..."

    # 清除静态配置
    route del default gw "$GATEWAY" dev "$WLAN_IF" 2>/dev/null
    ifconfig "$WLAN_IF" 0.0.0.0

    # 尝试 DHCP（假设系统有 udhcpc，常见于 BusyBox）
    if command -v udhcpc >/dev/null 2>&1; then
        udhcpc -i "$WLAN_IF" -n -t 5
    elif command -v dhclient >/dev/null 2>&1; then
        dhclient "$WLAN_IF"
    else
        echo "Error: No DHCP client (udhcpc/dhclient) found!" >&2
        exit 1
    fi
fi

# 5. 确保接口处于 up 状态
ifconfig "$WLAN_IF" up

echo "================ End of wifi ================"
