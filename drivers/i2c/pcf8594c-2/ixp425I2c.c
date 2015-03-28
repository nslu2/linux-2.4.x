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

/* ixp425I2c.c - Intel IXP425 I2C source file */


/*
DESCRIPTION
This is the driver for the Intel IXP425 I2C bus protocol. This is a software
only implementation which uses two pins on the IXP425 GPIO Controller.

Note the IXP425 only supports one master device i.e. the GPIO controller
itself.

USAGE
An example read from an I2C device using the individual protocol functions
is shown below:
{
		:
    if (ixp425I2CStart() == OK)
        {
        ixp425I2CByteTransmit((devAddr & IXP425_I2C_WRITE_MSK));
        ixp425I2CAckReceive();

	ixp425I2CByteTransmit(offset);
	ixp425I2CAckReceive();

        -- Switch to read mode --
	IXP425_I2C_SCL_SET_HIGH;
        IXP425_I2C_SDA_SET_HIGH;

        if(ixp425I2CStart() != OK)
	    {
	    ixp425I2CStop();
	    return(ERROR);
	    }

        ixp425I2CByteTransmit((devAddr | IXP425_I2C_READ_FLAG));
	ixp425I2CAckReceive();

	for(byteCnt=0; byteCnt<num; byteCnt++)
	    {
            ixp425I2CByteReceive(Buf);
            Buf++;

	    -- Prevent giving an ACK on the last byte --
            if (byteCnt < (num - 1))
                ixp425I2CAckSend();
	    }

	ixp425I2CStop();
		:
	}
}

INCLUDE FILES: ixp425I2c.h

SEE ALSO:
.I "Ixp425 Data Sheet,"
*/

/* includes */

#include "os.h"
#include "ixp425I2c.h"

#ifndef BIT
#define BIT(x) (1<<(x))
#endif

/* defines */
#define IXP425_I2C_SDA IXP425_GPIO_PIN_7
#define IXP425_I2C_SCL IXP425_GPIO_PIN_6

/* Microsecond delay for SCL setup and hold times */
#define IXP425_I2C_SCLDELAY sysMicroDelay(10)

/* Data setup time */
#define IXP425_I2C_SDADELAY sysMicroDelay(1)

#define IXP425_I2C_SDA_GET(val) (ixp425GPIOLineGet(IXP425_I2C_SDA, val))
#define IXP425_I2C_SCL_GET(val) (ixp425GPIOLineGet(IXP425_I2C_SCL, val)) 

#define IXP425_I2C_SDA_SET_HIGH {\
        ixp425GPIOLineSet(IXP425_I2C_SDA, IXP425_GPIO_HIGH); \
	IXP425_I2C_SDADELAY;}

#define IXP425_I2C_SDA_SET_LOW {\
	ixp425GPIOLineSet(IXP425_I2C_SDA, IXP425_GPIO_LOW); \
	IXP425_I2C_SDADELAY;}

#define IXP425_I2C_SCL_SET_HIGH {\
	ixp425GPIOLineSet(IXP425_I2C_SCL, IXP425_GPIO_HIGH); \
	IXP425_I2C_SCLDELAY;}

#define IXP425_I2C_SCL_SET_LOW {\
	ixp425GPIOLineSet(IXP425_I2C_SCL, IXP425_GPIO_LOW); \
	IXP425_I2C_SCLDELAY;}

/* forward declarations */

LOCAL STATUS ixp425I2CBusFree (void);


/******************************************************************************
*
* ixp425I2CStart (Control signal) - Initiate a transfer on the I2C bus. This 
*		 should only be called from a master device (i.e. GPIO controller
*		 on IXP425)
*
* RETURNS: OK when the bus is free. ERROR if the bus is already in use by another
*	   task.
*/
STATUS ixp425I2CStart ()
    {
    int key;

    key =  intLock();

    if (ixp425I2CBusFree() == OK)
        {
        ixp425GPIOLineConfig(IXP425_I2C_SDA, IXP425_GPIO_OUT);
        ixp425GPIOLineConfig(IXP425_I2C_SCL, IXP425_GPIO_OUT);

	IXP425_I2C_SCL_SET_HIGH;
        IXP425_I2C_SDA_SET_HIGH;	
        IXP425_I2C_SDA_SET_LOW;

	intUnlock(key);
	return (OK);
        }
    
    intUnlock(key);
    return (ERROR);
    }

