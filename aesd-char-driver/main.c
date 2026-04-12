/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations, fixed size llseek
#include <linux/slab.h>
#include "aesdchar.h"
#include "aesd_ioctl.h"


int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("tps01");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open\n");
    // private data is a void * pointing to aesd_dev
    filp->private_data = container_of(inode->i_cdev, struct aesd_dev, cdev);
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release\n");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_circular_buffer *buffer = &dev->buffer;
    struct aesd_buffer_entry *temp_entry;
    size_t offset_return;

    mutex_lock(&dev->lock);
    PDEBUG("Locked mutex for read\n");

    //get entry
    temp_entry = aesd_circular_buffer_find_entry_offset_for_fpos(buffer, *f_pos, &offset_return);
    PDEBUG("Found entry with offset %ld\n", offset_return);

    //check for completely empty buffer
    if (temp_entry == NULL){
        PDEBUG("Unlocked mutex for read due to empty circular buffer\n");
        mutex_unlock(&dev->lock);
        return 0;
    }


    size_t read_size = temp_entry->size - offset_return;
    if (temp_entry->size > count) {
        read_size = count;
    }
    //entry has char *buffptr and size_t size
    //put it in the user buffer
    PDEBUG("Size of entry %ld\n", temp_entry->size);

    PDEBUG("Copying %s to user space\n", temp_entry->buffptr);
    copy_to_user(buf, temp_entry->buffptr + offset_return, read_size);


    retval = read_size;
    *f_pos = *f_pos + read_size; // move pointer to next entry
    mutex_unlock(&dev->lock);
    PDEBUG("Unlocked mutex for read\n");

    PDEBUG("read %zu bytes with offset %lld\n",count,*f_pos);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    //f_pos does nothing in this implementation.
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld\n",count,*f_pos);
    struct aesd_dev *dev = filp->private_data;
    struct aesd_circular_buffer *buffer = &dev->buffer;

    mutex_lock(&dev->lock);
    //filp->private_data is the buffer set up in open().
    PDEBUG("Locked mutex for write\n");

    if (buf == NULL) {
        mutex_unlock(&dev->lock);
        return retval;
    }

    if (dev->we.buffptr == NULL){ //no data
        PDEBUG("No data in buffer\n");
        dev->we.buffptr = (char *)kmalloc(count,GFP_KERNEL);
        dev->we.size = 0;
    } else { //already data from previous writes
        PDEBUG("Data already in buffer\n");
        dev->we.buffptr = (char *)krealloc(dev->we.buffptr,count+dev->we.size,GFP_KERNEL);
    }

    PDEBUG("Getting user buffer contents\n");
    copy_from_user(dev->we.buffptr + dev->we.size, buf, count);
    dev->we.size = count+dev->we.size;
    retval = count; // originally had = dev->we.size, but due to the above line that would be 
    //greater than the count passed by echo and it would error out. (This function still worked though)
    PDEBUG("our buf: %s\n", dev->we.buffptr);
    //PDEBUG("buf from user space: %s\n", buf);

    //check for newline at the end of the working entry
    if (dev->we.buffptr[dev->we.size - 1] == '\n') { 
        PDEBUG("Newline detected, adding to circular buffer\n");
        aesd_circular_buffer_add_entry(buffer, &dev->we);
        PDEBUG("Added to buffer\n");
        *f_pos = *f_pos + dev->we.size;
        dev->we.buffptr = NULL; // reset the working entry
        dev->we.size = 0;
        //kfree(dev->we.buffptr);
    } 

    //filp->private_data 
    mutex_unlock(&dev->lock);
    PDEBUG("Unlocked mutex for write\n");
    return retval;
}


/*
 * ioctl() based on scull implementation
 */

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{

	int err = 0, tmp;
	int retval = 0;
    

	if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > AESD_IOC_MAGIC) return -ENOTTY;

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok_wrapper(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok_wrapper(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err) return -EFAULT;

	switch(cmd) {
	  case AESDCHAR_IOCSEEKTO:
        struct aesd_dev *dev = filp->private_data;
        struct aesd_circular_buffer *buffer = &dev->buffer;
        struct aesd_seekto *seekto;
        mutex_lock(&dev->lock);
        seekto = (struct aesd_seekto *)kmalloc(sizeof(struct aesd_seekto), GFP_KERNEL);
        copy_from_user(seekto, &arg, sizeof(struct aesd_seekto));
        PDEBUG("our seekto cmd: %d, offset: %d\n", seekto->write_cmd, seekto->write_cmd_offset);


        kfree(seekto);
        mutex_unlock(&dev->lock);
		break;
	  default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	return retval;

}


loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
	struct aesd_dev *dev = filp->private_data;
    struct aesd_circular_buffer *buffer = &dev->buffer;
    struct aesd_buffer_entry *temp_entry;
	loff_t newpos;
    size_t offset_return;
    loff_t f_pos = 0;
    size_t total_size = 0;

    //lock device
    mutex_lock(&dev->lock);
    PDEBUG("Locked mutex for llseek\n");
    //get size of each entry
    int i; // declrating i up here because of c99 features being unsupported
    for (i=0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        temp_entry = aesd_circular_buffer_find_entry_offset_for_fpos(buffer, f_pos, &offset_return);
        if (temp_entry) {
            PDEBUG("Found entry with offset %ld\n", offset_return);
            total_size += temp_entry->size;
            f_pos += temp_entry->size;
            PDEBUG("New total size and offset %ld, %ld\n", total_size, f_pos);
        } else {
            return -EINVAL; // seek too far out, the entry was null
        }
    }
    //do the thing
	newpos = fixed_size_llseek(filp, off, whence, total_size);
    //unlock
    mutex_unlock(&dev->lock);
    PDEBUG("unlocked mutex after llseek\n");
	if (newpos < 0) return -EINVAL;
	//filp->f_pos = newpos; I assume fixed size llseek does this internally
    PDEBUG("New file position: %ld\n", newpos);
	return newpos;
}



struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev\n", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));
    mutex_init(&aesd_device.lock);
    PDEBUG("Initialized buffer lock\n");
    aesd_circular_buffer_init(&aesd_device.buffer);
    PDEBUG("Initialized circular buffer\n");
    result = aesd_setup_cdev(&aesd_device);
    PDEBUG("Created char device\n");

    if( result ) {
        unregister_chrdev_region(dev, 1);
        return result;
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    PDEBUG("Deleting char device\n");
    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    if (aesd_device.we.buffptr != NULL) {
        kfree(aesd_device.we.buffptr);
    }
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
