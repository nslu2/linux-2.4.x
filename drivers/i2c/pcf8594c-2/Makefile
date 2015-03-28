O_TARGET = pcf8594c2.o

export-objs := syms.o

obj-y           += ixp425I2c.o ixp425Eeprom.o syms.o
obj-m		:= $(O_TARGET) pcf8594c2_demo.o ixp425-nvram.o

include $(TOPDIR)/Rules.make
