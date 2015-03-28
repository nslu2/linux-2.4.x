/*
=========================================================================
 r8169.c: A RealTek RTL8169s/8110s Gigabit Ethernet driver for Linux kernel 2.4.x.
 --------------------------------------------------------------------

 History:
 Feb  4 2002	- created initially by ShuChen <shuchen@realtek.com.tw>.
 May 20 2002	- Add link status force-mode and TBI mode support.
=========================================================================
  1. The media can be forced in 5 modes.
	 Command: 'insmod r8169 media = SET_MEDIA'
	 Ex:	  'insmod r8169 media = 0x04' will force PHY to operate in 100Mpbs Half-duplex.

	 SET_MEDIA can be:
 		_10_Half	= 0x01
 		_10_Full	= 0x02
 		_100_Half	= 0x04
 		_100_Full	= 0x08
 		_1000_Full	= 0x10

  2. Support TBI mode.
//=========================================================================
RTL8169_VERSION "1.1"	<2002/10/4>

	The bit4:0 of MII register 4 is called "selector field", and have to be
	00001b to indicate support of IEEE std 802.3 during NWay process of
	exchanging Link Code Word (FLP).

RTL8169_VERSION "1.2"	<2003/6/17>
	Update driver module name.
	Modify ISR.
        Add chip mac_version.

RTL8169_VERSION "1.3"	<2003/6/20>
        Add chip phy_version.
	Add priv->phy_timer_t, rtl8169_phy_timer_t_handler()
	Add rtl8169_hw_PHY_config()
	Add rtl8169_hw_PHY_reset()

RTL8169_VERSION "1.4"	<2003/7/14>
	Add tx_bytes, rx_bytes.

RTL8169_VERSION "1.5"	<2003/7/18>
	Set 0x0000 to PHY at offset 0x0b.
	Modify chip mac_version, phy_version
	Force media for multiple card.
RTL8169_VERSION "1.6"	<2003/8/25>
	Modify receive data buffer.
*/


#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>

#include <linux/timer.h>
#include <linux/init.h>


#define RTL8169_VERSION "1.6 <2003-08-25>"
#define MODULENAME "RTL8169s/8110s"
#define RTL8169_DRIVER_NAME   MODULENAME " Gigabit Ethernet driver " RTL8169_VERSION
#define PFX MODULENAME ": "


#undef RTL8169_DEBUG

