/*
 * drivers/mtd/maps
 *
 * MTD Map file for IXP425 based systems
 *
 * Original Author: Intel Corporation
 * Maintainer: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright 2002 Intel Corporation
 *
 */


#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/ioport.h>
#include <asm/io.h>

#include <linux/reboot.h>

#define WINDOW_ADDR 	0x50000000
#define BUSWIDTH 	2
#define WINDOW_SIZE	0x01000000

#ifndef __ARMEB__
#define	B0(h)	((h) & 0xFF)
#define	B1(h)	(((h) >> 8) & 0xFF)
#else
#define	B0(h)	(((h) >> 8) & 0xFF)
#define	B1(h)	((h) & 0xFF)
#endif

static __u16 ixp425_read16(struct map_info *map, unsigned long ofs)
{
	return *(__u16 *)(map->map_priv_1 + ofs);
}

/*
 * The IXP425 expansion bus only allows 16-bit wide acceses
 * when attached to a 16-bit wide device (such as the 28F128J3A),
 * so we can't use a memcpy_fromio as it does byte acceses.
 */
static void ixp425_copy_from(struct map_info *map, void *to,
    unsigned long from, ssize_t len)
{
	int i;
	u8 *dest = (u8*)to;
	u16 *src = (u16 *)(map->map_priv_1 + from);
	u16 data;

	for(i = 0; i < (len / 2); i++) {
		data = src[i];
		dest[i * 2] = B0(data);
		dest[i * 2 + 1] = B1(data);
	}

	if(len & 1)
		dest[len - 1] = B0(src[i]);
}

static void ixp425_write16(struct map_info *map, __u16 d, unsigned long adr)
{
	*(__u16 *)(map->map_priv_1 + adr) = d;
}

static struct map_info ixp425_map = {
	name: 		"IXP425 Flash",
	buswidth: 	BUSWIDTH,
	read16:		ixp425_read16,
	copy_from:	ixp425_copy_from,
	write16:	ixp425_write16,
};

/*
 * HACK: Put flash back in read mode so RedBoot can boot properly.
 */
static int ixp425_mtd_reboot(struct notifier_block *n, unsigned long code, void *p)
{
	if(code != SYS_RESTART)
		return NOTIFY_DONE;

	printk("Enabling flash read mode\n");
	ixp425_write16(&ixp425_map, 0xff, 0x55 * 0x2);
}

static struct notifier_block ixp425_mtd_notifier = {
	notifier_call:	ixp425_mtd_reboot,
	next:		NULL,
	priority:	0
};


#ifdef CONFIG_MTD_REDBOOT_PARTS
static struct mtd_partition *parsed_parts;
#endif

static struct mtd_partition ixp425_partitions[] = {
	{
		name:	"RedBoot ",
		offset:	0x00000000,
		size:	0x00040000, 
    	},
	{
		name:	"System Configuration",
		offset:	0x00040000,
		size:	0x00020000, 
    	},
	{
		name:	"Kernel",
		offset:	0x00060000,
		size:	0x00100000, 
    	},
	{
		name:	"Ramdisk",
		offset:	0x00160000,
		size:	0x006a0000,
	}
#if 0
	{
		name:	"RedBoot Config",
		offset:	0x007c0000,
		size:	0x00010000,
	},
	{
		name:	"FIS directory",
		offset:	0x007e0000,
		size:	0x00020000,
	}

#endif
};

#define NB_OF(x)  (sizeof(x)/sizeof(x[0]))

static struct mtd_info *ixp425_mtd;
static struct resource *mtd_resource;

static void ixp425_exit(void)
{
    if(ixp425_mtd){
	del_mtd_partitions(ixp425_mtd);
	map_destroy(ixp425_mtd);
    }
    if (ixp425_map.map_priv_1)
	iounmap((void *)ixp425_map.map_priv_1);
    if (mtd_resource)
	release_mem_region(WINDOW_ADDR, WINDOW_SIZE);
    
#ifdef CONFIG_MTD_REDBOOT_PARTS
    if (parsed_parts)
	kfree(parsed_parts);
#endif

    unregister_reboot_notifier(&ixp425_mtd_notifier);

    /* Disable flash write */
    *IXP425_EXP_CS0 &= ~IXP425_FLASH_WRITABLE;
}

static int __init ixp425_init(void)
{
    int res = -1;

#ifdef CONFIG_MTD_REDBOOT_PARTS
    int npart;
#endif
    /* Enable flash write */
    *IXP425_EXP_CS0 |= IXP425_FLASH_WRITABLE;

    ixp425_map.map_priv_1 = 0;
    mtd_resource = 
	request_mem_region(WINDOW_ADDR, WINDOW_SIZE, "ixp425 Flash");
    if(!mtd_resource) {
	printk(KERN_ERR "ixp425 flash: Could not request mem region.\n" );
	res = -ENOMEM;
	goto Error;
    }

    ixp425_map.map_priv_1 =
	(unsigned long)ioremap(WINDOW_ADDR, WINDOW_SIZE);
    if (!ixp425_map.map_priv_1) {
	printk("ixp425 Flash: Failed to map IO region. (ioremap)\n");
	res = -EIO;
	goto Error;
    }
    ixp425_map.size = WINDOW_SIZE;


    /* 
     * Probe for the CFI complaint chip
     * suposed to be 28F128J3A
     */
    ixp425_mtd = do_map_probe("cfi_probe", &ixp425_map);
    if (!ixp425_mtd) {
	res = -ENXIO;
	goto Error;
    }
    ixp425_mtd->module = THIS_MODULE;
   
    /* Initialize flash partiotions 
     * Note: Redeboot partition info table can be parsed by MTD, and used
     *       instead of hard-coded partions. TBD
     */

#ifdef CONFIG_MTD_REDBOOT_PARTS
    /* Try to parse RedBoot partitions */
    npart = parse_redboot_partitions(ixp425_mtd, &parsed_parts);
    if (npart > 0) 
	res = add_mtd_partitions(ixp425_mtd, parsed_parts, npart);
    else   
	res = -EIO;
#endif

    if (res) {
	printk("Using static MTD partitions.\n");
	/* RedBoot partitions not found - use hardcoded partition table */
	res = add_mtd_partitions(ixp425_mtd, ixp425_partitions,
	    NB_OF(ixp425_partitions));
    }


    register_reboot_notifier(&ixp425_mtd_notifier);

    if (res)
	goto Error;

    return res;
Error:
    ixp425_exit();
    return res;
}


//super add
void copy_from_flash(unsigned long from, void *to,ssize_t len)
{
	int i;
	u8 *dest = (u8*)to;
	u16 data;
	unsigned long remap = (unsigned long)ioremap(WINDOW_ADDR,WINDOW_SIZE);
	u16 *src = (u16 *)(remap + from);
	for(i = 0; i < (len / 2);i++){
		data = src[i];
		dest[i * 2] = B0(data);
		dest[i * 2 + 1] = B1(data);
		
	}
	if(len & 1)
		dest[len - 1] = B0(src[i]);
}
module_init(ixp425_init);
module_exit(ixp425_exit);
//end
//
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTD map driver for Intel(R) IXDP425 Development platform");


