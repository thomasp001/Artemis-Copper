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
#include <hal/nrf_power.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/init.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/drivers/uart.h>
#include <string.h>

#define SLEEP_TIME_MS	1
#define RECORD_TIME 1000
#define MAX_FRAMES 2
#define MAX_FRAME_LENGTH 20
#define MAX_SAVES 12

//Timings for sending TV signals
#define US_9000 295
#define US_4500 144
#define US_1687 56
#define US_563 17

#define ONE 1
#define ZERO 0

#define PWM_ON pwm_set_dt(&pwm_led0, 26315, 13158)
#define PWM_OFF pwm_set_dt(&pwm_led0, 26315, 0)

/* change this to any other UART peripheral if desired */
#define UART_DEVICE_NODE DT_NODELABEL(uart1)
#define MSG_SIZE 32

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);



/* receive buffer used in UART ISR callback */
static char rx_buf[MSG_SIZE];
static int rx_buf_pos;

/*
 * Get button configuration from the devicetree sw0 alias. This is mandatory.
 */
#define SW0_NODE	DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});
static struct gpio_callback button_cb_data;
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

/*
 * The led0 devicetree alias is optional. If present, we'll use it
 * to turn on the LED whenever the button is pressed.
 */
static const struct pwm_dt_spec pwm_led0 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));

#define MIN_PERIOD PWM_SEC(1U) / 128U
#define MAX_PERIOD PWM_SEC(1U)

bool capture[1000];
int capturing = 0;
uint8_t deviceType = 'T';
uint8_t message[160];
uint8_t Frames[MAX_FRAMES][MAX_FRAME_LENGTH];
uint8_t FramesLength[MAX_FRAMES];
uint8_t TVframes[MAX_SAVES][MAX_FRAMES][MAX_FRAME_LENGTH];
uint8_t TVframesLength[MAX_SAVES][MAX_FRAMES];
uint8_t ACframes[MAX_SAVES][MAX_FRAMES][MAX_FRAME_LENGTH];
uint8_t ACframesLength[MAX_SAVES][MAX_FRAMES];
uint32_t old_time;
uint32_t new_time;
uint32_t start_time;
uint32_t finish_time;
uint32_t difference;
int CyclesPerSecond;

/*
 * Read characters from UART until line end is detected. Afterwards push the
 * data to the message queue.
 */
void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart_dev)) {
		return;
	}

	if (!uart_irq_rx_ready(uart_dev)) {
		return;
	}

	/* read until FIFO empty */
	while (uart_fifo_read(uart_dev, &c, 1) == 1) {
		if ((c == '\n' || c == '\r') && rx_buf_pos > 0) {
			/* terminate string */
			rx_buf[rx_buf_pos] = '\0';

			/* if queue is full, message is silently dropped */
			k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);

			/* reset the buffer (it was copied to the msgq) */
			rx_buf_pos = 0;
		} else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
			rx_buf[rx_buf_pos++] = c;
		}
		/* else: characters beyond buffer size are dropped */
	}
}

/*
 * Print a null-terminated string character by character to the UART interface
 */