#ifdef RTL8169_DEBUG
#define assert(expr) \
        if(!(expr)) {					\
	        printk( "Assertion failed! %s,%s,%s,line=%d\n", #expr,__FILE__,__FUNCTION__,__LINE__);		\
        }
#define DBG_PRINT( fmt, args...)   printk("r8169: " fmt, ## args);
#else
#define assert(expr) do {} while (0)
#define DBG_PRINT( fmt, args...)   ;
#endif	// end of #ifdef RTL8169_DEBUG


/* media options */
#define MAX_UNITS 8
static int media[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
   The RTL chips use a 64 element hash table based on the Ethernet CRC.  */
static int multicast_filter_limit = 32;

/* MAC address length*/
#define MAC_ADDR_LEN	6

/* max supported gigabit ethernet frame size -- must be at least (dev->mtu+14+4).*/
#define MAX_ETH_FRAME_SIZE	1536

#define TX_FIFO_THRESH 256		/* In bytes */

#define RX_FIFO_THRESH	7		/* 7 means NO threshold, Rx buffer level before first PCI xfer.  */
#define RX_DMA_BURST	6		/* Maximum PCI burst, '6' is 1024 */
#define TX_DMA_BURST	6		/* Maximum PCI burst, '6' is 1024 */
#define ETTh 	0x3F		/* 0x3F means NO threshold */
#define RxPacketMaxSize	0x0800		/* Maximum size supported is 16K-1 */
#define InterFrameGap	0x03		/* 3 means InterFrameGap = the shortest one */

#define NUM_TX_DESC	64		/* Number of Tx descriptor registers*/
#define NUM_RX_DESC	64		/* Number of Rx descriptor registers*/
#define RX_BUF_SIZE	1536		/* Rx Buffer size */

#define MAX_RX_SKBDATA_SIZE 1600

#define RTL_MIN_IO_SIZE 0x80
#define TX_TIMEOUT  (6*HZ)

/* write/read MMIO register */
#define RTL_W8(reg, val8)	writeb ((val8), ioaddr + (reg))
#define RTL_W16(reg, val16)	writew ((val16), ioaddr + (reg))
#define RTL_W32(reg, val32)	writel ((val32), ioaddr + (reg))
#define RTL_R8(reg)		readb (ioaddr + (reg))
#define RTL_R16(reg)		readw (ioaddr + (reg))
#define RTL_R32(reg)		((unsigned long) readl (ioaddr + (reg)))


#define RTL_GIGA_MAC_VER_B  0x00    //MAC version 0000
//#define RTL_GIGA_MAC_VER_C  0x03
#define RTL_GIGA_MAC_VER_D  0x01    //MAC version 0001
#define RTL_GIGA_MAC_VER_E  0x02    //MAC version 0002

#define RTL_GET_MAC_VERSION(mac_version)     \
{\
	mac_version = RTL_GIGA_MAC_VER_B;\
	if( (RTL_R32(TxConfig)&0x7c800000) & (0x1<<26) ){ \
		mac_version = RTL_GIGA_MAC_VER_E;\
	}\
	else if( (RTL_R32(TxConfig)&0x7c800000) & (0x1<<23) ){\
		mac_version = RTL_GIGA_MAC_VER_D;\
	}\
	else if( (RTL_R32(TxConfig)&0x7c800000) == 0x00000000 ){\
		mac_version = RTL_GIGA_MAC_VER_B;\
	}\
}

#define RTL_PRINT_MAC_VERSION(mac_version) \
{\
	switch(mac_version) \
	{ \
		case RTL_GIGA_MAC_VER_E: \
			DBG_PRINT("mac_version == RTL_GIGA_MAC_VER_E (0002)\n"); \
			break; \
		case RTL_GIGA_MAC_VER_D: \
			DBG_PRINT("mac_version == RTL_GIGA_MAC_VER_D (0001)\n"); \
			break; \
		case RTL_GIGA_MAC_VER_B: \
			DBG_PRINT("mac_version == RTL_GIGA_MAC_VER_B (0000)\n"); \
			break; \
		default: \
			DBG_PRINT("mac_version == Unknown\n"); \
			break; \
	} \
}

#define RTL_GIGA_PHY_VER_C    0x03	//PHY Reg 0x03 bit0-3 == 0x0000
#define RTL_GIGA_PHY_VER_D    0x04	//PHY Reg 0x03 bit0-3 == 0x0000
#define RTL_GIGA_PHY_VER_E    0x05	//PHY Reg 0x03 bit0-3 == 0x0000
#define RTL_GIGA_PHY_VER_F    0x06	//PHY Reg 0x03 bit0-3 == 0x0001
#define RTL_GIGA_PHY_VER_G    0x07	//PHY Reg 0x03 bit0-3 == 0x0002

#define RTL_GET_PHY_VERSION(phy_version)     \
{\
	phy_version = RTL_GIGA_PHY_VER_D;\
	if( (RTL8169_READ_GMII_REG(ioaddr,3)&0x000f) == 0x0002 ){ \
		phy_version = RTL_GIGA_PHY_VER_G;\
	} \
	else if( (RTL8169_READ_GMII_REG(ioaddr,3)&0x000f) == 0x0001 ){ \
		phy_version = RTL_GIGA_PHY_VER_F;\
	} \
	else if( (RTL8169_READ_GMII_REG(ioaddr,3)&0x000f) == 0x0000 ){ \
		phy_version = RTL_GIGA_PHY_VER_E;\
	} \
}

#define RTL_PRINT_PHY_VERSION(phy_version)  \
{\
	switch(phy_version)\
	{ \
		case RTL_GIGA_PHY_VER_G: \
			DBG_PRINT("phy_version == RTL_GIGA_PHY_VER_G (0002)\n"); \
			break; \
		case RTL_GIGA_PHY_VER_F: \
			DBG_PRINT("phy_version == RTL_GIGA_PHY_VER_F (0001)\n"); \
			break; \
		case RTL_GIGA_PHY_VER_E: \
			DBG_PRINT("phy_version == RTL_GIGA_PHY_VER_E (0000)\n"); \
			break; \
		case RTL_GIGA_PHY_VER_D: \
			DBG_PRINT("phy_version == RTL_GIGA_PHY_VER_D (0000)\n"); \
			break; \
		default: \
			DBG_PRINT("phy_version == Unknown\n"); \
			break; \
	} \
}


const static struct {
	const char *name;
	u8 mac_version;		 /* depend on RTL8169 docs */
	u32 RxConfigMask; 	/* should clear the bits supported by this chip */
} rtl_chip_info[] = {
	{ "RTL8169",  RTL_GIGA_MAC_VER_B,  0xff7e1880 },
	{ "RTL8169s/8110s",  RTL_GIGA_MAC_VER_D,  0xff7e1880 },
	{ "RTL8169s/8110s",  RTL_GIGA_MAC_VER_E,  0xff7e1880 },
};




static struct pci_device_id rtl8169_pci_tbl[] __devinitdata = {
	{ 0x10ec, 0x8169, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{0,},
};



MODULE_DEVICE_TABLE (pci, rtl8169_pci_tbl);

enum RTL8169_registers {
	MAC0 = 0x0,		/* Ethernet hardware address. */
	MAR0 = 0x8,		/* Multicast filter. */
	TxDescStartAddr	= 0x20,
	TxHDescStartAddr= 0x28,
	FLASH	= 0x30,
	ERSR	= 0x36,
	ChipCmd	= 0x37,
	TxPoll	= 0x38,
	IntrMask = 0x3C,
	IntrStatus = 0x3E,
	TxConfig = 0x40,
	RxConfig = 0x44,
	RxMissed = 0x4C,
	Cfg9346 = 0x50,
	Config0	= 0x51,
	Config1	= 0x52,
	Config2	= 0x53,
	Config3	= 0x54,
	Config4	= 0x55,
	Config5	= 0x56,
	MultiIntr = 0x5C,
	PHYAR	= 0x60,
	TBICSR	= 0x64,
	TBI_ANAR = 0x68,
	TBI_LPAR = 0x6A,
	PHYstatus = 0x6C,
	RxMaxSize = 0xDA,
	CPlusCmd = 0xE0,
	RxDescStartAddr	= 0xE4,
	ETThReg	= 0xEC,
	FuncEvent	= 0xF0,
	FuncEventMask	= 0xF4,
	FuncPresetState	= 0xF8,
	FuncForceEvent	= 0xFC,		
};

enum RTL8169_register_content {
	/*InterruptStatusBits*/
	SYSErr 		= 0x8000,
	PCSTimeout	= 0x4000,
	SWInt		= 0x0100,
	TxDescUnavail	= 0x80,
	RxFIFOOver 	= 0x40,
	LinkChg 	= 0x20,
	RxOverflow 	= 0x10,
	TxErr 	= 0x08,
	TxOK 	= 0x04,
	RxErr 	= 0x02,
	RxOK 	= 0x01,

	/*RxStatusDesc*/
	RxRES = 0x00200000,
	RxCRC = 0x00080000,
	RxRUNT= 0x00100000,
	RxRWT = 0x00400000,

	/*ChipCmdBits*/
	CmdReset = 0x10,
	CmdRxEnb = 0x08,
	CmdTxEnb = 0x04,
	RxBufEmpty = 0x01,

	/*Cfg9346Bits*/
	Cfg9346_Lock = 0x00,
	Cfg9346_Unlock = 0xC0,

	/*rx_mode_bits*/
	AcceptErr = 0x20,
	AcceptRunt = 0x10,
	AcceptBroadcast = 0x08,
	AcceptMulticast = 0x04,
	AcceptMyPhys = 0x02,
	AcceptAllPhys = 0x01,

	/*RxConfigBits*/
	RxCfgFIFOShift = 13,
	RxCfgDMAShift = 8,

	/*TxConfigBits*/
	TxInterFrameGapShift = 24,
	TxDMAShift = 8,		/* DMA burst value (0-7) is shift this many bits */

	/*rtl8169_PHYstatus*/
	TBI_Enable	= 0x80,
	TxFlowCtrl	= 0x40,
	RxFlowCtrl	= 0x20,
	_1000bpsF	= 0x10,
	_100bps		= 0x08,
	_10bps		= 0x04,
	LinkStatus	= 0x02,
	FullDup		= 0x01,

	/*GIGABIT_PHY_registers*/
	PHY_CTRL_REG = 0,
	PHY_STAT_REG = 1,
	PHY_AUTO_NEGO_REG = 4,
	PHY_1000_CTRL_REG = 9,

	/*GIGABIT_PHY_REG_BIT*/
	PHY_Restart_Auto_Nego	= 0x0200,
	PHY_Enable_Auto_Nego	= 0x1000,

	//PHY_STAT_REG = 1;
	PHY_Auto_Neco_Comp	= 0x0020,

	//PHY_AUTO_NEGO_REG = 4;
	PHY_Cap_10_Half		= 0x0020,
	PHY_Cap_10_Full		= 0x0040,
	PHY_Cap_100_Half	= 0x0080,
	PHY_Cap_100_Full	= 0x0100,

	//PHY_1000_CTRL_REG = 9;
	PHY_Cap_1000_Full	= 0x0200,

	PHY_Cap_Null		= 0x0,


	/*_MediaType*/
	_10_Half	= 0x01,
	_10_Full	= 0x02,
	_100_Half	= 0x04,
	_100_Full	= 0x08,
	_1000_Full	= 0x10,

	/*_TBICSRBit*/
	TBILinkOK 	= 0x02000000,
};



enum _DescStatusBit {
	OWNbit	= 0x80000000,
	EORbit	= 0x40000000,
	FSbit	= 0x20000000,
	LSbit	= 0x10000000,
};


struct TxDesc {
	u32		status;
	u32		vlan_tag;
	u32		buf_addr;
	u32		buf_Haddr;
};

struct RxDesc {
	u32		status;
	u32		vlan_tag;
	u32		buf_addr;
	u32		buf_Haddr;
};

#ifndef timer_t
typedef struct timer_list timer_t;
#endif //#ifndef timer_t

struct rtl8169_private {
	void *mmio_addr;				/* memory map physical address*/
	struct pci_dev *pci_dev;			/* Index of PCI device  */
	struct net_device_stats stats;			/* statistics of net device */
	spinlock_t lock;				/* spin lock flag */
	int chipset;
	int mac_version;
	int phy_version;
	timer_t phy_timer_t;
	unsigned long phy_link_down_cnt;
	unsigned long cur_rx;				/* Index into the Rx descriptor buffer of next Rx pkt. */
	unsigned long cur_tx;				/* Index into the Tx descriptor buffer of next Rx pkt. */
	unsigned long dirty_tx;
	unsigned char	*TxDescArrays;			/* Index of Tx Descriptor buffer */
	unsigned char	*RxDescArrays;			/* Index of Rx Descriptor buffer */
	struct	TxDesc	*TxDescArray;			/* Index of 256-alignment Tx Descriptor buffer */
	struct	RxDesc	*RxDescArray;			/* Index of 256-alignment Rx Descriptor buffer */
	struct	sk_buff	*Tx_skbuff[NUM_TX_DESC];	/* Index of Transmit data buffer */
	struct	sk_buff	*Rx_skbuff[NUM_RX_DESC];	/* Receive data buffer */
	unsigned char   drvinit_fail;
};


MODULE_AUTHOR ("Realtek");
MODULE_DESCRIPTION ("RealTek RTL-8169 Gigabit Ethernet driver");
MODULE_PARM (media, "1-" __MODULE_STRING(MAX_UNITS) "i");

static int rtl8169_open (struct net_device *dev);
static int rtl8169_start_xmit (struct sk_buff *skb, struct net_device *dev);
static void rtl8169_interrupt (int irq, void *dev_instance, struct pt_regs *regs);
static void rtl8169_init_ring (struct net_device *dev);
static void rtl8169_hw_start (struct net_device *dev);
static int rtl8169_close (struct net_device *dev);
static inline u32 ether_crc (int length, unsigned char *data);
static void rtl8169_set_rx_mode (struct net_device *dev);
static void rtl8169_tx_timeout (struct net_device *dev);
static struct net_device_stats *rtl8169_get_stats(struct net_device *netdev);

static void rtl8169_hw_PHY_config (struct net_device *dev);
static void rtl8169_hw_PHY_reset(struct net_device *dev);
static const u16 rtl8169_intr_mask = LinkChg | RxOverflow | RxFIFOOver | TxErr | TxOK | RxErr | RxOK ;
static const unsigned int rtl8169_rx_config = (RX_FIFO_THRESH << RxCfgFIFOShift) | (RX_DMA_BURST << RxCfgDMAShift) ;


#define RTL8169_WRITE_GMII_REG_BIT( ioaddr, reg, bitnum, bitval )\
{ \
	int val; \
	if( bitval == 1 ){ val = ( RTL8169_READ_GMII_REG( ioaddr, reg ) | (bitval<<bitnum) ) & 0xffff ; } \
	else{ val = ( RTL8169_READ_GMII_REG( ioaddr, reg ) & (~(0x0001<<bitnum)) ) & 0xffff ; } \
	RTL8169_WRITE_GMII_REG( ioaddr, reg, val ); \
}



#ifdef RTL8169_DEBUG
unsigned alloc_rxskb_cnt = 0;
#define RTL8169_ALLOC_RXSKB(bufsize) 	dev_alloc_skb(bufsize); alloc_rxskb_cnt ++ ;
#define RTL8169_FREE_RXSKB(skb) 	kfree_skb(skb); alloc_rxskb_cnt -- ;
#define RTL8169_NETIF_RX(skb) 		netif_rx(skb); alloc_rxskb_cnt -- ;
#else
#define RTL8169_ALLOC_RXSKB(bufsize) 	dev_alloc_skb(bufsize);
#define RTL8169_FREE_RXSKB(skb) 	kfree_skb(skb);
#define RTL8169_NETIF_RX(skb) 		netif_rx(skb);
#endif //end #ifdef RTL8169_DEBUG




//=================================================================
//	PHYAR
//	bit		Symbol
//	31		Flag
//	30-21	reserved
//	20-16	5-bit GMII/MII register address
//	15-0	16-bit GMII/MII register data
//=================================================================
void RTL8169_WRITE_GMII_REG( void *ioaddr, int RegAddr, int value )
{
	int	i;

	RTL_W32 ( PHYAR, 0x80000000 | (RegAddr&0xFF)<<16 | value);
	udelay(1000);

	for( i = 2000; i > 0 ; i -- ){
		// Check if the RTL8169 has completed writing to the specified MII register
		if( ! (RTL_R32(PHYAR)&0x80000000) ){
			break;
		}
		else{
			udelay(100);
		}// end of if( ! (RTL_R32(PHYAR)&0x80000000) )
	}// end of for() loop
}
//=================================================================
int RTL8169_READ_GMII_REG( void *ioaddr, int RegAddr )
{
	int i, value = -1;

	RTL_W32 ( PHYAR, 0x0 | (RegAddr&0xFF)<<16 );
	udelay(1000);

	for( i = 2000; i > 0 ; i -- ){
		// Check if the RTL8169 has completed retrieving data from the specified MII register
		if( RTL_R32(PHYAR) & 0x80000000 ){
			value = (int)( RTL_R32(PHYAR)&0xFFFF );
			break;
		}
		else{
			udelay(100);
		}// end of if( RTL_R32(PHYAR) & 0x80000000 )
	}// end of for() loop
	return value;
}



#define rtl8169_request_timer( timer, timer_expires, timer_func, timer_data ) \
{ \
	init_timer(timer); \
	timer->expires = (unsigned long)(jiffies + timer_expires); \
	timer->data = (unsigned long)(timer_data); \
	timer->function = (void *)(timer_func); \
	add_timer(timer); \
	DBG_PRINT("request_timer at 0x%08lx\n", (unsigned long)timer); \
}

#define rtl8169_delete_timer( del_timer_t ) \
{ \
	del_timer(del_timer_t); \
	DBG_PRINT("delete_timer at 0x%08lx\n", (unsigned long)del_timer_t); \
}

#define rtl8169_mod_timer( timer, timer_expires ) \
{ \
	mod_timer( timer, jiffies + timer_expires ); \
}




//======================================================================================================
//======================================================================================================
void rtl8169_phy_timer_t_handler( void	*timer_data )
{
	struct net_device *dev = (struct net_device *)timer_data;
	struct rtl8169_private *priv = (struct rtl8169_private *) (dev->priv);
	void *ioaddr = priv->mmio_addr;

	assert( priv->mac_version > RTL_GIGA_MAC_VER_B );
	assert( priv->phy_version < RTL_GIGA_PHY_VER_G );

	if( RTL_R8(PHYstatus) & LinkStatus ){
		priv->phy_link_down_cnt = 0 ;
	}
	else{
		priv->phy_link_down_cnt ++ ;
		if( priv->phy_link_down_cnt >= 12 ){
			// If link on 1000, perform phy reset.
			if( RTL8169_READ_GMII_REG( ioaddr, PHY_1000_CTRL_REG ) & PHY_Cap_1000_Full )
			{
				rtl8169_hw_PHY_reset( dev );
			}

			priv->phy_link_down_cnt = 0 ;
		}
	}

	//---------------------------------------------------------------------------
	//mod_timer is a more efficient way to update the expire field of an active timer.
	//---------------------------------------------------------------------------
	rtl8169_mod_timer( (&priv->phy_timer_t), 100 );
}




//======================================================================================================
//======================================================================================================
static int __devinit rtl8169_init_board ( struct pci_dev *pdev, struct net_device **dev_out, void **ioaddr_out)
{
	void *ioaddr = NULL;
	struct net_device *dev;
	struct rtl8169_private *priv;
	int rc, i;
	unsigned long mmio_start, mmio_end, mmio_flags, mmio_len;


	assert (pdev != NULL);
	assert (ioaddr_out != NULL);

	*ioaddr_out = NULL;
	*dev_out = NULL;

	// dev zeroed in init_etherdev 
	dev = init_etherdev (NULL, sizeof (*priv));
	if (dev == NULL) {
		printk (KERN_ERR PFX "unable to alloc new ethernet\n");
		return -ENOMEM;
	}

	SET_MODULE_OWNER(dev);
	priv = dev->priv;

	// enable device (incl. PCI PM wakeup and hotplug setup)
	rc = pci_enable_device (pdev);
	if (rc)
		goto err_out;

	mmio_start = pci_resource_start (pdev, 1);
	mmio_end = pci_resource_end (pdev, 1);
	mmio_flags = pci_resource_flags (pdev, 1);
	mmio_len = pci_resource_len (pdev, 1);

	// make sure PCI base addr 1 is MMIO
	if (!(mmio_flags & IORESOURCE_MEM)) {
		printk (KERN_ERR PFX "region #1 not an MMIO resource, aborting\n");
		rc = -ENODEV;
		goto err_out;
	}

	// check for weird/broken PCI region reporting
	if ( mmio_len < RTL_MIN_IO_SIZE ) {
		printk (KERN_ERR PFX "Invalid PCI region size(s), aborting\n");
		rc = -ENODEV;
		goto err_out;
	}


	rc = pci_request_regions (pdev, dev->name);
	if (rc)
		goto err_out;

	// enable PCI bus-mastering
	pci_set_master (pdev);


	// ioremap MMIO region
	ioaddr = ioremap (mmio_start, mmio_len);
	if (ioaddr == NULL) {
		printk (KERN_ERR PFX "cannot remap MMIO, aborting\n");
		rc = -EIO;
		goto err_out_free_res;
	}


	// Soft reset the chip.
	RTL_W8 ( ChipCmd, CmdReset);

	// Check that the chip has finished the reset.
	for (i = 1000; i > 0; i--){
		if ( (RTL_R8(ChipCmd) & CmdReset) == 0){
			break;
		}
		else{
			udelay (10);
		}
	}

	// identify chip attached to board
	RTL_GET_MAC_VERSION(priv->mac_version);
	RTL_GET_PHY_VERSION(priv->phy_version);

	RTL_PRINT_MAC_VERSION(priv->mac_version);
	RTL_PRINT_PHY_VERSION(priv->phy_version);



	for (i = ARRAY_SIZE (rtl_chip_info) - 1; i >= 0; i--){
		if (priv->mac_version == rtl_chip_info[i].mac_version) {
			priv->chipset = i;
			goto match;
		}
	}

	//if unknown chip, assume array element #0, original RTL-8169 in this case
	printk (KERN_DEBUG PFX "PCI device %s: unknown chip version, assuming RTL-8169\n", pdev->slot_name);
	priv->chipset = 0;

match:

	*ioaddr_out = ioaddr;
	*dev_out = dev;
	return 0;

//err_out_iounmap:
//	assert (ioaddr > 0);
//	iounmap (ioaddr);

err_out_free_res:
	pci_release_regions (pdev);

err_out:
	unregister_netdev (dev);
	kfree (dev);
	return rc;
}







//======================================================================================================
static int __devinit rtl8169_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev = NULL;
	struct rtl8169_private *priv = NULL;
	void *ioaddr = NULL;
	static int board_idx = -1;
	int i;
	int option = -1, Cap10_100 = 0, Cap1000 = 0;


	assert (pdev != NULL);
	assert (ent != NULL);

	board_idx++;


	i = rtl8169_init_board (pdev, &dev, &ioaddr);
	if (i < 0) {
		return i;
	}

	priv = dev->priv;

	assert (ioaddr != NULL);
	assert (dev != NULL);
	assert (priv != NULL);

	// Get MAC address //
	for (i = 0; i < MAC_ADDR_LEN ; i++){
		dev->dev_addr[i] = RTL_R8( MAC0 + i );
	}

	dev->open		= rtl8169_open;
	dev->hard_start_xmit 	= rtl8169_start_xmit;
	dev->get_stats    	= rtl8169_get_stats;
	dev->stop 		= rtl8169_close;
	dev->tx_timeout 	= rtl8169_tx_timeout;
	dev->set_multicast_list = rtl8169_set_rx_mode;
	dev->watchdog_timeo 	= TX_TIMEOUT;
	dev->irq 		= pdev->irq;
	dev->base_addr 		= (unsigned long) ioaddr;
//	dev->do_ioctl 		= mii_ioctl;

	priv = dev->priv;				// private data //
	priv->pci_dev 	= pdev;
	priv->mmio_addr 	= ioaddr;

	spin_lock_init (&priv->lock);
	
	pdev->driver_data = dev;

	printk (KERN_DEBUG "%s: Identified chip type is '%s'.\n",dev->name,rtl_chip_info[priv->chipset].name);
	printk (KERN_INFO "%s: %s at 0x%lx, "
				"%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x, "
				"IRQ %d\n",
				dev->name,
				RTL8169_DRIVER_NAME,
				dev->base_addr,
				dev->dev_addr[0], dev->dev_addr[1],
				dev->dev_addr[2], dev->dev_addr[3],
				dev->dev_addr[4], dev->dev_addr[5],
				dev->irq);

	
	// Config PHY
	rtl8169_hw_PHY_config(dev);

	DBG_PRINT("Set MAC Reg C+CR Offset 0x82h = 0x01h\n");
	RTL_W8( 0x82, 0x01 );


	if( priv->mac_version < RTL_GIGA_MAC_VER_E ){
		DBG_PRINT("Set PCI Latency=0x40\n");
		pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x40);
	}

	if( priv->mac_version == RTL_GIGA_MAC_VER_D ){
		DBG_PRINT("Set MAC Reg C+CR Offset 0x82h = 0x01h\n");
		RTL_W8( 0x82, 0x01 );
		DBG_PRINT("Set PHY Reg 0x0bh = 0x00h\n");
		RTL8169_WRITE_GMII_REG( ioaddr, 0x0b, 0x0000 );	//w 0x0b 15 0 0
	}

	// if TBI is not endbled
	if( !(RTL_R8(PHYstatus) & TBI_Enable) ){
		int	val = RTL8169_READ_GMII_REG( ioaddr, PHY_AUTO_NEGO_REG );

		option = (board_idx >= MAX_UNITS) ? 0 : media[board_idx];
		// Force RTL8169 in 10/100/1000 Full/Half mode.
		if( option > 0 ){
			printk(KERN_INFO "%s: Force-mode Enabled. \n", dev->name);
			Cap10_100 = 0;
			Cap1000 = 0;
			switch( option ){
				case _10_Half:
						Cap10_100 = PHY_Cap_10_Half;
						Cap1000 = PHY_Cap_Null;
						break;
				case _10_Full:
						Cap10_100 = PHY_Cap_10_Full | PHY_Cap_10_Half;
						Cap1000 = PHY_Cap_Null;
						break;
				case _100_Half:
						Cap10_100 = PHY_Cap_100_Half | PHY_Cap_10_Full | PHY_Cap_10_Half;
						Cap1000 = PHY_Cap_Null;
						break;
				case _100_Full:
						Cap10_100 = PHY_Cap_100_Full | PHY_Cap_100_Half | PHY_Cap_10_Full | PHY_Cap_10_Half;
						Cap1000 = PHY_Cap_Null;
						break;
				case _1000_Full:
						Cap10_100 = PHY_Cap_100_Full | PHY_Cap_100_Half | PHY_Cap_10_Full | PHY_Cap_10_Half;
						Cap1000 = PHY_Cap_1000_Full;
						break;
				default:
						break;
			}
			RTL8169_WRITE_GMII_REG( ioaddr, PHY_AUTO_NEGO_REG, Cap10_100 | ( val&0x1F ) );	//leave PHY_AUTO_NEGO_REG bit4:0 unchanged
			RTL8169_WRITE_GMII_REG( ioaddr, PHY_1000_CTRL_REG, Cap1000 );
		}
		else{
			printk(KERN_INFO "%s: Auto-negotiation Enabled.\n", dev->name);

			// enable 10/100 Full/Half Mode, leave PHY_AUTO_NEGO_REG bit4:0 unchanged
			RTL8169_WRITE_GMII_REG( ioaddr, PHY_AUTO_NEGO_REG,
				PHY_Cap_10_Half | PHY_Cap_10_Full | PHY_Cap_100_Half | PHY_Cap_100_Full | ( val&0x1F ) );

			// enable 1000 Full Mode
			RTL8169_WRITE_GMII_REG( ioaddr, PHY_1000_CTRL_REG, PHY_Cap_1000_Full );

		}// end of if( option > 0 )

		// Enable auto-negotiation and restart auto-nigotiation
		RTL8169_WRITE_GMII_REG( ioaddr, PHY_CTRL_REG, PHY_Enable_Auto_Nego | PHY_Restart_Auto_Nego );
		udelay(100);

		// wait for auto-negotiation process
		for( i = 10000; i > 0; i-- ){
			//check if auto-negotiation complete
			if( RTL8169_READ_GMII_REG(ioaddr, PHY_STAT_REG) & PHY_Auto_Neco_Comp ){
				udelay(100);
				option = RTL_R8(PHYstatus);
				if( option & _1000bpsF ){
					printk(KERN_INFO "%s: 1000Mbps Full-duplex operation.\n", dev->name);
				}
				else{
					printk(KERN_INFO "%s: %sMbps %s-duplex operation.\n", dev->name,
							(option & _100bps) ? "100" : "10", (option & FullDup) ? "Full" : "Half" );
				}
				break;
			}
			else{
				udelay(100);
			}// end of if( RTL8169_READ_GMII_REG(ioaddr, 1) & 0x20 )
		}// end for-loop to wait for auto-negotiation process

	}// end of TBI is not enabled
	else{
		udelay(100);
		printk(KERN_INFO "%s: 1000Mbps Full-duplex operation, TBI Link %s!\n",
							dev->name, (RTL_R32(TBICSR) & TBILinkOK) ? "OK" : "Failed" );

	}// end of TBI is not enabled

	return 0;
}







//======================================================================================================
static void __devexit rtl8169_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pdev->driver_data;
	struct rtl8169_private *priv = (struct rtl8169_private *) (dev->priv);

	assert (dev != NULL);
	assert (priv != NULL);

	unregister_netdev (dev);
	iounmap (priv->mmio_addr);
	pci_release_regions (pdev);

	// poison memory before freeing
	memset (dev, 0xBC, sizeof (struct net_device) +	sizeof (struct rtl8169_private));

	kfree (dev);
	pdev->driver_data = NULL;
}







