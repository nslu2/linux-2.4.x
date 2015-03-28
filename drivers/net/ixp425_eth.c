/*
 * File: ixp425_eth.c
 *
 * Author: Intel Corporation
 *
 * IXP425 Ethernet Driver for Linux
 *
 * -- Copyright Notice --
 * Copyright ?2004, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * -- End Copyright Notice --
 */

/* 
 * DESIGN NOTES:
 * This driver is written and optimized for Intel Xscale technology.
 *
 * SETUP NOTES:
 * By default, this driver uses predefined MAC addresses.
 * These are set in global var 'default_mac_addr' in this file.
 * If required, these can be changed at run-time using
 * the 'ifconfig' tool.
 *
 * Example - to set ixp0 MAC address to 00:02:B3:66:88:AA, 
 * run ifconfig with the following arguments:
 *
 *   ifconfig ixp0 hw ether 0002B36688AA
 *
 * (more information about ifconfig is available thru ifconfig -h)
 *
 * Example - to set up the ixp1 IP address to 192.168.10.1
 * run ifconfig with the following arguments:
 * 
 * ifconfig ixp1 192.168.10.1 up
 *
 */

/*
 * System-defined header files
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <linux/socket.h>
#include <linux/cache.h>
#include <asm/io.h>
#include <asm/errno.h>
#include <net/pkt_sched.h>
#include <net/ip.h>
#include <linux/sysctl.h>
#include <linux/unistd.h>

/*
 * Intel IXP400 Software specific header files
 */
#include <ixp425.h>
#include <IxEthAcc.h>
#include <IxEthMii.h>
#include <IxEthDB.h>
#include <IxQMgr.h>
#include <IxNpeDl.h>
#include <IxNpeMh.h>
#include <IxOsServices.h>
#include <IxOsBuffPoolMgt.h>
#include <IxEthNpe.h>
#include <IxVersionId.h>
#include <IxFeatureCtrl.h>

/* We want to use interrupts from the XScale PMU timer to
 * drive our NPE Queue Dispatcher loop.  But if this #define
 * is set, then it means the system is already using this timer
 * so we cannot.
 */
#ifdef CONFIG_XSCALE_PMU_TIMER
#error "XScale PMU Timer not available (CONFIG_XSCALE_PMU_TIMER is defined). Cannot continue"
#endif


 /* default mac address assign in flash
  */
#define DEFAULT_MAC_ADDRESS	0x3FFB0
/*
 * Module version information
 */
MODULE_DESCRIPTION("IXP425 NPE Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel Corporation");
#define MODULE_NAME "ixp425_eth"
#define MODULE_VERSION "1.1"

/* Module parameters */
static int npe_learning = 1; /* default : NPE learning & filtering enable */
static int log_level = 0;    /* default : no log */
static int no_csr_init = 0;  /* default : init CSR */
static int no_phy_scan = 1;  /* default : no phy discovery */
static int phy_reset = 0;    /* default : mo phy reset */
/* netdev_max_backlog: ideally /proc/sys/net/core/netdev_max_backlog, but any 
 * value > 46 looks to work. This is used to control the maximum number of 
 * skbuf to push into the linux stack, and avoid the performance degradations 
 * during overflow.
 */
static int netdev_max_backlog = 290;

MODULE_PARM(npe_learning, "i");
MODULE_PARM_DESC(npe_learning, "If non-zero, NPE MAC Address Learning & Filtering feature will be enabled");
MODULE_PARM(log_level, "i");
MODULE_PARM_DESC(log_level, "Set log level: 0 - None, 1 - Verbose, 2 - Debug");
MODULE_PARM(no_csr_init, "i");
MODULE_PARM_DESC(no_csr_init, "If non-zero, do not initialise Intel IXP400 Software Release core components");
MODULE_PARM(no_phy_scan, "i");
MODULE_PARM_DESC(no_phy_scan, "If non-zero, use hard-coded phy addresses");
MODULE_PARM(phy_reset, "i");
MODULE_PARM_DESC(phy_reset, "If non-zero, reset the phys");
MODULE_PARM(netdev_max_backlog, "i");
MODULE_PARM_DESC(netdev_max_backlog, "Should be set to the value of /proc/sys/net/core/netdev_max_backlog (perf affecting)");

/* devices will be called ixp0 and ixp1 */
#define DEVICE_NAME "ixp"

/* boolean values for PHY link speed, duplex, and autonegotiation */
#define PHY_SPEED_10    0
#define PHY_SPEED_100   1
#define PHY_DUPLEX_HALF 0
#define PHY_DUPLEX_FULL 1
#define PHY_AUTONEG_OFF 0
#define PHY_AUTONEG_ON  1

/* will clean skbufs from the sw queues when they are older
 * than this time (this mechanism is needed to prevent the driver 
 * holding skbuf and memory space for too long). Unit are in seconds
 * because the timestamp in buffers are in seconds.
 */
#define BUFFER_MAX_HOLD_TIME_S (3)

/* maintenance time (jiffies) */
#define DB_MAINTENANCE_TIME (IX_ETH_DB_MAINTENANCE_TIME*HZ)

/* Time before kernel will decide that the driver had stuck in transmit (jiffies) */
#define DEV_WATCHDOG_TIMEO (10*HZ)

/* Interval between media-sense/duplex checks (jiffies) */
#define MEDIA_CHECK_INTERVAL (3*HZ)

/* Dictates the rate (per second) at which the NPE queues are serviced */
/*   4000 times/sec = 37 mbufs/interrupt at line rate */
#define QUEUE_DISPATCH_TIMER_RATE (2000)

/* Tunable value, the highest helps for steady rate traffic, but too high
 * increases the packet drops.
 * The lowest helps for bursty traffic (smartApps).
 * Value range from 5 up to 8. 6 is a "good enough" compromize.
 */
#define BACKLOG_TUNE (6)

/* Tunable value, Number of packets that will go to the netif_rx 
 * interface whatever the congestion is.
 * Value range from 2 up to 10. 2 is a "good enough" compromize.
 */
#define BACKLOG_MIN  (2)

/* number of packets to prealloc for the Rx pool (per driver instance) */
#define RX_QUEUE_PREALLOC (80)

/* Maximum number of packets in Tx+TxDone queue */
#define TX_QUEUE_MAX_LEN (256)

/* Number of mbufs in sw queue */
#define MB_QSIZE  (256) /* must be a power of 2 and greater than TX_QUEUE_MAX_LEN */
#define SKB_QSIZE (256) /* must be a power of 2 and greater than RX_PREALLOC */

/* Maximum number of addresses the EthAcc multicast filter can hold */
#define IX_ETH_ACC_MAX_MULTICAST_ADDRESSES (256)

/* Parameter passed to Linux in ISR registration (cannot be 0) */
#define IRQ_ANY_PARAMETER (1)

/* 
 * The size of the SKBs to allocate is more than the MSDU size.
 *
 * skb->head starts here
 * (a) 16 bytes reserved in dev_sk_alloc()
 * (b) 48 RNDIS optional reserved bytes
 * (c) 2 bytes for necessary alignment of the IP header on a word boundary
 * skb->data starts here, the NPE will store the payload at this address
 * (d)      14 bytes  (dev->dev_hard_len)
 * (e)      1500 bytes (dev->mtu, can grow up for Jumbo frames)
 * (f)      4 bytes fcs (stripped out by MAC core)
 * (g)      xxx round-up needed for a NPE 64 bytes boundary
 * skb->tail the NPE will not write more than these value
 * (h)      yyy round-up needed for a 32 bytes cache line boundary
 * (i) sizeof(struct sk_buff). there is some shared info sometimes
 *     used by the system (allocated in net/core/dev.c)
 *
 * The driver private structure stores the following fields
 *
 *  msdu_size = d+e : used to set the msdu size
 *  replenish_size = d+e+g : used for replenish
 *  pkt_size = b+c+d+e+g+h : used to allocate skbufs
 *  alloc_size = a+b+c+d+e+g+h+i, compare to skb->truesize
*/

#ifdef CONFIG_USB_RNDIS
#define HDR_SIZE (2 + 48)
#else
#define HDR_SIZE (2)
#endif

/* dev_alloc_skb reserves 16 bytes in the beginning of the skbuf
 * and sh_shared_info at the end of the skbuf. But the value used
 * used to set truesize in skbuff.c is struct sk_buff (???). This
 * behaviour is reproduced here for consistency.
*/
#define SKB_RESERVED_HEADER_SIZE (16)
#define SKB_RESERVED_TRAILER_SIZE sizeof(struct sk_buff)

/* NPE-B Functionality: Ethernet only */
#define IX_ETH_NPE_B_IMAGE_ID IX_NPEDL_NPEIMAGE_NPEB_ETH

/* NPE-C Functionality: Ethernet only  */
#define IX_ETH_NPE_C_IMAGE_ID IX_NPEDL_NPEIMAGE_NPEC_ETH

/*
 * Macros to turn on/off debug messages
 */
/* Print kernel error */
#define P_ERROR(args...) \
    printk(KERN_ERR MODULE_NAME ": " args)
/* Print kernel warning */
#define P_WARN(args...) \
    printk(KERN_WARNING MODULE_NAME ": " args)
/* Print kernel notice */
#define P_NOTICE(args...) \
    printk(KERN_NOTICE MODULE_NAME ": " args)
/* Print kernel info */
#define P_INFO(args...) \
    printk(KERN_INFO MODULE_NAME ": " args)
/* Print verbose message. Enabled/disabled by 'log_level' param */
#define P_VERBOSE(args...) \
    if (log_level >= 1) printk(MODULE_NAME ": " args)
/* Print debug message. Enabled/disabled by 'log_level' param  */
#define P_DEBUG(args...) \
    if (log_level >= 2) { \
        printk("%s: %s()\n", MODULE_NAME, __FUNCTION__); \
        printk(args); }

#ifdef DEBUG
/* Print trace message */
#define TRACE \
    if (log_level >= 2) printk("%s: %s(): line %d\n", MODULE_NAME, __FUNCTION__, __LINE__)
#else
/* no trace */
#define TRACE 
#endif

/* extern Linux kernel data */
extern struct softnet_data softnet_data[]; /* used to get the current queue level */
extern unsigned long loops_per_jiffy; /* used to calculate CPU clock speed */

/* internal Ethernet Access layer polling entry points */
extern void 
ixEthRxFrameQMCallback(IxQMgrQId qId, IxQMgrCallbackId callbackId);
extern void 
ixEthTxFrameDoneQMCallback(IxQMgrQId qId, IxQMgrCallbackId callbackId);

