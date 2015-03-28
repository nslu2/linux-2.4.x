#include <linux/config.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include "os.h"
#include "ixp425Eeprom.h"

int op = 0;
MODULE_PARM(op, "i");

int from = 0;
MODULE_PARM(from, "i");

int count = 1;
MODULE_PARM(count, "i");

int val = 0;
MODULE_PARM(val, "i");

static int __init demo_init_module(void)
{
    int i, to;
    
    printk("Load pcf8594c2-demo: op=%d, from=%d, count=%d, val=%d\n",
	op, from, count, val);

    to = from + count;
    switch (op)
    {
    case 0:
	for ( i = from; i < to; i++ )
	    printk("%d: %d\n",i, NV_RAM_READ(i));
	break;

    case 1:
	for ( i = from; i < to; i++ )
	    NV_RAM_WRITE(i, val);
	break;
    }

    return 0;
}

static void __exit demo_cleanup_module(void)
{
    printk("Unload pcf8594c2-demo\n");
}

module_init(demo_init_module);
module_exit(demo_cleanup_module);




