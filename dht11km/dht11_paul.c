/* dht11.c
 *
 * dht11 - Device driver for reading values from DHT11 temperature and humidity sensor.

 *	insmod ./dht11.ko 
 *	To read the values from the sensor: cat /dev/DHT11
 *
 * Copyright (C) 2015 Wan-Ting CHEN <wanting@gmail.com>
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/mutex.h>				// mutex
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>   				// file_operations
#include <linux/cdev.h>         		// Char Device Registration
#include <linux/gpio.h>                	// gpio control
#include <linux/interrupt.h>        	// interrupt
#include <linux/delay.h>         		// udelay, msleep
#include <linux/device.h>        		// class_create
#include <linux/types.h>            	// dev_t
#include <linux/completion.h>			// completion
#include <linux/uaccess.h>  			// copy_to_user #include <asm/uaccess.h>

#define DHT11_DRIVER_NAME "DHT11"
#define SUCCESS 0
#define BUF_LEN 80		// Max length of the message from the device
#define	DATABIT	40
#define TOTAL_INT_BLOCK	(DATABIT+1)
#define DHT11_DATA_BIT_LOW	26
#define DHT11_DATA_BIT_HIGH	70

// module parameters
static struct timeval lasttv = {0, 0};
static struct timeval tv = {0, 0};
// Forward declarations
static int open_dht11(struct inode *, struct file *);
static int close_dht11(struct inode *, struct file *);
static ssize_t read_dht11(struct file *, char *, size_t, loff_t *);
static void clear_interrupts(void);
static unsigned int timeBit[TOTAL_INT_BLOCK];
struct dht11_bit {
    unsigned int Temperature[2];
    unsigned int Humidity[2];
    unsigned int checksum;
};
// Global variables are declared as static, so are global within the file.
static char msg[BUF_LEN];	// The msg the device will give when asked
static char *msg_Ptr;
static unsigned int nBit = 0;
static unsigned int gpio_pin = 4;		//Default GPIO pin
static dev_t driverno ;		//Default driver number
static struct cdev *gpio_cdev;
static struct class *gpio_class = NULL;
static struct device* gpio_device = NULL;
static struct completion gpio_completion;
static struct dht11_bit dht11;
//struct mutex gpio_mutex;
static DEFINE_MUTEX(gpio_mutex);

//Operations that can be performed on the device
static struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = read_dht11,
	.open = open_dht11,
	.release = close_dht11
};


static unsigned char dht11_decode_byte(int *timing, int threshold)
{
    unsigned char ret = 0;
    int i;

    for (i = 0; i < 8; ++i) {
	ret <<= 1;
	if (timing[i] >= threshold) ++ret;
    }

    return ret;
}

static int dht11_decode(void) {
    int i, bitM = DHT11_DATA_BIT_LOW * 3 / 2, bitN = DHT11_DATA_BIT_HIGH*2/3, wtime=0;

    for (i=0; i<nBit; ++i) {
	pr_info("Bit %d\t%d\n",i, timeBit[i]);
	if (timeBit[i]>bitM && timeBit[i]<bitN) {
	    ++wtime;
	}
    }

    if (wtime>0) {
        pr_info("Poor resolution \n");
	return 1;
    }

    dht11.Humidity[0] = dht11_decode_byte(&timeBit[1], bitM);
    dht11.Humidity[1] = dht11_decode_byte(&timeBit[9], bitM);
    dht11.Temperature[0] = dht11_decode_byte(&timeBit[17], bitM);
    dht11.Temperature[1] = dht11_decode_byte(&timeBit[25], bitM);
    dht11.checksum = dht11_decode_byte(&timeBit[33], bitM);

    if (dht11.Temperature[0]+dht11.Temperature[1]+dht11.Humidity[0]+dht11.Humidity[1] == dht11.checksum) return 0;
    else return 2;
}

// IRQ handler - where the timing takes place
static irqreturn_t irq_handler(int i, void *blah)
{
    int data, signal;
    do_gettimeofday(&tv);
    data = (int) ((tv.tv_sec-lasttv.tv_sec)*1000000 + (tv.tv_usec - lasttv.tv_usec));
    signal = gpio_get_value(gpio_pin);
    lasttv = tv;    //Save last interrupt time
    // use the GPIO signal level
    if (signal==0) {
	timeBit[nBit++] = data;
    }
    if (nBit >= TOTAL_INT_BLOCK) complete(&gpio_completion);
    return IRQ_HANDLED;
}

static int setup_interrupts(void)
{
    int result;
    result = request_irq(gpio_to_irq(gpio_pin), (irq_handler_t) irq_handler, IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, DHT11_DRIVER_NAME, NULL);

    switch (result) {
	case -EBUSY:
	    pr_info(KERN_ERR DHT11_DRIVER_NAME ": IRQ %d is busy\n", gpio_to_irq(gpio_pin));
	    return -EBUSY;
	case -EINVAL:
	    pr_info(KERN_ERR DHT11_DRIVER_NAME ": Bad irq number or handler\n");
	    return -EINVAL;
	default:
	    pr_info(KERN_INFO DHT11_DRIVER_NAME ": Interrupt %04x obtained\n", gpio_to_irq(gpio_pin));
	    break;
    };

    return 0;
}
/**
 * dht11_init_module - inital check and register the device driver
 *
 *
 * Description: Intial the DHT11 module
 * 	-gpio_request()
 * 	-alloc_chrdev_region()
 * 	-cdev_alloc()
 * 	-cdev_init()
 *	-cdev_add()
 *	-class_create
 *	-device_create
 *	-mutex_init
 *	-init_completion
 */

