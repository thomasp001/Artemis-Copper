/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define SLEEP_TIME_MS	1


/*
 * Get button configuration from the devicetree sw0 alias. This is mandatory.
 */
#define SW0_NODE	DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});
static struct gpio_callback button_cb_data;

/*
 * The led0 devicetree alias is optional. If present, we'll use it
 * to turn on the LED whenever the button is pressed.
 */
static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,
						     {0});

bool capture[1000];
bool message[160];
uint8_t messageBytes[20];
uint32_t old_time;
uint32_t new_time;
uint32_t start_time;
uint32_t finish_time;
uint32_t difference;

void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
  gpio_pin_interrupt_configure_dt(&button, GPIO_INT_DISABLE);
  // Do the things here
  int counter = 0;
  start_time = k_cyc_to_us_floor32(k_cycle_get_32());
  old_time = start_time;
  while (counter < 1000) {
    new_time = k_cyc_to_us_floor32(k_cycle_get_32());
    if ((new_time - old_time) >= 250) {
      capture[counter] = gpio_pin_get_dt(&button);
      counter++;
      old_time = new_time;
    }
  }
  finish_time = k_cyc_to_us_floor32(k_cycle_get_32());
  /*
  for (int i = 0; i < 1000; i++) {
    printk("%d, ", capture[i]);
  } 
  printk("\n");
  */
  printk("Completed in %dus\n", (finish_time - start_time));
  //Decode the capture
  counter = 0;
  int last1 = 0;
  int diff = 0;
  int diff1 = 0;
  int diff0 = 0;
  int messageCounter = 0;
  //Find the first block of 0's
  while (counter < 1000) {
    if (capture[counter] == 0) {
      break;
    }
    counter++;
  }
  //Find the next block of 1's
  while (counter < 1000) {
    if (capture[counter] == 1) {
      last1 = counter;
      break;
    }
    counter++;
  }

  while (counter < 1000) {
    //Find the next block of 0's
    while (counter < 1000) {
      if (capture[counter] == 0) {
        break;
      }
      counter++;
    }
    //Find the next block of 1's & interpret the bit
    while (counter < 1000) {
      if (capture[counter] == 1) {
        diff = counter - last1;
        diff0 = abs(diff - 3); // 3 -> 0, 7 -> 4
        diff1 = abs(diff - 7); // 3 -> 4 7 -> 0
        if (diff1 < diff0) {
          // 1 bit
          message[messageCounter] = 1;
          messageCounter++;
        } else {
          // 0 bit
          message[messageCounter] = 0;
          messageCounter++;
        }
        last1 = counter;
        break;
      }
      counter++;
    }
  }
  
  /*
  for (int i = 0; i < messageCounter; i++) {
    printk("%d", message[i]);
    if ((i+1)%8 == 0) {
      printk("\n");
    }
  }
  */
  
  int numBytes = messageCounter/8;
  for (int i = 0; i < numBytes; i++) {
    for (int j = 0; j < 8; j++) {
      messageBytes[i] |= (message[(i*8)+j] << (j));
    }
    printk("Byte #%02d: 0x%02hhX\n", i, messageBytes[i]);
  }
  memset(capture, 0, 1000);
  memset(message, 0, 160);
  memset(messageBytes, 0, 20);
  gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
  printk("Ready :)\n");
}

int main(void)
{
  printk("Ticks/Second: %d\n", sys_clock_hw_cycles_per_sec());
	int ret;
	printk("1\n");
	if (!gpio_is_ready_dt(&button)) {
		printk("Error: button device %s is not ready\n",
		       button.port->name);
		return 0;
	}
	printk("2\n");
	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button.port->name, button.pin);
		return 0;
	}

	ret = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button.port->name, button.pin);
		return 0;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	printk("Set up button at %s pin %d\n", button.port->name, button.pin);

	if (led.port && !gpio_is_ready_dt(&led)) {
		printk("Error %d: LED device %s is not ready; ignoring it\n",
		       ret, led.port->name);
		led.port = NULL;
	}
	if (led.port) {
		ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, led.port->name, led.pin);
			led.port = NULL;
		} else {
			printk("Set up LED at %s pin %d\n", led.port->name, led.pin);
		}
	}

	printk("Press the button\n");
	if (led.port) {
    /*
		while (1) {
			int val = gpio_pin_get_dt(&button);

			if (val >= 0) {
				gpio_pin_set_dt(&led, val);
			}
			k_msleep(SLEEP_TIME_MS);
		}
    */
	}
	return 0;
}
