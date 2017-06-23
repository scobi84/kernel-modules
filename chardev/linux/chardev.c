/*
 * chardev - basic character device kernel module.
 * Copyright (c) 2016, Sebastien Vincent
 *
 * Distributed under the terms of the BSD 3-clause License.
 * See the LICENSE file for details.
 */

/**
 * \file chardev.c
 * \brief Basic character device module for GNU/Linux.
 * \author Sebastien Vincent
 * \date 2016-2017
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>

#include <asm/uaccess.h>

/* forward declarations */
static int chardev_open(struct inode* inodep, struct file* filep);
static int chardev_release(struct inode* inodep, struct file* filep);
static ssize_t chardev_read(struct file* filep, char* __user u_buffer,
        size_t len, loff_t* offset);
static ssize_t chardev_write(struct file* filep, const char* __user u_buffer,
        size_t len, loff_t* offset);

/**
 * \brief Class name.
 */
static const char* const CLASS_NAME = "test";

/**
 * \brief Name of the module (configuration parameter).
 */
static char* name = "chardev";

/**
 * \brief Cookie value (configuration parameter).
 */
static int cookie = 0;

/**
 * \brief Major number.
 */
static int major = 0;

/**
 * \brief The device class.
 */
static struct class* chardev_class = NULL;

/**
 * \brief The device numbers.
 */
static dev_t chardev_dev = MKDEV(0, 0);

/**
 * \brief The device.
 */
static struct cdev chardev_cdev;

/**
 * \brief Message in kernel side for the device.
 */
static char g_message[1024] = {0};

/**
 * \brief Size of g_message stored in kernel side.
 */
static size_t g_message_size = 0;

/**
 * \brief Number of times device is opened.
 */
static size_t g_number_open = 0;

/**
 * \brief Mutex to have only one process to open and use device.
 */
static DEFINE_MUTEX(mutex_chardev);

/**
 * \brief File operations.
 */
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = chardev_open,
    .release = chardev_release,
    .read = chardev_read,
    .write = chardev_write,
};

/**
 * \brief Open callback for character device.
 * \param inodep inode.
 * \param filep file.
 * \return 0 if success, negative value otherwise.
 */
static int chardev_open(struct inode* inodep, struct file* filep)
{
    if(!mutex_trylock(&mutex_chardev))
    {
        printk(KERN_ALERT "%s.%d mutex already locked!\n", name, cookie);
        return -EBUSY;
    }
    g_number_open++;
    printk(KERN_INFO "%s.%d: open (%zu)\n", name, cookie, g_number_open);
    return 0;
}

/**
 * \brief Release callback for character device.
 * \param inodep inode.
 * \param filep file.
 * \return 0 if success, negative value otherwise.
 */
static int chardev_release(struct inode* inodep, struct file* filep)
{
    g_number_open--;
    printk(KERN_INFO "%s.%d: release (%zu)\n", name, cookie, g_number_open);
    mutex_unlock(&mutex_chardev);
    return 0;
}

/**
 * \brief Read callback for character device.
 * \param filep file.
 * \param u_buffer buffer to fill.
 * \param len length to read.
 * \param offset offset of the buffer.
 * \return number of characters read, or negative value if failure.
 */
static ssize_t chardev_read(struct file* filep, char* __user u_buffer,
        size_t len, loff_t* offset)
{
    int err = 0;
    ssize_t len_msg = 0;

    printk(KERN_INFO "%s.%d: wants to read %zu bytes from offset %lld\n", name,
            cookie, len, *offset);

    /* calculate buffer size left to copy */
    len_msg = g_message_size - *offset;

    if(len_msg == 0)
    {
        /* EOF */
        return 0;
    }
    else if(len_msg > len)
    {
        len_msg = len;
    }
    else if(len_msg < 0)
    {
        return -EINVAL;
    }

    err = copy_to_user(u_buffer, g_message + *offset, len_msg);

    if(err == 0)
    {
        /* success */
        printk(KERN_DEBUG "%s.%d sent %zu characters to user\n", name, cookie,
                len_msg);

        *offset += len_msg;
        return len_msg;
    }
    else
    {
        printk(KERN_DEBUG "%s.%d failed to send %zu characters to user\n",
                name, cookie, len_msg);
        return -EFAULT;
    }
}