/* Private device data */
typedef struct {
    spinlock_t lock;  /* multicast management lock */
    
    unsigned int msdu_size;
    unsigned int replenish_size;
    unsigned int pkt_size;
    unsigned int alloc_size;

    struct net_device_stats stats; /* device statistics */

    IxEthAccPortId port_id; /* MAC port_id */

    /* private scheduling discipline */
    struct Qdisc *qdisc;

    /* Implements a software queue for mbufs 
     * This queue is written in the tx done process and 
     * read during tx. Because there is 
     * 1 reader and 1 writer, there is no need for
     * a locking algorithm.
     */

    /* mbuf Tx queue indexes */
    unsigned int mbTxQueueHead;
    unsigned int mbTxQueueTail;

    /* mbuf Rx queue indexes */
    unsigned int mbRxQueueHead;
    unsigned int mbRxQueueTail;

    /* software queue containers */
    IX_MBUF *mbTxQueue[MB_QSIZE];
    IX_MBUF *mbRxQueue[MB_QSIZE];

    /* preallocated RX pool */
    IX_MBUF rx_pool_buf[RX_QUEUE_PREALLOC];
    IX_MBUF_POOL *rx_pool;

    /* preallocated TX pool */
    IX_MBUF tx_pool_buf[TX_QUEUE_MAX_LEN];
    IX_MBUF_POOL *tx_pool;

    /* id of thread for the link duplex monitoring */
    int maintenanceCheckThreadId;

    /* mutex locked by thread, until the thread exits */
    struct semaphore *maintenanceCheckThreadComplete;

    /* Used to stop the kernel thread for link monitoring. */
    volatile BOOL maintenanceCheckStopped;

    /* used for tx timeout */
    struct tq_struct tq_timeout;

    /* used to control the message output */
    UINT32 devFlags;
} priv_data_t;

/* Collection of boolean PHY configuration parameters */
typedef struct {
    BOOL speed100;
    BOOL duplexFull;
    BOOL autoNegEnabled;
    BOOL linkMonitor;
} phy_cfg_t;

/*
 * STATIC VARIABLES
 *
 * This section sets several default values for each port.
 * These may be edited if required.
 */

/* values used inside the irq */
static unsigned long timer_countup_ticks;
static IxQMgrDispatcherFuncPtr dispatcherFunc;
static struct timeval  irq_stamp;  /* time of interrupt */
static unsigned int maxbacklog = RX_QUEUE_PREALLOC;

/* Implements a software queue for skbufs 
 * This queue is written in the tx done process and 
 * read during rxfree. Because there is 
 * 1 reader and 1 writer, there is no need for
 * a locking algorithm.
 *
 * This software queue is shared by both ports.
 */

/* skbuf queue indexes */
static unsigned int skQueueHead;
static unsigned int skQueueTail;

/* software queue containers */
static struct sk_buff *skQueue[SKB_QSIZE];

/* 
 * The PHY addresses mapped to Intel IXP400 Software EthAcc ports.
 *
 * These are hardcoded and ordered by increasing ports.
 * Overwriting these values by a PHY discovery is disabled by default but
 * can optionally be enabled if required
 * by passing module param no_phy_scan with a zero value
 *
 * Up to 32 PHYs may be discovered by a phy scan. Addresses
 * of all PHYs found will be stored here, but only the first
 * 2 will be used with the Intel IXP400 Software EthAcc ports.
 *
 * See also the function phy_init() in this file.
 *
 * NOTE: The hardcoded PHY addresses have been verified on
 * the IXDP425 and Coyote (IXP4XX RG) Development platforms.
 * However, they may differ on other platforms.
 */
static int phyAddresses[IXP425_ETH_ACC_MII_MAX_ADDR] =
{
#ifdef CONFIG_ARCH_IXDP425
 /*
 		* Our board only one Ethernet port. And this port connect to PHY 2 while 
        * we call this port ixp0. So we need to cross port 1 and 2. 
        *                                          Argon Cheng 
        *                                          2004-07-23 
*/ 

    /* 1 PHY per NPE port */
    1, /* Port 1 (ixp0) */
    0  /* Port 2 (ixp1) */

#elif defined(CONFIG_ARCH_COYOTE) || defined(CONFIG_ARCH_IXRD425)
    4, /* Port 1 (ixp0) - Connected to PHYs 1-4               */
    5, /* Port 2 (ixp1) - Only connected to PHY 5             */

    3,  /******************************************************/
    2,  /* PHY addresses on Coyote platform (physical layout) */
    1   /* (4 LAN ports, switch)  (1 WAN port)                */
        /*       ixp0              ixp1                       */
        /*  ________________       ____                       */
        /* /_______________/|     /___/|                      */
	/* | 1 | 2 | 3 | 4 |      | 5 |                       */
        /* ----------------------------------------           */
#else
    /* other platforms : suppose 1 PHY per NPE port */
    0, /* Port 1 (ixp0) */
    1  /* Port 2 (ixp1) */

#endif
};

/* The default configuration of the phy on each Intel IXP400 Software EthAcc port.
 *
 * This configuration is loaded when the phy's are discovered.
 * More PHYs can optionally be configured here by adding more
 * configuration entries in the array below.  The PHYs will be
 * configured in the order that they are found, starting with the
 * lowest PHY address.
 *
 * See also function phy_init() in this file
 */
static phy_cfg_t default_phy_cfg[] =
{
#ifdef CONFIG_ARCH_IXDP425
    {PHY_SPEED_100, PHY_DUPLEX_FULL, PHY_AUTONEG_ON,TRUE},/* Port 1: monitor the phy */
    {PHY_SPEED_100, PHY_DUPLEX_FULL, PHY_AUTONEG_ON,TRUE} /* Port 2: monitor the link */

#elif defined(CONFIG_ARCH_COYOTE) || defined(CONFIG_ARCH_IXRD425)
    {PHY_SPEED_100, PHY_DUPLEX_FULL, PHY_AUTONEG_ON,FALSE},/* Port 1: NO link */
    {PHY_SPEED_100, PHY_DUPLEX_FULL, PHY_AUTONEG_ON,TRUE},/* Port 2: monitor the link */
    {PHY_SPEED_100, PHY_DUPLEX_FULL, PHY_AUTONEG_ON,FALSE},
    {PHY_SPEED_100, PHY_DUPLEX_FULL, PHY_AUTONEG_ON,FALSE},
    {PHY_SPEED_100, PHY_DUPLEX_FULL, PHY_AUTONEG_ON,FALSE}

#else
    {PHY_SPEED_100, PHY_DUPLEX_FULL, PHY_AUTONEG_ON,TRUE},/* Port 1: monitor the link*/
    {PHY_SPEED_100, PHY_DUPLEX_FULL, PHY_AUTONEG_ON,TRUE} /* Port 2: monitor the link*/

#endif
};

/* Default MAC addresses for ixp0 and ixp1 (using Intel MAC prefix) */
static IxEthAccMacAddr default_mac_addr[IX_ETH_ACC_NUMBER_OF_PORTS] =
{
    {{0x00, 0x02, 0xB3, 0x01, 0x01, 0x01}}, /* Port 1 */
    {{0x00, 0x02, 0xB3, 0x02, 0x02, 0x02}}  /* Port 2 */
};

/* Mutex lock used to coordinate access to IxEthAcc functions
 * which manipulate the MII registers on the PHYs
 */
static struct semaphore *miiAccessMutex;

/* mutex locked when maintenance is being performed */
static struct semaphore *maintenance_mutex;

/* Flags which is set when corresponding NPE is running,
 * cleared when NPE is stopped
 */
static int npeRunning[IX_ETH_ACC_NUMBER_OF_PORTS];

/* Flags which is set when the corresponding IRQ is running,
 */
static int irq_pmu_used = 0;
static int irq_qm1_used = 0;

/*
 * ERROR COUNTERS
 */

static UINT32 skbAllocFailErrorCount = 0;
static UINT32 replenishErrorCount = 0;
static UINT32 chainedRxErrorCount = 0;

/*
 * ERROR NUMBER FUNCTIONS
 */

/* Convert IxEthAcc return codes to suitable Linux return codes */
static int convert_error_ethAcc (IxEthAccStatus error)
{
    switch (error)
    {
	case IX_ETH_ACC_SUCCESS:            return  0;
	case IX_ETH_ACC_FAIL:               return -1;
	case IX_ETH_ACC_INVALID_PORT:       return -ENODEV;
	case IX_ETH_ACC_PORT_UNINITIALIZED: return -EPERM;
	case IX_ETH_ACC_MAC_UNINITIALIZED:  return -EPERM;
	case IX_ETH_ACC_INVALID_ARG:        return -EINVAL;
	case IX_ETH_TX_Q_FULL:              return -EAGAIN;
	case IX_ETH_ACC_NO_SUCH_ADDR:       return -EFAULT;
	default:                            return -1;
    };
}

/*
 * DEBUG UTILITY FUNCTIONS
 */
#ifdef DEBUG_DUMP

static void hex_dump(void *buf, int len)
{
    int i;

    for (i = 0 ; i < len; i++)
    {
	printk("%02x", ((u8*)buf)[i]);
	if (i%2)
	    printk(" ");
	if (15 == i%16)
	    printk("\n");
    }
    printk("\n");
}

static void mbuf_dump(char *name, IX_MBUF *mbuf)
{
    printk("+++++++++++++++++++++++++++\n"
	"%s MBUF dump mbuf=%p, m_data=%p, m_len=%d, len=%d\n",
	name, mbuf, mbuf->m_data, mbuf->m_len, mbuf->m_pkthdr.len);
    printk(">> mbuf:\n");
    hex_dump(mbuf, sizeof(*mbuf));
    printk(">> m_data:\n");
    hex_dump(__va(mbuf->m_data), mbuf->m_pkthdr.len);
    printk("\n-------------------------\n");
}

static void skb_dump(char *name, struct sk_buff *skb)
{
    printk("+++++++++++++++++++++++++++\n"
	"%s SKB dump skb=%p, data=%p, tail=%p, len=%d\n",
	name, skb, skb->data, skb->tail, skb->len);
    printk(">> data:\n");
    hex_dump(skb->data, skb->len);
    printk("\n-------------------------\n");
}
#endif

/*
 * MEMORY MANAGEMENT
 */

/* XScale specific : preload a cache line to memeory */
static inline void dev_preload(void *virtualPtr) 
{
   __asm__ (" pld [%0]\n" : : "r" (virtualPtr));
}

static inline void dev_skb_preload(struct sk_buff *skb)
{
    /* from include/linux/skbuff.h, skb->len field and skb->data filed 
     * don't share the same cache line (more than 32 bytes between the fields)
     */
    dev_preload(&skb->len);
    dev_preload(&skb->data);
}

/*
 * BUFFER MANAGEMENT
 */

/*
 * Utility functions to handle software queues
 * Each driver has a software queue of skbufs and 
 * a software queue of mbufs for tx and mbufs for rx
 */
#define MB_QINC(index) ((index)++ & (MB_QSIZE - 1))
#define SKB_QINC(index) ((index)++ & (SKB_QSIZE - 1))
#define SKB_QFETCH(index) ((index) & (SKB_QSIZE - 1))

/* dev_skb_dequeue: remove a skb from the skb queue */
static inline struct sk_buff * dev_skb_dequeue(priv_data_t *priv)
{
    struct sk_buff *skb;
    if (skQueueHead != skQueueTail)
    {
	/* get from queue (fast) packet is ready for use 
	 * because the fields are reset during the enqueue
	 * operations
	 */
	skb = skQueue[SKB_QINC(skQueueTail)];
	if (skQueueHead != skQueueTail)
	{
	    /* preload the next skbuf : this is an optimisation to 
	     * avoid stall cycles when acessing memory.
	     */
	    dev_skb_preload(skQueue[SKB_QFETCH(skQueueTail)]);
	}
	/* check the skb size fits the driver requirements 
	 */
	if (skb->truesize >= priv->alloc_size)
	{
	    return skb;
	}
	/* the skbuf is too small : put it to pool (slow)
	 * This may occur when the ports are configured
	 * with a different mtu size.
	 */
	dev_kfree_skb_any(skb);
    }
    
    /* get a skbuf from pool (slow) */
    skb = dev_alloc_skb(priv->pkt_size);
    if (skb != NULL)
    {
	skb_reserve(skb, HDR_SIZE);
	skb->len = priv->replenish_size;
	IX_ACC_DATA_CACHE_INVALIDATE(skb->data, priv->replenish_size);
    }
    else
    {
	skbAllocFailErrorCount++;
    }
    
    return skb;
}

