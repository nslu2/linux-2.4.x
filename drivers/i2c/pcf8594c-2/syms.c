#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include "os.h"
#include "ixp425I2c.h"
#include "ixp425Eeprom.h"

EXPORT_SYMBOL(ixp425EepromRead);
EXPORT_SYMBOL(ixp425EepromWrite);
EXPORT_SYMBOL(ixp425EepromByteRead);
EXPORT_SYMBOL(ixp425EepromByteWrite);

static int __init pcf8594c2_init_module(void)
{
    printk("Load pcf8594c-2\n");
    return 0;
}

static void __exit pcf8594c2_cleanup_module(void)
{
    printk("Unload pcf8594c-2.\n");
}

module_init(pcf8594c2_init_module);
module_exit(pcf8594c2_cleanup_module);

