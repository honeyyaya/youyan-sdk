#!/bin/sh
# Filename: wifi_sta_try_init.sh
# Description: Check wpa_supplicant configuration and start Wi-Fi STA mode

# Check if configuration file exists
if [ ! -f "/etc/wifi/config/wpa_supplicant.conf" ]; then
    echo "Warning: /etc/wifi/config/wpa_supplicant.conf does not exist, so skip wifi init!"
    exit 1
fi

# Check if configuration file content is valid
check_config() {
    local config_file="$1"
    local has_ssid=0
    local has_psk=0
    
    # Check if the file contains ssid field
    if grep -q '^[[:space:]]*ssid=' "$config_file" 2>/dev/null; then
        has_ssid=1
    fi
    
    # Check if the file contains psk field
    if grep -q '^[[:space:]]*psk=' "$config_file" 2>/dev/null; then
        has_psk=1
    fi
    
    # If ssid or psk fields are missing, return error
    if [ "$has_ssid" -eq 0 ] || [ "$has_psk" -eq 0 ]; then
        return 1
    fi
    return 0
}

# Validate configuration file
if ! check_config "/etc/wifi/config/wpa_supplicant.conf"; then
    echo "Error: wpa_supplicant.conf configuration is invalid, missing ssid or psk field"
    exit 1
fi

# Configuration is valid, start Wi-Fi
echo "wpa_supplicant.conf configuration is valid, starting Wi-Fi STA mode..."
/system/wifi/wifi_sta.sh 1 &

# Check if startup was successful
if [ $? -eq 0 ]; then
    echo "Wi-Fi STA mode start command has been executed"
else
    echo "Warning: An issue occurred while starting Wi-Fi STA mode"
    exit 1
fi

exit 0
