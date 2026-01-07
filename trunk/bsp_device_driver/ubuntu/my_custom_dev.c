#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/poll.h>

#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/jiffies.h>

#include <linux/errno.h>
#include <linux/mutex.h>

// #include "my_custom_ioctl.h"
// #include "my_custom_typedef.h"


#define DEBUG

#define DEVICE_NAME "my_custom_device_driver"
#define CLASS_NAME  "my_custom_device_class"
// ROT_ENC
#define GPIO_ROT_ENC_S1		17
#define GPIO_ROT_ENC_S2		27
#define GPIO_ROT_ENC_KY		22
// RTC DS1302
#define GPIO_DS1302_DAT		5
#define GPIO_DS1302_CLK		6
#define GPIO_DS1302_RST		13

// #define LED0_GPIO	5
// #define LED1_GPIO	6
// #define LED2_GPIO	7
// #define LED3_GPIO	8
// #define LED4_GPIO	9
// #define LED5_GPIO	10
// #define LED6_GPIO	11
// #define LED7_GPIO	12

#define DEBOUNCE_MS 10	
#define DS1302_TIMER_MS	870
// #define BLINK_TIMER_MS	500

MODULE_LICENSE("GPL");
MODULE_AUTHOR("joshua");
MODULE_DESCRIPTION("my custom device driver");
static dev_t dev_num;
static struct cdev my_custom_cdev;
static struct class *my_custom_class;

static int interrupt_num_s1;	// int number of s1 gpio
static int interrupt_num_ky;
static int rotary_value = 0;	// record rotary encoder value
static int key_value = 0;
static unsigned long last_interrupt_time = 0;	// record debouncing time
static int data_update_finish_s1 = 0;	// data ready
static int data_update_finish_ky = 0;
// MACRO DECLARE_WAIT_QUEUE_HEAD for make Wait QUEUE
static DECLARE_WAIT_QUEUE_HEAD(my_custom_wait_queue);
// struct wait_queue_head rotary_wait_queue = {

// write command address only ( read = write + 1 )
#define ADDR_SECONDS	0x80
#define ADDR_MINUTES	0x82
#define ADDR_HOURS		0x84
#define ADDR_DATE		0x86
#define ADDR_MONTH		0x88
#define ADDR_DAYOFWEEK	0x8A
#define ADDR_YEAR		0x8C
#define ADDR_WRITEPROTECTED	0x8E

typedef struct
{
	uint8_t	seconds;
	uint8_t	minutes;
	uint8_t	hours;
	uint8_t	date;
	uint8_t	month;
	uint8_t	dayofweek;	// 1 : SUN , 2 : MON
	uint8_t	year;
	uint8_t ampm;		// 1 : PM , 2 : AM
	uint8_t	hourmode;	// 0 : 24hr, 1 : 12hr
}t_ds1302;

static inline uint8_t convert_dec_to_bcd(uint8_t _in)
{
    return (_in / 10) * 16 + (_in % 10);
}

static inline uint8_t convert_bcd_to_dec(uint8_t _in)
{
	return (_in >> 4) * 10 + (_in & 0x0f);
}

// timer set
static struct timer_list ds1302_timer;
static t_ds1302 ds_time;
static DEFINE_MUTEX(timeMutex);

static void ds1302_clock(void)
{
	gpio_set_value(GPIO_DS1302_CLK, 1);
	gpio_set_value(GPIO_DS1302_CLK, 0);
}

static void ds1302_tx(uint8_t _tx)
{
	// 1. set ds1302_IO to output
	gpio_direction_output(GPIO_DS1302_DAT, 0);

	gpio_set_value(GPIO_DS1302_CLK, 0);

	// 2. transmit data by 1bit LSB first
	for (int i = 0; i < 8; i++)
	{
		if ((_tx & (1 << i)))
			gpio_set_value(GPIO_DS1302_DAT, 1);
		else
			gpio_set_value(GPIO_DS1302_DAT, 0);

		// 3. CLK HIGH -> LOW
		ds1302_clock();
	}
}

static void ds1302_rx(uint8_t* _prx)
{
	*_prx = 0;

	gpio_direction_input(GPIO_DS1302_DAT);

	for (int i = 0; i < 8; i++)
	{
		// 1. read 1bit
		// 2. store temp
		if (gpio_get_value(GPIO_DS1302_DAT))
			(*_prx) |= (1 << i);

		// 3. CLK
		if (i < 7)
			ds1302_clock();
	}
}

static void ds1302_write(uint8_t _command, uint8_t _data)
{
	// 1. CE HIGH
	gpio_set_value(GPIO_DS1302_RST, 1);

	// 2. transmit command
	ds1302_tx(_command);

	// 3. transmit data
	ds1302_tx(_data);

	// 4. CE LOW
	gpio_set_value(GPIO_DS1302_RST, 0);
}

static uint8_t ds1302_read(uint8_t _command)
{
	uint8_t ret = 0;

	// 1. CE HIGH
	gpio_set_value(GPIO_DS1302_RST, 1);

	// 2. transmit command
	ds1302_tx(_command);

	// 3. receive data
	ds1302_rx(&ret);

	// 4. CE LOW
	gpio_set_value(GPIO_DS1302_RST, 0);

	// 5. bcd to dec
	return convert_bcd_to_dec(ret);
}

