/* Driver for Maxtor external USB hard-drives that implement the OneTouch
 * feature
 * Maxtor OneTouch Functions File
 *
 * Current development and maintenance by:
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */


/* Note!
 * This code is still having some problem to detect and sequence the
 * press/release operation properly. It is possible that a " fast"
 * press/release be detected as 2 presses or 2 releases.
 */



#include "transport.h"
#include "protocol.h"
#include "usb.h"
#include "debug.h"
#include "maxtor_onetouch.h"

/* For debug... */
#ifdef CONFIG_USB_STORAGE_MAXTOR_ONETOUCH_DEBUG
#define US_MXO_OT_DBG_INFO	0x01
#define US_MXO_OT_DBG_WARNING	0x02
#define US_MXO_OT_DBG_ERROR	0x04
#define US_MXO_OT_DBG_EVENT	0x08
#define US_MXO_OT_DBG_LEVEL	(US_MXO_OT_DBG_ERROR | US_MXO_OT_DBG_WARNING | US_MXO_OT_DBG_EVENT)
#define US_MXO_OT_DEBUG(level, x...)	if (level & US_MXO_OT_DBG_LEVEL) printk( x )
#else
#define US_MXO_OT_DEBUG(level, x...)
#endif

/* Maximum number of IRQs we can buffer */
#define US_MXO_OT_MAX_IRQ	20

/* Extra data structure */
struct maxtor_onetouch {
	struct us_data		*us;		/* points back to the us_data structure */
	
	/* IRQ URB stuff */
//	struct urb		*irq_urb;	/* the actual irq URB */
	struct semaphore	irq_urb_sem;	/* to protect the urb (?) */
	unsigned char		irqbuf[2];	/* buffer for USB IRQ */
	
	/* IRQ data */
	unsigned char		irqdata[US_MXO_OT_MAX_IRQ];	/* data from USB IRQ */
	int			irq_nb;		/* current irq number */
	
	/* action thread stuff */
	int			pid;		/* action thread */
	struct completion	notify;		/* thread begin/end */
	char			term_thread;	/* if not NULL, terminates the action thread */
	struct semaphore	thread_sem;	/* to wake/sleep the action thread */
};

/* The interrupt handler */
void mxo_ot_irq(struct urb *urb)
{
        struct us_data *us = (struct us_data *)urb->context;
	struct maxtor_onetouch *mxo_ot = (struct maxtor_onetouch*) us->extra;

	US_MXO_OT_DEBUG(US_MXO_OT_DBG_INFO,
			"USB Maxtor OneTouch: -- IRQ received\n");
	
	/* reject improper IRQs */
        if (urb->actual_length != 2)
        {
                US_MXO_OT_DEBUG(US_MXO_OT_DBG_WARNING,
				"USB Maxtor OneTouch: -- IRQ too short\n");
                return;
        }

        /* is the device removed? */
        if (urb->status == -ENODEV)
        {
		US_MXO_OT_DEBUG(US_MXO_OT_DBG_WARNING,
				"USB Maxtor OneTouch: -- device has been removed");
                return;
        }

	if (mxo_ot->irqbuf[1] == 4) {
		/* save IRQ URB buffer contents */
		mxo_ot->irqdata[mxo_ot->irq_nb++] = mxo_ot->irqbuf[0];
		mxo_ot->irq_nb %= US_MXO_OT_MAX_IRQ;
		
		/* wake up the action thread */		
		up(&(mxo_ot->thread_sem));
        }
}

#ifdef CONFIG_USB_STORAGE_MAXTOR_ONETOUCH_PERSO
int mxo_ot_call_perso (struct maxtor_onetouch *mxo_ot, const char * action)
{
	static char * envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
	char *argv[] = { "/bin/touch", "name", NULL };
        char filename[32];
	
        /* use CONFIG_USB_STORAGE_MAXTOR_ONETOUCH_PERSO_APP_PATH */
	US_MXO_OT_DEBUG(US_MXO_OT_DBG_INFO,
			"USB Maxtor OneTouch: calling application %s\n",
			CONFIG_USB_STORAGE_MAXTOR_ONETOUCH_PERSO_APP_PATH);

        /* generate filename we will touch */
        sprintf(filename, "/tmp/%s", action);

        /* argv[1] point to filename */
        argv[1] = filename;

        call_usermodehelper("/bin/touch", argv, envp);
        
	return 0; /*call_usermodehelper ();*/
}
#endif

#ifdef CONFIG_USB_STORAGE_MAXTOR_ONETOUCH_USB_EVENT
/* Called to generate hotplug events */
extern void mxo_ot_call_policy (const char *, struct usb_device *);
#endif

