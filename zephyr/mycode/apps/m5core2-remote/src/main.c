
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>

#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <lvgl_input_device.h>

#include <zephyr/sys/util.h>
#include <zephyr/device.h>


#define UART_DEVICE_NODE DT_ALIAS(uart1)



#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

static uint32_t count;
static uint32_t count2;




typedef struct {
	int signal[22];
	lv_obj_t *lv_button;
	lv_obj_t *lv_label;
} Button; //signal list for a button

Button new_Button(uint8_t count);



//Screen display;
lv_obj_t * create_screen;
lv_obj_t * count2_label;
lv_obj_t *generic_title;

lv_obj_t * remote_label;
int ac_signal[15][22] = {0}; //signals for ac
int tv_signal[15][22] = {0}; //signals for tv
Button button_matrix[15]; //array of button structs 
uint8_t remote_selection = 0; //toggles what array to get signals for buttons from (and title)
char* remote_names[2] = {"TV", "AC"};
int new_signal[22] = {0}; //holding for the new signal
uint8_t new_IR = 0;


// setting up usart requirements
#define MY_USART2 DT_NODELABEL(uart0)

static const struct device *const usart_dev = DEVICE_DT_GET(MY_USART2);

uint8_t buf; // buffer in for usart

static struct curve_params {
    int x_mag;
	int y_mag;
};


#ifdef CONFIG_GPIO
static struct gpio_dt_spec button_gpio = GPIO_DT_SPEC_GET_OR(
		DT_ALIAS(sw0), gpios, {0});
static struct gpio_callback button_callback;

static void button_isr_callback(const struct device *port,
				struct gpio_callback *cb,
				uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	count = 0;
}
#endif /* CONFIG_GPIO */

#ifdef CONFIG_LV_Z_ENCODER_INPUT
static const struct device *lvgl_encoder =
	DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_lvgl_encoder_input));
#endif /* CONFIG_LV_Z_ENCODER_INPUT */

#ifdef CONFIG_LV_Z_KEYPAD_INPUT
static const struct device *lvgl_keypad =
	DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_lvgl_keypad_input));
#endif /* CONFIG_LV_Z_KEYPAD_INPUT */

static void lv_btn_click_callback(lv_event_t *e)
{
	ARG_UNUSED(e);
	


	//do normal button signal send
	for (uint8_t i = 0; i < 12; i++) {
		if (lv_obj_get_state(button_matrix[i].lv_button) == LV_EVENT_PRESSING) {
			//change the signal
			//Byte 1 Device - T for TV - A for AC
			//Byte 2 Program Mode - 1 for Yes, 0 for No
			//Byte 3 Number - int 0 - 11
			uint8_t data_buf[4] = "000\0";
			if (remote_selection == 0) {
				data_buf[0] = 'T';
			} else {
				data_buf[0] = 'A';
			}
			data_buf[2] = i + 48;
			if (new_IR == 1) {
				//print_uart("PROGRAM ");
				//print_uart(num_buf);
				//print_uart("\n");
				data_buf[1] = 'P';
				new_IR = 0;
			} else {
				data_buf[1] = 'S';
				//print_uart("SEND ");
				//print_uart(num_buf);
				//print_uart("\n");
			}
			print_uart(data_buf);
			print_uart("\n");
		}
	}

}

static void lv_btn_screen_right(lv_event_t *e)
{
	//print_uart("RIGHT\n");
	ARG_UNUSED(e);
	//these become a title toggle and determine what the 
	//buttons data is selected from 
	remote_selection++;
	if (remote_selection > 1) {
		remote_selection = 0;
	}
	char name[11] = {0};
	sprintf(name, "%s", remote_names[remote_selection]);
	lv_label_set_text(generic_title, name);
}

static void lv_btn_screen_left(lv_event_t *e)
{
	//print_uart("LEFT\n");
	ARG_UNUSED(e);
	//as above
	if (remote_selection == 0) {
		remote_selection = 1;
	} else {
		remote_selection--;
	}
	char name[11] = {0};
	sprintf(name, "%s", remote_names[remote_selection]);
	lv_label_set_text(generic_title, name);
}

static void lv_button_signal_callback(lv_event_t *e) {
	//print_uart("NEW SIGNAL\n");
	ARG_UNUSED(e);
	new_IR = 1;
	//receive a new signal
	//uart_poll_out(usart_dev, 'u');

	for (uint8_t i = 0; i < 22; i++) {
		//uart check
		if (uart_poll_in(usart_dev, &buf) != -1) { //reads uart 1 char at a time 
			//received char is stored in buf
			new_signal[i] = buf;
		}
	}

	count = 0;
}