static void ds1302_write_datetime(void)
{
	ds1302_write(ADDR_SECONDS,	convert_dec_to_bcd(ds_time.seconds) & 0x7f);
	ds1302_write(ADDR_MINUTES,	convert_dec_to_bcd(ds_time.minutes));
	ds1302_write(ADDR_HOURS,	convert_dec_to_bcd(ds_time.hours));
	ds1302_write(ADDR_DATE,		convert_dec_to_bcd(ds_time.date));
	ds1302_write(ADDR_MONTH,	convert_dec_to_bcd(ds_time.month));
	ds1302_write(ADDR_DAYOFWEEK,	convert_dec_to_bcd(ds_time.dayofweek));
	ds1302_write(ADDR_YEAR,		convert_dec_to_bcd(ds_time.year));
}

static void ds1302_read_datetime(void)
{
	ds_time.seconds = ds1302_read(ADDR_SECONDS + 1);
	ds_time.minutes = ds1302_read(ADDR_MINUTES + 1);
	ds_time.hours = ds1302_read(ADDR_HOURS + 1);
	ds_time.date = ds1302_read(ADDR_DATE + 1);
	ds_time.month = ds1302_read(ADDR_MONTH + 1);
	ds_time.dayofweek = ds1302_read(ADDR_DAYOFWEEK + 1);
	ds_time.year = ds1302_read(ADDR_YEAR + 1);
}

static void ds1302_timer_fn(struct timer_list *t)
{
	if(mutex_trylock(&timeMutex) != 0)
	{
		ds1302_read_datetime();
		
		mutex_unlock(&timeMutex);
	}

    mod_timer(&ds1302_timer, jiffies + msecs_to_jiffies(DS1302_TIMER_MS));
}

// -- interrupt handler --
static irqreturn_t my_custom_int_handler(int irq, void *dev_id)
{
	unsigned long current_time = jiffies;
	unsigned long debounce_jiffies = msecs_to_jiffies(DEBOUNCE_MS);

	if(time_before(current_time, last_interrupt_time + debounce_jiffies)){
		return IRQ_HANDLED;
	}
	last_interrupt_time = current_time;

	if (irq == interrupt_num_s1)
	{
		int val_s1 = gpio_get_value(GPIO_ROT_ENC_S1);
		int val_s2 = gpio_get_value(GPIO_ROT_ENC_S2);
		if(val_s1 == 0) {
			if(val_s2 == 0) {
				rotary_value = 2;
			}
			else {
				rotary_value = 1;
			}
		}
		// alert data update signal to user program
		data_update_finish_s1 = 1;
	}
	else if (irq == interrupt_num_ky)
	{
		int val_key = gpio_get_value(GPIO_ROT_ENC_KY);
		if (val_key == 0)
			key_value = 1;

		data_update_finish_ky = 1;
	}
	wake_up_interruptible(&my_custom_wait_queue); 	// linux/wait.h

	return IRQ_HANDLED;
}

// --- read function ---
static ssize_t my_custom_read(struct file *file, char __user *user_buff, size_t count, loff_t *ppos) {
	char buffer[64];
	int len;

	// if (data_update_finish_s1 == 0 && data_update_finish_ky == 0)
	// {
    //     if (file->f_flags & O_NONBLOCK)
    //         return -EAGAIN;
		
	// 	// blocking i/o: wait while update data
	// 	wait_event_interruptible(my_custom_wait_queue, data_update_finish_s1 != 0 || data_update_finish_ky != 0);		
	// }
	data_update_finish_s1 = 0;
	data_update_finish_ky = 0;
	len = snprintf(buffer, sizeof(buffer), "%02d%02d%02d%02d%02d%02d%01d%01d\n", ds_time.year, ds_time.month, ds_time.date, ds_time.hours, ds_time.minutes, ds_time.seconds, rotary_value, key_value);
	// copy user space
	if(copy_to_user(user_buff, buffer, len)) {
		return -EFAULT;
	}
	
	// "yymmddhhMMssxx\n"
#ifdef DEBUG
	printk(KERN_INFO "  [read] buf : %s\n", buffer);
#endif

	rotary_value = 0;
	key_value = 0;

	return len;
}

static ssize_t my_custom_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
#ifdef DEBUG
	printk(KERN_INFO "  [write]");
#endif

	char buffer[64];
	int len = min(count, sizeof(buffer) - 1);

	int ret = copy_from_user(buffer,buf,len);
	if(ret<0)
		return ret;

	buffer[len] = '\0';

	// "yymmddhhMMss\n"
#ifdef DEBUG
	printk(KERN_INFO "  [write] buf : %s\n", buffer);