//======================================================================================================
static int rtl8169_open (struct net_device *dev)
{
	struct rtl8169_private *priv = dev->priv;
	int retval;
	u8 diff;
	u32 TxPhyAddr, RxPhyAddr;


	if( priv->drvinit_fail == 1 ){
		printk("%s: Gigabit driver open failed.\n", dev->name );
		return -ENOMEM;
	}

	retval = request_irq (dev->irq, rtl8169_interrupt, SA_SHIRQ, dev->name, dev);
	if (retval) {
		return retval;
	}


	//////////////////////////////////////////////////////////////////////////////
	priv->TxDescArrays = kmalloc( NUM_TX_DESC * sizeof(struct TxDesc)+256 , GFP_KERNEL );
	// Tx Desscriptor needs 256 bytes alignment;
	TxPhyAddr = virt_to_bus(priv->TxDescArrays);
	diff = 256 - (TxPhyAddr-((TxPhyAddr >> 8)<< 8));
	TxPhyAddr += diff;
	priv->TxDescArray = (struct TxDesc *)(priv->TxDescArrays + diff);

	priv->RxDescArrays = kmalloc( NUM_RX_DESC * sizeof(struct RxDesc)+256 , GFP_KERNEL );
	// Rx Desscriptor needs 256 bytes alignment;
	RxPhyAddr = virt_to_bus(priv->RxDescArrays);
	diff = 256 - (RxPhyAddr-((RxPhyAddr >> 8)<< 8));
	RxPhyAddr += diff;
	priv->RxDescArray = (struct RxDesc *)(priv->RxDescArrays + diff);

	if ( priv->TxDescArrays == NULL || priv->RxDescArrays == NULL ) {
		printk(KERN_INFO"Allocate RxDescArray or TxDescArray failed\n");
		free_irq(dev->irq, dev);
		if (priv->TxDescArrays) kfree(priv->TxDescArrays);
		if (priv->RxDescArrays) kfree(priv->RxDescArrays);
		return -ENOMEM;
	}

	{
        int i;
		struct sk_buff *skb = NULL;

		for(i=0;i<NUM_RX_DESC;i++){
			skb = RTL8169_ALLOC_RXSKB(MAX_RX_SKBDATA_SIZE);
			if( skb != NULL ) {
				skb_reserve (skb, 2);	// 16 byte align the IP fields. //
				priv->Rx_skbuff[i] = skb;
			}
			else{
				printk("%s: Gigabit driver failed to allocate skbuff.\n", dev->name);
				priv->drvinit_fail = 1;
			}
		}
	}


	//////////////////////////////////////////////////////////////////////////////
	rtl8169_init_ring (dev);
	rtl8169_hw_start (dev);

	// ------------------------------------------------------
	if( (priv->mac_version > RTL_GIGA_MAC_VER_B) && (priv->phy_version < RTL_GIGA_PHY_VER_G) ){
		DBG_PRINT("FIX PCS -> rtl8169_request_timer\n");
		rtl8169_request_timer( (&priv->phy_timer_t), 100, rtl8169_phy_timer_t_handler, ((void *)dev) );  //in open()
		priv->phy_link_down_cnt = 0;
	}

	DBG_PRINT("%s: %s() alloc_rxskb_cnt = %d\n", dev->name, __FUNCTION__, alloc_rxskb_cnt );

	return 0;

}//end of rtl8169_open (struct net_device *dev)