/* The action thread */
static int mxo_ot_action_thread(void * __mxo_ot)
{
	struct maxtor_onetouch *mxo_ot = (struct maxtor_onetouch *) __mxo_ot;
	char irqdata, *action;
	int handled_irq_nb = 0;

	lock_kernel();

	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all our resources..
	 */
	exit_files(current);
	current->files = init_task.files;
	atomic_inc(&current->files->count);
	daemonize();
	reparent_to_init();

	/* avoid getting signals */
	spin_lock_irq(&current->sigmask_lock);
	flush_signals(current);
	sigfillset(&current->blocked);
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	/* set our name for identification purposes */
	sprintf(current->comm, "usb-mxo-ot%d", mxo_ot->us->host_number);

	unlock_kernel();

	/* signal that we've started the thread */
	complete(&(mxo_ot->notify));
	set_current_state(TASK_INTERRUPTIBLE);

	for(;;) {
		US_MXO_OT_DEBUG(US_MXO_OT_DBG_INFO,
				"USB Maxtor OneTouch: *** thread sleeping.\n");
		if(down_interruptible(&mxo_ot->thread_sem))
			break;
			
		US_MXO_OT_DEBUG(US_MXO_OT_DBG_INFO,
				"USB Maxtor OneTouch: *** thread awakened.\n");

		/* if mxo_ot->term_thread is not NULL, we are being asked to exit */
		if (mxo_ot->term_thread) {
			US_MXO_OT_DEBUG(US_MXO_OT_DBG_INFO,
					"USB Maxtor OneTouch: -- action thread exit condition detected\n");
			break;
		}
		
		irqdata = mxo_ot->irqdata[handled_irq_nb++];
		handled_irq_nb %= US_MXO_OT_MAX_IRQ;
		action = NULL;

                switch (irqdata) {
                case 0:
                        US_MXO_OT_DEBUG(US_MXO_OT_DBG_EVENT,
					"USB Maxtor OneTouch: action thread -- release.\n");
			action = "onetouch-release";
                        break;
                case 2:
                        US_MXO_OT_DEBUG(US_MXO_OT_DBG_EVENT,
					"USB Maxtor OneTouch: action thread -- press.\n");
			action = "onetouch-press";
			break;
                default:
			US_MXO_OT_DEBUG(US_MXO_OT_DBG_EVENT,
					"USB Maxtor OneTouch: action thread -- unknown.\n");
                        break;
		}
				
		if (action) {			
			
#ifdef CONFIG_USB_STORAGE_MAXTOR_ONETOUCH_USB_EVENT
			/* send an "usb" hotplug event */
			down(&(mxo_ot->us->dev_semaphore));
			mxo_ot_call_policy (action, mxo_ot->us->pusb_dev);
			up(&(mxo_ot->us->dev_semaphore));
#endif
			
#ifdef CONFIG_USB_STORAGE_MAXTOR_ONETOUCH_PERSO
			/* call our own program... */
			if (mxo_ot_call_perso(mxo_ot, action)) {
				US_MXO_OT_DEBUG(US_MXO_OT_DBG_ERROR,
						"USB Maxtor OneTouch: error calling application!\n");
			}
#endif
		}
	} /* for (;;) */

	/* notify the exit routine that we're actually exiting now */
	complete(&(mxo_ot->notify));

	return 0;
}

/* To allocate the IRQ URB */
static int mxo_ot_allocate_irq(struct maxtor_onetouch *mxo_ot)
{
	unsigned int pipe;
	int maxp;
	int result;

	US_MXO_OT_DEBUG(US_MXO_OT_DBG_INFO,
			"USB Maxtor OneTouch: mxo_ot_allocate_irq()\n");

	/* lock access to the data structure */
	down(&(mxo_ot->irq_urb_sem));

	/* allocate the URB */
//	mxo_ot->irq_urb = usb_alloc_urb(0);
	mxo_ot->us->irq_urb = usb_alloc_urb(0);
//	if (!mxo_ot->irq_urb) {
	if (!mxo_ot->us->irq_urb) {
		up(&(mxo_ot->irq_urb_sem));
		US_MXO_OT_DEBUG(US_MXO_OT_DBG_ERROR,
				"USB Maxtor OneTouch: couldn't allocate interrupt URB");
		return 1;
	}

	/* calculate the pipe and max packet size */
	pipe = usb_rcvintpipe(mxo_ot->us->pusb_dev, mxo_ot->us->ep_int->bEndpointAddress & 
			      USB_ENDPOINT_NUMBER_MASK);
	maxp = usb_maxpacket(mxo_ot->us->pusb_dev, pipe, usb_pipeout(pipe));
	if (maxp > sizeof(mxo_ot->irqbuf))
		maxp = sizeof(mxo_ot->irqbuf);

	/* fill in the URB with our data */
//	FILL_INT_URB(mxo_ot->irq_urb, mxo_ot->us->pusb_dev, pipe, mxo_ot->irqbuf, maxp, 
	FILL_INT_URB(mxo_ot->us->irq_urb, mxo_ot->us->pusb_dev, pipe, mxo_ot->irqbuf, maxp, 
		     mxo_ot_irq, mxo_ot->us, mxo_ot->us->ep_int->bInterval); 

	/* submit the URB for processing */
//	result = usb_submit_urb(mxo_ot->irq_urb);
	result = usb_submit_urb(mxo_ot->us->irq_urb);
	US_MXO_OT_DEBUG(US_MXO_OT_DBG_INFO,
			"USB Maxtor OneTouch: usb_submit_urb() returns %d\n", result);
	if (result) {
//		usb_free_urb(mxo_ot->irq_urb);
		usb_free_urb(mxo_ot->us->irq_urb);
		up(&(mxo_ot->irq_urb_sem));
		return 2;
	}

	/* unlock the data structure and return success */
	up(&(mxo_ot->irq_urb_sem));
	return 0;
}