#endif
	
	ds_time.year	= (buffer[ 0] - '0') * 10 + (buffer[ 1] - '0');
	ds_time.month	= (buffer[ 2] - '0') * 10 + (buffer[ 3] - '0');
	ds_time.date	= (buffer[ 4] - '0') * 10 + (buffer[ 5] - '0');
	ds_time.hours	= (buffer[ 6] - '0') * 10 + (buffer[ 7] - '0');
	ds_time.minutes	= (buffer[ 8] - '0') * 10 + (buffer[ 9] - '0');
	ds_time.seconds	= (buffer[10] - '0') * 10 + (buffer[11] - '0');

	ds1302_write_datetime();

	return count;
}

static __poll_t my_custom_poll(struct file * filp, struct poll_table_struct *wait)
{
	unsigned int mask = 0;	
	
	poll_wait(filp,&my_custom_wait_queue, wait);

	if(data_update_finish_s1 != 0 || data_update_finish_ky != 0)
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static struct file_operations fops = {
//	.owner	= THIS_MODULE,
	.read	= my_custom_read,
	.write	= my_custom_write,
	.poll	= my_custom_poll,
};

static int __init rotary_driver_init(void)
{
	int ret;

	printk(KERN_INFO "=== my custom device driver initializing ===\n");
	// 1. alloc device number
	if((ret = alloc_chrdev_region(&dev_num, 230, 1, DEVICE_NAME)) == -1) {
		printk(KERN_ERR "ERROR: alloc_chrdev_regin ....\n");
		return ret;
	}
	// 2. register char device
	cdev_init(&my_custom_cdev, &fops);
	if((ret = cdev_add(&my_custom_cdev, dev_num, 1)) == -1) {
		printk(KERN_ERR "ERROR: cdev_add ....\n");
		unregister_chrdev_region(dev_num, 1);
		return ret;
	}
	// 3. create class & create device
	my_custom_class = class_create(THIS_MODULE, CLASS_NAME);
	if(IS_ERR(my_custom_class)) {
		cdev_del(&my_custom_cdev);
		unregister_chrdev_region(dev_num, 1);
		return PTR_ERR(my_custom_class);
	}
	device_create(my_custom_class, NULL, dev_num, NULL, DEVICE_NAME);
	// 4. request gpio
	// LED output mode
	if(gpio_request(GPIO_ROT_ENC_S1, "my_custom")
		|| gpio_request(GPIO_ROT_ENC_S2, "my_custom")
		|| gpio_request(GPIO_ROT_ENC_KY, "my_custom")) {
		printk(KERN_ERR "ERROR: gpio_rot_enc_request...\n");
		return -1;			
	}
	if(gpio_request(GPIO_DS1302_DAT, "my_custom")
		|| gpio_request(GPIO_DS1302_CLK, "my_custom")
		|| gpio_request(GPIO_DS1302_RST, "my_custom")) {
		printk(KERN_ERR "ERROR: gpio_ds1302_request...\n");
		return -1;			
	}
	// set input mode
	gpio_direction_input(GPIO_ROT_ENC_S1);
	gpio_direction_input(GPIO_ROT_ENC_S2);
	gpio_direction_input(GPIO_ROT_ENC_KY);
	// set output mode
	gpio_direction_output(GPIO_DS1302_DAT, 0);
	gpio_direction_output(GPIO_DS1302_CLK, 0);
	gpio_direction_output(GPIO_DS1302_RST, 0);

	// 5. assign gpio to irq
	interrupt_num_s1 = gpio_to_irq(GPIO_ROT_ENC_S1);
	ret = request_irq(interrupt_num_s1, my_custom_int_handler, IRQF_TRIGGER_FALLING, "my_custom_irq_S1", NULL);
	if(ret){
		printk(KERN_ERR "ERROR: request_irq...\n");
		return ret;
	}
	interrupt_num_ky = gpio_to_irq(GPIO_ROT_ENC_KY);
	ret = request_irq(interrupt_num_ky, my_custom_int_handler, IRQF_TRIGGER_FALLING, "my_custom_irq_KY", NULL);
	if(ret){
		printk(KERN_ERR "ERROR: request_irq...\n");
		return ret;
	}
	printk(KERN_INFO "my custom driver init success...\n");

	// 6. timer set
	timer_setup(&ds1302_timer, ds1302_timer_fn, 0);
	mod_timer(&ds1302_timer, jiffies + msecs_to_jiffies(DS1302_TIMER_MS));
	return 0;
}

static void __exit rotary_driver_exit(void)
{
	del_timer_sync(&ds1302_timer);
	free_irq(interrupt_num_s1, NULL);
	free_irq(interrupt_num_ky, NULL);
	gpio_free(GPIO_ROT_ENC_S1); gpio_free(GPIO_ROT_ENC_S2); gpio_free(GPIO_ROT_ENC_KY);
	gpio_free(GPIO_DS1302_DAT); gpio_free(GPIO_DS1302_CLK); gpio_free(GPIO_DS1302_RST);
	device_destroy(my_custom_class, dev_num);
	class_destroy(my_custom_class);
	cdev_del(&my_custom_cdev);
	unregister_chrdev_region(dev_num, 1);
	printk(KERN_INFO "my_custom_driver_exit !!\n");
}
module_init(rotary_driver_init);
module_exit(rotary_driver_exit);