//======================================================================================================
static void rtl8169_hw_PHY_reset(struct net_device *dev)
{
	int val, phy_reset_expiretime = 50;
	struct rtl8169_private *priv = dev->priv;
	void *ioaddr = priv->mmio_addr;

	DBG_PRINT("%s: Reset RTL8169s PHY\n", dev->name);

	val = ( RTL8169_READ_GMII_REG( ioaddr, 0 ) | 0x8000 ) & 0xffff;
	RTL8169_WRITE_GMII_REG( ioaddr, 0, val );

	do //waiting for phy reset
	{
		if( RTL8169_READ_GMII_REG( ioaddr, 0 ) & 0x8000 ){
			phy_reset_expiretime --;
			udelay(100);
		}
		else{
			break;
		}
	}while( phy_reset_expiretime >= 0 );

	assert( phy_reset_expiretime > 0 );
}




//======================================================================================================
static void rtl8169_hw_PHY_config (struct net_device *dev)
{
	struct rtl8169_private *priv = dev->priv;
	void *ioaddr = priv->mmio_addr;
	int val;

	RTL_PRINT_MAC_VERSION(priv->mac_version);
	RTL_PRINT_PHY_VERSION(priv->phy_version);

	if( (priv->mac_version > RTL_GIGA_MAC_VER_B) && ( priv->phy_version < RTL_GIGA_PHY_VER_F ) )
	{
			DBG_PRINT("MAC version!=0 && PHY version == 0 or 1\n");
			DBG_PRINT("Do final_reg2.cfg\n");
			// phy config for RTL8169s mac_version C chip
			RTL8169_WRITE_GMII_REG( ioaddr, 31, 0x0001 );	//w 31 2 0 1
			RTL8169_WRITE_GMII_REG( ioaddr, 21, 0x1000 );	//w 21 15 0 1000
			RTL8169_WRITE_GMII_REG( ioaddr, 24, 0x65c7 );	//w 24 15 0 65c7
			RTL8169_WRITE_GMII_REG_BIT( ioaddr, 4, 11, 0 );	//w 4 11 11 0
			val = RTL8169_READ_GMII_REG( ioaddr, 4 ) & 0x0fff;
			RTL8169_WRITE_GMII_REG( ioaddr, 4, val );	//w 4 15 12 0
			RTL8169_WRITE_GMII_REG( ioaddr, 3, 0x00a1 );	//w 3 15 0 00a1
			RTL8169_WRITE_GMII_REG( ioaddr, 2, 0x0008 );	//w 2 15 0 0008
			RTL8169_WRITE_GMII_REG( ioaddr, 1, 0x1020 );	//w 1 15 0 1020
			RTL8169_WRITE_GMII_REG( ioaddr, 0, 0x1000 );	//w 0 15 0 1000
			RTL8169_WRITE_GMII_REG_BIT( ioaddr, 4, 11, 1 );	//w 4 11 11 1
			RTL8169_WRITE_GMII_REG_BIT( ioaddr, 4, 11, 0 );	//w 4 11 11 0
			val = ( RTL8169_READ_GMII_REG( ioaddr, 4 ) & 0x0fff ) | 0x7000;
			RTL8169_WRITE_GMII_REG( ioaddr, 4, val );	//w 4 15 12 7
			RTL8169_WRITE_GMII_REG( ioaddr, 3, 0xff41 );	//w 3 15 0 ff41
			RTL8169_WRITE_GMII_REG( ioaddr, 2, 0xde60 );	//w 2 15 0 de60
			RTL8169_WRITE_GMII_REG( ioaddr, 1, 0x0140 );	//w 1 15 0 0140
			RTL8169_WRITE_GMII_REG( ioaddr, 0, 0x0077 );	//w 0 15 0 0077
			RTL8169_WRITE_GMII_REG_BIT( ioaddr, 4, 11, 1 );	//w 4 11 11 1
			RTL8169_WRITE_GMII_REG_BIT( ioaddr, 4, 11, 0 );	//w 4 11 11 0
			val = ( RTL8169_READ_GMII_REG( ioaddr, 4 ) & 0x0fff ) | 0xa000;
			RTL8169_WRITE_GMII_REG( ioaddr, 4, val );	//w 4 15 12 a
			RTL8169_WRITE_GMII_REG( ioaddr, 3, 0xdf01 );	//w 3 15 0 df01
			RTL8169_WRITE_GMII_REG( ioaddr, 2, 0xdf20 );	//w 2 15 0 df20
			RTL8169_WRITE_GMII_REG( ioaddr, 1, 0xff95 );	//w 1 15 0 ff95
			RTL8169_WRITE_GMII_REG( ioaddr, 0, 0xfa00 );	//w 0 15 0 fa00
			RTL8169_WRITE_GMII_REG_BIT( ioaddr, 4, 11, 1 );	//w 4 11 11 1
			RTL8169_WRITE_GMII_REG_BIT( ioaddr, 4, 11, 0 );	//w 4 11 11 0
			val = ( RTL8169_READ_GMII_REG( ioaddr, 4 ) & 0x0fff ) | 0xb000;
			RTL8169_WRITE_GMII_REG( ioaddr, 4, val );	//w 4 15 12 b
			RTL8169_WRITE_GMII_REG( ioaddr, 3, 0xff41 );	//w 3 15 0 ff41
			RTL8169_WRITE_GMII_REG( ioaddr, 2, 0xde20 );	//w 2 15 0 de20
			RTL8169_WRITE_GMII_REG( ioaddr, 1, 0x0140 );	//w 1 15 0 0140
			RTL8169_WRITE_GMII_REG( ioaddr, 0, 0x00bb );	//w 0 15 0 00bb
			RTL8169_WRITE_GMII_REG_BIT( ioaddr, 4, 11, 1 );	//w 4 11 11 1
			RTL8169_WRITE_GMII_REG_BIT( ioaddr, 4, 11, 0 );	//w 4 11 11 0
			val = ( RTL8169_READ_GMII_REG( ioaddr, 4 ) & 0x0fff ) | 0xf000;
			RTL8169_WRITE_GMII_REG( ioaddr, 4, val );	//w 4 15 12 f
			RTL8169_WRITE_GMII_REG( ioaddr, 3, 0xdf01 );	//w 3 15 0 df01
			RTL8169_WRITE_GMII_REG( ioaddr, 2, 0xdf20 );	//w 2 15 0 df20
			RTL8169_WRITE_GMII_REG( ioaddr, 1, 0xff95 );	//w 1 15 0 ff95
			RTL8169_WRITE_GMII_REG( ioaddr, 0, 0xbf00 );	//w 0 15 0 bf00
			RTL8169_WRITE_GMII_REG_BIT( ioaddr, 4, 11, 1 );	//w 4 11 11 1
			RTL8169_WRITE_GMII_REG_BIT( ioaddr, 4, 11, 0 );	//w 4 11 11 0
			RTL8169_WRITE_GMII_REG( ioaddr, 31, 0x0000 );	//w 31 2 0 0
	}
}










