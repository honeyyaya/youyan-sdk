#!/bin/sh
# Copyright (c) CompanyNameMagicTag 2023-2023. All rights reserved.
# Description:
# Author: pqtool team
# Create: 2023/6/1

stream="-s"
all="-a"
help="-h"
control="-c"
allstop="-as"
stream_control="-sc"
CtrlPID=0
StreamPID=0
timeout=0
exitflag=0
DLL_PATH=${LD_LIBRARY_PATH}:${PWD}/libs

fun_kill_control() {
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
}
fun_kill_stream() {
    killall ittb_stream  
    while true;do
        StreamPID=$(pidof ittb_stream)
        if [ -z "$CtrlPID" ] && [ -z "$StreamPID" ];then
            echo    ">>>>no pid,run it"
            break
        else
            echo "stream PID:" $StreamPID
            echo ">>>>program is exiting waiting"
            sleep 2
            timeout=$(expr $timeout + 1)
        fi
        if [ $timeout -eq 5 ];then
			if [ -z "$StreamPID" ];then
				:
			else
				wait $StreamPID
				kill -9 $StreamPID
			fi
			break
		fi
done
}

if [ "$1" == "$allstop" ] ;then
fun_kill_control
fun_kill_stream

exit
elif [ $# -gt 3 ] ;then
echo "To start only the ittb_stream and obtain initial configurations from the board, run ./PQTools.sh -s  sensortype.(sensortype: type of the sensor)"
echo "To start only the ittb_control, run ./PQTools.sh -c."
echo "To start full services and obtain initial configurations from the board, run ./PQTools.sh -a  sensortype.(sensortype: type of the sensor)"
echo "Help: ./PQTools.sh -h"
echo "Note ,All initial configuration files are stored in the release\configs directory."
exit

else
case "$1" in
control|$control)
timeout=0

fun_kill_control
fun_kill_stream

export LD_LIBRARY_PATH=${DLL_PATH}
./ittb_control& exit;;

stream|$stream)
timeout=0

fun_kill_control
fun_kill_stream

export LD_LIBRARY_PATH=${DLL_PATH}
./ittb_stream $2 $3&  exit;;

all|$all)
fun_kill_control
fun_kill_stream

export LD_LIBRARY_PATH=${DLL_PATH}
./ittb_stream $2 $3 $4&
while true;do
	sleep 1
	if [ $(ps -T| grep {RecvCfgProc}  |head -1| awk '{print $4}') = "{RecvCfgProc}" ];then
		break
	fi
	sleep 1
	if [ $(ps -T| grep {IspRun  |head -1| awk '{print $5}') = "./ittb_stream" ];then
		break
	fi
	sleep 1
	if [ $(ps | grep ittb_stream |head -1| awk '{print $4}') = "grep" ];then
		exit
	fi
done
./ittb_control & exit;;

stream_control|$stream_control)
fun_kill_control
fun_kill_stream

export LD_LIBRARY_PATH=${DLL_PATH}
./ittb_stream $2 $3 $4&
while true;do
	sleep 1
	if [ $(ps -T| grep {RecvCfgProc}  |head -1| awk '{print $4}') = "{RecvCfgProc}" ];then
		break
	fi
	sleep 1
	if [ $(ps -T| grep {IspRun  |head -1| awk '{print $5}') = "./ittb_stream" ];then
		break
	fi
	sleep 1
	if [ $(ps | grep ittb_stream |head -1| awk '{print $4}') = "grep" ];then
		break
	fi
done
./ittb_control 1 & exit;;
usage|$help) 
echo "To start only the ittb_stream and obtain initial configurations from the board, run ./PQTools.sh -s  sensortype.(sensortype: type of the sensor)"
echo "To start only the ittb_control, run ./PQTools.sh -c."
echo "To start full services and obtain initial configurations from the board, run ./PQTools.sh -a  sensortype.(sensortype: type of the sensor)"
echo "Help: ./PQTools.sh -h"
echo "Note ,All initial configuration files are stored in the release\configs directory."
echo "To Stop ittb_stream and ittb_control use ./PQTools.sh -as"
exit;;
esac

fi

echo "To start only the ittb_stream and obtain initial configurations from the board, run ./PQTools.sh -s  sensortype.(sensortype: type of the sensor)"
echo "To start only the ittb_control, run ./PQTools.sh -c."
echo "To start full services and obtain initial configurations from the board, run ./PQTools.sh -a  sensortype.(sensortype: type of the sensor)"
echo "Help: ./PQTools.sh -h"
echo "Note ,All initial configuration files are stored in the release\configs directory."

