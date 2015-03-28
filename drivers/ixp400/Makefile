# "Adapter" makefile
#
# This makefile serves as the interface between the linux kernel
# makefiles and Intel's xscale_sw makefile. It's purpose is to allow
# the 'make modules' rule to build the access library modules, and to
# get them included in the ramdisk image produced by the 'make
# ramdisk' rules.
#
# -- Intel/GPL Copyright Notice --
#
# This software program is available to you under a choice of one of two
# licenses. You may choose to be licensed under either the GNU General Public
# License (GPL) Version 2, June 1991, available at
# http://www.fsf.org/copyleft/gpl.html,
# or the Intel BSD + Patent License, the text of which follows:
#
# "Recipient has requested a license and Intel Corporation ("Intel") is
# willing to grant a license for the software entitled ixp400 module Adapter
# Makefile (the "Software") being provided by Intel Corporation.
#
# The following definitions apply to this License:
#
# "Licensed Patents" means patent claims licensable by Intel Corporation which
# are necessarily infringed by the use or sale of the Software alone or when
# combined with the operating system referred to below.
#
# "Recipient" means the party to whom Intel delivers this Software.
# "Licensee" means Recipient and those third parties that receive a license to
# any operating system available under the GNU Public License version 2.0 or
# later.
#
# Copyright (c) 2002-2003 Intel Corporation. All rights reserved.
#
# The license is provided to Recipient and Recipient’s Licensees under the
# following terms.
#
# Redistribution and use in source and binary forms of the Software, with or
# without modification, are permitted provided that the following conditions
# are met:
# Redistributions of source code of the Software may retain the above
# copyright notice, this list of conditions and the following disclaimer.
#
# Redistributions in binary form of the Software may reproduce the above
# copyright notice, this list of conditions and the following disclaimer in
# the documentation and/or other materials provided with the distribution.
#
# Neither the name of Intel Corporation nor the names of its contributors
# shall be used to endorse or promote products derived from this Software
# without specific prior written permission.
#
# Intel hereby grants Recipient and Licensees a non-exclusive, worldwide,
# royalty-free patent license under Licensed Patents to make, use, sell, offer
# to sell, import and otherwise transfer the Software, if any, in source code
# and object code form. This license shall include changes to the Software
# that are error corrections or other minor changes to the Software that do
# not add functionality or features when the Software is incorporated in any
# version of a operating system that has been distributed under the GNU
# General Public License 2.0 or later.  This patent license shall apply to the
# combination of the Software and any operating system licensed under the GNU
# Public License version 2.0 or later if, at the time Intel provides the
# Software to Recipient, such addition of the Software to the then publicly
# available versions of such operating system available under the GNU Public
# License version 2.0 or later (whether in gold, beta or alpha form) causes
# such combination to be covered by the Licensed Patents. The patent license
# shall not apply to any other combinations which include the Software. No
# hardware per se is licensed hereunder.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR ITS CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
# DAMAGE."
#
# -- End Intel/GPL Copyright Notice --

#include $(ROOTDIR)/IXP400lib/ixp400_xscale_sw/buildUtils/environment.linux
# IX_XSCALE_SW=$(ROOTDIR)/IXP400lib/ixp400_xscale_sw/
# The kernel makefiles override the value of CFLAGS on the make
# commandline. Because the xscale_sw makefile is supposed to be
# stand-alone, we don't want this. So we reset the special variable
# MAKEOVERRIDES (See the make manual, section 5 "Communicating Options
# to a Sub-make"). This will affect all invocations of make from this
# makefile.
MAKEOVERRIDES=

ifdef CONFIG_MODVERSIONS
Makefile: force
	$(error The IXP425 Access library cannot be built with CONFIG_MODVERSIONS enabled. You need to disable either "Set version information on all module symbols" or "Build IXP425 Access Library" in your kernel configuration)
endif

# A rule to test the IX_XSCALE_SW is set
ifndef IX_XSCALE_SW
Makefile: force
	$(error IX_XSCALE_SW must be set to the absolute path of the xscale_sw directory)
endif

# 'component_list.mk' defines COMPONENTS as a list of component
# names. It's generated from the xscale_sw makefile by the rule below.
include component_list.mk

# Extract the list of components to build from the xscale_sw makefile.
#
# The reason we use '&&' instead of a pipe to connect the $(MAKE) and
# 'tail' commands is that we don't want the 'tail' to execute if the
# $(MAKE) fails.
component_list.mk : $(IX_XSCALE_SW)/Makefile $(TOPDIR)/.config Makefile
	echo 'COMPONENTS := \' > $@ # ' for syntax highlighting
	$(MAKE) -C $(IX_XSCALE_SW) --no-print-directory showmacro MACRO=COMPONENTS IX_BYPASS_DEPS=1\
		> $@.tmp && tail -n 1 < $@.tmp >> $@
	echo 'CODELETS_COMPONENTS := \' >> $@ # ' for syntax highlighting
	$(MAKE) -C $(IX_XSCALE_SW) --no-print-directory showmacro MACRO=CODELETS_COMPONENTS IX_BYPASS_DEPS=1\
		> $@.tmp && tail -n 1 < $@.tmp >> $@
	rm $@.tmp

# These are the module files we can build
IXP400_MODULES := ixp400.o $(COMPONENTS:%=ixp400_%.o) $(CODELETS_COMPONENTS:%=ixp400_codelets_%.o)

# This is where they're located in the xscale_sw tree
REAL_IXP400_MODULES := $(IXP400_MODULES:%=$(IX_XSCALE_SW)/lib/linuxbe/%)

# Having the modules depend on the build_ixp400_modules rule instead of
# putting the commands in this rule means the commands are executed
# only once for all of the modules.
$(REAL_IXP400_MODULES): build_ixp400_modules

.PHONY : force
force:
# This rule builds the modules.
# The dependency on modversions.h is needed here so it'll be built for
# us in a 'make modules'.
build_ixp400_modules: force $(TOPDIR)/include/linux/modversions.h
#	cd $(IX_XSCALE_SW); $(MAKE) IX_NOSYM=1 $(REAL_IXP400_MODULES:$(IX_XSCALE_SW)/%=%)

# After building the modules, we copy them to this directory as that's
# where the kernel makefiles expect to find them. We can't use
# symlinks, as Jungo's 'make ramdisk' rule would just copy the links.
$(IXP400_MODULES): $(REAL_IXP400_MODULES)
#	cp $(IX_XSCALE_SW)/lib/linuxbe/$@ $@

# This macro is the interface to the rules in $(TOPDIR)/Rules.make. It
# contains the names of all module files that can be built in this
# directory. Note that they must be filenames in this directory - the
# ramdisk rules cannot handle paths in this list.
obj-m := $(IXP400_MODULES)

# Include the generic kernel build system rules
include $(TOPDIR)/Rules.make