void print_uart(char *buf)
{
	int msg_len = strlen(buf);

	for (int i = 0; i < msg_len; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}

void delay(uint32_t seconds) {
    uint32_t start_time = k_cycle_get_32();
    uint32_t end_time = start_time + (CyclesPerSecond * seconds);

    while (k_cycle_get_32() < end_time) {
        // Busy wait until the desired time has elapsed
    }
}

void delay_units(uint32_t units) {
    uint32_t start_time = k_cycle_get_32();
    uint32_t end_time = start_time + units;

    while (k_cycle_get_32() < end_time) {
        // Busy wait until the desired time has elapsed
    }
}

void send_tv_message(int cap) {
  printk("Sending %d\n", TVframesLength[cap][0]);
  for (int i = 0; i < MAX_FRAMES; i++) {
    for (int j = 0; j < TVframesLength[cap][i]; j++) {
      printk("Frame #%02d / Byte #%02d: 0x%02hhX\n", i, j, TVframes[cap][i][j]);
    }
  }

  uint8_t bitValue;
  PWM_ON;
  delay_units(US_9000);
  PWM_OFF;
  delay_units(US_4500);
  
  for (int i = 0; i < TVframesLength[cap][0]; i++) {
    for (int j = 0; j < 8; j++) {
      PWM_ON;
      delay_units(US_563);
      PWM_OFF;
      bitValue = (!!(TVframes[cap][0][i] & (1 << j)));
      if (!bitValue) {
        // 0 bit
        delay_units(US_563);
      } else {
        // 1 bit
        delay_units(US_1687);
      }
    }
  }
  
  PWM_OFF;
  delay_units(US_563);
  PWM_ON;
  delay_units(US_563);
  PWM_OFF;
  
}


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
  int diffNewFrame = 0;
  int messageCounter = 0;
  //Find the first block of 0's
  while (counter < RECORD_TIME) {
    if (capture[counter] == 0) {
      break;
    }
    counter++;
  }
  //Find the next block of 1's
  while (counter < RECORD_TIME) {
    if (capture[counter] == 1) {
      last1 = counter;
      break;
    }
    counter++;
  }

  while (counter < RECORD_TIME) {
    //Find the next block of 0's
    while (counter < RECORD_TIME) {
      if (capture[counter] == 0) {
        break;
      }
      counter++;
    }
    //Find the next block of 1's & interpret the bit
    while (counter < RECORD_TIME) {
      if (capture[counter] == 1) {
        diff = counter - last1;
        diff0 = abs(diff - 3); // 3 -> 0, 7 -> 4, 37 -> 34
        diff1 = abs(diff - 7); // 3 -> 4, 7 -> 0, 37 -> 30
        diffNewFrame = abs(diff - 37); // 3-> 34, 7 -> 30, 37 -> 0
        if ((diffNewFrame < diff0) && (diffNewFrame < diff1)) {
          // New Frame
          message[messageCounter] = 0xFF;
          messageCounter++;
        }
        else if (diff1 < diff0) {
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
  /*
  int numBytes = messageCounter/8;
  for (int i = 0; i < numBytes; i++) {
    for (int j = 0; j < 8; j++) {
      messageBytes[i] |= (message[(i*8)+j] << (j));
    }
    printk("Byte #%02d: 0x%02hhX\n", i, messageBytes[i]);
  }
  */
  uint8_t frameCounter = 0;
  uint8_t bitCounter = 0;
  uint8_t byteCounter = 0;
  for (int i = 0; i < messageCounter; i++) {
    if (message[i] == 0xFF) {
      //New Frame
      if (bitCounter == 8) {
        byteCounter++;
      }
      FramesLength[frameCounter] = byteCounter;
      byteCounter = 0;
      bitCounter = 0;
      frameCounter++;
    } else {
      //Logic level 1 or 0
      if (bitCounter == 8) {
        bitCounter = 0;
        byteCounter++;
      }
      Frames[frameCounter][byteCounter] |= (message[i] << bitCounter);
      bitCounter++;
    }
  }
  if (bitCounter == 8) {
    byteCounter++;
  }
  FramesLength[frameCounter] = byteCounter;
  for (int i = 0; i < MAX_FRAMES; i++) {
    for (int j = 0; j < FramesLength[i]; j++) {
      printk("Frame #%02d / Byte #%02d: 0x%02hhX\n", i, j, Frames[i][j]);
    }
  }
  if (capturing) {
    if (deviceType == 'T') {
      memcpy(&TVframes[capturing-1], &Frames, MAX_FRAMES*MAX_FRAME_LENGTH);
      memcpy(&TVframesLength[capturing-1], &FramesLength, MAX_FRAMES);
      printk("Saved TV #%d\n", capturing-1);
    } else if (deviceType == 'A') {
      memcpy(ACframes[capturing-1], Frames, MAX_FRAMES*MAX_FRAME_LENGTH);
      memcpy(ACframesLength[capturing-1], FramesLength, MAX_FRAMES);
      printk("Saved TV #%d\n", capturing-1);
    }
    capturing = 0;
  }
  memset(capture, 0, 1000);
  memset(message, 0, 160);
  memset(Frames, 0, MAX_FRAME_LENGTH * MAX_FRAMES);
  memset(FramesLength, 0, MAX_FRAMES);
  gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
  printk("Ready :)\n");
}

static int board_nrf52840dongle_nrf52840_init(void)
{

	/* if the nrf52840dongle_nrf52840 board is powered from USB
	 * (high voltage mode), GPIO output voltage is set to 1.8 volts by
	 * default and that is not enough to turn the green and blue LEDs on.
	 * Increase GPIO voltage to 3.0 volts.
	 */
	if ((nrf_power_mainregstatus_get(NRF_POWER) ==
	     NRF_POWER_MAINREGSTATUS_HIGH) &&
	    ((NRF_UICR->REGOUT0 & UICR_REGOUT0_VOUT_Msk) ==
	     (UICR_REGOUT0_VOUT_DEFAULT << UICR_REGOUT0_VOUT_Pos))) {

		NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
		while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
			;
		}

		NRF_UICR->REGOUT0 =
		    (NRF_UICR->REGOUT0 & ~((uint32_t)UICR_REGOUT0_VOUT_Msk)) |
		    (UICR_REGOUT0_VOUT_3V0 << UICR_REGOUT0_VOUT_Pos);

		NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
		while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
			;
		}

		/* a reset is required for changes to take effect */
		NVIC_SystemReset();
	}

	return 0;
}


int main(void)
{
  char tx_buf[MSG_SIZE];
  board_nrf52840dongle_nrf52840_init();
  CyclesPerSecond = sys_clock_hw_cycles_per_sec();
  printk("Ticks/Second: %d\n", CyclesPerSecond);
	int ret;
	
  if (!pwm_is_ready_dt(&pwm_led0)) {
      printk("Error: PWM device %s is not ready\n",
            pwm_led0.dev->name);
      return 0;
    }
  PWM_OFF;
  
	if (!gpio_is_ready_dt(&button)) {
		printk("Error: button device %s is not ready\n",
		       button.port->name);
		return 0;
	}
	
  
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



  //gpio_pin_set_dt(&led, ZERO);
  //pwm_pin_set_usec(led, PWM_CHANNEL, 26, 0);
  //pwm_set_dt(&pwm_led0, 26315, 26315 / 2U);
  printk("G'day\n");
	printk("Press the button\n");

  if (!device_is_ready(uart_dev)) {
		printk("UART device not found!");
		return 0;
	}

	/* configure interrupt and callback to receive data */
	ret = uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);

	if (ret < 0) {
		if (ret == -ENOTSUP) {
			printk("Interrupt-driven UART API support not enabled\n");
		} else if (ret == -ENOSYS) {
			printk("UART device does not support interrupt-driven API\n");
		} else {
			printk("Error setting UART callback: %d\n", ret);
		}
		return 0;
	}
	uart_irq_rx_enable(uart_dev);

	/* indefinitely wait for input from the user */
	while (k_msgq_get(&uart_msgq, &tx_buf, K_FOREVER) == 0) {
		if (strlen(tx_buf) == 3) {
			if (tx_buf[0] == 'T') {
				printk("TV ");
			} else {
				printk("AC ");
			}
      deviceType = tx_buf[0];
			if (tx_buf[1] == 'P') {
				printk("PROGRAM ");
        capturing = (tx_buf[2] - 48 + 1);
			} else {
				printk("SEND ");
        capturing = 0;
			}
			if (tx_buf[2] <= 57) {
				printk("%c\n", tx_buf[2]);
			} else if (tx_buf[2] == 58) {
				printk("10\n");
			} else if (tx_buf[2] == 59) {
				printk("11\n");
			}
			if ((tx_buf[1] == 'S') && (tx_buf[0] == 'T')) { //Send TV
        send_tv_message(tx_buf[2]-48);
      }
		}
	}

	return 0;
}


SYS_INIT(board_nrf52840dongle_nrf52840_init, PRE_KERNEL_1,
	 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);