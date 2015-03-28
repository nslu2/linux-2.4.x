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

/* ixp425I2c.h - Intel IXP425 I2c header file */

#ifndef __INCixp425I2ch 
#define __INCixp425I2ch


#ifdef __cplusplus
extern "C" {
#endif

#define IXP425_I2C_READ_FLAG 0x1	/* Read from slave */
#define IXP425_I2C_WRITE_MSK 0xFE	/* Write to slave */
#define IXP425_I2C_ACK_RTY 5	/* Acknowledge Receive retry count */


/* Function Declarations */
STATUS ixp425I2CStart(void);
void ixp425I2CStop(void);
void ixp425I2CAckSend(void);
STATUS ixp425I2CAckReceive(void);
void ixp425I2CByteTransmit(unsigned char dataByte);
void ixp425I2CByteReceive (unsigned char *dataByte);
STATUS ixp425I2CWriteTransfer(UINT8 devAddr, UINT8 *buffer, UINT32 num, UINT8 offset);
STATUS ixp425I2CReadTransfer(UINT8 devAddr, UINT8 *buffer, UINT32 num, UINT8 offset);

#ifdef __cplusplus
}
#endif

#endif /* __INCixp425I2ch */ 
