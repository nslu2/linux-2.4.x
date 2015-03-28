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

/* ixp425Eeprom.h - Philips PCF8582C-2T/03 256byte I2C EEPROM driver header file */


#ifndef __INCixp425Eepromh 
#define __INCixp425Eepromh


#ifdef __cplusplus
extern "C" {
#endif

#define IXP425_EEPROM_ADDR 0xA0
#define IXP425_EEPROM_SIZE 256	/* 256byte EEPROM */

/* Macros used by NVRAM driver */
#define NV_RAM_READ(x) (ixp425EepromByteRead (x))
#define NV_RAM_WRITE(x,y) (ixp425EepromByteWrite (x, y))

int ixp425EepromRead (UINT8 *buf, UINT32 num, UINT8 offset);
int ixp425EepromWrite (UINT8 *buf, UINT32 num, UINT8 offset);

char ixp425EepromByteRead (UINT8 offset);
void ixp425EepromByteWrite (UINT8 offset, char data);

#ifdef __cplusplus
}
#endif

#endif /* __INCixp425Eepromh */