static int __init dht11_init_module(void)
{
    int ret;

    pr_info(KERN_INFO DHT11_DRIVER_NAME " gpio_request \n");
    ret = gpio_request(gpio_pin, DHT11_DRIVER_NAME);

    if(ret < 0) {
        pr_info(KERN_ERR DHT11_DRIVER_NAME " Unable to request GPIOs: %d\n", ret);
        goto exit_gpio_request;
    }

    // Get driver number
    pr_info(KERN_INFO DHT11_DRIVER_NAME " alloc_chrdev_region \n");

    ret=alloc_chrdev_region(&driverno,0,1,DHT11_DRIVER_NAME);
    if (ret) {
        pr_info(KERN_EMERG DHT11_DRIVER_NAME " alloc_chrdev_region failed\n");
        goto exit_gpio_request;
    }

    pr_info(KERN_INFO DHT11_DRIVER_NAME " DRIVER No. of %s is %d\n", DHT11_DRIVER_NAME, MAJOR(driverno));

    pr_info(KERN_INFO DHT11_DRIVER_NAME " cdev_alloc\n");
    gpio_cdev = cdev_alloc();
    if(gpio_cdev == NULL)
    {
        pr_info(KERN_EMERG DHT11_DRIVER_NAME " Cannot alloc cdev\n");
        ret = -ENOMEM;
        goto exit_unregister_chrdev;
    }

    pr_info(KERN_INFO DHT11_DRIVER_NAME " cdev_init\n");
    cdev_init(gpio_cdev,&fops);
    gpio_cdev->owner=THIS_MODULE;

    pr_info(KERN_INFO DHT11_DRIVER_NAME " cdev_add\n");
    ret=cdev_add(gpio_cdev,driverno,1);

    if (ret)
    {
        pr_info(KERN_EMERG DHT11_DRIVER_NAME " cdev_add failed!\n");
        goto exit_cdev;
    }

    pr_info(KERN_INFO DHT11_DRIVER_NAME " Create class \n");
    gpio_class = class_create(THIS_MODULE, DHT11_DRIVER_NAME);

    if (IS_ERR(gpio_class))
    {
        pr_info(KERN_ERR DHT11_DRIVER_NAME " class_create failed\n");
        ret = PTR_ERR(gpio_class);
        goto exit_cdev;
    }

    pr_info(KERN_INFO DHT11_DRIVER_NAME " Create device \n");
    if ((gpio_device=device_create(gpio_class,NULL, driverno, NULL, DHT11_DRIVER_NAME)) == NULL) {
        pr_info(KERN_ERR DHT11_DRIVER_NAME " device_create failed\n");
        ret = -1;
        goto exit_cdev;
    }

    mutex_init(&gpio_mutex);
    init_completion(&gpio_completion);

    return 0;

exit_cdev:
    cdev_del(gpio_cdev);

exit_unregister_chrdev:
    unregister_chrdev_region(driverno, 1);

exit_gpio_request:
    gpio_free(gpio_pin);

    return ret;
}