//======================================================================================================
static void rtl8169_hw_start (struct net_device *dev)
{
	struct rtl8169_private *priv = dev->priv;
	void *ioaddr = priv->mmio_addr;
	u32 i;


	/* Soft reset the chip. */
	RTL_W8 ( ChipCmd, CmdReset);

	/* Check that the chip has finished the reset. */
	for (i = 1000; i > 0; i--){
		if ((RTL_R8( ChipCmd ) & CmdReset) == 0) break;
		else udelay (10);
	}

	RTL_W8 ( Cfg9346, Cfg9346_Unlock);
	RTL_W8 ( ChipCmd, CmdTxEnb | CmdRxEnb);
	RTL_W8 ( ETThReg, ETTh);

	// For gigabit rtl8169
	RTL_W16	( RxMaxSize, RxPacketMaxSize );

	// Set Rx Config register
	i = rtl8169_rx_config | ( RTL_R32( RxConfig ) & rtl_chip_info[priv->chipset].RxConfigMask);
	RTL_W32 ( RxConfig, i);

	/* Set DMA burst size and Interframe Gap Time */
	RTL_W32 ( TxConfig, (TX_DMA_BURST << TxDMAShift) | (InterFrameGap << TxInterFrameGapShift) );

	RTL_W16( CPlusCmd, RTL_R16(CPlusCmd) );

	//2003-07-18
	if( priv->mac_version == RTL_GIGA_MAC_VER_D ){
		DBG_PRINT("Set MAC Reg C+CR Offset 0xE0: bit-3 and bit-14 MUST be 1\n");
		RTL_W16( CPlusCmd, (RTL_R16(CPlusCmd)|(1<<14)|(1<<3)) );
	}

	priv->cur_rx = 0;

	RTL_W32 ( TxDescStartAddr, virt_to_bus(priv->TxDescArray));
	RTL_W32 ( RxDescStartAddr, virt_to_bus(priv->RxDescArray));
	RTL_W8 ( Cfg9346, Cfg9346_Lock );
	udelay (10);

	RTL_W32 ( RxMissed, 0 );

	rtl8169_set_rx_mode (dev);

	/* no early-rx interrupts */
	RTL_W16 ( MultiIntr, RTL_R16(MultiIntr) & 0xF000);

	/* Enable all known interrupts by setting the interrupt mask. */
	RTL_W16 ( IntrMask, rtl8169_intr_mask);

	netif_start_queue (dev);

}//end of rtl8169_hw_start (struct net_device *dev)







