#
# Copyright (c) 1999-2003 Silicon Graphics, Inc.  All Rights Reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of version 2 of the GNU General Public License as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it would be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# Further, this software is distributed without any warranty that it is
# free of the rightful claim of any third person regarding infringement
# or the like.  Any license provided herein, whether implied or
# otherwise, applies only to this software file.  Patent licenses, if
# any, provided herein do not apply to combinations of this program with
# other software, or any other product whatsoever.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write the Free Software Foundation, Inc., 59
# Temple Place - Suite 330, Boston MA 02111-1307, USA.
#
# Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
# Mountain View, CA  94043, or:
#
# http://www.sgi.com
#
# For further information regarding this notice, see:
#
# http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
#

O_TARGET	:= kdb.o
export-objs	:= kdbmain.o kdb_io.o
obj-y		:= kdb_bt.o kdb_bp.o kdb_id.o kdbsupport.o gen-kdb_cmds.o kdbmain.o kdb_io.o

subdir-$(CONFIG_KDB_MODULES) := modules
obj-y += $(addsuffix /vmlinux-obj.o, $(subdir-y))

override CFLAGS := $(CFLAGS:%-pg=% )

EXTRA_CFLAGS += -I $(TOPDIR)/arch/$(ARCH)/kdb

include $(TOPDIR)/Rules.make

gen-kdb_cmds.c:	kdb_cmds Makefile
	$(AWK) 'BEGIN {print "#include <linux/init.h>"} \
		/^ *#/{next} \
		/^[ \t]*$$/{next} \
		{gsub(/"/, "\\\"", $$0); \
		  print "static __initdata char kdb_cmd" cmds++ "[] = \"" $$0 "\\n\";"} \
		END {print "char __initdata *kdb_cmds[] = {"; for (i = 0; i < cmds; ++i) {print "  kdb_cmd" i ","}; print("  0\n};");}' \
		kdb_cmds > gen-kdb_cmds.c