/******************************************************************************
*
* ixp425I2CStop (Control signal) - End a transfer session. The I2C bus will be
*		left in a free state; i.e. SCL HIGH and SDA HIGH. This should
*		only be called from a master device (i.e. GPIO controller on 
*		IXP425)
*
* RETURNS: N/A
*/
void ixp425I2CStop ()
    {
    ixp425GPIOLineConfig(IXP425_I2C_SDA, IXP425_GPIO_OUT);
    ixp425GPIOLineConfig(IXP425_I2C_SCL, IXP425_GPIO_OUT);

    IXP425_I2C_SDA_SET_LOW;
    IXP425_I2C_SCL_SET_HIGH;
    IXP425_I2C_SDA_SET_HIGH;
    }

/******************************************************************************
* ixp425I2CAckSend (Control signal) - Send an acknowledgement
*
* This function sends an acknowledgement on the I2C bus.
* 
* RETURNS: N/A
*/
void ixp425I2CAckSend ()
    {
    ixp425GPIOLineConfig(IXP425_I2C_SDA, IXP425_GPIO_OUT);
    ixp425GPIOLineConfig(IXP425_I2C_SCL, IXP425_GPIO_OUT);

    IXP425_I2C_SDA_SET_LOW;

    IXP425_I2C_SCL_SET_HIGH;
    IXP425_I2C_SCL_SET_LOW;

    IXP425_I2C_SDA_SET_HIGH;
    }

/******************************************************************************
* ixp425I2CAckReceive (Control Signal) - Get an acknowledgement from the I2C bus.
* 
* RETURNS: OK if acknowledge received by slave; ERROR otherwise.
*/
STATUS ixp425I2CAckReceive ()
    {
    int retryCnt = 0;

    IXP425_GPIO_SIG sda;

    ixp425GPIOLineConfig(IXP425_I2C_SDA, IXP425_GPIO_OUT);
    ixp425GPIOLineConfig(IXP425_I2C_SCL, IXP425_GPIO_OUT);

    IXP425_I2C_SDA_SET_HIGH;   

    ixp425GPIOLineConfig(IXP425_I2C_SDA, IXP425_GPIO_IN);
    IXP425_I2C_SCL_SET_HIGH;

    do
    {
    IXP425_I2C_SDA_GET(&sda);
    retryCnt++;
    }while( (sda != IXP425_GPIO_LOW) && (retryCnt < IXP425_I2C_ACK_RTY) );

    IXP425_I2C_SCL_SET_LOW;    

    if (sda != IXP425_GPIO_LOW)
	return (ERROR);
    else
	return (OK);
    }

/******************************************************************************
*
* ixp425I2CByteTransmit - Transmit a byte on the I2C bus. All byte transfers are
* Most Significant Bit(MSB) first.
* 
* RETURNS: N/A
*/
void ixp425I2CByteTransmit (unsigned char dataByte)
    {
    int bitCnt = 0;

    ixp425GPIOLineConfig(IXP425_I2C_SDA, IXP425_GPIO_OUT);
    ixp425GPIOLineConfig(IXP425_I2C_SCL, IXP425_GPIO_OUT);

    IXP425_I2C_SCL_SET_LOW;

    for (bitCnt = 7; bitCnt >= 0; bitCnt--)
        {
        if (dataByte & BIT(bitCnt))
            {
	    IXP425_I2C_SDA_SET_HIGH;
            }
        else
            {
            IXP425_I2C_SDA_SET_LOW;
            }

	IXP425_I2C_SCL_SET_HIGH;
	IXP425_I2C_SCL_SET_LOW;
	}

    }

/******************************************************************************
*
* ixp425I2CByteReceive - Receive a byte on the I2C bus.
*
* RETURNS: N/A
*/
void ixp425I2CByteReceive (unsigned char *dataByte)
    {
    IXP425_GPIO_SIG sda = 0;
    unsigned char tmpByte = 0;
    int bitCnt = 0;

    ixp425GPIOLineConfig(IXP425_I2C_SDA, IXP425_GPIO_IN);
    ixp425GPIOLineConfig(IXP425_I2C_SCL, IXP425_GPIO_OUT);

    IXP425_I2C_SCL_SET_LOW;

    for (bitCnt = 7; bitCnt >= 0; bitCnt--)
        {
        IXP425_I2C_SCL_SET_HIGH;

        IXP425_I2C_SDA_GET(&sda);     /* Get the data bit */
	
        tmpByte |= (sda << bitCnt);
        IXP425_I2C_SCL_SET_LOW;
        }

    ixp425GPIOLineConfig(IXP425_I2C_SDA, IXP425_GPIO_OUT);
    IXP425_I2C_SDA_SET_LOW;
    *dataByte = tmpByte;
    }

