#! /bin/sh

_4G_TYPE=${1}


if [ "${_4G_TYPE}" == "" ]; then
    echo "Please Run: ${0} [4G type]"
    echo "Example: '${0} EC200A'  or  '${0} ML307'"
    echo ""
    exit 0
fi
	echo "==================== 4G ==================="
	echo "start 4G..."
	echo "host" > /proc/10300000.usb20drd/mode
	sleep 4
if [ "${_4G_TYPE}" == "EC200A" ]; then

	echo "==================== EC200A ==================="	
	echo -e "AT+QCFG=\"usbnet\",1\r\n" > /dev/ttyUSB1
	sleep 1
	echo -e "AT+QNETDEVCTL=3,1,1\r\n" > /dev/ttyUSB1
	sleep 1
	ifconfig eth0 down
	sleep 1
	ifconfig usb0 up
	sleep 1
	udhcpc -i usb0
	sleep 3
	echo "================ End of 4G ================"


elif [ "${_4G_TYPE}" == "ML307" ]; then

	echo "================ ML307 ================"
	echo -e "AT+MDIALUPCFG=\"mode\",0\r\n" > /dev/ttyUSB2
	sleep 1
	echo -e "AT+MDIALUP=1,1\r\n" > /dev/ttyUSB2
	sleep 1
	ifconfig eth0 down
	sleep 3
	ifconfig eth1 up
	sleep 1
	udhcpc -i eth1
	sleep 3
	echo "================ End of 4G ================"

fi


