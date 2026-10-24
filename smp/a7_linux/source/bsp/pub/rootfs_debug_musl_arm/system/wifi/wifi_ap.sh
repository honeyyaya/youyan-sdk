#!/bin/sh
# wifi_ap.sh - WiFi AP模式管理脚本
# 参数: 1 - 启动AP模式, 0 - 停止AP模式

set -e  # 遇到错误立即退出

# 常量定义
WLAN_IF="wlan0"
AP_IP="192.168.10.1"
AP_NETMASK="255.255.255.0"
UDHCPD_PID_FILE="/var/run/udhcpd.pid"
HOSTAPD_PID_FILE="/var/run/hostapd.pid"
WPA_SUPPLICANT_PATH="/var/run/wpa_supplicant/${WLAN_IF}"
UDHCPD_CONF="/etc/udhcp/udhcpd.conf"
HOSTAPD_CONF="/system/wifi/config/hostapd.conf"

# 日志函数
log_info() {
    echo "[INFO] $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

log_error() {
    echo "[ERROR] $(date '+%Y-%m-%d %H:%M:%S') - $1" >&2
}

# 检查是否以root权限运行
check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        log_error "此脚本需要root权限"
        exit 1
    fi
}

# 检查必要文件是否存在
check_files() {
    if [ ! -f "$UDHCPD_CONF" ]; then
        log_error "udhcpd配置文件不存在: $UDHCPD_CONF"
        return 1
    fi
    
    if [ ! -f "$HOSTAPD_CONF" ]; then
        log_error "hostapd配置文件不存在: $HOSTAPD_CONF"
        return 1
    fi
}

# 检查网络接口是否存在
check_interface() {
    if ! ip link show "$WLAN_IF" > /dev/null 2>&1; then
        log_error "网络接口 $WLAN_IF 不存在"
        return 1
    fi
}

# 停止后台进程函数
stop_process() {
    local process_name="$1"
    local pid_file="$2"
    
    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file" 2>/dev/null)
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            log_info "停止 $process_name (PID: $pid)"
            kill "$pid" 2>/dev/null
            sleep 1
            if kill -0 "$pid" 2>/dev/null; then
                kill -9 "$pid" 2>/dev/null
                log_info "强制停止 $process_name"
            fi
            rm -f "$pid_file"
        else
            rm -f "$pid_file"
            # 添加延迟检查
            if pkill -x "$process_name" 2>/dev/null; then
                sleep 0.5  # 等待进程处理信号
                log_info "通过进程名停止 $process_name"
            fi
        fi
    else
        # 添加延迟检查
        if pkill -x "$process_name" 2>/dev/null; then
            sleep 0.5  # 等待进程处理信号
            log_info "通过进程名停止 $process_name"
        fi
    fi
}
# 启动AP模式
start_ap_mode() {
    log_info "正在启动WiFi AP模式..."
    
    # 检查前置条件
    check_interface || return 1    
    check_files || return 1
    
    # 停止可能存在的wpa_supplicant
    stop_process "wpa_supplicant" "/var/run/wpa_supplicant.pid"
    
    # 清理旧的wpa_supplicant文件
    if [ -e "$WPA_SUPPLICANT_PATH" ]; then
        log_info "清理wpa_supplicant文件"
        rm -f "$WPA_SUPPLICANT_PATH"
    fi
    
    # 关闭接口
    log_info "关闭网络接口 $WLAN_IF"
    ifconfig "$WLAN_IF" down 2>/dev/null || {
        ip link set "$WLAN_IF" down 2>/dev/null || true
    }
    
    sleep 1
    
    # 配置接口IP并启动
    log_info "配置 $WLAN_IF IP地址为 $AP_IP"
    ifconfig "$WLAN_IF" "$AP_IP" netmask "$AP_NETMASK" up || {
        ip addr add "$AP_IP"/24 dev "$WLAN_IF" && ip link set "$WLAN_IF" up
    } || {
        log_error "无法启动网络接口 $WLAN_IF"
        return 1
    }
    
    # 检查接口是否真的起来了
    if ! ifconfig "$WLAN_IF" | grep -q "$AP_IP"; then
        log_error "接口 $WLAN_IF 启动失败"
        return 1
    fi
    
    # 启动udhcpd
    log_info "启动DHCP服务器 (udhcpd)"
    if [ -f "$UDHCPD_PID_FILE" ]; then
        rm -f "$UDHCPD_PID_FILE"
    fi
    udhcpd -f "$UDHCPD_CONF" &
    UDHCPD_PID=$!
    echo "$UDHCPD_PID" > "$UDHCPD_PID_FILE"
    
    # 短暂延迟确保udhcpd启动
    usleep 100000 2>/dev/null || sleep 0.1
    
    # 检查udhcpd是否在运行
    if ! kill -0 "$UDHCPD_PID" 2>/dev/null; then
        log_error "udhcpd启动失败"
        return 1
    fi
    
    # 启动hostapd
    log_info "启动hostapd"
    if [ -f "$HOSTAPD_PID_FILE" ]; then
        rm -f "$HOSTAPD_PID_FILE"
    fi
    /system/wifi/bin/hostapd "$HOSTAPD_CONF" &
    HOSTAPD_PID=$!
    echo "$HOSTAPD_PID" > "$HOSTAPD_PID_FILE"
    
    # 检查hostapd是否在运行
    sleep 1
    if ! kill -0 "$HOSTAPD_PID" 2>/dev/null; then
        log_error "hostapd启动失败"
        # 如果hostapd失败，也停止udhcpd
        kill "$UDHCPD_PID" 2>/dev/null
        return 1
    fi
    
    log_info "WiFi AP模式启动完成"
    log_info "AP IP: $AP_IP, 接口: $WLAN_IF"
    return 0
}