static void __exit dht11_exit_module(void)
{
    pr_info(KERN_INFO DHT11_DRIVER_NAME " %s\n", __func__);

    device_destroy(gpio_class, driverno);
    class_destroy(gpio_class);
    cdev_del(gpio_cdev);
    unregister_chrdev_region(driverno, 1);
    gpio_free(gpio_pin);

}

// Called when a process wants to read the dht11 "cat /dev/dht11"
static int open_dht11(struct inode *inode, struct file *file)
{
    char result[3];			//To say if the result is trustworthy or not
    int retry = 0, ret;

    if (!mutex_trylock(&gpio_mutex)) {
	pr_info(KERN_ERR DHT11_DRIVER_NAME " another process is accessing the device\n");
  	return -EBUSY;
    }

    reinit_completion(&gpio_completion);

    pr_info(KERN_INFO DHT11_DRIVER_NAME " Start setup (read_dht11)\n");

start_read:

    nBit = 0;
    gpio_direction_output(gpio_pin, 0);
    msleep(20);					// DHT11 needs min 18mS to signal a startup
    gpio_direction_output(gpio_pin, 1);
    udelay(40);                                 // Stay high for a bit before swapping to read mode
    gpio_direction_input(gpio_pin);

    //Start timer to time pulse length
    do_gettimeofday(&lasttv);

    // Set up interrupts
    setup_interrupts();

    //Give the dht11 time to reply
    ret = wait_for_completion_killable_timeout(&gpio_completion,HZ);
    clear_interrupts();
    //Check if the read results are valid. If not then try again!
    if(dht11_decode()==0) sprintf(result, "OK");
    else {
	retry++;
	sprintf(result, "BAD");
	if(retry == 10) goto return_result;		//We tried 5 times so bail out
	ssleep(1);
	goto start_read;
    }

return_result:
    sprintf(msg, "Humidity: %d.%d%%\nTemperature: %d.%dC\nResult:%s\n", dht11.Humidity[0], dht11.Humidity[1], dht11.Temperature[0], dht11.Temperature[1], result);
    msg_Ptr = msg;
    pr_info("strlen is %d", strlen(msg));
    return SUCCESS;
}

// Called when a process closes the device file.
static int close_dht11(struct inode *inode, struct file *file)
{
	mutex_unlock(&gpio_mutex);
	pr_info(KERN_INFO DHT11_DRIVER_NAME ": Device release (close_dht11)\n");

	return 0;
}

// Clear the GPIO edge detect interrupts
static void clear_interrupts(void)
{
    free_irq(gpio_to_irq(gpio_pin), NULL);
}

// Called when a process, which already opened the dev file, attempts to read from it.
static ssize_t read_dht11(struct file *filp,	// see include/linux/fs.h
			   char *buffer,	// buffer to fill with data
			   size_t length,	// length of the buffer
			   loff_t * offset)
{
	if (*msg_Ptr == '\0') return 0;
	if (copy_to_user(buffer, msg_Ptr, strlen(msg)+1)!=0 ) return -EFAULT;
	msg_Ptr += strlen(msg);
	return strlen(msg);
}


//load module
module_init(dht11_init_module);
//exit module
module_exit(dht11_exit_module);

// Command line paramaters for gpio pin and driver major number
module_param(driverno, int, S_IRUGO);
MODULE_PARM_DESC(driverno, "Driver handler major value");

MODULE_DESCRIPTION("DHT11 temperature/humidity sendor driver for Raspberry Pi GPIO.");
MODULE_AUTHOR("Boring Chen <eric20021232000@gmail.com>");
MODULE_LICENSE("Dual BSD/GPL");