void mxo_ot_destructor(void * __extra)
{
	struct maxtor_onetouch *mxo_ot = (struct maxtor_onetouch*) __extra;
	int result;
	
	US_MXO_OT_DEBUG(US_MXO_OT_DBG_INFO,
			"USB Maxtor OneTouch: mxo_ot_destructor()\n");
	
	/* allocation should have worked */
	if (!mxo_ot) {
		US_MXO_OT_DEBUG(US_MXO_OT_DBG_ERROR,
				"USB Maxtor OneTouch: NULL pointer\n");
		return;
	}
	
	/* free IRQ URB */
	down(&(mxo_ot->irq_urb_sem));
//	if (mxo_ot->irq_urb) {
	if (mxo_ot->us->irq_urb) {
		US_MXO_OT_DEBUG(US_MXO_OT_DBG_INFO,
				"USB Maxtor OneTouch:  releasing irq URB\n");
//		result = usb_unlink_urb(mxo_ot->irq_urb);
		result = usb_unlink_urb(mxo_ot->us->irq_urb);
		US_MXO_OT_DEBUG(US_MXO_OT_DBG_INFO,
				"USB Maxtor OneTouch:  usb_unlink_urb() returned %d\n", result);
//		usb_free_urb(mxo_ot->irq_urb);
		usb_free_urb(mxo_ot->us->irq_urb);
//		mxo_ot->irq_urb = NULL;
		mxo_ot->us->irq_urb = NULL;
	}
	up(&(mxo_ot->irq_urb_sem));
	
	/* terminate thread */
	if (mxo_ot->pid) {
		US_MXO_OT_DEBUG(US_MXO_OT_DBG_INFO,
				"USB Maxtor OneTouch: -- sending exit command to action thread\n");
		mxo_ot->term_thread = 1;
		up(&(mxo_ot->thread_sem));
		wait_for_completion(&(mxo_ot->notify));
	}
	
	/* free extra data */
	mxo_ot->us->extra = NULL;
	kfree(mxo_ot);

	return ;
}

int mxo_ot_init(void * __us)
{
	struct us_data *us = (struct us_data*) __us;
	struct maxtor_onetouch *mxo_ot;
	
	mxo_ot = (struct maxtor_onetouch*) kmalloc(sizeof(struct maxtor_onetouch), GFP_KERNEL);
	if (!mxo_ot) {
		printk ("USB Maxtor OneTouch: Out of memory!\n");
		return -ENOMEM;
	}
	
	/* Update us_data structure */
	us->extra = (void *) mxo_ot;
	us->extra_destructor = mxo_ot_destructor;
	
	/* Init the mxo_ot */
	mxo_ot->us = us;
	mxo_ot->term_thread = 0;
	mxo_ot->irq_nb = 0;
	init_MUTEX(&(mxo_ot->irq_urb_sem));
	init_MUTEX_LOCKED(&(mxo_ot->thread_sem));
	init_completion(&(mxo_ot->notify));
	
	/* allocate and register irq */
	if (mxo_ot_allocate_irq(mxo_ot)) {
		US_MXO_OT_DEBUG(US_MXO_OT_DBG_ERROR,
				"USB Maxtor OneTouch: mxo_ot_allocate_irq() failed\n");
		return -1;
	}
	
	/* start thread */
	int p = kernel_thread(mxo_ot_action_thread, mxo_ot, CLONE_VM);
	if (p < 0) {
		US_MXO_OT_DEBUG(US_MXO_OT_DBG_ERROR,
				"USB Maxtor OneTouch: Unable to start action thread\n");
		return p;
	}
	mxo_ot->pid = p;

	/* Wait for the thread to start */
	wait_for_completion(&(mxo_ot->notify));
	
	return 0;
}