void print_uart(char *buf)
{
	int msg_len = strlen(buf);

	for (int i = 0; i < msg_len; i++) {
		uart_poll_out(usart_dev, buf[i]);
	}
}

int main(void)
{
    char count_str[11] = {0};
	const struct device *display_dev;
	lv_obj_t *hello_world_label;
	lv_obj_t *count_label;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return 0;
	}

#ifdef CONFIG_GPIO
	if (gpio_is_ready_dt(&button_gpio)) {
		int err;

		err = gpio_pin_configure_dt(&button_gpio, GPIO_INPUT);
		if (err) {
			LOG_ERR("failed to configure button gpio: %d", err);
			return 0;
		}

		gpio_init_callback(&button_callback, button_isr_callback,
				   BIT(button_gpio.pin));

		err = gpio_add_callback(button_gpio.port, &button_callback);
		if (err) {
			LOG_ERR("failed to add button callback: %d", err);
			return 0;
		}

		err = gpio_pin_interrupt_configure_dt(&button_gpio,
						      GPIO_INT_EDGE_TO_ACTIVE);
		if (err) {
			LOG_ERR("failed to enable button callback: %d", err);
			return 0;
		}
	}
#endif /* CONFIG_GPIO */

#ifdef CONFIG_LV_Z_ENCODER_INPUT
	lv_obj_t *arc;
	lv_group_t *arc_group;

	arc = lv_arc_create(lv_scr_act());
	lv_obj_align(arc, LV_ALIGN_CENTER, 0, -15);
	lv_obj_set_size(arc, 150, 150);

	arc_group = lv_group_create();
	lv_group_add_obj(arc_group, arc);
	lv_indev_set_group(lvgl_input_get_indev(lvgl_encoder), arc_group);
#endif /* CONFIG_LV_Z_ENCODER_INPUT */

#ifdef CONFIG_LV_Z_KEYPAD_INPUT
	lv_obj_t *btn_matrix;
	lv_group_t *btn_matrix_group;
	static const char *const btnm_map[] = {"1", "2", "3", "4", ""};

	btn_matrix = lv_btnmatrix_create(lv_scr_act());
	lv_obj_align(btn_matrix, LV_ALIGN_CENTER, 0, 70);
	lv_btnmatrix_set_map(btn_matrix, (const char **)btnm_map);
	lv_obj_set_size(btn_matrix, 100, 50);

	btn_matrix_group = lv_group_create();
	lv_group_add_obj(btn_matrix_group, btn_matrix);
	lv_indev_set_group(lvgl_input_get_indev(lvgl_keypad), btn_matrix_group);
#endif /* CONFIG_LV_Z_KEYPAD_INPUT */

	count_label = lv_label_create(lv_scr_act());
	lv_obj_align(count_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);


	lv_task_handler();
	display_blanking_off(display_dev);

	//create buttons
	for (uint8_t i = 0; i < 12; i++) {
		button_matrix[i] = new_Button(i);
	}

//label and shows what remote is being used 
	char name[11] = {0};
	generic_title = lv_label_create(lv_scr_act());
	sprintf(name, "%s", remote_names[remote_selection]);
	lv_label_set_text(generic_title, name);
	lv_obj_align(generic_title, LV_ALIGN_TOP_MID, 0, 0);