//======================================================================================================
static void rtl8169_init_ring (struct net_device *dev)
{
	struct rtl8169_private *priv = dev->priv;
	int i;

	priv->cur_rx = 0;
	priv->cur_tx = 0;
	priv->dirty_tx = 0;
	memset(priv->TxDescArray, 0x0, NUM_TX_DESC*sizeof(struct TxDesc));
	memset(priv->RxDescArray, 0x0, NUM_RX_DESC*sizeof(struct RxDesc));

	for (i=0 ; i<NUM_TX_DESC ; i++){
		priv->Tx_skbuff[i]=NULL;
	}
	for (i=0; i <NUM_RX_DESC; i++) {
		if(i==(NUM_RX_DESC-1))
			priv->RxDescArray[i].status = (OWNbit | EORbit) + RX_BUF_SIZE;
		else
			priv->RxDescArray[i].status = OWNbit + RX_BUF_SIZE;

		{//-----------------------------------------------------------------------
			struct sk_buff *skb = priv->Rx_skbuff[i];

			if( skb != NULL ){
				priv->RxDescArray[i].buf_addr = virt_to_bus( skb->data );
			}
			else{
				DBG_PRINT("%s: %s() Rx_skbuff == NULL\n", dev->name, __FUNCTION__);
				priv->drvinit_fail = 1;
			}
		}//-----------------------------------------------------------------------

	}
}







