#!/bin/sh

export status=$1

function high_function()
{
    echo 1 > /sys/class/gpio/gpio63/value
	echo 1 > /sys/class/gpio/gpio8/value
	echo 1 > /sys/class/gpio/gpio12/value
}

function low_function()
{
    echo 0 > /sys/class/gpio/gpio63/value
	echo 0 > /sys/class/gpio/gpio8/value
	echo 0 > /sys/class/gpio/gpio12/value
}

GPIO_NUM="63 8 12"

function gpio_export()
{
	for dir in $GPIO_NUM
	do
		if test ! -f /sys/class/gpio/gpio$dir/value; then
			echo $dir > /sys/class/gpio/export
			echo out > /sys/class/gpio/gpio$dir/direction
			echo 0 > /sys/class/gpio/gpio$dir/value
		fi
	done
}

gpio_export

case $status in
	hi) high_function
	;;
	lo) low_function
	;;
	-h) echo 'Options: hi lo'
	;;
	*)  echo 'Use '-h' to display command line options of sub-processes'
	;;
esac
