/*******************************************************************************

Project:
    LCLS 

File:
    drvXyTamc220.h

Description:
    Header file for the XVME-Tamc220 Industrial I/O Pack PCIe Carrier

Author:
    rdabney <rdabney@slac.stanford.edu>

Created:
      12th February 2013

*******************************************************************************/

#ifndef INCdrvXyTamc220H
#define INCdrvXyTamc220H

#ifdef		__cplusplus
extern "C"	{
#endif	/*	__cplusplus	*/

#define UIODEVNAME		"/dev/uio"
#define	UIOCLASSPATH_CONFIG	"/sys/class/uio/uio%d/device/config"
#define	UIOCLASSPATH_CTLSTS	"/sys/class/uio/uio%d/device/resource2"
#define	UIOCLASSPATH_MMIO	"/sys/class/uio/uio%d/device/resource3"

#define	TAMC220_CONFIG_SIZE	256
//#define	TAMC220_IO_SIZE		1024	
#define	TAMC220_CTLSTS_SIZE	256	
#define	TAMC220_IO_SIZE		4096	

#define TAMC220_SLOT_A_IO_OFFSET	0x000000	/*  Slot A IO space addr. offset from carrier base addr. */
#define TAMC220_SLOT_A_ID_OFFSET	0x000080	/*  Slot A ID space addr. offset from carrier base addr. */
#define TAMC220_SLOT_A_MEM_OFFSET	0x0000C0	/*  Slot A MEM space addr. */
#define TAMC220_SLOT_A_INT_OFFSET	0x0000C0	/*  Slot A INT space addr. */

#define TAMC220_SLOT_B_IO_OFFSET	0x000100	/*  Slot B IO space addr. offset from carrier base addr. */
#define TAMC220_SLOT_B_ID_OFFSET	0x000180	/*  Slot B ID space addr. offset from carrier base addr. */
#define TAMC220_SLOT_B_MEM_OFFSET	0x0001c0	/*  Slot B MEM space addr. */
#define TAMC220_SLOT_B_INT_OFFSET	0x0001c0	/*  Slot B INT space addr. */

#define TAMC220_SLOT_C_IO_OFFSET	0x000200	/*  Slot C IO space addr. offset from carrier base addr. */
#define TAMC220_SLOT_C_ID_OFFSET	0x000280	/*  Slot C ID space addr. offset from carrier base addr. */
#define TAMC220_SLOT_C_MEM_OFFSET	0x000300	/*  Slot C MEM space addr. */
#define TAMC220_SLOT_C_INT_OFFSET	0x000300	/*  Slot C INT space addr. */


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

#define 	TAMC220_CTLSTS_INT0_ENABLE	0x40
#define 	TAMC220_CTLSTS_INT1_ENABLE	0x80

/* Board Status Register bit positions */

#define GLOBAL_PEND     4       /* global interrupt pending bit position */
#define GLOBAL_EN       8       /* global interrupt enable bit position */
#define SOFT_RESET   0x10       /* software reset bit position */

/* Masks for Interrupt Enable, Interrupt Pending and Interrupt Clear Registers */

#define SLOTA_ZERO   0xFE       /* Write 0 to Int0 of A */
#define SLOTB_ZERO   0xFB       /* Write 0 to Int0 of B */
#define SLOTC_ZERO   0xEF       /* Write 0 to Int0 of C */
#define SLOTD_ZERO   0xBF       /* Write 0 to Int0 of D */

typedef struct ctlStatus 
{
    epicsUInt16 revID;
    epicsUInt16 ipCtl[3];
    epicsUInt16 rsvd0;
    epicsUInt16 ipReset;
    epicsUInt16 ipStatus;
    epicsUInt16 rsvd1;
    epicsUInt16 rsvd2[256];
} ctlStatus;
    


/* Structure to hold the board's configuration information. */

struct configTamc220
{
    epicsUInt16 card;        		/* Number of IP carrier board           */
    epicsUInt16 attr;        		/* attribute mask for configuring board */
    epicsUInt16 param;			/* parameter mask for configuring board */
    epicsUInt8 clear;			/* interrupt clear register */
    epicsUInt8 enable;			/* interrupt enable register */
    epicsUInt8 level;			/* interrupt level register */
    epicsUInt8 mem_enable;		/* memory enable register */
    epicsUInt8 ambasr;			/* IPA memory base addr & size register */
    epicsUInt8 bmbasr;			/* IPB memory base addr & size register */
    epicsUInt8 cmbasr;			/* IPC memory base addr & size register */
    epicsUInt8 dmbasr;			/* IPD memory base addr & size register */
    epicsUInt32 initialized;
    int uioDevFd;			/* UIO /dev/uioN file descriptor		*/
    int uioClassPathConfigFd;		/* UIO sysfs class path	config file descriptor (BAR0)	*/        
    int uioClassPathCtlStsFd;		/* UIO sysfs class path	ctl/status descriptor  (BAR2)	*/        
    int uioClassPathMMIOFd;		/* UIO sysfs class path	MMIO file descriptor   (BAR3)	*/        
    epicsThreadId tid;
    void *ioBase;
    void *ctlStsBase;
};

extern int	ipacAddTamc220(	const char	*	cardParams	);

#ifdef		__cplusplus
}
#endif	/*	__cplusplus	*/

#endif  /* INCdrvXyTamc220H */

