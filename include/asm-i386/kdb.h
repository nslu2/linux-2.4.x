#ifndef	_ASM_KDB_H
#define _ASM_KDB_H

/*
 * Kernel Debugger Architecture Dependent Global Headers
 *
 * Copyright (C) 1999-2003 Silicon Graphics, Inc.  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

	/*
	 * KDB_ENTER() is a macro which causes entry into the kernel
	 * debugger from any point in the kernel code stream.  If it
	 * is intended to be used from interrupt level, it must  use
	 * a non-maskable entry method.
	 */
#define KDB_ENTER()	do {if (kdb_on && !KDB_IS_RUNNING()) { asm("\tint $129\n"); }} while(0)

	/*
	 * Needed for exported symbols.
	 */
typedef unsigned long kdb_machreg_t;

#define kdb_machreg_fmt		"0x%lx"
#define kdb_machreg_fmt0	"0x%08lx"
#define kdb_bfd_vma_fmt		"0x%lx"
#define kdb_bfd_vma_fmt0	"0x%08lx"
#define kdb_elfw_addr_fmt	"0x%x"
#define kdb_elfw_addr_fmt0	"0x%08x"

	/*
	 * Per cpu arch specific kdb state.  Must be in range 0xff000000.
	 */
#define KDB_STATE_A_IF		0x01000000	/* Saved IF flag */

	/*
	 * Functions to safely read and write kernel areas.  The {to,from}_xxx
	 * addresses are not necessarily valid, these functions must check for
	 * validity.  If the arch already supports get and put routines with
	 * suitable validation and/or recovery on invalid addresses then use
	 * those routines, otherwise check it yourself.
	 */

	/*
	 * asm-i386 uaccess.h supplies __copy_to_user which relies on MMU to
	 * trap invalid addresses in the _xxx fields.  Verify the other address
	 * of the pair is valid by accessing the first and last byte ourselves,
	 * then any access violations should only be caused by the _xxx
	 * addresses,
	 */

#include <asm/uaccess.h>

extern int kdb_getuserarea_size(void *, unsigned long, size_t);
extern int kdb_putuserarea_size(unsigned long, void *, size_t);

static inline int
__kdba_putarea_size(unsigned long to_xxx, void *from, size_t size)
{
	mm_segment_t oldfs = get_fs();
	int r;
	char c;
	c = *((volatile char *)from);
	c = *((volatile char *)from + size - 1);
	
	if (to_xxx < PAGE_OFFSET) {
		return kdb_putuserarea_size(to_xxx, from, size);
	}

	set_fs(KERNEL_DS);
	r = __copy_to_user((void *)to_xxx, from, size);
	set_fs(oldfs);
	return r;
}

static inline int
__kdba_getarea_size(void *to, unsigned long from_xxx, size_t size)
{
	mm_segment_t oldfs = get_fs();
	int r;
	*((volatile char *)to) = '\0';
	*((volatile char *)to + size - 1) = '\0';

	if (from_xxx < PAGE_OFFSET) {
		return kdb_getuserarea_size(to, from_xxx, size);
	}

	set_fs(KERNEL_DS);
	switch (size) {
	case 1:
		r = __copy_to_user(to, (void *)from_xxx, 1);
		break;
	case 2:
		r = __copy_to_user(to, (void *)from_xxx, 2);
		break;
	case 4:
		r = __copy_to_user(to, (void *)from_xxx, 4);
		break;
	case 8:
		r = __copy_to_user(to, (void *)from_xxx, 8);
		break;
	default:
		r = __copy_to_user(to, (void *)from_xxx, size);
		break;
	}
	set_fs(oldfs);
	return r;
}

/* For numa with replicated code/data, the platform must supply its own
 * kdba_putarea_size and kdba_getarea_size routines.  Without replication kdb
 * uses the standard architecture routines.
 */
#ifdef CONFIG_NUMA_REPLICATE
extern int kdba_putarea_size(unsigned long to_xxx, void *from, size_t size);
extern int kdba_getarea_size(void *to, unsigned long from_xxx, size_t size);
#else
#define kdba_putarea_size __kdba_putarea_size
#define kdba_getarea_size __kdba_getarea_size
#endif

static inline int
kdba_verify_rw(unsigned long addr, size_t size)
{
	unsigned char data[size];
	return(kdba_getarea_size(data, addr, size) || kdba_putarea_size(addr, data, size));
}

static inline unsigned long
kdba_funcptr_value(void *fp)
{
	return (unsigned long)fp;
}

#endif	/* !_ASM_KDB_H */
