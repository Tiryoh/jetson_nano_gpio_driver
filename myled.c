#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

MODULE_AUTHOR("Tiryoh");
MODULE_DESCRIPTION("A simple driver for controlling Jetson Nano");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

#define gpioLED 13 // PIN22
#define gpioSW 17  // PIN21

#define MAX_BUFLEN 64
#define DEBOUNCE_TIME 50

#define DEV_MAJOR 0
#define DEV_MINOR 0

#define REG_GPIO_NAME "Jetson Nano GPIO"

#define NUM_DEV_LED 1
#define NUM_DEV_SWITCH 1
#define NUM_DEV_TOTAL (NUM_DEV_LED + NUM_DEV_SWITCH)

#define DEVNAME_LED "myled"
#define DEVNAME_SWITCH "myswitch"

static struct cdev *cdev_array = NULL;
static struct class *class_led = NULL;
static struct class *class_switch = NULL;

static volatile int cdev_index = 0;
static volatile int open_counter = 0;

static int _major_led = DEV_MAJOR;
static int _minor_led = DEV_MINOR;

static int _major_switch = DEV_MAJOR;
static int _minor_switch = DEV_MINOR;

static ssize_t led_write(struct file *flip, const char *buf, size_t count,
			 loff_t *pos)
{
	char cval;

	if (count > 0) {
		if (copy_from_user(&cval, buf, sizeof(char))) {
			return -EFAULT;
		}
		switch (cval) {
		case '1':
			gpio_set_value(gpioLED, 1);
			break;
		case '0':
			gpio_set_value(gpioLED, 0);
			break;
		}
		return sizeof(char);
	}
	return 0;
}

static ssize_t sw_read(struct file *filep, char __user *buf, size_t count,
		       loff_t *pos)
{
	unsigned int ret = 0;
	int len;
	unsigned char sw_buf[MAX_BUFLEN];
	static int buflen = 0;

	if (*pos > 0)
		return 0; // End of file

	ret = gpio_get_value(gpioSW);
	sprintf(sw_buf, "%d\n", ret);

	buflen = strlen(sw_buf);
	count = buflen;
	len = buflen;

	if (copy_to_user((void *)buf, &sw_buf, count)) {
		printk(KERN_INFO "err read buffer from ret  %d\n", ret);
		printk(KERN_INFO "err read buffer from %s\n", sw_buf);
		printk(KERN_INFO "err sample_char_read size(%ld)\n", count);
		printk(KERN_INFO "sample_char_read size err(%d)\n", -EFAULT);
		return 0;
	}
	*pos += count;
	buflen = 0;
	return count;
}

static int dev_open(struct inode *inode, struct file *filep)
{
	int *minor = (int *)kmalloc(sizeof(int), GFP_KERNEL);
	// int major = MAJOR(inode->i_rdev);
	*minor = MINOR(inode->i_rdev);
	// printk(KERN_INFO "open request major:%d minor: %d \n", major,
	// *minor);
	filep->private_data = (void *)minor;
	open_counter++;
	return 0;
}

static int dev_release(struct inode *inode, struct file *filep)
{
	kfree(filep->private_data);
	open_counter--;
	return 0;
}

static struct file_operations sw_fops = {
    .open = dev_open,
    .read = sw_read,
    .release = dev_release,
};

static struct file_operations led_fops = {
    .open = dev_open,
    .release = dev_release,
    .write = led_write,
};

static int switch_register_dev(void)
{
	int retval;
	dev_t dev;
	int i;
	dev_t devno;

	retval = alloc_chrdev_region(&dev, DEV_MINOR, NUM_DEV_SWITCH,
				     DEVNAME_SWITCH);

	if (retval < 0) {
		printk(KERN_ERR "alloc_chrdev_region failed.\n");
		return retval;
	}
	_major_switch = MAJOR(dev);

	class_switch = class_create(THIS_MODULE, DEVNAME_SWITCH);
	if (IS_ERR(class_switch)) {
		return PTR_ERR(class_switch);
	}

	for (i = 0; i < NUM_DEV_SWITCH; i++) {
		devno = MKDEV(_major_switch, _minor_switch + i);
		cdev_init(&(cdev_array[cdev_index]), &sw_fops);
		cdev_array[cdev_index].owner = THIS_MODULE;

		if (cdev_add(&(cdev_array[cdev_index]), devno, 1) < 0) {
			printk(KERN_ERR "cdev_add failed minor = %d\n",
			       _minor_switch + i);
		} else {
			device_create(class_switch, NULL, devno, NULL,
				      DEVNAME_SWITCH "%u", _minor_switch + i);
		}
		cdev_index++;
	}
	return 0;
}

