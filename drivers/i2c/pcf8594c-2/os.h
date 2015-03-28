#include <linux/delay.h>
#include <linux/sched.h> 
#include <asm/system.h>
#include <asm/types.h>
#include <asm/arch/ixp425-gpio.h>


#define LOCAL			static
#define STATUS			int
#define UINT8			u8
#define UINT32			u32

#define ERROR	(-1)
#define OK 			0
#define ixp425GPIOLineConfig 	gpio_line_config
#define ixp425GPIOLineGet 	gpio_line_get
#define ixp425GPIOLineSet 	gpio_line_set
#define sysMicroDelay(x)	udelay(x)
#define IXP425_GPIO_SIG		int

static inline int intLock(void)
{
    int flags;
    save_flags(flags);
    cli();
    return flags;
}

static inline void intUnlock(int flags)
{
    restore_flags(flags);
}

static inline void taskDelay(int milliseconds)
{
    current->state = TASK_INTERRUPTIBLE;
    schedule_timeout((milliseconds*HZ)/1000);
}
