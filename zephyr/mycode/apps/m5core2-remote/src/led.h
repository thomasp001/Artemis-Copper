#ifndef led_H_
#define led_H_


#include <zephyr/drivers/gpio.h>

#include <zephyr/device.h>

#include <stdio.h>
#include <stdlib.h>
#include <zephyr/sys/util.h>
#include <inttypes.h>
#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>


void led_Task(void);


#endif /* led_H_ */