/* dev_skb_enqueue: add a skb to the skb queue */
static inline void dev_skb_enqueue(priv_data_t *priv, struct sk_buff *skb)
{
    /* check for big-enough unshared skb, and check the queue 
     * is not full If the queue is full or the complete ownership of the
     * skb is not guaranteed, just free the skb.
     * (atomic counters are read on the fly, there is no need for lock)
     */

    if ((skb->truesize >= priv->alloc_size) && 
	(skb->users.counter == 1) && 
	(skb->cloned == 0) && 
	(skb_shinfo(skb)->dataref.counter == 1) &&
        (skb_shinfo(skb)->nr_frags == 0) &&
        (skb_shinfo(skb)->frag_list == NULL) &&
	(skQueueHead - skQueueTail < SKB_QSIZE)&&
        (skb->destructor == NULL))
    {
 	/* put big unshared mbuf to queue (fast)
	 * reset the skb fields, so they will be ready for reuse
	 * during rx.
	 * The following fields are reset during a skb_free and 
	 * skb_alloc sequence (dev/core/skbuf.h)
	 */
	skb->sk = NULL;
        skb->dst = NULL;
	skb->pkt_type = PACKET_HOST;    /* Default type */
        skb->ip_summed = 0;
        skb->priority = 0;
        skb->security = 0;

/* Some packets may get incorrectly process by netfilter firewall software
 * if CONFIG_NETFILTER is enabled and filtering is in use.  The solution is to
 * reset the following fields in the skbuff before re-using it on the Rx-path
 */
#ifdef CONFIG_NETFILTER
        skb->nfmark = skb->nfcache = 0;
        nf_conntrack_put(skb->nfct);
        skb->nfct = NULL;
#ifdef CONFIG_NETFILTER_DEBUG
        skb->nf_debug = 0;
#endif
#endif
#ifdef CONFIG_NET_SCHED
	skb->tc_index = 0;
#endif

	/* reset the data pointer (skb_reserve is not used for efficiency) */
	skb->data = skb->head + SKB_RESERVED_HEADER_SIZE + HDR_SIZE;
	skb->len = priv->replenish_size;

	/* invalidate the payload */
	IX_ACC_DATA_CACHE_INVALIDATE(skb->data, skb->len);

	skQueue[SKB_QINC(skQueueHead)] = skb;
    }
    else
    {
	/* put to pool (slow) */
	dev_kfree_skb_any(skb);
    }
}

/* dev_skb_queue_drain: remove all entries from the skb queue */
static void dev_skb_queue_drain(priv_data_t *priv)
{
    struct sk_buff *skb;
    int key = ixOsServIntLock();

    /* check for skbuf, then get it and release it */
    while (skQueueHead != skQueueTail)
    {
	skb = dev_skb_dequeue(priv);
	dev_kfree_skb_any(skb);
    }

    ixOsServIntUnlock(key);
}

/* dev_rx_mb_dequeue: return one mbuf from the rx mbuf queue */
static inline IX_MBUF * dev_rx_mb_dequeue(priv_data_t *priv)
{
    IX_MBUF *mbuf;
    if (priv->mbRxQueueHead != priv->mbRxQueueTail)
    {
	/* get from queue (fast) */
	mbuf = priv->mbRxQueue[MB_QINC(priv->mbRxQueueTail)];
    }
    else
    {
	/* get from pool (slow) */
	IX_MBUF_POOL_GET(priv->rx_pool, &mbuf);
    }
    return mbuf;
}

/* dev_rx_mb_enqueue: add one mbuf to the rx mbuf queue */
static inline void dev_rx_mb_enqueue(priv_data_t *priv, IX_MBUF *mbuf)
{
    /* check for queue not full */
    if (priv->mbRxQueueHead - priv->mbRxQueueTail < MB_QSIZE)
    {
	/* put to queue (fast) */
	priv->mbRxQueue[MB_QINC(priv->mbRxQueueHead)] = mbuf;
    }
    else
    {
	IX_MBUF_POOL_PUT(mbuf);
    }
}

/* dev_rx_mb_queue_drain: remove all mbufs from the rx mbuf queue */
static void dev_rx_mb_queue_drain(priv_data_t *priv)
{
    IX_MBUF *mbuf;
    int key = ixOsServIntLock();

    /* free all queue entries */
    while(priv->mbRxQueueHead != priv->mbRxQueueTail)
    {
	mbuf = dev_rx_mb_dequeue(priv);
	IX_MBUF_POOL_PUT(mbuf);
    }

    ixOsServIntUnlock(key);
}

/* dev_tx_mb_dequeue: remove one mbuf from the tx mbuf queue */
static inline IX_MBUF * dev_tx_mb_dequeue(priv_data_t *priv)
{
    IX_MBUF *mbuf;
    if (priv->mbTxQueueHead != priv->mbTxQueueTail)
    {
	/* int key = ixOsServIntLock(); */
	/* get from queue (fast) */
	mbuf = priv->mbTxQueue[MB_QINC(priv->mbTxQueueTail)];
	/* ixOsServIntUnlock(key); */
    }
    else
    {
	/* get from pool (slow) */
	IX_MBUF_POOL_GET(priv->tx_pool, &mbuf);
    }
    return mbuf;
}

/* dev_tx_mb_enqueue: add one mbuf to the tx mbuf queue */
static inline void dev_tx_mb_enqueue(priv_data_t *priv, IX_MBUF *mbuf)
{
    /* check for queue not full */
    if (priv->mbTxQueueHead - priv->mbTxQueueTail < MB_QSIZE)
    {
	/* put to queue (fast) */
	priv->mbTxQueue[MB_QINC(priv->mbTxQueueHead)] = mbuf;
    }
    else
    {
	IX_MBUF_POOL_PUT(mbuf);
    }
}

/* dev_tx_mb_queue_drain: remove all mbufs from the tx mbuf queue */
static void dev_tx_mb_queue_drain(priv_data_t *priv)
{
    IX_MBUF *mbuf;
    int key = ixOsServIntLock();

    /* free all queue entries */
    while(priv->mbTxQueueHead != priv->mbTxQueueTail)
    {
	mbuf = dev_tx_mb_dequeue(priv);
	IX_MBUF_POOL_PUT(mbuf);
    }

    ixOsServIntUnlock(key);
}

/* provides mbuf+skb pair to the NPEs.
 * In the case of an error, free skb and return mbuf to the pool
 */
static void dev_rx_buff_replenish(int port_id, IX_MBUF *mbuf)
{
    IX_STATUS status;

    /* reset the flags field */
    mbuf->m_flags = 0;

    /* send mbuf to the NPEs */
    if ((status = ixEthAccPortRxFreeReplenish(port_id, mbuf)) != IX_SUCCESS)
    {
	replenishErrorCount++;

	P_WARN("ixEthAccPortRxFreeReplenish failed for port %d, res = %d",
		port_id, status);
	
	/* detach the skb from the mbuf, free it, then free the mbuf */
	dev_kfree_skb_any(mbuf_swap_skb(mbuf, NULL));
	IX_MBUF_POOL_PUT(mbuf);
    }
}

/* Allocate an skb for every mbuf in the rx_pool
 * and pass the pair to the NPEs
 */
static int dev_rx_buff_prealloc(priv_data_t *priv)
{
    int res = 0;
    IX_MBUF *mbuf;
    struct sk_buff *skb;
    int key;

    while ((IX_MBUF_POOL_GET(priv->rx_pool, &mbuf) == IX_SUCCESS))
    {
	/* because this function may be called during monitoring steps
	 * interrupt are disabled so it won't conflict with the
	 * QMgr context.
	 */
	key = ixOsServIntLock();
	/* get a skbuf (alloc from pool if necessary) */
	skb = dev_skb_dequeue(priv);

	if (skb == NULL)
	{
	    /* failed to alloc skb -> return mbuf to the pool, it'll be
	     * picked up later by the monitoring task
	     */
	    IX_MBUF_POOL_PUT(mbuf);
	    res = -ENOMEM;
	}
	else
	{
	    /* attach a skb to the mbuf */
	    mbuf_swap_skb(mbuf, skb);
	    dev_rx_buff_replenish(priv->port_id, mbuf);
	}
	ixOsServIntUnlock(key);

	if (res != 0)
	    return res;
    }

    return 0;
}


/* Replenish if necessary and slowly drain the sw queues
 * to limit the driver holding the memory space permanently.
 * This action should run at a low frequency, e.g during
 * the link maintenance.
 */
static int dev_buff_maintenance(struct net_device *dev)
{
    struct sk_buff *skb;
    int key;
    priv_data_t *priv = dev->priv;

    dev_rx_buff_prealloc(priv);

    /* Drain slowly the skb queue and free the skbufs to the 
     * system. This way, after a while, skbufs will be 
     * reused by other components, instead of being
     * parmanently held by this driver.
     */
    key = ixOsServIntLock();
    
    /* check for queue not empty, then get the skbuff and release it */
    if (skQueueHead != skQueueTail)
    {
	skb = dev_skb_dequeue(priv);
	dev_kfree_skb_any(skb);
    }
    ixOsServIntUnlock(key);

    return 0;
}


/* 
 * KERNEL THREADS
 */

/* This timer will check the PHY for the link duplex and
 * update the MAC accordingly. It also executes some buffer
 * maintenance to release mbuf in excess or replenish after
 * a severe starvation
 *
 * This function loops and wake up every 3 seconds.
 */
