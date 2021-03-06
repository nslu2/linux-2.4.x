/*
 * Kernel Debugger Architecture Independent Stack Traceback
 *
 * Copyright (C) 1999-2003 Silicon Graphics, Inc.  All Rights Reserved.
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

#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/nmi.h>
#include <asm/system.h>


/*
 * kdb_bt
 *
 *	This function implements the 'bt' command.  Print a stack
 *	traceback.
 *
 *	bt [<address-expression>]	(addr-exp is for alternate stacks)
 *	btp <pid>			Kernel stack for <pid>
 *	btt <address-expression>	Kernel stack for task structure at <address-expression>
 *	bta [DRSTZU]			All processes, optionally filtered by state
 *	btc [<cpu>]			The current process on one cpu, default is all cpus
 *
 * 	address expression refers to a return address on the stack.  It
 *	is expected to be preceeded by a frame pointer.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	Backtrack works best when the code uses frame pointers.  But
 *	even without frame pointers we should get a reasonable trace.
 *
 *	mds comes in handy when examining the stack to do a manual
 *	traceback.
 */

static int
kdb_bt1(struct task_struct *p, unsigned long mask, int argcount, int btaprompt)
{
	int diag;
	char buffer[2];
	if (kdb_getarea(buffer[0], (unsigned long)p) ||
	    kdb_getarea(buffer[0], (unsigned long)(p+1)-1))
		return KDB_BADADDR;
	if (!kdb_task_state(p, mask))
		return 0;
	kdb_printf("Stack traceback for pid %d\n", p->pid);
	kdb_ps1(p);
	diag = kdba_bt_process(p, argcount);
	if (btaprompt) {
		kdb_getstr(buffer, sizeof(buffer), "Enter <q> to end, <cr> to continue:");
		if (buffer[0] == 'q') {
			kdb_printf("\n");
			return 1;
		}
	}
	touch_nmi_watchdog();
	return 0;
}

int
kdb_bt(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int	diag;
	int	argcount = 5;
	int	btaprompt = 1;
	int 	nextarg;
	unsigned long addr;
	long	offset;

	kdbgetintenv("BTARGS", &argcount);	/* Arguments to print */
	kdbgetintenv("BTAPROMPT", &btaprompt);	/* Prompt after each proc in bta */

	if (strcmp(argv[0], "bta") == 0) {
		struct task_struct *p;
		unsigned long cpu;
		unsigned long mask = kdb_task_state_string(argc, argv, envp);
		/* Run the active tasks first */
		for (cpu = 0; cpu < smp_num_cpus; ++cpu) {
			p = kdb_active_task[cpu];
			if (kdb_bt1(p, mask, argcount, btaprompt))
				return 0;
		}
		/* Now the inactive tasks */
		for_each_task(p) {
			if (kdb_task_has_cpu(p) && kdb_active_task[kdb_process_cpu(p)] == p)
				continue;
			if (kdb_bt1(p, mask, argcount, btaprompt))
				return 0;
		}
	} else if (strcmp(argv[0], "btp") == 0) {
		struct task_struct *p = NULL;
		unsigned long	   pid;
		if (argc != 1)
			return KDB_ARGCOUNT;
		if ((diag = kdbgetularg((char *)argv[1], &pid)))
			return diag;
		for_each_task(p) {
			if (p->pid == (pid_t)pid)
				break;
		}
		if (p && p->pid == (pid_t)pid)
			return kdb_bt1(p, ~0, argcount, 0);
		kdb_printf("No process with pid == %ld found\n", pid);
		return 0;
	} else if (strcmp(argv[0], "btt") == 0) {
		unsigned long addr;
		if (argc != 1)
			return KDB_ARGCOUNT;
		if ((diag = kdbgetularg((char *)argv[1], &addr)))
			return diag;
		return kdb_bt1((struct task_struct *)addr, ~0, argcount, 0);
	} else if (strcmp(argv[0], "btc") == 0) {
		unsigned long cpu = ~0;
		struct kdb_running_process *krp;
		char buf[80];
		if (argc > 1)
			return KDB_ARGCOUNT;
		if (argc == 1 && (diag = kdbgetularg((char *)argv[1], &cpu)))
			return diag;
		/* Recursive use of kdb_parse, do not use argv after this point */
		argv = NULL;
		if (cpu != ~0) {
			krp = kdb_running_process + cpu;
			if (cpu >= smp_num_cpus || !krp->seqno) {
				kdb_printf("no process for cpu %ld\n", cpu);
				return 0;
			}
			sprintf(buf, "btt 0x%p\n", krp->p);
			kdb_parse(buf, regs);
			return 0;
		}
		kdb_printf("btc: cpu status: ");
		kdb_parse("cpu\n", regs);
		for (cpu = 0, krp = kdb_running_process; cpu < smp_num_cpus; ++cpu, ++krp) {
			if (!krp->seqno)
				continue;
			sprintf(buf, "btt 0x%p\n", krp->p);
			kdb_parse(buf, regs);
			touch_nmi_watchdog();
		}
		return 0;
	} else {
		if (argc) {
			nextarg = 1;
			diag = kdbgetaddrarg(argc, argv, &nextarg, &addr,
					     &offset, NULL, regs);
			if (diag)
				return diag;
			return kdba_bt_address(addr, argcount);
		} else {
			return kdb_bt1(current, ~0, argcount, 0);
		}
	}

	/* NOTREACHED */
	return 0;
}
