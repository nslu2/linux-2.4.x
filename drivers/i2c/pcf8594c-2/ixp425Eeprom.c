/**
 * ============================================================================
 * = COPYRIGHT
 *              INTEL CORPORATION PROPRIETARY INFORMATION
 *   This software is supplied under the terms of a licence agreement or
 *   nondisclosure agreement with Intel Corporation and may not be copied 
 *   or disclosed except in accordance with the terms in that agreement.
 *      Copyright (C) 2000-2002 Intel Corporation. All rights reserved.
 *
 * = PRODUCT
 *      Intel(r) IXP425 Network Processor
 *
 *
 * ============================================================================
 */


/* ixp425Eeprom.c Philips PCF8582C-2T/03 256byte I2C EEPROM driver */


/*
DESCRIPTION
This is the driver for the I2C EEPROM device. The EEPROM can act as a slave
transmitter or receiver depending on whether you want to read or write to
the device.

This driver uses the I2C protocol and calls as outlined in ixp425I2c.c 

INCLUDE FILES: ixp425Eeprom.h

*/

/* includes */

#include "os.h"
#include "ixp425I2c.h"
#include "ixp425Eeprom.h"

/* defines */

#define IXP425_EEPROM_MAX_WRITE 8	/* 8 bytes page mode */
#define IXP425_EEPROM_WRITE_DELAY     100 /* E/W time delay = 100ms */

/******************************************************************************
*
* ixp425EepromRead - Read "num" bytes from the EEPROM at "offset"
*
* RETURNS: The number of bytes successfully read; ERROR otherwise
*	   
*/
int ixp425EepromRead (UINT8 *buf, UINT32 num, UINT8 offset)
    {
    if ((num + offset) > IXP425_EEPROM_SIZE || buf == NULL)
        return (ERROR);

    return(ixp425I2CReadTransfer(IXP425_EEPROM_ADDR, buf, num, offset));
    }


/******************************************************************************
*
* ixp425EepromWrite - Write "num" bytes to the EEPROM device at "offset"
*
* RETURNS: The number of bytes actually written; ERROR otherwise
*	   
*/
int ixp425EepromWrite (UINT8 *buf, UINT32 num, UINT8 offset)
    {
    int byteCnt = 0;
    int tmpCnt = 0;
    int currNum = num;

    if((num + offset) > IXP425_EEPROM_SIZE || buf == NULL)
        return (ERROR);

    while(currNum > 0)
        {
	tmpCnt = IXP425_EEPROM_MAX_WRITE > currNum ? currNum : IXP425_EEPROM_MAX_WRITE;

	byteCnt = ixp425I2CWriteTransfer(IXP425_EEPROM_ADDR, buf, tmpCnt, offset);	
	taskDelay(IXP425_EEPROM_WRITE_DELAY);

	if(byteCnt != ERROR)
	    {
	    currNum -= byteCnt;
	    buf += byteCnt;
	    offset += byteCnt;
	    }
	else
	    {
	    return(num);
	    }
	}

    return(num);
    }


/******************************************************************************
*
* ixp425EepromByteRead - Read and return one byte from the EEPROM device at "offset"
*			 This function is used by sysNvRamGet()
*
* RETURNS: the byte read
*/
char ixp425EepromByteRead (UINT8 offset)
    {
    char buf;

    ixp425I2CReadTransfer(IXP425_EEPROM_ADDR, &buf, 1, offset);
    return(buf);
    }


/******************************************************************************
*
* ixp425EepromByteWrite - Write one byte to the EEPROM device at "offset"
*			  This function is used by sysNvRamSet()
*
* RETURNS: N/A
*/
void ixp425EepromByteWrite (UINT8 offset, char data)
    {
    if((offset + 1) <= IXP425_EEPROM_SIZE)
        {
        ixp425I2CWriteTransfer(IXP425_EEPROM_ADDR, &data, 1, offset);
        taskDelay(IXP425_EEPROM_WRITE_DELAY);
	}
    }
