/*
Copyright (c) 2013 Martin Knoll

Permission is hereby granted, free of charge, to any person obtaining a copy of 
this software and associated documentation files (the "Software"), to deal in 
the Software without restriction, including without limitation the rights to 
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so, 
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all 
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR 
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER 
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN 
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/moduleparam.h>
#include <linux/gpio.h>
#include "font.h"

#define SPI_BUS 0
#define SPI_BUS_CS 11
#define SPI_BUS_SPEED 2000000

#define GPIO_RESET 3
#define GPIO_DC 8

const char this_driver_name[] = "pcd8544";


struct pcd8544_dev {
	dev_t devt;
	struct cdev cdev;
	struct class *class;
	struct spi_device *spi_device;
};

char * tx_buff;

const char init_sequence[] = {0x21, 0xAC, 0x04, 0x14, 0x20, 0x0C};

static struct pcd8544_dev pcd8544_dev;


static inline int pcd8544_spi_write(struct spi_device *spi, void *buf, size_t len)
{
	struct spi_transfer	t = {
			.tx_buf		= buf,
			.len		= len,
			.speed_hz	= SPI_BUS_SPEED,
		};
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(spi, &m);
}

static ssize_t pcd8544_write(struct file *filp, const char __user *buff, size_t len, loff_t *f_pos)
{
    char * user_buff = kmalloc(len, GFP_KERNEL);
    int i;

	memset(user_buff, 0, len);

    if (copy_from_user(user_buff, buff, len)) {
	  len = -EFAULT;
  	}

    gpio_set_value(GPIO_DC, 1);

    for (i = 0; i < len; i++) {
      memcpy(tx_buff, &font[user_buff[i] - 0x20], 7);
      pcd8544_spi_write(pcd8544_dev.spi_device, tx_buff, 7);
    }

    kfree(user_buff);

	return len;
}

static int pcd8544_open(struct inode *inode, struct file *filp)
{	
	return 0;
}

static int __devinit pcd8544_probe(struct spi_device *spi_device)
{
	pcd8544_dev.spi_device = spi_device;
    tx_buff = kmalloc(32, GFP_KERNEL);

    gpio_request(GPIO_RESET, "PCD8544 Reset Pin");
    gpio_direction_output(GPIO_RESET, 0);
    gpio_request(GPIO_DC, "PCD8544 Data/Command Pin");
    gpio_direction_output(GPIO_DC, 0);

    gpio_set_value(GPIO_RESET, 1);

    gpio_set_value(GPIO_DC, 0);
    memcpy(tx_buff, init_sequence, 6);
    pcd8544_spi_write(pcd8544_dev.spi_device, tx_buff, 6);

	return 0;
}

static int __devexit pcd8544_remove(struct spi_device *spi_device)
{
	pcd8544_dev.spi_device = NULL;
    kfree(tx_buff);

    gpio_free(GPIO_RESET);
    gpio_free(GPIO_DC);

	return 0;
}

static int __init add_pcd8544_device_to_bus(void)
{
	struct spi_master *spi_master;
	struct spi_device *spi_device;
	struct device *pdev;
	char buff[64];
	int status = 0;

	spi_master = spi_busnum_to_master(SPI_BUS);
	if (!spi_master) {
		printk(KERN_ALERT "spi_busnum_to_master(%d) returned NULL\n",
			SPI_BUS);
		printk(KERN_ALERT "Missing modprobe omap2_mcspi?\n");
		return -1;
	}

	spi_device = spi_alloc_device(spi_master);
	if (!spi_device) {
		put_device(&spi_master->dev);
		printk(KERN_ALERT "spi_alloc_device() failed\n");
		return -1;
	}

	spi_device->chip_select = SPI_BUS_CS;

	/* Check whether this SPI bus.cs is already claimed */
	snprintf(buff, sizeof(buff), "%s.%u", 
			dev_name(&spi_device->master->dev),
			spi_device->chip_select);

	pdev = bus_find_device_by_name(spi_device->dev.bus, NULL, buff);
 	if (pdev) {
		/* We are not going to use this spi_device, so free it */ 
		spi_dev_put(spi_device);
		
		/* 
		 * There is already a device configured for this bus.cs  
		 * It is okay if it us, otherwise complain and fail.
		 */
		if (pdev->driver && pdev->driver->name && 
				strcmp(this_driver_name, pdev->driver->name)) {
			printk(KERN_ALERT 
				"Driver [%s] already registered for %s\n",
				pdev->driver->name, buff);
			status = -1;
		} 
	} else {
		spi_device->max_speed_hz = SPI_BUS_SPEED;
		spi_device->mode = SPI_MODE_0;
		spi_device->bits_per_word = 8;
		spi_device->irq = -1;
		spi_device->controller_state = NULL;
		spi_device->controller_data = NULL;
		strlcpy(spi_device->modalias, this_driver_name, SPI_NAME_SIZE);
		
		status = spi_add_device(spi_device);		
		if (status < 0) {	
			spi_dev_put(spi_device);
			printk(KERN_ALERT "spi_add_device() failed: %d\n", 
				status);		
		}				
	}

	put_device(&spi_master->dev);

	return status;
}