static int dev_media_check_thread (void* arg)
{
    struct net_device *dev = (struct net_device *) arg;
    priv_data_t *priv = dev->priv;
    int linkUp;
    int speed100;
    int fullDuplex = -1; /* unknown duplex mode */
    int newDuplex;
    int autonegotiate;
    unsigned phyNum = phyAddresses[priv->port_id];
    int res;

    TRACE;

    /* Lock the mutex for this thread.
       This mutex can be used to wait until the thread exits
    */
    down (priv->maintenanceCheckThreadComplete);

    daemonize();
    reparent_to_init();
    spin_lock_irq(&current->sigmask_lock);
    sigemptyset(&current->blocked);
    recalc_sigpending(current);
    spin_unlock_irq(&current->sigmask_lock);
    
    snprintf(current->comm, sizeof(current->comm), "ixp425 %s", dev->name);

    TRACE;
    
    while (1)
    {
	/* We may have been woken up by a signal. If so, we need to flush it out */
	if (signal_pending (current)) {
	    spin_lock_irq(&current->sigmask_lock);
	    flush_signals(current);
	    spin_unlock_irq(&current->sigmask_lock);
	}

	/* If the interface is down, we need to exit this thread */
	if (priv->maintenanceCheckStopped == TRUE)
	{
	    break;
	}
	
	/*
	 * Determine the link status
	 */

	TRACE;

	if (default_phy_cfg[priv->port_id].linkMonitor)
	{
	    /* lock the MII register access mutex */
	    down(miiAccessMutex);
	    
	    res = ixEthMiiLinkStatus(phyNum,
				     &linkUp,
				     &speed100,
				     &newDuplex, 
				     &autonegotiate);
	    /* release the MII register access mutex */
	    up(miiAccessMutex);
	    
	    if (priv->maintenanceCheckStopped == TRUE)
	    {
		break;
	    }
	    
	    if (res != IX_ETH_ACC_SUCCESS)
	    {
		P_WARN("ixEthMiiLinkStatus failed on PHY%d.\n"
		       "\tCan't determine\nthe auto negotiated parameters. "
		       "Using default values.\n",
		       phyNum); 
		
		/* this shouldn't happen. exit the thread if it does */
		break;
	    }
	    
	    if (linkUp)
	    {
		if (! netif_carrier_ok(dev))
		{
		    /* inform the kernel of a change in link state */
		    netif_carrier_on(dev);
		}

		/*
		 * Update the MAC mode to match the PHY mode if 
		 * there is a phy mode change.
		 */
		if (newDuplex != fullDuplex)
		{
		    fullDuplex = newDuplex;
		    if (fullDuplex)
		    {
			ixEthAccPortDuplexModeSet (priv->port_id, IX_ETH_ACC_FULL_DUPLEX);
		    }
		    else
		    {
			ixEthAccPortDuplexModeSet (priv->port_id, IX_ETH_ACC_HALF_DUPLEX);
		    }
		}
	    }
	    else
	    {
		fullDuplex = -1;
		if (netif_carrier_ok(dev))
		{
		    /* inform the kernel of a change in link state */
		    netif_carrier_off(dev);
		}
	    }
	}
    
	TRACE;
    
	/* this is to prevent the rx pool from emptying when
	 * there's not enough memory for a long time
	 * It prevents also from holding the memory for too
	 * long
	 */
	dev_buff_maintenance(dev);
    
	/* Now sleep for 3 seconds */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MEDIA_CHECK_INTERVAL);
    } /* while (1) ... */

    /* free the mutex for this thread. */
    up (priv->maintenanceCheckThreadComplete);

    return 0;
}

/*
 * TIMERS
 *
 * PMU Timer : This timer based on IRQ  will call the qmgr dispatcher 
 * function a few thousand times per second.
 *
 * Maintenance Timer : This timer run the maintanance action every 
 * 60 seconds approximatively.
 *
 */

/* PMU Timer reload : this should be done at each interrupt */
static void dev_pmu_timer_restart(void)
{
    __asm__(" mcr p14,0,%0,c1,c1,0\n"  /* write current counter */
            : : "r" (timer_countup_ticks));

    __asm__(" mrc p14,0,r1,c4,c1,0; "  /* get int enable register */
            " orr r1,r1,#1; "
            " mcr p14,0,r1,c5,c1,0; "  /* clear overflow */
            " mcr p14,0,r1,c4,c1,0\n"  /* enable interrupts */
            : : : "r1");
}

/* Internal ISR : run a few thousand times per second and calls 
 * the queue manager dispatcher entry point.
 */
static void dev_qmgr_os_isr(int irg, void *dev_id, struct pt_regs *regs)
{
    int qlevel = softnet_data[0].input_pkt_queue.qlen;

    /* at the interrupt entry, the queue contains already a few entries 
     * so it is safe to decrease the number of entries
     * that should be added during this interrupt.
     *
     * This algorithm prevents the expensive throttling done
     * in net/core/dev.c
     *
     * maxbacklog will be the maximum number of Rx buffers to push
     * into the stack for this interrupt.
     */
    if (qlevel != 0)
    {
	maxbacklog = netdev_max_backlog - qlevel;
	if (maxbacklog > BACKLOG_MIN) 
	{
	    maxbacklog -= BACKLOG_MIN; 
	}
	else  
	{
	    maxbacklog = BACKLOG_MIN;
	}
    }
    else
    {
       maxbacklog = (maxbacklog + netdev_max_backlog) / 2;
    }

    /* get the time of this interrupt : all buffers received during this
     * interrupt will be assigned the same time */
    do_gettimeofday(&irq_stamp);

    /* call the queue manager entry point */
    dispatcherFunc(IX_QMGR_QUELOW_GROUP);
}

/* Internal ISR : run a few thousand times per second and calls 
 * the ethernet entry point.
 */
static void dev_poll_os_isr(int irg, void *dev_id, struct pt_regs *regs)
{
    int qlevel = softnet_data[0].input_pkt_queue.qlen;

    dev_pmu_timer_restart(); /* set up the timer for the next interrupt */

    /* at the interrupt entry, the queue contains already a few entries 
     * so it is safe to decrease the number of entries
     * that should be added during this interrupt.
     *
     * This algorithm prevents the expensive throttling done
     * in net/core/dev.c
     *
     * maxbacklog will be the maximum number of Rx buffers to push
     * into the stack for this interrupt.
     */
    if (qlevel != 0)
    {
	maxbacklog = netdev_max_backlog - qlevel;
	if (maxbacklog > BACKLOG_MIN) 
	{
	    maxbacklog -= BACKLOG_MIN; 
	}
	else  
	{
	    maxbacklog = BACKLOG_MIN;
	}
    }
    else
    {
       maxbacklog = (maxbacklog + netdev_max_backlog) / 2;
    }

    /* get the time of this interrupt : all buffers received during this
     * interrupt will be assigned the same time */
    do_gettimeofday(&irq_stamp);

    ixEthRxFrameQMCallback(0,0);
    ixEthTxFrameDoneQMCallback(0,0);
}

/* initialize the PMU timer */
static int dev_pmu_timer_init(void)
{
    UINT32 controlRegisterMask =
        BIT(0) | /* enable counters */
        BIT(2);  /* reset clock counter; */
        
    /* 
    *   Compute the number of xscale cycles needed between each 
    *   PMU IRQ. This is done from the result of an OS calibration loop.
    *
    *   For 533MHz CPU, 533000000 tick/s / 4000 times/sec = 138250
    *   4000 times/sec = 37 mbufs/interrupt at line rate 
    *   The pmu timer is reset to -138250 = 0xfffde3f6, to trigger an IRQ
    *   when this up counter overflows.
    *
    *   The multiplication gives a number of instructions per second.
    *   which is close to the processor frequency, and then close to the
    *   PMU clock rate.
    *
    *      HZ : jiffies/second (global OS constant)
    *      loops/jiffy : global OS value cumputed at boot time
    *      2 is the number of instructions per loop
    *
    */
    UINT32 timer_countdown_ticks = (loops_per_jiffy * HZ * 2) / 
	QUEUE_DISPATCH_TIMER_RATE;

    timer_countup_ticks = -timer_countdown_ticks; 

    /* enable the CCNT (clock count) timer from the PMU */
    __asm__(" mcr p14,0,%0,c0,c1,0\n"  /* write control register */
            : : "r" (controlRegisterMask));
    
    return 0;
}

/* stops the timer when the module terminates */
static void dev_pmu_timer_disable(void)
{
    __asm__(" mrc p14,0,r1,c4,c1,0; "  /* get int enable register */
            " and r1,r1,#0x1e; "
            " mcr p14,0,r1,c4,c1,0\n"  /* disable interrupts */
            : : : "r1");
}

/* This timer will call ixEthDBDatabaseMaintenance every
 * IX_ETH_DB_MAINTENANCE_TIME jiffies
 */
static void maintenance_timer_cb(unsigned long data);

static struct timer_list maintenance_timer = {
    function:&maintenance_timer_cb
};

static void maintenance_timer_task(void *data);

/* task spawned by timer interrupt for EthDB maintenance */
static struct tq_struct tq_maintenance = {
  routine:maintenance_timer_task
};

static void maintenance_timer_set(void)
{
    maintenance_timer.expires = jiffies + DB_MAINTENANCE_TIME;
    add_timer(&maintenance_timer);
}

static void maintenance_timer_clear(void)
{
    del_timer_sync(&maintenance_timer);
}

static void maintenance_timer_task(void *data)
{
    down(maintenance_mutex);
    ixEthDBDatabaseMaintenance();
    up(maintenance_mutex);
}

static void maintenance_timer_cb(unsigned long data)
{
    schedule_task(&tq_maintenance);

    maintenance_timer_set();
}

/*
 *  DATAPLANE
 */

/* This callback is called when transmission of the packed is done, and
 * IxEthAcc does not need the buffer anymore. The port is down or
 * a portDisable is running. The action is to free the buffers
 * to the pools.
 */
static void tx_done_disable_cb(UINT32 callbackTag, IX_MBUF *mbuf)
{
    struct net_device *dev = (struct net_device *)callbackTag;
    priv_data_t *priv = dev->priv;

    TRACE;

    priv->stats.tx_packets++; /* total packets transmitted */
    priv->stats.tx_bytes += mbuf->m_len; /* total bytes transmitted */

    /* extract skb from the mbuf, free skb and return the mbuf to the pool */
    dev_kfree_skb_any(mbuf_swap_skb(mbuf, NULL));
    IX_MBUF_POOL_PUT(mbuf);

    TRACE;
}


/* This callback is called when transmission of the packed is done, and
 * IxEthAcc does not need the buffer anymore. The buffers will be returned to
 * the software queues.
 */
static void tx_done_cb(UINT32 callbackTag, IX_MBUF *mbuf)
{
    struct net_device *dev = (struct net_device *)callbackTag;
    priv_data_t *priv = dev->priv;

    TRACE;

    priv->stats.tx_packets++; /* total packets transmitted */
    priv->stats.tx_bytes += mbuf->m_len; /* total bytes transmitted */

    /* extract skb from the mbuf, free skb and return the mbuf to the pool */
    dev_skb_enqueue(priv, mbuf_swap_skb(mbuf, NULL));
    dev_tx_mb_enqueue(priv, mbuf);
}

/* This callback is called when transmission of the packed is done, and
 * IxEthAcc does not need the buffer anymore. The buffers will be returned to
 * the software queues.  Also, it checks to see if the netif_queue has been
 * stopped (due to buffer starvation) and it restarts it because it now knows
 * that there is at least 1 mbuf available for Tx.
 */
static void tx_done_queue_stopped_cb(UINT32 callbackTag, IX_MBUF *mbuf)
{
    struct net_device *dev = (struct net_device *)callbackTag;
    priv_data_t *priv = dev->priv;

    TRACE;

    tx_done_cb(callbackTag, mbuf);

    if (netif_queue_stopped(dev))
    {
        ixEthAccPortTxDoneCallbackRegister(priv->port_id, 
                                           tx_done_cb,
                                           (UINT32)dev);
        netif_wake_queue(dev);
    }
}

/* the following function performs the operations normally done
 * in eth_type_trans() (see net/ethernet/eth.c) , and takes care about 
 * the flags returned by the NPE, so a payload lookup is not needed
 * in most of the cases.
 */
static inline void dev_eth_type_trans(int mflags, 
					 struct sk_buff *skb, 
					 struct net_device *dev)
{
    unsigned header_len = dev->hard_header_len;
    skb->mac.raw=skb->data;
    /* skip the mac header : there is no need for length comparison since
     * the skb during a receive is always greater than the header size and 
     * runt frames are not enabled.
     */
    skb->data += header_len;
    skb->len -= header_len;
    
    /* fill the pkt arrival time (set at the irq callback entry) */
    skb->stamp.tv_sec = irq_stamp.tv_sec;
    skb->stamp.tv_usec = irq_stamp.tv_usec;
    