# 停止AP模式
stop_ap_mode() {
    log_info "正在停止WiFi AP模式..."
    
    # 停止hostapd
    stop_process "hostapd" "$HOSTAPD_PID_FILE"
    
    # 停止udhcpd
    stop_process "udhcpd" "$UDHCPD_PID_FILE"
    
    # 清理wpa_supplicant文件
    if [ -e "$WPA_SUPPLICANT_PATH" ]; then
        log_info "清理wpa_supplicant文件"
        rm -f "$WPA_SUPPLICANT_PATH"
    fi
    
    # 关闭接口（可选）
    # ifconfig "$WLAN_IF" down 2>/dev/null || true
    
    log_info "WiFi AP模式已停止"
}

# 显示使用说明
show_usage() {
    echo "用法: $0 [0|1]"
    echo "  1 - 启动WiFi AP模式"
    echo "  0 - 停止WiFi AP模式"
    echo ""
    echo "当前状态:"
    
    # 检查进程状态
    if [ -f "$HOSTAPD_PID_FILE" ] && kill -0 $(cat "$HOSTAPD_PID_FILE") 2>/dev/null; then
        echo "  hostapd: 运行中 (PID: $(cat "$HOSTAPD_PID_FILE"))"
    else
        echo "  hostapd: 未运行"
    fi
    
    if [ -f "$UDHCPD_PID_FILE" ] && kill -0 $(cat "$UDHCPD_PID_FILE") 2>/dev/null; then
        echo "  udhcpd: 运行中 (PID: $(cat "$UDHCPD_PID_FILE"))"
    else
        echo "  udhcpd: 未运行"
    fi
    
    # 检查接口状态
    if ifconfig "$WLAN_IF" >/dev/null 2>&1; then
        echo "  接口 $WLAN_IF: 存在"
        if ifconfig "$WLAN_IF" | grep -q "$AP_IP"; then
            echo "  IP地址: $AP_IP"
        fi
    else
        echo "  接口 $WLAN_IF: 不存在"
    fi
}

# 主函数
main() {
    check_root
    
    case "${1}" in
        "1")
            start_ap_mode
            if [ $? -eq 0 ]; then
                log_info "AP模式启动成功"
                exit 0
            else
                log_error "AP模式启动失败"
                exit 1
            fi
            ;;
        "0")
            stop_ap_mode
            log_info "AP模式停止成功"
            exit 0
            ;;
        "-h"|"--help")
            show_usage
            exit 0
            ;;
        *)
            log_error "无效参数: $1"
            show_usage
            exit 1
            ;;
    esac
}

# 异常处理
trap 'log_error "脚本执行被中断"; exit 1' INT TERM

# 执行主函数
main "$@"