//======================================================================================================
static void rtl8169_tx_clear (struct rtl8169_private *priv)
{
	int i;

	priv->cur_tx = 0;
	for ( i = 0 ; i < NUM_TX_DESC ; i++ ){
		if ( priv->Tx_skbuff[i] != NULL ) {
			dev_kfree_skb ( priv->Tx_skbuff[i] );
			priv->Tx_skbuff[i] = NULL;
			priv->stats.tx_dropped++;
		}
	}
}







//======================================================================================================
static void rtl8169_tx_timeout (struct net_device *dev)
{
	struct rtl8169_private *priv = dev->priv;
	void *ioaddr = priv->mmio_addr;
	u8 tmp8;

	/* disable Tx, if not already */
	tmp8 = RTL_R8( ChipCmd );
	if (tmp8 & CmdTxEnb){
		RTL_W8 ( ChipCmd, tmp8 & ~CmdTxEnb);
	}

	/* Disable interrupts by clearing the interrupt mask. */
	RTL_W16 ( IntrMask, 0x0000);

	/* Stop a shared interrupt from scavenging while we are. */
	spin_lock_irq (&priv->lock);
	rtl8169_tx_clear (priv);
	spin_unlock_irq (&priv->lock);

	/* ...and finally, reset everything */
	rtl8169_hw_start (dev);

	netif_wake_queue (dev);
}







//======================================================================================================
static int rtl8169_start_xmit (struct sk_buff *skb, struct net_device *dev)
{
	struct rtl8169_private *priv = dev->priv;
	void *ioaddr = priv->mmio_addr;
	int entry = priv->cur_tx % NUM_TX_DESC;

	spin_lock_irq (&priv->lock);

	if( (priv->TxDescArray[entry].status & OWNbit)==0 ){
		priv->Tx_skbuff[entry] = skb;
		priv->TxDescArray[entry].buf_addr = virt_to_bus(skb->data);
		if( entry != (NUM_TX_DESC-1) )
			priv->TxDescArray[entry].status = (OWNbit | FSbit | LSbit) | ( (skb->len > ETH_ZLEN) ? skb->len : ETH_ZLEN);
		else
		 	priv->TxDescArray[entry].status = (OWNbit | EORbit | FSbit | LSbit) | ( (skb->len > ETH_ZLEN) ? skb->len : ETH_ZLEN);

		RTL_W8 ( TxPoll, 0x40);		//set polling bit

		dev->trans_start = jiffies;

		priv->stats.tx_bytes += ( (skb->len > ETH_ZLEN) ? skb->len : ETH_ZLEN);
		priv->cur_tx++;
	}//end of if( (priv->TxDescArray[entry].status & 0x80000000)==0 )

	spin_unlock_irq (&priv->lock);

	if ( (priv->cur_tx - NUM_TX_DESC) == priv->dirty_tx ){
		netif_stop_queue (dev);
	}
	else{
		if (netif_queue_stopped (dev)){
			netif_wake_queue (dev);
		}
	}

	return 0;
}







//======================================================================================================
static void rtl8169_tx_interrupt (struct net_device *dev, struct rtl8169_private *priv, void *ioaddr)
{
	unsigned long dirty_tx, tx_left=0;
	int entry = priv->cur_tx % NUM_TX_DESC;

	assert (dev != NULL);
	assert (priv != NULL);
	assert (ioaddr != NULL);


	dirty_tx = priv->dirty_tx;
	tx_left = priv->cur_tx - dirty_tx;

	while (tx_left > 0) {
		if( (priv->TxDescArray[entry].status & OWNbit) == 0 ){
			dev_kfree_skb_irq( priv->Tx_skbuff[dirty_tx % NUM_TX_DESC] );
			priv->Tx_skbuff[dirty_tx % NUM_TX_DESC] = NULL;
			priv->stats.tx_packets++;
			dirty_tx++;
			tx_left--;
			entry++;
		}
	}

	if (priv->dirty_tx != dirty_tx) {
		priv->dirty_tx = dirty_tx;
		if (netif_queue_stopped (dev))
			netif_wake_queue (dev);
	}
}






//======================================================================================================
static void rtl8169_rx_interrupt (struct net_device *dev, struct rtl8169_private *priv, void *ioaddr)
{
	int cur_rx;
//	struct sk_buff *skb;
	int pkt_size = 0 ;
    	int rxdesc_cnt = 0;
	int ret;
	struct sk_buff *n_skb = NULL;
	struct sk_buff *rx_skb = priv->Rx_skbuff[cur_rx];
	struct sk_buff *cur_skb;

	assert (dev != NULL);
	assert (priv != NULL);
	assert (ioaddr != NULL);


	cur_rx = priv->cur_rx;

	while ( ((priv->RxDescArray[cur_rx].status & OWNbit)== 0) && (rxdesc_cnt < 20) ){

	    rxdesc_cnt++;

		if( priv->RxDescArray[cur_rx].status & RxRES ){
			printk(KERN_INFO "%s: Rx ERROR!!!\n", dev->name);
			priv->stats.rx_errors++;
			if (priv->RxDescArray[cur_rx].status & (RxRWT|RxRUNT) )
				priv->stats.rx_length_errors++;
			if (priv->RxDescArray[cur_rx].status & RxCRC)
				priv->stats.rx_crc_errors++;
	    }
	    else{
			pkt_size=(int)(priv->RxDescArray[cur_rx].status & 0x00001FFF)-4;
			{// -----------------------------------------------------
				rx_skb = priv->Rx_skbuff[cur_rx];
				
				//n_skb = NULL;
				n_skb = RTL8169_ALLOC_RXSKB(MAX_RX_SKBDATA_SIZE);
				if( n_skb != NULL ) {
					skb_reserve (n_skb, 2);	// 16 byte align the IP fields. //

					// Indicate rx_skb
					if( rx_skb != NULL ){
						rx_skb->dev = dev;
						skb_put ( rx_skb, pkt_size );
						rx_skb->protocol = eth_type_trans ( rx_skb, dev );
						ret = RTL8169_NETIF_RX (rx_skb);

						dev->last_rx = jiffies;
						priv->stats.rx_bytes += pkt_size;
						priv->stats.rx_packets++;
#if 0
						switch(ret)
						{
						case NET_RX_SUCCESS:    printk("%s: NETIF_RX_SUCCESS\n", dev->name);    break;
						case NET_RX_CN_LOW:     printk("%s: NETIF_RX_CN_LOW\n", dev->name);     break;
						case NET_RX_CN_MOD:     printk("%s: NETIF_CN_MOD\n", dev->name);        break;
						case NET_RX_CN_HIGH:    printk("%s: NETIF_CN_HIGH\n", dev->name);       break;
						case NET_RX_DROP:       printk("%s: NETIF_RX_DROP\n", dev->name);       break;
						default:                printk("%s: netif_rx():Unknown\n", dev->name);  break;
						}
#endif
					}//end if( rx_skb != NULL )

					priv->Rx_skbuff[cur_rx] = n_skb;
				}
				else{
					priv->Rx_skbuff[cur_rx] = rx_skb;
				}

				// Update rx descriptor
				if( cur_rx == (NUM_RX_DESC-1) ){
					priv->RxDescArray[cur_rx].status  = (OWNbit | EORbit) + RX_BUF_SIZE;
				}
				else{
					priv->RxDescArray[cur_rx].status  = OWNbit + RX_BUF_SIZE;
				}

				cur_skb = priv->Rx_skbuff[cur_rx];
				if( cur_skb != NULL ){
					priv->RxDescArray[cur_rx].buf_addr = virt_to_bus( cur_skb->data );
				}
				else{
					DBG_PRINT("%s: %s() cur_skb == NULL\n", dev->name, __FUNCTION__);
				}

			}//------------------------------------------------------------

	    }// end of if( priv->RxDescArray[cur_rx].status & RxRES )

	    cur_rx = (cur_rx +1) % NUM_RX_DESC;

	}// end of while ( (priv->RxDescArray[cur_rx].status & 0x80000000)== 0)


	if( rxdesc_cnt == 20 ){
		printk("rxdesc_cnt == 20 ----------\n");
	}


	priv->cur_rx = cur_rx;
}