/******************************************************************************
*
* ixp425I2CBusFree - determine if the I2C bus is in use or not.
*
* RETURNS: OK if the bus is free, ERROR otherwise.
*/
LOCAL STATUS ixp425I2CBusFree ()
    {
    IXP425_GPIO_SIG sda = 0;
    IXP425_GPIO_SIG scl = 0;

    /* 
     * Listen in on the data (SDA), and clock (SCL) lines. If both
     * are high then the bus is free.
     */
    IXP425_I2C_SDA_GET(&sda);
    IXP425_I2C_SCL_GET(&scl);

    if( (sda == IXP425_GPIO_HIGH) && (scl == IXP425_GPIO_HIGH) )
	return (OK);
    else
	return (ERROR);
    }

/******************************************************************************
*
* ixp425I2CWriteTransfer - This function writes num bytes to a slave device with 
* address devAddr.
*
* RETURNS: the number of bytes actually written; ERROR otherwise
*/
int ixp425I2CWriteTransfer (UINT8 devAddr, UINT8 *buffer, UINT32 num, UINT8 offset)
    {
    int byteCnt = 0;

    if (buffer == NULL)
	return (ERROR);

    if (ixp425I2CStart() == OK)
        {
        ixp425I2CByteTransmit((devAddr & IXP425_I2C_WRITE_MSK));
        if(ixp425I2CAckReceive() != OK)
	    {
	    ixp425I2CStop();
	    return (ERROR);
	    }

	ixp425I2CByteTransmit(offset);
        if(ixp425I2CAckReceive() != OK)
	    {
	    ixp425I2CStop();
	    return (ERROR);
	    }

	for(byteCnt=0; byteCnt<num; byteCnt++)
	    {
	    ixp425I2CByteTransmit(*buffer);	    
	    if(ixp425I2CAckReceive() != OK)
	        {
		ixp425I2CStop();
		return (byteCnt);
	        }

	    buffer++;
	    }

	ixp425I2CStop();
	return (byteCnt);
	}
        
    return (ERROR);
    }

/******************************************************************************
*
* ixp425I2CReadTransfer - This function reads num bytes from the device with 
* address devAddr and places it into the area pointed to by buffer.
*
* RETURNS: the number of bytes actually read; ERROR otherwise
*/
int ixp425I2CReadTransfer (UINT8 devAddr, UINT8 *buffer, UINT32 num, UINT8 offset)
    {
    int byteCnt = 0;
    int key;

    if (buffer == NULL)
	return (ERROR);

    if (ixp425I2CStart() == OK)
        {
        ixp425I2CByteTransmit((devAddr & IXP425_I2C_WRITE_MSK));
        if(ixp425I2CAckReceive() != OK)
	    {
	    ixp425I2CStop();
	    return(ERROR);
	    }

	ixp425I2CByteTransmit(offset);
        if(ixp425I2CAckReceive() != OK)
	   {
	   ixp425I2CStop();
	   return(ERROR);
	   }

        /* Switch to read mode */
	key = intLock();

	IXP425_I2C_SCL_SET_HIGH;
        IXP425_I2C_SDA_SET_HIGH;

	intUnlock(key);

        if(ixp425I2CStart() != OK)
	    {
	    ixp425I2CStop();
	    
	    return(ERROR);
	    }

        ixp425I2CByteTransmit((devAddr | IXP425_I2C_READ_FLAG));
        if(ixp425I2CAckReceive() != OK)
	   {
	   ixp425I2CStop();
	   return (ERROR);
	   }

	for(byteCnt=0; byteCnt<num; byteCnt++)
	    {
            ixp425I2CByteReceive(buffer);
            buffer++;

	    /* Prevent giving an ACK on the last byte */
            if (byteCnt < (num - 1))
                ixp425I2CAckSend();
	    }

	ixp425I2CStop();
	return (byteCnt);
	}

    return (ERROR);
    }
