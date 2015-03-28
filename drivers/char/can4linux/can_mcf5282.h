/*
 * can_mc5282.h - can4linux CAN driver module
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2003 port GmbH Halle/Saale
 *------------------------------------------------------------------
 * $Header: /home/cvsroot/NSLU2_V2/linux-2.4.x/drivers/char/can4linux/can_mcf5282.h,v 1.1.1.1 2004/09/28 06:06:06 sure Exp $
 *
 *--------------------------------------------------------------------------
 *
 *
 * modification history
 * --------------------
 * $Log: can_mcf5282.h,v $
 * Revision 1.1.1.1  2004/09/28 06:06:06  sure
 * Add NSLU2_V2 into CVS server.
 *
 * Revision 1.1.1.1  2004/03/24 19:55:13  sure
 * Add NSLU2 model into CVS server.
 *
 *
 *
 *
 *--------------------------------------------------------------------------
 */


extern unsigned int Base[];


#define CAN_SYSCLK 32

/* define some types, header file comes from CANopen */
#define UNSIGNED8 u8
#define UNSIGNED16 u16


#define MCFFLEXCAN_BASE (MCF_MBAR + 0x1c0000)	/* Base address FlexCAN module */


/* can4linux does not use all the full CAN features, partly because it doesn't
   make sense.
 */

/* We use only one transmit object of all messages to be transmitted */
#define TRANSMIT_OBJ 0
#define RECEIVE_OBJ 1


#include "TouCAN.h"



