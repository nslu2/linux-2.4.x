/*
 * Copyright (c) 2003 Silicon Graphics, Inc.  All Rights Reserved.
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

#include <linux/blkdev.h>
#include <linux/types.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <asm/signal.h>

MODULE_AUTHOR("SGI");
MODULE_DESCRIPTION("Debug struct task and sigset information");
MODULE_LICENSE("GPL");

#ifdef __KDB_HAVE_NEW_SCHEDULER
static char *
kdb_cpus_allowed_string(struct task_struct *tp)
{
#ifndef CPUMASK_WORDCOUNT
	static char maskbuf[BITS_PER_LONG/4+8];
	sprintf(maskbuf, "0x%0lx", tp->cpus_allowed);
#else
	int i, j;
	static char maskbuf[CPUMASK_WORDCOUNT * BITS_PER_LONG / 4 + 8];

	strcpy(maskbuf, "0x");
	for (j=2, i=CPUMASK_WORDCOUNT-1; i >= 0; i--) {
		j += sprintf(maskbuf + j, "%0lx", tp->cpus_allowed[i]);
	}
#endif /* CPUMASK_WORDCOUNT */

	return maskbuf;
}
#endif	/* __KDB_HAVE_NEW_SCHEDULER */

static int
kdbm_task(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	unsigned long	addr;
	long		offset=0;
	int		nextarg;
	int		e = 0;
	struct task_struct *tp = NULL;
	
	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((e = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)) != 0)
		return(e);

	if (!(tp = kmalloc(sizeof(*tp), GFP_ATOMIC))) {
	    kdb_printf("%s: cannot kmalloc tp\n", __FUNCTION__);
	    goto out;
	}
	if ((e = kdb_getarea(*tp, addr))) {
	    kdb_printf("%s: invalid task address\n", __FUNCTION__);
	    goto out;
	}

	kdb_printf(
	    "struct task at 0x%p, pid=%d flags=0x%lx state=%ld comm=\"%s\"\n",
	    tp, tp->pid, tp->flags, tp->state, tp->comm);

	kdb_printf("  cpu=%d policy=%lu ", kdb_process_cpu(tp), tp->policy);
#ifdef __KDB_HAVE_NEW_SCHEDULER
	kdb_printf(
	    "prio=%d static_prio=%d cpus_allowed=%s",
	    tp->prio, tp->static_prio, kdb_cpus_allowed_string(tp));
#else
	kdb_printf(
	    "cpus_runnable=%lx cpus_allowed=%lx",
	    tp->cpus_runnable, tp->cpus_allowed);
#endif
	kdb_printf(" &thread=0x%p\n", &tp->thread);

	kdb_printf("  need_resched=%ld ", tp->need_resched);
#ifdef __KDB_HAVE_NEW_SCHEDULER
	kdb_printf(
	    "sleep_timestamp=%lu time_slice=%u",
	    tp->sleep_timestamp, tp->time_slice);
#else
	kdb_printf(
	    "counter=%ld nice=%ld",
	    tp->counter, tp->nice);
#endif
	kdb_printf(" lock_depth=%d\n", tp->lock_depth);

	kdb_printf(
	    "  fs=0x%p files=0x%p mm=0x%p nr_local_pages=%u\n",
	    tp->fs, tp->files, tp->mm, tp->nr_local_pages);

	kdb_printf(
	    "  uid=%d euid=%d suid=%d fsuid=%d gid=%d egid=%d sgid=%d fsgid=%d\n",
	    tp->uid, tp->euid, tp->suid, tp->fsuid, tp->gid, tp->egid, tp->sgid, tp->fsgid);

	kdb_printf(
	    "  user=0x%p locks=%d semundo=0x%p semsleeping=0x%p\n",
	    tp->user, tp->locks, tp->semundo, tp->semsleeping);

	kdb_printf(
	    "  sig=0x%p &blocked=0x%p &sigpending=0x%p\n",
	    tp->sig, &tp->blocked, &tp->sigpending);

	kdb_printf(
	    "  times.utime=%ld times_stime=%ld times_cutime=%ld times_cstime=%ld\n",
	    tp->times.tms_utime, tp->times.tms_stime, tp->times.tms_cutime,
	    tp->times.tms_cstime);

out:
	if (tp)
	    kfree(tp);
	return e;
}

static int
kdbm_sigset(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	sigset_t	*sp = NULL;
	unsigned long	addr;
	long		offset=0;
	int		nextarg;
	int		e = 0;
	int		i;
	char		fmt[32];
	
	if (argc != 1)
		return KDB_ARGCOUNT;

#ifndef _NSIG_WORDS
        kdb_printf("unavailable on this platform, _NSIG_WORDS not defined.\n");
#else
	nextarg = 1;
	if ((e = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)) != 0)
		return(e);

	if (!(sp = kmalloc(sizeof(*sp), GFP_ATOMIC))) {
	    kdb_printf("%s: cannot kmalloc sp\n", __FUNCTION__);
	    goto out;
	}
	if ((e = kdb_getarea(*sp, addr))) {
	    kdb_printf("%s: invalid sigset address\n", __FUNCTION__);
	    goto out;
	}

	sprintf(fmt, "[%%d]=0x%%0%dlx ", (int)sizeof(sp->sig[0])*2);
	kdb_printf("sigset at 0x%p : ", sp);
	for (i=_NSIG_WORDS-1; i >= 0; i--) {
	    if (i == 0 || sp->sig[i]) {
		kdb_printf(fmt, i, sp->sig[i]);
	    }
	}
        kdb_printf("\n");
#endif /* _NSIG_WORDS */

out:
	if (sp)
	    kfree(sp);
	return e;
}

static int __init kdbm_task_init(void)
{
	kdb_register("task", kdbm_task, "<vaddr>", "Display task_struct", 0);
	kdb_register("sigset", kdbm_sigset, "<vaddr>", "Display sigset_t", 0);
	
	return 0;
}

static void __exit kdbm_task_exit(void)
{
	kdb_unregister("task");
	kdb_unregister("sigset");
}

kdb_module_init(kdbm_task_init)
kdb_module_exit(kdbm_task_exit)