static int led_register_dev(void)
{
	int retval;
	dev_t dev;
	dev_t devno;
	int i;

	retval = alloc_chrdev_region(&dev, DEV_MINOR, NUM_DEV_LED, DEVNAME_LED);

	if (retval < 0) {
		printk(KERN_ERR "alloc_chrdev_region failed.\n");
		return retval;
	}
	_major_led = MAJOR(dev);

	class_led = class_create(THIS_MODULE, DEVNAME_LED);
	if (IS_ERR(class_led)) {
		return PTR_ERR(class_led);
	}

	for (i = 0; i < NUM_DEV_LED; i++) {
		devno = MKDEV(_major_led, _minor_led + i);

		cdev_init(&(cdev_array[cdev_index]), &led_fops);
		cdev_array[cdev_index].owner = THIS_MODULE;
		if (cdev_add(&(cdev_array[cdev_index]), devno, 1) < 0) {
			printk(KERN_ERR "cdev_add failed minor = %d\n",
			       _minor_led + i);
		} else {
			device_create(class_led, NULL, devno, NULL,
				      DEVNAME_LED "%u", _minor_led + i);
		}
		cdev_index++;
	}
	return 0;
}

static int __init init_mod(void)
{
	int retval = 0;
	size_t size;

	printk(KERN_INFO "loading %d devices...\n", NUM_DEV_TOTAL);

	if (!gpio_is_valid(gpioLED)) {
		printk(KERN_INFO "GPIO: invalid LED GPIO\n");
		return -ENODEV;
	}
	if (!gpio_is_valid(gpioSW)) {
		printk(KERN_INFO "GPIO: invalid SW GPIO\n");
		return -ENODEV;
	}
	retval = gpio_request(gpioLED, "sysfs");
	retval = gpio_direction_output(gpioLED, 0);
	retval = gpio_export(gpioLED, false);

	retval = gpio_request(gpioSW, "sysfs");
	retval = gpio_direction_input(gpioSW);
	retval = gpio_set_debounce(gpioSW, DEBOUNCE_TIME);
	retval = gpio_export(gpioSW, false);

	if (retval != 0) {
		printk(KERN_ALERT "Can not use GPIO registers.\n");
		return -EBUSY;
	}

	size = sizeof(struct cdev) * NUM_DEV_TOTAL;
	cdev_array = (struct cdev *)kmalloc(size, GFP_KERNEL);

	retval = led_register_dev();
	if (retval != 0) {
		printk(KERN_ALERT " switch driver register failed.\n");
		return retval;
	}
	retval = switch_register_dev();
	if (retval != 0) {
		printk(KERN_ALERT " switch driver register failed.\n");
		return retval;
	}
	return 0;
}

static void __exit cleanup_mod(void)
{
	int i;
	dev_t devno;
	dev_t devno_top;

	gpio_set_value(gpioLED, 0);
	gpio_unexport(gpioLED);
	gpio_free(gpioLED);
	gpio_free(gpioSW);

	for (i = 0; i < NUM_DEV_TOTAL; i++) {
		cdev_del(&(cdev_array[i]));
	}

	devno_top = MKDEV(_major_led, _minor_led);
	for (i = 0; i < NUM_DEV_LED; i++) {
		devno = MKDEV(_major_led, _minor_led + i);
		device_destroy(class_led, devno);
	}
	unregister_chrdev_region(devno_top, NUM_DEV_LED);

	devno_top = MKDEV(_major_switch, _minor_switch);
	for (i = 0; i < NUM_DEV_SWITCH; i++) {
		devno = MKDEV(_major_switch, _minor_switch + i);
		device_destroy(class_switch, devno);
	}
	unregister_chrdev_region(devno_top, NUM_DEV_SWITCH);

	class_destroy(class_led);
	class_destroy(class_switch);

	kfree(cdev_array);
	printk("module being removed at %lu\n", jiffies);
}

module_init(init_mod);
module_exit(cleanup_mod);
