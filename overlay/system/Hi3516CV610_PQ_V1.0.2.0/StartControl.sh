#!/bin/sh
# Copyright (c) CompanyNameMagicTag 2023-2023. All rights reserved.
# Description:
# Author: pqtool team
# Create: 2023/6/1

timeout=0
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${PWD}/libs
killall ittb_control
while true;do
    CtrlPID=$(pidof ittb_control)
    if [ -z "$CtrlPID" ];then
        echo    ">>>>no pid,run it"
        break
    else
	    echo "control PID:" $CtrlPID
        echo ">>>>program is exiting waiting"
        sleep 2
        timeout=$(expr $timeout + 1)
    fi
    if [ $timeout -eq 5 ];then
        if [ -z "$CtrlPID" ];then
            :
        else
            wait $CtrlPID
            kill -9 $CtrlPID
        fi
        break
    fi
done
if [ $# -gt 2 ]; then
echo "usage : ./StartControl.sh [-n | -name] <sensorname>"
echo "eg: ./StartControl.sh -name sc431hi"
elif [ "$1" == "-h" ];then
echo "usage : only start: ./StartControl.sh"
echo "usage : start for new sensor: ./StartControl.sh [-n | -name] <sensorname>"
echo "eg: ./StartControl.sh -name sc431hi"
elif [ "$1" == "-n" ] || [ "$1" == "-name" ]; then
if [ "$2" == "" ]; then
echo "please input sensor name."
echo "usage : ./StartControl.sh [-n | -name] <sensorname>"
echo "eg: ./StartControl.sh -name sc431hi"
exit
elif [ -d "./configs/$2" ]; then
echo "sensor lib $2 already exist."
./ittb_control 1 &
exit
fi
cp -rf ./libs/libsns_template_sns.so ./libs/libsns_$2.so
cp -rf ./configs/template_sns ./configs/$2
mv ./configs/$2/template_sns.ini ./configs/$2/$2.ini
sed -i "s/template_sns/$2/g" ./configs/$2/$2.ini
sed -i "s/template_sns/$2/g" ./configs/$2/config_entry.ini
sed -i '/^.*sensorini_name.*=.*$/c\sensorini_name = '$2'' config.cfg
sed -i '/^.*sensorini_path.*=.*$/c\sensorini_path=./configs/'$2'/'$2'.ini' config.cfg
./ittb_control 1 &
else
./ittb_control &
fi