    /* fill the input device field */
    skb->dev = dev;
    
    /* set the protocol from the bits filled by the NPE */
    if (mflags & BIT(6))
    { 
	/* the type_length field is 0x0800 */
	skb->protocol = htons(ETH_P_IP); 
    }
    else
    {
	/* use linux algorithm to find the protocol 
	 * from the type-length field. This costs a
	 * a lookup inside the packet payload. The algorithm
	 * and its constants are taken from the eth_type_trans()
	 * function.
	 */
	struct ethhdr *eth = skb->mac.ethernet;
	unsigned short hproto = ntohs(eth->h_proto);
	
	if (hproto >= 1536)
	{
	    skb->protocol = eth->h_proto;
	}
	else
	{
	    unsigned short rawp = *(unsigned short *)skb->data;
	    if (rawp == 0xFFFF)
		skb->protocol = htons(ETH_P_802_3);
	    else
		skb->protocol = htons(ETH_P_802_2);
	}
    }

	/* set the packet type 
	 * check mcast and bcast bits as filled by the NPE 
	 */
	if (mflags & (BIT(4) | BIT(5)))
	{
	    if (mflags & BIT(4))
	    {
		/* the packet is a broadcast one */
		skb->pkt_type=PACKET_BROADCAST;
	    }
	    else
	    {
		/* the packet is a multicast one */
		skb->pkt_type=PACKET_MULTICAST;
		((priv_data_t *)(dev->priv))->stats.multicast++;
	    }
	}
	else
	{
	    if (dev->flags & IFF_PROMISC)
	    {
		/* check dest mac address only if promiscuous
		 * mode is set This costs
		 * a lookup inside the packet payload.
		 */
		struct ethhdr *eth = skb->mac.ethernet;
		unsigned char *hdest = eth->h_dest;

		if (memcmp(hdest, dev->dev_addr, ETH_ALEN)!=0)
		{
		    skb->pkt_type = PACKET_OTHERHOST;
		}
	    }
	    else
	    {
		/* promiscuous mode is not set, All packets are filtered
		 * by the NPE and the destination MAC address matches
		 * the driver setup. There is no need for a lookup in the 
		 * payload.
		 */
	    }
	}

 	return;
}

/* This callback is called when new packet received from MAC
 * and not ready to be transfered up-stack. (portDisable
 * is in progress or device is down)
 *
 */
static void rx_disable_cb(UINT32 callbackTag, IX_MBUF *mbuf, IxEthAccPortId portId)
{
    TRACE;

    /* this is a buffer returned by NPEs during a call to PortDisable: 
     * free the skb & return the mbuf to the pool 
     */
    dev_kfree_skb_any(mbuf_swap_skb(mbuf, NULL));
    IX_MBUF_POOL_PUT(mbuf);

    TRACE;
}


/* This callback is called when new packet received from MAC
 * and ready to be transfered up-stack.
 *
 * If this is a valid packet, then new skb is allocated and switched
 * with the one in mbuf, which is pushed upstack.
 *
 */
static void rx_cb(UINT32 callbackTag, IX_MBUF *mbuf, IxEthAccPortId portId)
{
    struct net_device *dev;
    priv_data_t *priv;
    struct sk_buff *skb;
    int len;
    int mcastFlags;
    unsigned int qlevel;
   
    TRACE;

    dev = (struct net_device *)callbackTag;
    priv = dev->priv;

    qlevel = softnet_data[0].input_pkt_queue.qlen;

    /* check if the system accepts more traffic and
     * against chained mbufs 
     */
    if (qlevel < maxbacklog && mbuf->m_next == NULL)
    {      
	/* the netif_rx queue is not overloaded */
	TRACE;
  
	len = mbuf->m_len;
	mcastFlags = mbuf->m_flags;

	/* allocate new skb and "swap" it with the skb that is tied to the
	 * mbuf. then return the mbuf + new skb to the NPEs.
	 */
	skb = dev_skb_dequeue(priv);
	
	/* extract skb from mbuf and replace it with the new skbuf */
	skb = mbuf_swap_skb(mbuf, skb);

	if (mbuf->priv)
	{
	    TRACE;

	    /* a skb is attached to the mbuf */
	    dev_rx_buff_replenish(priv->port_id, mbuf);
	}
	else
	{
	    /* failed to alloc skb -> return mbuf to the pool, it'll be
	     * picked up later by the monitoring task when skb will
	     * be available again
	     */
	    TRACE;

	    IX_MBUF_POOL_PUT(mbuf);
	}

	/* set the length of the received skb from the mbuf length  */
	skb->tail = skb->data + len;
	skb->len = len;
	
#ifdef DEBUG_DUMP
	skb_dump("rx", skb);
#endif
	/* Set the skb protocol and set mcast/bcast flags */
	dev_eth_type_trans(mcastFlags, skb, dev);

	/* update the stats */
	priv->stats.rx_packets++; /* total packets received */
	priv->stats.rx_bytes += len; /* total bytes received */
   
	TRACE;
	
	/* push upstack */
	netif_rx(skb);
    }
    else
    {
	/* netif_rx queue threshold reached, stop hammering the system
	 * or we received an unexpected unsupported chained mbuf
	 */
	TRACE;

	/* update the stats */
	priv->stats.rx_dropped++; 

	/* replenish with unchained mbufs */
	do
	{
	    IX_MBUF *next = mbuf->m_next;
	    mbuf->m_next = NULL;
	    mbuf->m_len = priv->replenish_size;
	    dev_rx_buff_replenish(priv->port_id, mbuf);
	    mbuf = next;
	    if (mbuf)
	    {
		/* should not receive chained mbufs */
		chainedRxErrorCount++;
	    }
	}
	while (mbuf != NULL);
    }
    
    TRACE;
}

/* Set promiscuous/multicast mode for the MAC */
static void dev_set_multicast_list(struct net_device *dev)
{
    int res;
    priv_data_t *priv = dev->priv;
    IxEthAccMacAddr addr1 = {};

/* 4 possible scenarios here
 *
 * scenarios:
 * #1 - promiscuous mode ON
 * #2 - promiscuous mode OFF, accept NO multicast addresses
 * #3 - promiscuous mode OFF, accept ALL multicast addresses
 * #4 - promiscuous mode OFF, accept LIST of multicast addresses 
 */

    TRACE;

    /* if called from irq handler, lock already acquired */
    if (!in_irq())
	spin_lock_irq(&priv->lock);

    /* clear multicast addresses that were set the last time (if exist) */
    ixEthAccPortMulticastAddressLeaveAll (priv->port_id);

/**** SCENARIO #1 ****/
    /* Set promiscuous mode */
    if (dev->flags & IFF_PROMISC)
    {
	if ((res = ixEthAccPortPromiscuousModeSet(priv->port_id)))
	{
	    P_ERROR("%s: ixEthAccPortPromiscuousModeSet failed on port %d\n",
		    dev->name, priv->port_id);
	}
	else
	{
	    /* avoid redundant messages */
	    if (!(priv->devFlags & IFF_PROMISC))
	    {
		P_NOTICE("%s: Entering promiscuous mode\n", dev->name);
	    }
	    priv->devFlags = dev->flags;
	}

	goto Exit;
    }


/**** SCENARIO #2 ****/

    /* Clear promiscuous mode */
    if ((res = ixEthAccPortPromiscuousModeClear(priv->port_id)))
    {
	/* should not get here */
	P_ERROR("%s: ixEthAccPortPromiscuousModeClear failed for port %d\n",
		dev->name, priv->port_id);
    }
    else
    {
	/* avoid redundant messages */
	if (priv->devFlags & IFF_PROMISC)
	{
	    P_NOTICE("%s: Leaving promiscuous mode\n", dev->name);
	}
	priv->devFlags = dev->flags;
    }


/**** SCENARIO #3 ****/
    /* If there's more addresses than we can handle, get all multicast
     * packets and sort the out in software
     */
    /* Set multicast mode */
    if ((dev->flags & IFF_ALLMULTI) || 
	(dev->mc_count > IX_ETH_ACC_MAX_MULTICAST_ADDRESSES))
    {
	/* Get ALL multicasts:
	 * This will result in
         *     MASK == 01:00:00:00:00:00
         *     ADDR == 01:00:00:00:00:00
	 * Meaning ALL MULTICAST addresses will be accepted
	 */
        ixEthAccPortMulticastAddressJoinAll(priv->port_id);

	P_VERBOSE("%s: Accepting ALL multicast packets\n", dev->name);
	goto Exit;
    }


/**** SCENARIO #4 ****/
    /* Store all of the multicast addresses in the hardware filter */
    if ((dev->mc_count))
    {
	/* now join the current address list */
	/* Get only multicasts from the list */
	struct dev_mc_list *mc_ptr;

	for (mc_ptr = dev->mc_list; mc_ptr; mc_ptr = mc_ptr->next)
	{
	    memcpy (&addr1.macAddress[0], &mc_ptr->dmi_addr[0],
		    IX_IEEE803_MAC_ADDRESS_SIZE);
	    ixEthAccPortMulticastAddressJoin (priv->port_id, &addr1);
	}
    }


Exit:
    if (!in_irq())
	spin_unlock_irq(&priv->lock);
}

/* start the NPEs */
static int npe_start(IxEthAccPortId port_id)
{
    int res;
    UINT32 npeImageId;

    switch (port_id)
    {
        case IX_ETH_PORT_1:
	    npeImageId = IX_ETH_NPE_B_IMAGE_ID;
	    break;
        case IX_ETH_PORT_2:
	    npeImageId = IX_ETH_NPE_C_IMAGE_ID;
	    break;
	default:
	    P_ERROR("Invalid port specified. IXP Ethernet NPE not started\n");
	    return -ENODEV;
    }

    /* Initialise and Start NPEs */
    if ((res = ixNpeDlNpeInitAndStart(npeImageId)))
    {
	P_ERROR("Error starting NPE for Ethernet port %d!\n", port_id);
	return -1;
    }

    /* set this flag to indicate that NPE is running */
    npeRunning[port_id] = 1;

    return 0;
}

/* The QMgr dispatch entry point can be called from the 
 * IXP425_INT_LVL_QM1 irq (which will trigger
 * an interrupt for every packet) or a timer (which will
 * trigger interrupts on a regular basis). The PMU
 * timer offers the greatest performances and flexibility.
 *
 * This function setup the datapath in polling mode 
 * and is protected against multiple invocations. It should be
 * called at initialisation time.
 */
