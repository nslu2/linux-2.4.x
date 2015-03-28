#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/init.h> /* for __init */
#include <asm/uaccess.h> /* for copy_from/to_user */

#include "os.h"
#include "ixp425Eeprom.h"

/* Global lock to r/w acces to the device */
static spinlock_t nvram_lock;

/* usage counter and mode [O_EXCL/FMODE_WRITE] */
static int use_count;
static int open_mode;

enum {
    SEEK_SET = 0,
    SEEK_CUR = 1,
    SEEK_END = 2
};

static long long do_file_llseek(struct file *file, loff_t offset, int from)
{
    switch (from)
    {
    case SEEK_SET:
	break;
    case SEEK_CUR:
	offset += file->f_pos;
	break;
    case SEEK_END:
	offset += IXP425_EEPROM_SIZE;
	break;
    default:
	return -EINVAL;
    }

    if (offset < 0  || offset >= IXP425_EEPROM_SIZE)
	return -EINVAL;

    return file->f_pos = offset;
}

static ssize_t do_file_read(struct file *file,
    char *buf, size_t count, loff_t *offset)
{
    char buffer[IXP425_EEPROM_SIZE];

    if (*offset < 0 || *offset >= IXP425_EEPROM_SIZE)
	return 0;
    
    if (*offset + count >= IXP425_EEPROM_SIZE)
	count = IXP425_EEPROM_SIZE - *offset;

    spin_lock_irq(&nvram_lock);
    ixp425EepromRead(buffer, count, *offset);
    spin_unlock_irq(&nvram_lock);

    if (copy_to_user(buf, buffer, count))
	return -EFAULT;

    *offset += count;

    return count;
}

static ssize_t do_file_write(struct file *file,
    const char *buf, size_t count, loff_t *offset)
{
    char buffer[IXP425_EEPROM_SIZE];

    if (*offset < 0 || *offset >= IXP425_EEPROM_SIZE)
	return 0;

    if (*offset + count >= IXP425_EEPROM_SIZE)
	count = IXP425_EEPROM_SIZE - *offset;

    if (copy_from_user(buffer, buf, count))
	return -EFAULT;

    spin_lock_irq(&nvram_lock);
    ixp425EepromWrite(buffer, count, *offset);
    spin_unlock_irq(&nvram_lock);

    *offset += count;

    return count;
}

static int do_file_open( struct inode *inode, struct file *file )
{
    int res = -EBUSY;

    spin_lock_irq(&nvram_lock);

    if ((use_count && (file->f_flags & O_EXCL)) ||
	(open_mode & O_EXCL) ||
	((file->f_mode & FMODE_WRITE) && (open_mode & FMODE_WRITE)))
    {
	goto Exit;
    }
    
    if (file->f_flags & O_EXCL)
	open_mode |= O_EXCL;

    if (file->f_mode & FMODE_WRITE)
	open_mode |= FMODE_WRITE;

    use_count++;

    res = 0;

Exit:
    spin_unlock_irq(&nvram_lock);

    return res;
}

static int do_file_release( struct inode *inode, struct file *file )
{
    spin_lock_irq(&nvram_lock);

    use_count--;

    if (file->f_flags & O_EXCL)
	open_mode &= ~O_EXCL;

    if (file->f_mode & FMODE_WRITE)
	open_mode &= ~FMODE_WRITE;

    spin_unlock_irq(&nvram_lock);

    return 0;
}


static struct file_operations dev_fops = {
    owner:	THIS_MODULE,

    read:	do_file_read,
    write:	do_file_write,

    open:	do_file_open,
    release:	do_file_release,

    llseek:	do_file_llseek,
};

static struct miscdevice nvram_dev = {
    NVRAM_MINOR,
    "nvram",
    &dev_fops
};


static int __init do_module_init(void)
{
    int ret;

    ret = misc_register(&nvram_dev);
    if (ret)
    {
	printk(KERN_ERR __FUNCTION__
	    ":%d misc_register failed for ixp425 eeprom nvram device\n",
	    __LINE__);
	return ret;
    }

    printk(KERN_INFO "i2c eeprom nvram for ixp425 loaded.\n");

    return 0;
}

static void __exit do_module_exit (void)
{
    misc_deregister( &nvram_dev );
}

module_init(do_module_init);
module_exit(do_module_exit);

MODULE_LICENSE("GPL");

EXPORT_NO_SYMBOLS;