static struct spi_driver pcd8544_driver = {
	.driver = {
		.name =	this_driver_name,
		.owner = THIS_MODULE,
	},
	.probe = pcd8544_probe,
	.remove = __devexit_p(pcd8544_remove),	
};

static int __init pcd8544_init_spi(void)
{
	int error;

	error = spi_register_driver(&pcd8544_driver);
	if (error < 0) {
		printk(KERN_ALERT "spi_register_driver() failed %d\n", error);
		return error;	}

	error = add_pcd8544_device_to_bus();
	if (error < 0) {
		printk(KERN_ALERT "add_pcd8544_to_bus() failed\n");
		spi_unregister_driver(&pcd8544_driver);
		return error;	
	}

	return 0;
}

static const struct file_operations pcd8544_fops = {
	.owner =	THIS_MODULE,
	.write = 	pcd8544_write,
	.open =		pcd8544_open,	
};

static int __init pcd8544_init_cdev(void)
{
	int error;

	pcd8544_dev.devt = MKDEV(0, 0);

	error = alloc_chrdev_region(&pcd8544_dev.devt, 0, 1, this_driver_name);
	if (error < 0) {
		printk(KERN_ALERT "alloc_chrdev_region() failed: %d \n", 
			error);
		return -1;
	}

	cdev_init(&pcd8544_dev.cdev, &pcd8544_fops);
	pcd8544_dev.cdev.owner = THIS_MODULE;
	
	error = cdev_add(&pcd8544_dev.cdev, pcd8544_dev.devt, 1);
	if (error) {
		printk(KERN_ALERT "cdev_add() failed: %d\n", error);
		unregister_chrdev_region(pcd8544_dev.devt, 1);
		return -1;
	}	

	return 0;
}

static int __init pcd8544_init_class(void)
{
	pcd8544_dev.class = class_create(THIS_MODULE, this_driver_name);

	if (!pcd8544_dev.class) {
		printk(KERN_ALERT "class_create() failed\n");
		return -1;
	}

	if (!device_create(pcd8544_dev.class, NULL, pcd8544_dev.devt, NULL, 	
			this_driver_name)) {
		printk(KERN_ALERT "device_create(..., %s) failed\n",
			this_driver_name);
		class_destroy(pcd8544_dev.class);
		return -1;
	}

	return 0;
}

static int __init pcd8544_init(void)
{
	memset(&pcd8544_dev, ' ', sizeof(pcd8544_dev));

	if (pcd8544_init_cdev() < 0) 
		goto fail_1;
	
	if (pcd8544_init_class() < 0)  
		goto fail_2;

	if (pcd8544_init_spi() < 0) 
		goto fail_3;

	return 0;

fail_3:
	device_destroy(pcd8544_dev.class, pcd8544_dev.devt);
	class_destroy(pcd8544_dev.class);

fail_2:
	cdev_del(&pcd8544_dev.cdev);
	unregister_chrdev_region(pcd8544_dev.devt, 1);

fail_1:
	return -1;
}

static void __exit pcd8544_exit(void)
{
	spi_unregister_device(pcd8544_dev.spi_device);
	spi_unregister_driver(&pcd8544_driver);

	device_destroy(pcd8544_dev.class, pcd8544_dev.devt);
	class_destroy(pcd8544_dev.class);

	cdev_del(&pcd8544_dev.cdev);
	unregister_chrdev_region(pcd8544_dev.devt, 1);
}

module_exit(pcd8544_exit);
module_init(pcd8544_init);

MODULE_AUTHOR("Martin Knoll");
MODULE_DESCRIPTION("PCD8544 driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION("1");