static int ethAcc_datapath_poll_setup(void)
{
  static int poll_setup_done = 0;

  if (poll_setup_done == 0)
  {
    /* stop the timer and unregister the irqs */
    dev_pmu_timer_disable(); /* stop the timer */
    if (irq_pmu_used) 
    {
       free_irq(IXP425_INT_LVL_XSCALE_PMU,(void *)IRQ_ANY_PARAMETER);
      irq_pmu_used = 0;
    }
    if (irq_qm1_used) 
    {
        free_irq(IXP425_INT_LVL_QM1,(void *)IRQ_ANY_PARAMETER);
	irq_qm1_used = 0;
    }
    
    /* remove rx queue and txdone queue from qmgr dispatcher */
    ixQMgrNotificationDisable(IX_QMGR_QUEUE_23);
    ixQMgrNotificationDisable(IX_QMGR_QUEUE_28);

    /* poll the datapath frpm a timer IRQ */
    if (request_irq(IXP425_INT_LVL_XSCALE_PMU,
                    dev_poll_os_isr,
                    SA_SHIRQ,
                    MODULE_NAME,
                    (void *)IRQ_ANY_PARAMETER))
    {
        P_ERROR("Failed to reassign irq to PMU timer interrupt!\n");
        return -1;
    }
    irq_pmu_used = 1;

    /* run the other events from qmgr IRQ (rx free low and tx low)
     * from IRQ. These event should not occur often 
     */
    if (request_irq(IXP425_INT_LVL_QM1,
                    dev_qmgr_os_isr,
                    SA_SHIRQ,
                    MODULE_NAME,
                    (void *)IRQ_ANY_PARAMETER))
    {
        P_ERROR("Failed to reassign irq to QM1 timer interrupt!\n");
        return -1;
    }
    irq_qm1_used = 1;

    if (dev_pmu_timer_init())
    {
        P_ERROR("Error initialising IXP425 PMU timer!\n");
        return -1;
    }

    TRACE;

    dev_pmu_timer_restart(); /* set up the timer for the next interrupt */
    
    /* set a static flag for re-entrancy */
    poll_setup_done = 1;
  }
  return 0;
}

/* Enable the MAC port.
 * Called on do_dev_open, dev_tx_timeout and mtu size changes
 */
static int port_enable(struct net_device *dev)
{
    int res;
    priv_data_t *priv = dev->priv;
    IxEthAccMacAddr npeMacAddr;

    P_DEBUG("port_enable(%s)\n", dev->name);

    /* force replenish if necessary */
    dev_rx_buff_prealloc(priv);

    /* we need to restart the NPE here if it has been
     * stopped by port_disable()
     */
    if (!npeRunning[priv->port_id])
    {
        if ((res = npe_start(priv->port_id)))
        {
            TRACE;
            return res;
        }
    }

    /* when we restart the NPEs we must also set the unicast MAC address
     * again, before ixEthAccPortEnable is called
     */
    memcpy(&npeMacAddr.macAddress, dev->dev_addr, IX_IEEE803_MAC_ADDRESS_SIZE);
    if ((res = ixEthAccPortUnicastMacAddressSet(priv->port_id, &npeMacAddr)))
    {
        P_ERROR("Failed to set MAC address for port %d, res = %d\n",
                priv->port_id, res);
        return convert_error_ethAcc(res);
    }

    if ((res = ixEthAccPortTxFrameAppendFCSEnable(priv->port_id)))
    {
        TRACE;
        return convert_error_ethAcc(res);
    }

    if ((res = ixEthAccPortRxFrameAppendFCSDisable(priv->port_id)))
    {
	TRACE;
	return convert_error_ethAcc(res);
    }

    if ((res = ixEthAccTxSchedulingDisciplineSet(priv->port_id,
						FIFO_NO_PRIORITY)))
    {
        TRACE;
	return convert_error_ethAcc(res);
    }

    if ((res = ixEthDBFilteringPortMaximumFrameSizeSet(priv->port_id, 
						       priv->msdu_size)))
    {
        P_ERROR("%s: ixEthDBFilteringPortMaximumFrameSizeSet failed for port %d, res = %d\n",
                dev->name, priv->port_id, res);
        return -1;
    }

    if ((res = ixEthAccPortEnable(priv->port_id)))
    {
	P_ERROR("%s: ixEthAccPortEnable failed for port %d, res = %d\n",
		dev->name, priv->port_id, res);
	return convert_error_ethAcc(res);
    }

    /* Do not enable aging unless learning is enabled (nothing to age otherwise) */
    if (npe_learning)
    {
        if ((res = ixEthDBPortAgingEnable(priv->port_id)))
        {
            P_ERROR("%s: ixEthDBPortAgingEnable failed for port %d, res = %d\n",
                    dev->name, priv->port_id, res);
            return -1;
        }
    }

    TRACE;

    netif_start_queue(dev);

    TRACE;

    /* restore the scheduling discipline (it may have changed) 
     */
    dev->qdisc_sleeping = priv->qdisc;
    dev->qdisc = priv->qdisc;

    TRACE;

#if 0
    /* if no link monitoring is set, assume the link is up */
    if (!default_phy_cfg[priv->port_id].linkMonitor)
    {
       netif_carrier_on(dev);
    }
#endif

    /* restart the link-monitoring thread if necessary */
    if (priv->maintenanceCheckStopped == TRUE)
    {
	/* Starts the driver monitoring thread, if configured */
	priv->maintenanceCheckStopped = FALSE;
	
	priv->maintenanceCheckThreadId = 
	    kernel_thread(dev_media_check_thread,
			  (void *) dev,
			  CLONE_FS | CLONE_FILES);
	if (priv->maintenanceCheckThreadId < 0)
	{
	    P_ERROR("%s: Failed to start thread for media checks\n", dev->name);
	    priv->maintenanceCheckStopped = TRUE;
	}
    }

    return 0;
}

/* Disable the MAC port.
 * Called on do_dev_stop and dev_tx_timeout
 */
static void port_disable(struct net_device *dev)
{
    priv_data_t *priv = dev->priv;
    int res;
    IX_STATUS status;

    TRACE;

    if (!netif_queue_stopped(dev))
    {
        dev->trans_start = jiffies;
        netif_stop_queue(dev);
    }

    if (priv->maintenanceCheckStopped == TRUE)
    {
	/* thread is not running */
    }
    else
    {
	/* thread is running */
	priv->maintenanceCheckStopped = TRUE;
	/* Wake up the media-check thread with a signal.
	   It will check the 'running' flag and exit */
	if ((res = kill_proc (priv->maintenanceCheckThreadId, SIGKILL, 1)))
	{
	    P_ERROR("%s: unable to signal thread\n", dev->name);
	}
	else
	{
	    /* wait for the thread to exit. */
	    down (priv->maintenanceCheckThreadComplete);
	    up (priv->maintenanceCheckThreadComplete);
	}
    }

    /* Set callbacks when port is disabled */
    ixEthAccPortTxDoneCallbackRegister(priv->port_id, 
				       tx_done_disable_cb,
				       (UINT32)dev);
    ixEthAccPortRxCallbackRegister(priv->port_id, 
				   rx_disable_cb, 
				   (UINT32)dev);

    if ((status = ixEthAccPortDisable(priv->port_id)) != IX_SUCCESS)
    {
	/* should not get here */
	P_ERROR("%s: BUG: ixEthAccPortDisable(%d) failed\n",
		dev->name, priv->port_id);
    }

    /* remove all entries from the sw queues */
    dev_skb_queue_drain(priv);
    dev_tx_mb_queue_drain(priv);
    dev_rx_mb_queue_drain(priv);

    TRACE;
}

/* this function is called by the kernel to transmit packet 
 * It is expected to run in the context of the ksoftirq thread.
*/
static int dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    int res;
    priv_data_t *priv = dev->priv;
    IX_MBUF *mbuf;

    TRACE;

    /* get mbuf struct from tx software queue */
    mbuf = dev_tx_mb_dequeue(priv);

    if (mbuf == NULL)
    {
	/* No mbuf available, free the skbuf */
	dev_kfree_skb_any(skb);
	priv->stats.tx_dropped++;

        if (!netif_queue_stopped(dev))
        {
            ixEthAccPortTxDoneCallbackRegister(priv->port_id, 
                                               tx_done_queue_stopped_cb,
                                               (UINT32)dev);
            dev->trans_start = jiffies;
            netif_stop_queue (dev);
        }
	return 0;
    }

#ifdef DEBUG_DUMP
    skb_dump("tx", skb);
#endif

    mbuf_swap_skb(mbuf, skb); /* this should return NULL, as mbufs in the pool
    			       * have no skb attached
			       */

    /* flush the mbuf data from the cache */
    IX_ACC_DATA_CACHE_FLUSH(IX_MBUF_MDATA(mbuf), IX_MBUF_MLEN(mbuf));

    if ((res = ixEthAccPortTxFrameSubmit(priv->port_id, mbuf,
	IX_ETH_ACC_TX_DEFAULT_PRIORITY)))
    {
	dev_kfree_skb_any(skb);
	priv->stats.tx_dropped++;
	P_ERROR("%s: ixEthAccPortTxFrameSubmit failed for port %d, res = %d\n",
		dev->name, priv->port_id, res);
	return convert_error_ethAcc(res);
    }

    TRACE;

    return 0;
}

/* Open the device.
 * Request resources and start interrupts
 */
static int do_dev_open(struct net_device *dev)
{
    int res;
    priv_data_t *priv = dev->priv;

    /* Set callbacks */
    if ((res = ixEthAccPortTxDoneCallbackRegister(priv->port_id, 
						  tx_done_cb,
						  (UINT32)dev)))
    {
	P_ERROR("%s: ixEthAccPortTxDoneCallbackRegister for port %d failed, "
		"res = %d\n", dev->name, priv->port_id, res);
	return convert_error_ethAcc(res);
    }

    if ((res = ixEthAccPortRxCallbackRegister(priv->port_id, 
					      rx_cb,
					      (UINT32)dev)))
    {
	P_ERROR("%s: ixEthAccPortRxCallbackRegister for port %d failed, "
		"res = %d\n", dev->name, priv->port_id, res);
	return convert_error_ethAcc(res);
    }

    /* prevent the maintenace task from running while bringing up port */
    down(maintenance_mutex);
    if ((res = port_enable(dev)))
    {
        up(maintenance_mutex);
        return res;
    }
    up(maintenance_mutex);

    return res;
}

/* Close the device.
 * Free resources acquired in dev_start
 */
static int do_dev_stop(struct net_device *dev)
{
    TRACE;

    down(maintenance_mutex);
    port_disable(dev);
    up(maintenance_mutex);

    return 0;
}

static void
dev_tx_timeout_task(void *dev_id)
{
    struct net_device *dev = (struct net_device *)dev_id;
    priv_data_t *priv = dev->priv;

    P_ERROR("%s: Tx Timeout for port %d\n", dev->name, priv->port_id);

    down(maintenance_mutex);
    port_disable(dev);

    /* Note to user: Consider performing other reset operations here (such as PHY reset),
     * if it is known to help the Tx Flow to become "unstuck"
     */

    port_enable(dev);
    up(maintenance_mutex);
}


/* This function is called when kernel thinks that TX is stuck */
static void dev_tx_timeout(struct net_device *dev)
{
    priv_data_t *priv = dev->priv;

    TRACE;
    schedule_task(&priv->tq_timeout);
    
}

/* update the maximum msdu value for this device */
static void dev_change_msdu(struct net_device *dev, int new_msdu_size)
{
    priv_data_t *priv = dev->priv;
    unsigned int new_size = new_msdu_size;

    priv->msdu_size = new_size;

    /* ensure buffers are large enough (do not use too small buffers
     * even if it is possible to configure so. 
     */
    if (new_size < IX_ETHNPE_ACC_FRAME_LENGTH_DEFAULT)
    {
	new_size = IX_ETHNPE_ACC_FRAME_LENGTH_DEFAULT;
    }

    /* the NPE needs 64 bytes boundaries : round-up to the next
    * frame boundary. This size is used to invalidate and replenish.
    */
    new_size = IX_ETHNPE_ACC_RXFREE_BUFFER_ROUND_UP(new_size);
    priv->replenish_size = new_size;
    
    /* Xscale MMU needs a cache line boundary : round-up to the next
     * cache line boundary. This will be the size used to allocate
     * skbufs from the kernel.
     */
    new_size = HDR_SIZE + new_size;
    new_size = L1_CACHE_ALIGN(new_size);
    priv->pkt_size = new_size;

    /* Linux stack uses a reserved header.
     * skb contain shared info about fragment lists 
     * this will be the value stored in skb->truesize
     */
    priv->alloc_size = SKB_RESERVED_HEADER_SIZE + 
	SKB_DATA_ALIGN(new_size) + SKB_RESERVED_TRAILER_SIZE;
}

