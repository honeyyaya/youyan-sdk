#!/bin/sh





# J15 UART2
bspmm 0x11130048 0x1135 #UART2_RX GPIO1_1
#bspmm 0x1113004c 0x11f5 #UART2_TX GPIO7_6


#Pin 87 GPIO3_5 for pwr toggle
bspmm 0x10260028 0x1100
#N=29
echo 29 > /sys/class/gpio/export
echo in > /sys/class/gpio/gpio29/direction
#cat  /sys/class/gpio/gpio29/value


#Pin 84 83 ,GPIO4_0,GPIO4_1 for touch button
bspmm 0x10260034 0x1100
#N=32
echo 32 > /sys/class/gpio/export
echo in > /sys/class/gpio/gpio32/direction

bspmm 0x10260038 0x1100
#N=33
echo 33 > /sys/class/gpio/export
echo in > /sys/class/gpio/gpio33/direction


#Pin61(GPIO6_1) ,Pin62(GPIO6_0),Pin63(GPIO6_3)for PWM indicating LED control
bspmm 0x1794000c 0x0007
echo 0 > /sys/class/pwm/pwmchip0/export
echo 10000 > /sys/class/pwm/pwmchip0/pwm0/period
echo 5000 > /sys/class/pwm/pwmchip0/pwm0/duty_cycle
echo normal > /sys/class/pwm/pwmchip0/pwm0/polarity
echo 1 > /sys/class/pwm/pwmchip0/pwm0/enable

bspmm 0x17940010 0x0007
echo 1 > /sys/class/pwm/pwmchip0/export
echo 10000 > /sys/class/pwm/pwmchip0/pwm1/period
echo 5000 > /sys/class/pwm/pwmchip0/pwm1/duty_cycle
echo normal > /sys/class/pwm/pwmchip0/pwm1/polarity
echo 1 > /sys/class/pwm/pwmchip0/pwm1/enable

bspmm 0x17940014 0x0007
echo 2 > /sys/class/pwm/pwmchip0/export
echo 10000 > /sys/class/pwm/pwmchip0/pwm2/period
echo 5000 > /sys/class/pwm/pwmchip0/pwm2/duty_cycle
echo normal > /sys/class/pwm/pwmchip0/pwm2/polarity
echo 1 > /sys/class/pwm/pwmchip0/pwm2/enable


#Pin64(GPIO6_2) for LED control
bspmm 0x17940010 0x0001
echo 50 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio50/direction
echo 1 > /sys/class/gpio/gpio50/value
















