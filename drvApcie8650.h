/*******************************************************************************

Project:
    LCLS 

File:
    drvXyApcie8650.h

Description:
    Header file for the XVME-Apcie8650 Industrial I/O Pack PCIe Carrier

Author:
    rdabney <rdabney@slac.stanford.edu>

Created:
      12th February 2013

*******************************************************************************/

#ifndef INCdrvXyApcie8650H
#define INCdrvXyApcie8650H

#ifdef		__cplusplus
extern "C"	{
#endif	/*	__cplusplus	*/

#define UIODEVNAME		"/dev/uio"
#define	UIOCLASSPATH_CONFIG	"/sys/class/uio/uio%d/device/config"
#define	UIOCLASSPATH_MMIO	"/sys/class/uio/uio%d/device/resource2"

#define	APC8650_CONFIG_SIZE	256
#define	APC8650_IO_SIZE		67108864

#define APC8650_SLOT_A_IO_OFFSET	0x0180		/*  Slot A IO space addr. offset from carrier base addr. */
#define APC8650_SLOT_A_ID_OFFSET	0x0040		/*  Slot A ID space addr. offset from carrier base addr. */
#define APC8650_SLOT_A_MEM_OFFSET	0x00800000	/*  Slot A MEM space addr. */
#define APC8650_SLOT_B_IO_OFFSET	0x0200		/*  Slot B IO space addr. offset from carrier base addr. */
#define APC8650_SLOT_B_ID_OFFSET	0x0080		/*  Slot B ID space addr. offset from carrier base addr. */
#define APC8650_SLOT_B_MEM_OFFSET	0x01000000	/*  Slot B MEM space addr. */
#define APC8650_SLOT_C_IO_OFFSET	0x0280		/*  Slot C IO space addr. offset from carrier base addr. */
#define APC8650_SLOT_C_ID_OFFSET	0x00C0		/*  Slot C ID space addr. offset from carrier base addr. */
#define APC8650_SLOT_C_MEM_OFFSET	0x01800000	/*  Slot C MEM space addr. */
#define APC8650_SLOT_D_IO_OFFSET	0x0300		/*  Slot D IO space addr. offset from carrier base addr. */
#define APC8650_SLOT_D_ID_OFFSET	0x0100		/*  Slot D ID space addr. offset from carrier base addr. */
#define APC8650_SLOT_D_MEM_OFFSET	0x02000000	/*  Slot D MEM space addr. */


#define GLOBAL_ENAB    1        /* global interrupt enable bit */

/* Parameter mask bit positions */

#define CLR		  1     /* clear register */
#define INT_ENAB	  2     /* interrupt enable register */
#define INT_LEV		  4     /* interrupt level register */
#define MEM_ENABLE	  8	/* memory enable register */
#define AMBASR		 16	/* IPA memory base addr & size register */
#define BMBASR		 32	/* IPB memory base addr & size register */
#define CMBASR		 64	/* IPC memory base addr & size register */
#define DMBASR		128	/* IPD memory base addr & size register */

/* Board Status Register bit positions */

#define GLOBAL_PEND     4       /* global interrupt pending bit position */
#define GLOBAL_EN       8       /* global interrupt enable bit position */
#define SOFT_RESET   0x10       /* software reset bit position */

/* Masks for Interrupt Enable, Interrupt Pending and Interrupt Clear Registers */

#define SLOTA_ZERO   0xFE       /* Write 0 to Int0 of A */
#define SLOTB_ZERO   0xFB       /* Write 0 to Int0 of B */
#define SLOTC_ZERO   0xEF       /* Write 0 to Int0 of C */
#define SLOTD_ZERO   0xBF       /* Write 0 to Int0 of D */

/* Memory map for the Acromag Apcie8650 Carrier Board */

struct mapApcie8650                  /* Memory map of the board */
{
    volatile word stsCtl;        /* Status/Control Register */
    volatile word intPending;        /* Interrupt Pending Register */
    volatile word slotAInt0;         /* Slot A interrupt 0 select space */
    volatile word slotAInt1;         /* Slot A interrupt 1 select space */ 
    volatile word slotBInt0;         /* Slot B interrupt 0 select space */
    volatile word slotBInt1;         /* Slot B interrupt 1 select space */
    volatile word slotCInt0;         /* Slot C interrupt 0 select space */
    volatile word slotCInt1;         /* Slot C interrupt 1 select space */
    volatile word slotDInt0;         /* Slot D interrupt 0 select space */
    volatile word slotDInt1;         /* Slot D interrupt 1 select space */
    volatile word noslotEInt0;
    volatile word noslotEInt1;
    volatile word clkCtl;            /* IP Clock Control Register */
    volatile word ID;                /* 16 bit non-volatile identifier */
};


/* Structure to hold the board's configuration information. */

struct configApcie8650
{
    struct mapApcie8650 *brd_ptr; /* base address of the board */         
    unsigned short card;        /* Number of IP carrier board           */
    unsigned short attr;        /* attribute mask for configuring board */
    unsigned short param;       /* parameter mask for configuring board */
    unsigned char clear;        /* interrupt clear register */
    unsigned char enable;       /* interrupt enable register */
    unsigned char level;        /* interrupt level register */
    unsigned char mem_enable;   /* memory enable register */
    unsigned char ambasr;       /* IPA memory base addr & size register */
    unsigned char bmbasr;       /* IPB memory base addr & size register */
    unsigned char cmbasr;       /* IPC memory base addr & size register */
    unsigned char dmbasr;       /* IPD memory base addr & size register */
    unsigned int initialized;
    int uioDevFd;		/* UIO /dev/uioN file descriptor		*/
    int uioClassPathConfigFd;	/* UIO sysfs class path	config file descriptor (BAR0)	*/        
    int uioClassPathMMIOFd;	/* UIO sysfs class path	config file descriptor (BAR2)	*/        
    epicsThreadId tid;
    unsigned long ioBase;
};

extern int	ipacAddApcie8650(	const char	*	cardParams	);

#ifdef		__cplusplus
}
#endif	/*	__cplusplus	*/

#endif  /* INCdrvXyApcie8650H */