static int dev_change_mtu(struct net_device *dev, int new_mtu_size)
{
    priv_data_t *priv = dev->priv;
    /* the msdu size includes the ethernet header plus the 
     * mtu (IP payload), but does not include the FCS which is 
     * stripped out by the access layer.
     */
    unsigned int new_msdu_size = new_mtu_size + dev->hard_header_len;

    if (new_msdu_size > IX_ETHNPE_ACC_FRAME_LENGTH_MAX)
    {
	/* Unsupported msdu size */
	return -EINVAL;
    }

    /* safer to stop maintenance task while bringing port down and up */
    down(maintenance_mutex);

    if (npeRunning[priv->port_id])
    {
	/* stop all traffic, recycle all buffers */
	port_disable(dev);
    }

    if (ixEthDBFilteringPortMaximumFrameSizeSet(priv->port_id, 
						new_msdu_size))
    {
	P_ERROR("%s: ixEthDBFilteringPortMaximumFrameSizeSet failed for port %d\n",
		dev->name, priv->port_id);
	up(maintenance_mutex);

	return -1;
    }

    /* update the packet sizes needed */
    dev_change_msdu(dev, new_msdu_size);
    /* update the driver mtu value */
    dev->mtu = new_mtu_size;

    if (npeRunning[priv->port_id] && (dev->flags & IFF_UP))
    {
	/* restarts traffic, provision with new buffers */
	port_enable(dev);
    }

    up(maintenance_mutex);

    return 0;
}

static int do_dev_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
    priv_data_t *priv = dev->priv;
    struct mii_ioctl_data *data = (struct mii_ioctl_data *) & req->ifr_data;
    int phy = phyAddresses[priv->port_id];
    int res = 0;

    TRACE;

    switch (cmd)
    {
	/*
	 * IOCTL's for mii-tool support
	 */

	/* Get address of MII PHY in use */
	case SIOCGMIIPHY:
	case SIOCDEVPRIVATE:
	    data->phy_id = phy;
	    return 0;

        /* Read MII PHY register */
	case SIOCGMIIREG:		
	case SIOCDEVPRIVATE+1:
	    down (miiAccessMutex);     /* lock the MII register access mutex */
	    if ((res = ixEthAccMiiReadRtn (data->phy_id, data->reg_num, &data->val_out)))
	    {
		P_ERROR("Error reading MII reg %d on phy %d\n",
		       data->reg_num, data->phy_id);
		res = -1;
	    }
	    up (miiAccessMutex);	/* release the MII register access mutex */
	    return res;

	/* Write MII PHY register */
	case SIOCSMIIREG:
	case SIOCDEVPRIVATE+2:
	    down (miiAccessMutex);     /* lock the MII register access mutex */
	    if ((res = ixEthAccMiiWriteRtn (data->phy_id, data->reg_num, data->val_in)))
	    {
		P_ERROR("Error reading MII reg %d on phy %d\n",
                        data->reg_num, data->phy_id);
		res = -1;
	    }
	    up (miiAccessMutex);	/* release the MII register access mutex */
	    return res;

	/* set the MTU size */
	case SIOCSIFMTU:
	    return dev_change_mtu(dev, req->ifr_mtu);

	default:
	    return -EOPNOTSUPP;
    }

    return -EOPNOTSUPP;
}

static struct net_device_stats *dev_get_stats(struct net_device *dev)
{
    int res;
    /* "stats" should be cache-safe.
     * we alligne "stats" and "priv" by 32 bytes, so that the cache
     * operations will not affect "res" and "priv"
     */
    IxEthEthObjStats ethStats __attribute__ ((aligned(32))) = {};
    priv_data_t *priv;

    TRACE;

    priv = dev->priv;

    /* Get HW stats and translate to the net_device_stats */
    if (!netif_running(dev))
    {
	TRACE;
	return &priv->stats;
    }

    TRACE;

    invalidate_dcache_range((unsigned int)&ethStats, sizeof(ethStats));
    if ((res = ixEthAccMibIIStatsGetClear(priv->port_id, &ethStats)))
    {
	P_ERROR("%s: ixEthAccMibIIStatsGet failed for port %d, res = %d\n",
		dev->name, priv->port_id, res);
	return &priv->stats;
    }

    TRACE;

    /* bad packets received */
    priv->stats.rx_errors += 
	ethStats.dot3StatsAlignmentErrors +
	ethStats.dot3StatsFCSErrors +
	ethStats.dot3StatsFrameTooLongs +
	ethStats.dot3StatsInternalMacReceiveErrors;

    /* packet transmit problems */
    priv->stats.tx_errors += 
	ethStats.dot3StatsLateCollisions +
	ethStats.dot3StatsExcessiveCollsions +
	ethStats.dot3StatsInternalMacTransmitErrors +
	ethStats.dot3StatsCarrierSenseErrors;

    priv->stats.collisions +=
	ethStats.dot3StatsSingleCollisionFrames +
	ethStats.dot3StatsMultipleCollisionFrames;

    /* recved pkt with crc error */
    priv->stats.rx_crc_errors +=
	ethStats.dot3StatsFCSErrors;

    /* recv'd frame alignment error */
    priv->stats.rx_frame_errors +=
	ethStats.dot3StatsFrameTooLongs;

    /* detailed tx_errors */
    priv->stats.tx_carrier_errors +=
	ethStats.dot3StatsCarrierSenseErrors;

    return &priv->stats;
}


/* Initialize QMgr and bind it's interrupts */
static int qmgr_init(void)
{
    int res;

    /* Initialise Queue Manager */
    P_VERBOSE("Initialising Queue Manager...\n");
    if ((res = ixQMgrInit()))
    {
	P_ERROR("Error initialising queue manager!\n");
	return -1;
    }

    TRACE;

    /* Get the dispatcher entrypoint */
    ixQMgrDispatcherLoopGet (&dispatcherFunc);

    TRACE;

    /* The QMgr dispatch entry point can be called from the 
     * IXP425_INT_LVL_QM1 irq (which will trigger
     * an interrupt for every packet) or a timer (which will
     * trigger interrupts on a regular basis). The PMU
     * timer offers the greatest performances and flexibility.
     */
    if (request_irq(IXP425_INT_LVL_QM1,
                    dev_qmgr_os_isr,
                    SA_SHIRQ,
                    MODULE_NAME,
                    (void *)IRQ_ANY_PARAMETER))
    {
        P_ERROR("Failed to request_irq to Queue Manager interrupt!\n");
        return -1;
    }
    irq_qm1_used = 1;

    TRACE;
    return 0;
}

static int ethacc_init(void)
{
    int res;

    if ((res = ixEthAccInit()))
    {
	P_ERROR("ixEthAccInit failed with res=%d\n", res);
	return convert_error_ethAcc(res);
    }

    if ((res = ixEthAccPortInit(IX_ETH_PORT_1)))
    {
	P_ERROR("Failed to initialize Eth port 1 res = %d\n", res);
	return convert_error_ethAcc(res);
    }

    if ((res = ixEthAccPortInit(IX_ETH_PORT_2)))
    {
	P_ERROR("Failed to initialize Eth port 2 res = %d\n", res);
	return convert_error_ethAcc(res);
    }

    return 0;
}

static int phy_init(void)
{
    int res;
    BOOL physcan[IXP425_ETH_ACC_MII_MAX_ADDR];
    int i, phy_found, num_phys_to_set;

    /* initialise the MII register access mutex */
    miiAccessMutex = (struct semaphore *) kmalloc(sizeof(struct semaphore), GFP_KERNEL);
    if (!miiAccessMutex)
	return -ENOMEM;

    init_MUTEX(miiAccessMutex);

    TRACE;

    /* detect the PHYs (ethMii requires the PHYs to be detected) 
     * and provides a maximum number of PHYs to search for.
     */
    res = ixEthMiiPhyScan(physcan, 
			  sizeof(default_phy_cfg) / sizeof(phy_cfg_t));
    if (res != IX_SUCCESS)
    {
	P_ERROR("PHY scan failed\n");
	return convert_error_ethAcc(res);
    }

    /* Module parameter */
    if (no_phy_scan) 
    { 
	/* Use hardcoded phy addresses */
	num_phys_to_set = (sizeof(default_phy_cfg) / sizeof(phy_cfg_t));
    }
    else
    {
	/* Update the hardcoded values with discovered parameters */
	for (i=0, phy_found=0; i < IXP425_ETH_ACC_MII_MAX_ADDR; i++)
	{
	    if (physcan[i])
	    {
		P_INFO("Found PHY %d at %d\n", phy_found, i);
		phyAddresses[phy_found] = i;
		
		if (++phy_found == IXP425_ETH_ACC_MII_MAX_ADDR)
		    break;
	    }
	}

	num_phys_to_set = phy_found;
    }

    /* Reset and Set each phy properties */
    for (i=0; i < num_phys_to_set; i++) 
    {
	P_VERBOSE("Configuring PHY %d\n", i);
	P_VERBOSE("\tSpeed %s\tDuplex %s\tAutonegotiation %s\n",
		  (default_phy_cfg[i].speed100) ? "100" : "10",   
		  (default_phy_cfg[i].duplexFull) ? "FULL" : "HALF",  
		  (default_phy_cfg[i].autoNegEnabled) ? "ON" : "OFF");

	if (phy_reset)
	{
	    ixEthMiiPhyReset(phyAddresses[i]);
	}

	ixEthMiiPhyConfig(phyAddresses[i],
	    default_phy_cfg[i].speed100,   
	    default_phy_cfg[i].duplexFull,  
	    default_phy_cfg[i].autoNegEnabled);
    }

    for (i=0; i < IX_ETH_ACC_NUMBER_OF_PORTS && i <  num_phys_to_set; i++) 
    {
	P_INFO("%s%d is using the PHY at address %d\n",
	       DEVICE_NAME, i, phyAddresses[i]);

	/* Set the MAC to the same duplex mode as the phy */
	ixEthAccPortDuplexModeSet(i,
            (default_phy_cfg[i].duplexFull) ?
                 IX_ETH_ACC_FULL_DUPLEX : IX_ETH_ACC_HALF_DUPLEX);
    }

    return 0;
}

/* set port MAC addr and update the dev struct if successfull */
int dev_set_mac_address(struct net_device *dev, void *addr)
{
    int res;
    priv_data_t *priv = dev->priv;
    IxEthAccMacAddr npeMacAddr;
    struct sockaddr *saddr = (struct sockaddr *)addr;

    /* Set MAC addr in h/w */
    memcpy(&npeMacAddr.macAddress,
	   &saddr->sa_data[0],
	   IX_IEEE803_MAC_ADDRESS_SIZE);

    if ((res = ixEthAccPortUnicastMacAddressSet(priv->port_id, &npeMacAddr)))
    {
        P_ERROR("Failed to set MAC address for port %d, res = %d\n",
                priv->port_id, res);
        return convert_error_ethAcc(res);
    }

    /* update dev struct */
    memcpy(dev->dev_addr, 
	   &saddr->sa_data[0],
	   IX_IEEE803_MAC_ADDRESS_SIZE);

    return 0;
}

