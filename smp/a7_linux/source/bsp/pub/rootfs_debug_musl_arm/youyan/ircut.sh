#!/bin/sh

export status=$1

function on_function()
{
	echo 0 > /sys/class/gpio/gpio10/value
	echo 1 > /sys/class/gpio/gpio11/value
	sleep 0.1;
	echo 0 > /sys/class/gpio/gpio10/value
	echo 0 > /sys/class/gpio/gpio11/value
}

function off_function()
{
    echo 1 > /sys/class/gpio/gpio10/value
	echo 0 > /sys/class/gpio/gpio11/value
	sleep 0.1;
	echo 0 > /sys/class/gpio/gpio10/value
	echo 0 > /sys/class/gpio/gpio11/value
}

if test ! -f /sys/class/gpio/gpio10/value; then
    echo 10 > /sys/class/gpio/export
    echo out > /sys/class/gpio/gpio10/direction
    echo 0 > /sys/class/gpio/gpio10/value
fi

if test ! -f /sys/class/gpio/gpio11/value; then
    echo 11 > /sys/class/gpio/export
    echo out > /sys/class/gpio/gpio11/direction
    echo 1 > /sys/class/gpio/gpio11/value
fi

sleep 0.5;

case $status in
	on) on_function
	;;
	off) off_function
	;;
	-h) echo 'Options: on off '
	;;
	*)  echo 'Use '-h' to display command line options of sub-processes'
	;;
esac