/**
 * \brief Write callback for character device.
 * \param filep file.
 * \param u_buffer buffer that contains data to write.
 * \param len length to write.
 * \param offset offset of the buffer.
 * \return number of characters written, or negative value if failure.
 */
static ssize_t chardev_write(struct file* filep, const char* __user u_buffer,
        size_t len, loff_t* offset)
{
    ssize_t len_msg = len + *offset;

    printk(KERN_INFO "%s.%d: wants to write %zu bytes from %lld offset\n", name,
            cookie, len, *offset);

    if(len_msg > sizeof(g_message))
    {
        return -EFBIG;
    }

    if(copy_from_user(g_message + *offset, u_buffer, len) != 0)
    {
        g_message_size = 0;
        return -EFAULT;
    }

    if(*offset == 0)
    {
        g_message_size = 0;
    }
    
    g_message_size += len;
    *offset += len;

    printk(KERN_INFO "%s.%d received %zu characters from user\n", name, cookie,
        len);
    return len;
}

/**
 * \brief Module initialization.
 *
 * Set up stuff when module is added.
 * \return 0 if success, negative value otherwise.
 */
static int __init chardev_init(void)
{
    int ret = 0;

    printk(KERN_INFO "%s.%d: initialization\n", name, cookie);

    /* register major number */
    if(major == 0)
    {
        ret = alloc_chrdev_region(&chardev_dev, 0, 1, THIS_MODULE->name);
    }
    else
    {
        chardev_dev = MKDEV(major, 0);
        ret = register_chrdev_region(chardev_dev, 1, THIS_MODULE->name);
    }

    if(ret < 0)
    {
        printk(KERN_ALERT "%s.%d failed to register a major number\n", name,
            cookie);
        return ret;
    }

    printk(KERN_INFO "%s.%d registered correctly a major number (%d)\n", name,
            cookie, major);

    /* register class */
    chardev_class = class_create(THIS_MODULE, CLASS_NAME);
    if(IS_ERR(chardev_class))
    {
        unregister_chrdev(major, THIS_MODULE->name);
        printk(KERN_ALERT "%s.%d failed to register device class\n", name,
            cookie);
        return PTR_ERR(chardev_class);
    }
    printk(KERN_INFO "%s.%d device class registered correctly\n", name, cookie);

    /* register device */
    if(device_create(chardev_class, NULL, chardev_dev, NULL,
                    THIS_MODULE->name) == NULL)
    {
        class_destroy(chardev_class);
        unregister_chrdev(major, THIS_MODULE->name);
        printk(KERN_ALERT "%s.%d failed to create the device\n", name, cookie);
        return PTR_ERR(NULL);
    }
    
    cdev_init(&chardev_cdev, &fops);
    ret = cdev_add(&chardev_cdev, chardev_dev, 1);

    if(ret == 0)
    {
        printk(KERN_INFO "%s.%d device created correctly\n", name, cookie);
    }
    return ret;
}

/**
 * \brief Module finalization.
 *
 * Clean up stuff when module is removed.
 */
static void __exit chardev_exit(void)
{
    cdev_del(&chardev_cdev);
    device_destroy(chardev_class, chardev_dev);
    class_destroy(chardev_class);
    unregister_chrdev_region(chardev_dev, 1);
    mutex_destroy(&mutex_chardev);
    printk(KERN_INFO "%s.%d: exit\n", name, cookie);
}

/* entry/exit points of the module */
module_init(chardev_init);
module_exit(chardev_exit);

/* configuration parameters */
module_param(name, charp, S_IRUGO);
MODULE_PARM_DESC(name, "Name of the module");
module_param(cookie, int, (S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR));
MODULE_PARM_DESC(cookie, "Cookie value");

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Sebastien Vincent");
MODULE_DESCRIPTION("character device module");
MODULE_VERSION("0.1");