/* 
 *  TX QDISC
 */

/* new tx scheduling discipline : the algorithm is based on a 
 * efficient JBI technology : Just Blast It. There is no need for
 * software queueing where the hardware provides this feature
 * and makes the internal transmission non-blocking.
 *
 * because of this reason, there is no need for throttling using
 * netif_stop_queue() and netif_start_queue() (there is no sw queue
 * that linux may restart)
 */
static int dev_qdisc_no_enqueue(struct sk_buff *skb, struct Qdisc * qdisc)
{
        return dev_hard_start_xmit(skb, qdisc->dev);     
}

static struct sk_buff *dev_qdisc_no_dequeue(struct Qdisc * qdisc)
{
	return NULL;
}

static struct Qdisc_ops dev_qdisc_ops =
{
	NULL, NULL, "ixp425_eth", 0,
	dev_qdisc_no_enqueue, 
	dev_qdisc_no_dequeue,
	dev_qdisc_no_enqueue, 
	NULL, 
	NULL, NULL, NULL, NULL, NULL
};

/* Initialize device structs.
 * Resource allocation is deffered until do_dev_open
 */
char * get_ixp0_ethaddr(void);

static int __devinit dev_eth_probe(struct net_device *dev)
{
    static int found_devices = 0;
    priv_data_t *priv;

    TRACE;

    /* there are only 2 available ports */
    if (found_devices >= IX_ETH_ACC_NUMBER_OF_PORTS)
	return -ENODEV;

    SET_MODULE_OWNER(dev);

    /* set device name */
    strcpy(dev->name, found_devices ? DEVICE_NAME "1" : DEVICE_NAME "0");

    /* allocate and initialize priv struct */
    priv = dev->priv = kmalloc(sizeof(priv_data_t), GFP_KERNEL);
    if (dev->priv == NULL)
	return -ENOMEM;

    memset(dev->priv, 0, sizeof(priv_data_t));

    priv->port_id = found_devices ? IX_ETH_PORT_2 : IX_ETH_PORT_1;

    /* initialize RX pool */
    if (IX_MBUF_POOL_INIT_NO_ALLOC(&priv->rx_pool, 
				   priv->rx_pool_buf, NULL,
				   RX_QUEUE_PREALLOC, 
				   0, 
				   "IXP425 Ethernet driver Rx Pool"))
    {
	P_ERROR("%s: Buffer Pool init failed on port %d\n",
		dev->name, priv->port_id);
	kfree(dev->priv);
	return -ENOMEM;
    }

    /* initialize TX pool */
    if (IX_MBUF_POOL_INIT_NO_ALLOC(&priv->tx_pool, 
				   priv->tx_pool_buf, 
				   NULL,
				   TX_QUEUE_MAX_LEN,
				   0, 
				   "IXP425 Ethernet driver Tx Pool"))
    {
	P_ERROR("%s: Buffer Pool init failed on port %d\n",
		dev->name, priv->port_id);
	kfree(dev->priv);
	return -ENOMEM;
    }

    /* initialise the MII register access mutex */
    priv->maintenanceCheckThreadComplete = (struct semaphore *)
	kmalloc(sizeof(struct semaphore), GFP_KERNEL);
    if (!priv->maintenanceCheckThreadComplete)
    {
	kfree(dev->priv);
	return -ENOMEM;
    }
    priv->lock = SPIN_LOCK_UNLOCKED;
    init_MUTEX(priv->maintenanceCheckThreadComplete);
    priv->maintenanceCheckStopped = TRUE;

    /* initialize ethernet device (default handlers) */
    ether_setup(dev);

     /* fill in dev struct callbacks with customized handlers */
    dev->open = do_dev_open;
    dev->stop = do_dev_stop;

    dev->hard_start_xmit = dev_hard_start_xmit;

    dev->watchdog_timeo = DEV_WATCHDOG_TIMEO;
    dev->tx_timeout = dev_tx_timeout;
    dev->change_mtu = dev_change_mtu;
    dev->do_ioctl = do_dev_ioctl;
    dev->get_stats = dev_get_stats;
    dev->set_multicast_list = dev_set_multicast_list;
    dev->flags |= IFF_MULTICAST;

    dev->set_mac_address = dev_set_mac_address;
//SUPER ADDED
//    memcpy(dev->dev_addr, &default_mac_addr[priv->port_id].macAddress, IX_IEEE803_MAC_ADDRESS_SIZE);
     memcpy(dev->dev_addr,get_ixp0_ethaddr(),IX_IEEE803_MAC_ADDRESS_SIZE);
    //
    /* update the internal packet size */
    dev_change_msdu(dev, dev->mtu + dev->hard_header_len);

    priv->tq_timeout.routine = dev_tx_timeout_task;
    priv->tq_timeout.data = (void *)dev;

    /* set the scheduling discipline */
    priv->qdisc = qdisc_create_dflt(dev, &dev_qdisc_ops);
    dev->qdisc_sleeping = priv->qdisc;
    dev->qdisc = priv->qdisc;

    if (!dev->qdisc_sleeping)
    {
	P_ERROR("%s: qdisc_create_dflt failed on port %d\n",
		dev->name, priv->port_id);
	kfree(dev->priv);
	return -ENOMEM;
    }

    /* set the internal maximum queueing capabilities */
    dev->tx_queue_len = TX_QUEUE_MAX_LEN;

    if (!netif_queue_stopped(dev))
    {
        dev->trans_start = jiffies;
        netif_stop_queue(dev);
    }

    found_devices++;

    return 0;
}

/* Module initialization and cleanup */

#ifdef MODULE

static struct net_device ixp425_devices[IX_ETH_ACC_NUMBER_OF_PORTS];

int init_module(void)
{
    int res, i;
    struct net_device *dev;

    TRACE;

    P_INFO("\nInitializing IXP425 NPE Ethernet driver software v. " MODULE_VERSION " \n");

    TRACE;

    /* Enable/disable the EthDB MAC Learning & Filtering feature.
     * This is a half-bridge feature, and should be disabled if this interface 
     * is used on a bridge with other non-NPE ethernet interfaces.
     * This is because the NPE's are not aware of the other interfaces and thus
     * may incorrectly filter (drop) incoming traffic correctly bound for another
     * interface on the bridge.
     */
    if (npe_learning)
    {
        ixFeatureCtrlSwConfigurationWrite (IX_FEATURECTRL_ETH_LEARNING, TRUE);
    }
    else
    {
        ixFeatureCtrlSwConfigurationWrite (IX_FEATURECTRL_ETH_LEARNING, FALSE);
    }


    /* Do not initialise core components if no_csr_init is set */
    if (no_csr_init) /* module parameter */
    {
	P_WARN("init_csr==0, no component initialisation"
	       "core components performed\n");
    }
    else
    {
	if ((res = qmgr_init()))
	    return res;
        TRACE;

	P_INFO("CPU clock speed (approx) = %lu MHz\n",
		-timer_countup_ticks * QUEUE_DISPATCH_TIMER_RATE / 1000000);

	if ((res = ixNpeMhInitialize(IX_NPEMH_NPEINTERRUPTS_YES)))
	    return -1;
    }

    TRACE;

    for (i = 0; i < IX_ETH_ACC_NUMBER_OF_PORTS; i++)
    {
	if ((res = npe_start(i)))
	    return res;
        TRACE;
    }

    if ((res = ethacc_init()))
	return res;

    TRACE;

    if ((res = phy_init()))
	return res;

    TRACE;

    /* Set scheduling discipline */
    ixEthAccTxSchedulingDisciplineSet(IX_ETH_PORT_1, FIFO_NO_PRIORITY);
    ixEthAccTxSchedulingDisciplineSet(IX_ETH_PORT_2, FIFO_NO_PRIORITY);

    for (i = 0; i < IX_ETH_ACC_NUMBER_OF_PORTS; i++)
    {
	dev = &ixp425_devices[i];

	dev->init = dev_eth_probe;

	if ((res = register_netdev(dev)))
	    P_ERROR("Failed to register netdev. res = %d\n", res);

        TRACE;

        /* This was added in v0.1.8 of the driver.
         * It seems that we need to enable the port before the user can set
         * a mac address for the port using 'ifconfig hw ether ...'.
         * To enable the port we must first register Q callbacks, so we register
         * the portDisable callbacks to ensure that no buffers are passed up to the
         * kernel until the port is brought up properly (ifconfig up)
         */
        ixEthAccPortTxDoneCallbackRegister(i, 
                                           tx_done_disable_cb,
                                           (UINT32)dev);
        ixEthAccPortRxCallbackRegister(i, 
                                       rx_disable_cb, 
                                       (UINT32)dev);

        port_enable(dev);
    }

    TRACE;

    if (no_csr_init == 0) /* module parameter */
    {
      /* The QMgr dispatch entry point is called from the 
       * IXP425_INT_LVL_QM1 irq (which will trigger
       * an interrupt for every packet)
       * This function setup the datapath in polling mode
       * for better performances.
       */

        if ((res = ethAcc_datapath_poll_setup()))
	{
           return res;
	}
    }

    TRACE;

    /* initialise the DB Maintenance task mutex */
    maintenance_mutex = (struct semaphore *) kmalloc(sizeof(struct semaphore), GFP_KERNEL);
    if (!maintenance_mutex)
	return -ENOMEM;

    init_MUTEX(maintenance_mutex);

    TRACE;

    /* Do not start the EthDB maintenance thread if learning & filtering feature is disabled */
    if (npe_learning)
    {
        maintenance_timer_set();
    }

    TRACE;

    /* set the softirq rx queue thresholds 
     * (These numbers are based on tuning experiments)
     * maxbacklog =  (netdev_max_backlog * 10) / 63;
    */
    if (netdev_max_backlog == 0)
    {
	netdev_max_backlog = 290; /* system default */
    }
    netdev_max_backlog /= BACKLOG_TUNE;

    TRACE;

    return 0;
}

void cleanup_module(void)
{
    int i;

    TRACE;

    /* We can only get here when the module use count is 0,
     * so there's no need to stop devices.
     */

    dev_pmu_timer_disable(); /* stop the timer */
    if (irq_pmu_used) 
    {
       free_irq(IXP425_INT_LVL_XSCALE_PMU,(void *)IRQ_ANY_PARAMETER);
       irq_pmu_used = 0;
    }
    if (irq_qm1_used) 
    {
        free_irq(IXP425_INT_LVL_QM1,(void *)IRQ_ANY_PARAMETER);
	irq_qm1_used = 0;
    }

    /* stop the maintenance timer */
    maintenance_timer_clear();
    /* wait for maintenance task to complete */
    down(maintenance_mutex);
    up(maintenance_mutex);

    for (i = 0; i < IX_ETH_ACC_NUMBER_OF_PORTS; i++)
    {
	struct net_device *dev = &ixp425_devices[i];
	if (dev->priv != NULL)
	{
	    unregister_netdev(dev);
	    kfree(dev->priv);
	    dev->priv = NULL;
	}
    }

    TRACE;
}

#endif /* MODULE */