//change title label / what remote
	lv_obj_t *rightButton;
	lv_obj_t *rightButton_label;

	rightButton = lv_btn_create(lv_scr_act());
	lv_obj_align(rightButton, LV_ALIGN_TOP_RIGHT, 0, 0);
	lv_obj_add_event_cb(rightButton, lv_btn_screen_right, LV_EVENT_CLICKED, NULL);
	rightButton_label = lv_label_create(rightButton);	
	lv_label_set_text(rightButton_label, ">");
	lv_obj_align(rightButton_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_size(rightButton_label, 30, 20);
	

	lv_obj_t *leftButton;
	lv_obj_t *leftButton_label;

	leftButton = lv_btn_create(lv_scr_act());
	lv_obj_align(leftButton, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_add_event_cb(leftButton, lv_btn_screen_left, LV_EVENT_CLICKED, NULL);
	leftButton_label = lv_label_create(leftButton);	
	lv_label_set_text(leftButton_label, "<");
	lv_obj_align(leftButton_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_size(leftButton_label, 30, 20);

	lv_obj_t *signalButton;
	lv_obj_t *signalButton_label;

	signalButton = lv_btn_create(lv_scr_act());
	lv_obj_align(signalButton, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_obj_add_event_cb(signalButton, lv_button_signal_callback, LV_EVENT_CLICKED, NULL);
	signalButton_label = lv_label_create(signalButton);	
	lv_label_set_text(signalButton_label, "New Signal");
	lv_obj_align(signalButton_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_size(signalButton_label, 90, 20);

	if (!device_is_ready(usart_dev))
	{
		printk("USART device not found!");
		return 0;
	}

	while (1) {

		if ((count % 100) == 0U) {
			sprintf(count_str, "%d", count/100U);
			lv_label_set_text(count_label, count_str);
		}

		lv_task_handler(); //progress lv things.
		++count;
		
		

		// uart_poll_out(usart_dev, '7');
		k_sleep(K_MSEC(50));
	}
}


Button new_Button(uint8_t count) {
	Button new_button;

	int x = 65 * (count % 4) + 30;
	int y = 50 * (count / 4) - 10;

	new_button.lv_button = lv_btn_create(lv_scr_act());
	lv_obj_align(new_button.lv_button, LV_ALIGN_LEFT_MID, x, y - 35);
	lv_obj_add_event_cb(new_button.lv_button, lv_btn_click_callback, LV_EVENT_CLICKED, NULL);

	new_button.lv_label = lv_label_create(new_button.lv_button);	
	char name[11] = {0};
	sprintf(name, "%d", count);
	lv_label_set_text(new_button.lv_label, name);
	lv_obj_align(new_button.lv_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_size(new_button.lv_label, 30, 30);	


	return new_button;
}



/*

void display_init(void) {
	display.used = 2;
	display.select = 1;

	display.array[0] = tv_remote_init();
	display.array[1] = ac_remote_init();
}

Remote ac_remote_init(void) {
	Remote ac;
	ac.size = 4;
	ac.used = 2;
	ac.screen = lv_obj_create(NULL);
	lv_scr_load(ac.screen);
	
	for (uint8_t i = 0; i < 12; i++) {
		ac.array[i] = new_Button(i);
	}
	
	//title of remote
	lv_obj_t *ac_remote = lv_label_create(ac.screen);
	lv_label_set_text(ac_remote, "AC");
	lv_obj_align(ac_remote, LV_ALIGN_TOP_MID, 0, 0);

	lv_obj_t *rightButton;
	lv_obj_t *rightButton_label;

	rightButton = lv_btn_create(ac.screen);
	lv_obj_align(rightButton, LV_ALIGN_TOP_RIGHT, 0, 0);
	lv_obj_add_event_cb(rightButton, lv_btn_screen_right, LV_EVENT_CLICKED, NULL);
	rightButton_label = lv_label_create(rightButton);	
	lv_label_set_text(rightButton_label, ">");
	lv_obj_align(rightButton_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_size(rightButton_label, 30, 20);
	

	lv_obj_t *leftButton;
	lv_obj_t *leftButton_label;

	leftButton = lv_btn_create(ac.screen);
	lv_obj_align(leftButton, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_add_event_cb(leftButton, lv_btn_screen_left, LV_EVENT_CLICKED, NULL);
	leftButton_label = lv_label_create(leftButton);	
	lv_label_set_text(leftButton_label, "<");
	lv_obj_align(leftButton_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_size(leftButton_label, 30, 20);

	return ac;
}

Remote tv_remote_init(void) {
	//base remotes initialise
	Remote tv;
	tv.size = 4;
	tv.used = 2;
	tv.screen = lv_obj_create(NULL);
	lv_scr_load(tv.screen);

	for (uint8_t i = 0; i < 12; i++) {
		tv.array[i] = new_Button(i);
	}


	//title of remote
	lv_obj_t *tv_remote = lv_label_create(tv.screen);
	lv_label_set_text(tv_remote, "TV");
	lv_obj_align(tv_remote, LV_ALIGN_TOP_MID, 0, 0);


	lv_obj_t *rightButton;
	lv_obj_t *rightButton_label;

	rightButton = lv_btn_create(tv.screen);
	lv_obj_align(rightButton, LV_ALIGN_TOP_RIGHT, 0, 0);
	lv_obj_add_event_cb(rightButton, lv_btn_screen_right, LV_EVENT_CLICKED, NULL);
	rightButton_label = lv_label_create(rightButton);	
	lv_label_set_text(rightButton_label, ">");
	lv_obj_align(rightButton_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_size(rightButton_label, 30, 20);
	

	lv_obj_t *leftButton;
	lv_obj_t *leftButton_label;

	leftButton = lv_btn_create(tv.screen);
	lv_obj_align(leftButton, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_add_event_cb(leftButton, lv_btn_screen_left, LV_EVENT_CLICKED, NULL);
	leftButton_label = lv_label_create(leftButton);	
	lv_label_set_text(leftButton_label, "<");
	lv_obj_align(leftButton_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_size(leftButton_label, 30, 20);

	return tv;
}

Remote remote_init(void) {
	//add/delete buttons

	Remote newRemote;
	newRemote.size = 4;
	newRemote.used = 0;
	newRemote.screen = lv_obj_create(NULL);
	lv_scr_load(newRemote.screen);

	for (uint8_t i = 0; i < 12; i++) {
		newRemote.array[i] = new_Button(i);
	}
	
	

	//title of remote
	char name[11] = {0};
	lv_obj_t *generic_title = lv_label_create(newRemote.screen);
	sprintf(name, "%d", display.used);
	lv_label_set_text(generic_title, name);
	lv_obj_align(generic_title, LV_ALIGN_TOP_MID, 0, 0);

	//change screen buttons
	lv_obj_t *rightButton;
	lv_obj_t *rightButton_label;

	rightButton = lv_btn_create(newRemote.screen);
	lv_obj_align(rightButton, LV_ALIGN_TOP_RIGHT, 0, 0);
	lv_obj_add_event_cb(rightButton, lv_btn_screen_right, LV_EVENT_CLICKED, NULL);
	rightButton_label = lv_label_create(rightButton);	
	lv_label_set_text(rightButton_label, ">");
	lv_obj_align(rightButton_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_size(rightButton_label, 30, 20);
	

	lv_obj_t *leftButton;
	lv_obj_t *leftButton_label;

	leftButton = lv_btn_create(newRemote.screen);
	lv_obj_align(leftButton, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_add_event_cb(leftButton, lv_btn_screen_left, LV_EVENT_CLICKED, NULL);
	leftButton_label = lv_label_create(leftButton);	
	lv_label_set_text(leftButton_label, "<");
	lv_obj_align(leftButton_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_size(leftButton_label, 30, 20);


	count2_label = lv_label_create(newRemote.screen);
	lv_obj_align(count2_label, LV_ALIGN_RIGHT_MID, 0, 0);

	//add button signal change button in


	return newRemote;

}


void create_screen_init(void) {
	create_screen = lv_obj_create(NULL);
	
		//title of remote
	lv_obj_t *generic_title = lv_label_create(create_screen);
	lv_label_set_text(generic_title, "New Remote");
	lv_obj_align(generic_title, LV_ALIGN_TOP_MID, 0, 0);


	//change screen buttons
	lv_obj_t *rightButton;
	lv_obj_t *rightButton_label;

	rightButton = lv_btn_create(create_screen);
	lv_obj_align(rightButton, LV_ALIGN_TOP_RIGHT, 0, 0);
	lv_obj_add_event_cb(rightButton, lv_btn_screen_right, LV_EVENT_CLICKED, NULL);
	rightButton_label = lv_label_create(rightButton);	
	lv_label_set_text(rightButton_label, ">");
	lv_obj_align(rightButton_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_size(rightButton_label, 30, 20);
	

	lv_obj_t *leftButton;
	lv_obj_t *leftButton_label;

	leftButton = lv_btn_create(create_screen);
	lv_obj_align(leftButton, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_add_event_cb(leftButton, lv_btn_screen_left, LV_EVENT_CLICKED, NULL);
	leftButton_label = lv_label_create(leftButton);	
	lv_label_set_text(leftButton_label, "<");
	lv_obj_align(leftButton_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_size(leftButton_label, 30, 20);
	
	lv_obj_t *createButton;
	lv_obj_t *createButton_label;

	createButton = lv_btn_create(create_screen);
	lv_obj_align(createButton, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_event_cb(createButton, lv_new_screen_callback, LV_EVENT_CLICKED, NULL);
	createButton_label = lv_label_create(createButton);	
	lv_label_set_text(createButton_label, "New Remote");
	lv_obj_align(createButton_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_size(createButton, 150, 100);
}
*/