//======================================================================================================
/* The interrupt handler does all of the Rx thread work and cleans up after the Tx thread. */
static void rtl8169_interrupt (int irq, void *dev_instance, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct rtl8169_private *priv = dev->priv;
	int boguscnt = max_interrupt_work;
	void *ioaddr = priv->mmio_addr;
	int status = 0;

	do {
		status = RTL_R16(IntrStatus);

		/* h/w no longer present (hotplug?) or major error, bail */
		if (status == 0xFFFF)
			break;

/*
		if (status & LinkChg)
			link_changed = RTL_R16 (CSCR) & CSCR_LinkChangeBit;
*/

		RTL_W16( IntrStatus, status );


		if ( (status & rtl8169_intr_mask ) == 0 )
			break;


		// Rx interrupt
//		if (status & (RxOK | LinkChg | RxOverflow | RxFIFOOver)){
			rtl8169_rx_interrupt (dev, priv, ioaddr);
//		}

		// Tx interrupt
//		if (status & (TxOK | TxErr)) {
			spin_lock (&priv->lock);
			rtl8169_tx_interrupt (dev, priv, ioaddr);
			spin_unlock (&priv->lock);
//		}

		boguscnt--;
	} while (boguscnt > 0);

	if (boguscnt <= 0) {
		printk (KERN_WARNING "%s: Too much work at interrupt!\n", dev->name);
		/* Clear all interrupt sources. */
		RTL_W16( IntrStatus, 0xffff);
	}
}







//======================================================================================================
static int rtl8169_close (struct net_device *dev)
{
	struct rtl8169_private *priv = dev->priv;
	void *ioaddr = priv->mmio_addr;
	int i;

	//////////////////////////////////////////////////////////////////////////////
	// ------------------------------------------------------
	if( (priv->mac_version > RTL_GIGA_MAC_VER_B) && (priv->phy_version < RTL_GIGA_PHY_VER_G) ){
		DBG_PRINT("FIX PCS -> rtl8169_delete_timer\n");
		rtl8169_delete_timer( &(priv->phy_timer_t) ); //in close()
		priv->phy_link_down_cnt = 0;
	}

	netif_stop_queue (dev);

	spin_lock_irq (&priv->lock);

	/* Stop the chip's Tx and Rx DMA processes. */
	RTL_W8 ( ChipCmd, 0x00);

	/* Disable interrupts by clearing the interrupt mask. */
	RTL_W16 ( IntrMask, 0x0000);

	/* Update the error counts. */
	priv->stats.rx_missed_errors += RTL_R32(RxMissed);
	RTL_W32( RxMissed, 0);

	spin_unlock_irq (&priv->lock);

	synchronize_irq ();
	free_irq (dev->irq, dev);

	rtl8169_tx_clear (priv);
	kfree(priv->TxDescArrays);
	kfree(priv->RxDescArrays);
	priv->TxDescArrays = NULL;
	priv->RxDescArrays = NULL;
	priv->TxDescArray = NULL;
	priv->RxDescArray = NULL;

	{//-----------------------------------------------------------------------------
		for(i=0;i<NUM_RX_DESC;i++){
			if( priv->Rx_skbuff[i] != NULL ) {
				RTL8169_FREE_RXSKB ( priv->Rx_skbuff[i] );
			}
		}
	}//-----------------------------------------------------------------------------

	DBG_PRINT("%s: %s() alloc_rxskb_cnt = %d\n", dev->name, __FUNCTION__, alloc_rxskb_cnt );

	return 0;
}







//======================================================================================================
static unsigned const ethernet_polynomial = 0x04c11db7U;
static inline u32 ether_crc (int length, unsigned char *data)
{
	int crc = -1;


	while (--length >= 0) {
		unsigned char current_octet = *data++;
		int bit;
		for (bit = 0; bit < 8; bit++, current_octet >>= 1)
			crc = (crc << 1) ^ ((crc < 0) ^ (current_octet & 1) ? ethernet_polynomial : 0);
	}

	return crc;
}








//======================================================================================================
static void rtl8169_set_rx_mode (struct net_device *dev)
{
	struct rtl8169_private *priv = dev->priv;
	void *ioaddr = priv->mmio_addr;
	unsigned long flags;
	u32 mc_filter[2];	/* Multicast hash filter */
	int i, rx_mode;
	u32 tmp=0;
	

	if (dev->flags & IFF_PROMISC) {
		/* Unconditionally log net taps. */
		printk (KERN_NOTICE "%s: Promiscuous mode enabled.\n", dev->name);
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys | AcceptAllPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else if ((dev->mc_count > multicast_filter_limit) || (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else {
		struct dev_mc_list *mclist;
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0;
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count; i++, mclist = mclist->next)
			set_bit (ether_crc (ETH_ALEN, mclist->dmi_addr) >> 26, mc_filter);
	}

	spin_lock_irqsave (&priv->lock, flags);

	tmp = rtl8169_rx_config | rx_mode | (RTL_R32(RxConfig) & rtl_chip_info[priv->chipset].RxConfigMask);
	
	RTL_W32 ( RxConfig, tmp);
	RTL_W32 ( MAR0 + 0, mc_filter[0]);
	RTL_W32 ( MAR0 + 4, mc_filter[1]);

	spin_unlock_irqrestore (&priv->lock, flags);

}//end of rtl8169_set_rx_mode (struct net_device *dev)







//================================================================================
struct net_device_stats *rtl8169_get_stats(struct net_device *dev)

{
	struct rtl8169_private *priv = dev->priv;

    return &priv->stats;
}








//================================================================================
static struct pci_driver rtl8169_pci_driver = {
	name:		MODULENAME,
	id_table:	rtl8169_pci_tbl,
	probe:		rtl8169_init_one,
	remove:		rtl8169_remove_one,
	suspend:	NULL,
	resume:		NULL,
};





//======================================================================================================
static int __init rtl8169_init_module (void)
{
	return pci_module_init (&rtl8169_pci_driver);	// pci_register_driver (drv)
}




//======================================================================================================
static void __exit rtl8169_cleanup_module (void)
{
	pci_unregister_driver (&rtl8169_pci_driver);
}




//======================================================================================================
module_init(rtl8169_init_module);
module_exit(rtl8169_cleanup_module);
