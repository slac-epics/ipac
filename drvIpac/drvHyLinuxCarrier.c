/*******************************************************************************

Project:
    Hytec Carrier Linux Driver for EPICS
	Current support for devices:
		IOC9010
		uTCA7002
		PCIe6335

File:
    drvHyLinuxCarrier.c

Description:
    Linux version of IPAC Carrier Driver for Hytec carrier devices 
	and Industry Pack cards. It glues the IPAC and IP hardware device, 
	driver support.

Author:
    Oringinal developer:   Jim Chen, HyTec Electronics Ltd,
    Reading, Berks., UK
    http://www.hytec-electronics.co.uk
    The code is based on drvHy8002 code which is also based on
	Andrew Johnson's <anjohnson@iee.org> drvTvme200.c.
Created:
    27/10/2010 first version.

Version:
    $Id: drvHyLinuxCarrier.c,v 1.1 2014/09/19 16:31:30 rdabney Exp $

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

Modifications:
	Date			By		Version		Comments
	===========================================================================
    27-Oct-2010  Jim Chen   	2.0		The initial creation
    22-Nov-2010  Jim Chen   	2.1		Added multiple carrier support
    26-Nov-2010  Jim Chen   	2.2		Mdified to support multiple interrupt 
										callbacks as per vectors
    01-Dec-2010  Jim Chen   	2.3		Fixed a bug that amends the IP slot 
										number from 4 to 6 in the carrier
										jumper table for the case of 9010
    14-Mar-2011  Jim Chen   	2.4		Modifed intConnect function to pass 
										vectors to the device driver file
										in order for the device driver to 
										dispatch interrupts to the proper
										IP card callback.
										Also added epicsAtExit callback to 
										tidy up things.
    19-Oct-2012  Jim Chen   	2.5		Added support for 7003 
*******************************************************************************/

/*ANSI standard*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>

/*EPICS specific*/
#include <devLib.h>
#include "epicsExport.h"
#include <epicsExit.h>
#include "iocsh.h"

/*Linux specific*/
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#if	9
#include <linux/fs.h>
#endif
#include <curses.h>
#include <pthread.h>
#include <errno.h>

#include <drvIpac.h>

/*Hytec specific*/
#include "9010LinuxDriver.h"

/* Hytec IDs */
#define HYTECID    		0x8003
#define CARR_MODEL_9010 0x9010
#define CARR_MODEL_7002 0x7002
#define CARR_MODEL_7003 0x7003
#define CARR_MODEL_6335 0x6335

/*Carrier registers. These are all offsets from the A16 base address*/
#define CARR_CSR  	 	0x00
#define CARR_CONFIG  	0x02
#define CARR_DISP_CONT	0x04
#define CARR_DISP_DATA	0x06
#define CARR_INTS_LO  	0x08
#define CARR_INTS_HI 	0x0A
#define CARR_MASK_LO  	0x0C
#define CARR_MASK_HI 	0x0E
#define CARR_CLOCK 		0x10
#define CARR_FAN12 		0x12
#define CARR_FAN34 		0x14
#define CARR_FAN56 		0x16
#define CARR_FAN_CTRL	0x18
#define CARR_TEMP		0x1A
#define CARR_CONFIG2	0x1C

/* define individual bits in the carrier board's CSR register*/
#define CSR_PMC   	0x0001
#define CSR_TIMO   	0x0002
#define CSR_FAN1  	0x0008
#define CSR_FAN2  	0x0010
#define CSR_FAN3  	0x0020
#define CSR_FAN4  	0x0044
#define CSR_FAN5  	0x0080
#define CSR_FAN6  	0x0100
#define CSR_TP15 	0x0200
#define CSR_TP16 	0x0400
#define CSR_FCON 	0x1000

#define CONF_UP    	0x0001
#define CONF_OK    	0x0002
#define CONF_DOWN  	0x0004
#define CONF_RESET 	0x0008
#define CONF_OBSW0 	0x0010
#define CONF_OBSW1 	0x0020
#define CONF_OBSW2 	0x0040
#define CONF_OBSW3 	0x0080

#define CSR_INTSELSHIFT 2
#define CSR_IPMEMSHIFT  7
#define CSROFF     ~(CSR_INTEN)

/* Characteristics of the card */
#define MAGICNUM 99                             /* the magic number representing when only has a single device */
#define NUMIPSLOTS 6                            /* number of IP slot */
#define IP_MEM_SIZE 0x0100                      /* the memory size reserved for an IP module (A16) */
#define CARRIER_MEM_SIZE 0x40                   /* the size of the memory to register for this carrier board.
                                                Don't make this too big or it will interfere with the
                                                memory space of the IP cards. */

/* Carrier maps all accessing to the IP Cards as Memory starting at Base Address 3 */
#define IP_A_MEMORY_BASE_ADDR	0x000000
#define IP_B_MEMORY_BASE_ADDR	0x200000
#define IP_C_MEMORY_BASE_ADDR	0x400000
#define IP_D_MEMORY_BASE_ADDR	0x600000
#define IP_E_MEMORY_BASE_ADDR	0x800000
#define IP_F_MEMORY_BASE_ADDR	0xA00000
#define IP_IO_BASE_ADDR			0xE00000


/* The IP I/O Address Space is sub-divided by the following offsets...*/
#define IP_A_IO_BASE_ADDR		0x000
#define IP_A_ID_BASE_ADDR		0x080
#define IP_B_IO_BASE_ADDR		0x100
#define IP_B_ID_BASE_ADDR		0x180
#define IP_C_IO_BASE_ADDR		0x200
#define IP_C_ID_BASE_ADDR		0x280
#define IP_D_IO_BASE_ADDR		0x300
#define IP_D_ID_BASE_ADDR		0x380
#define IP_E_IO_BASE_ADDR		0x400
#define IP_E_ID_BASE_ADDR		0x480
#define IP_F_IO_BASE_ADDR		0x500
#define IP_F_ID_BASE_ADDR		0x580
#define IP_A_INT_BASE_ADDR		0x800
#define IP_B_INT_BASE_ADDR		0x900
#define IP_C_INT_BASE_ADDR		0xA00
#define IP_D_INT_BASE_ADDR		0xB00
#define IP_E_INT_BASE_ADDR		0xC00
#define IP_F_INT_BASE_ADDR		0xD00


/* prototype define */
typedef unsigned short word;
typedef unsigned int longword;

/* private structure used to keep track of a carrier card.
 We also keep a list of all Hytec carriers (carlist).
 This leterally doesn't mean very much since IOC9010 
 is standalone blade hence it has only one carrier at
 any time. */
typedef struct privinfo{
	struct privinfo* next;
	word carrier;                                 	/* carrier number. less meaning */
	volatile int carrierBaseAddr;        			/* carrier register base address */
	volatile void *memoryBaseAddr;         			/* IP memory base address */
	volatile int plxBaseAddr;            			/* PLX chip base address */
	int model;                                    	/* carrier model, 9010/7002/6335 */
	int clock;                                    	/* carrier clock frequency */
	word ipintsel;                                	/* IP cards interrupt settings in CSR. This is only used for reporting purpose */
	int devHandler;
	int carrierType;
	int ipadresses[NUMIPSLOTS][IPAC_ADDR_SPACES]; 	/* address mapping */
	pthread_t thread;								/* interrupt thread */

	/* the following are mainly used for multi-carrier cards support */
	int carrierSlot;								/* carrier slot number */
	word carrierINTLevel;                         	/* carrier card interrupt level. only useful for VME system */
	struct irq_desc *irqdesc;						/* Interrupt descriptor */
	int intFlag;									/* interrupt thread created flag */  
}privinfo;

/* Structure of the IRQ. */
struct irq_desc {
        void   *driverp[255];						/* array of interrupt callback function argument */
        void    (*handler[255])(void *);			/* array of interrupt callback functions */
        int     irqfd;								/* device handler */
};

/************GLOBAL VARIABLES********************/
static privinfo *carlist = NULL;
/*static char* charid="drvHyLinuxCarrier";*/

/* interrupt thread */
static void *interrupt_thread(void *arg);

/* function prototype */
static int regaddr(privinfo* pv);
static int scanparm(char* cp, int* carrierslot, int* intlevel, int* ipclcka, int* ipclckb, int* ipclckc, int* ipclckd, int* ipclcke, int* ipclckf);
long IOC9010CarrierWrite( void *cPrivate, ushort_t add, ushort_t data);
long IOC9010CarrierRead( void *cPrivate, ushort_t add, ushort_t *data );
int probe( void *cPrivate );
static void Exit(void *cPrivate);

/*******************************************************************************

Routine:
    initialise

Purpose:
    Registers a new Hytec carrier card such as 9010, 7002 or 6335 with carrier  
	ID number. 

Description:
    Reads the parameter string from cp and set up the carrier registers for initialisation.

Parameters:
    *cp: The parameter string please refer to ipacAddHyLinux9010 routine.   
    **cPrivate: private info structure
    carrier: the latest added carrier number

Returns:
    OK(0): if successful 
    Error code otherwise.

*/
static int initialise(const char *cp, void **cPrivate, epicsUInt16 carrier)
{
    int	res,carrierslot=0,intlevel=0,ipclckA,ipclckB,ipclckC,ipclckD,ipclckE,ipclckF;
    privinfo *pv;
    int	clk;
    IOCTL_BUF ioctl_buf;
    unsigned long length=0xE00E00;
    unsigned long startAddr;
    char devname[32];
    char* defaultname="/dev/IOC9010";
    
    /*begin*/

    ipclckA=ipclckB=ipclckC=ipclckD=ipclckE=ipclckF=0;				//zero clock settings to make them default to 8MHz
    devname[0] = 0;

    res=scanparm(cp, &carrierslot, &intlevel, &ipclckA, &ipclckB, &ipclckC, &ipclckD, &ipclckE, &ipclckF);	//get parameters
    if (res != OK) 
        return res;

    if(intlevel < 0 || intlevel > 7) 
        return S_IPAC_badIntLevel;

    pv=(privinfo*)calloc(1,sizeof(privinfo));					//allocate memory for private structure
    if(pv==NULL) 
        return S_IPAC_noMemory;
	
    if(carrierslot != MAGICNUM)
        sprintf(devname, "%s%d", defaultname, carrierslot);		//copy default device name if not passed by parameters
    else
        strcpy(devname, defaultname);							//set device to default

    puts(devname);
    pv->devHandler = open(devname, O_RDWR);		//open device driver
    if (pv->devHandler < 0) 
    {
        printf("open %d\n", pv->devHandler);
        return S_IPAC_badDriver;
    }

    pv->carrierType=0;

    /* In this IOC9010, the PCI to IP bridge address is 01:04.0 */
    ioctl(pv->devHandler, OP_BASE_ADD3, &ioctl_buf);				//get IOCMemoryBaseAddr
    startAddr = ioctl_buf.lAddress;

    pv->memoryBaseAddr = mmap(0, length, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED, pv->devHandler, startAddr);	//map memory to usr space
    if (pv->memoryBaseAddr < 0) 
        return S_IPAC_noMemory;

    ioctl(pv->devHandler, OP_BASE_ADD2, &ioctl_buf);				//get carrier base address
    pv->carrierBaseAddr = ioctl_buf.lAddress;

    ioctl(pv->devHandler, OP_BASE_ADD1, &ioctl_buf);				//get PLX chip base address
    pv->plxBaseAddr = ioctl_buf.lAddress;

    epicsAtExit((void*)&Exit, (void*)pv);

    /*determine the CSR*/
    clk=ipclckA | (ipclckB << 1) | (ipclckC << 2)
		| (ipclckD << 3) | (ipclckE << 4) | (ipclckF << 5);

    probe(pv);														//probe carrier type

    res = IOC9010CarrierWrite(pv, REG_IP_CLOCK, clk);				//set up IP clock. Either 8MHz or 32MHz
    if (res!=OK) 
        return res;

#if	0
    pv->next=carlist;carlist=pv;									//logistic storing
#else
    pv->next=NULL;									//logistic storing
#endif
    pv->carrierSlot=carrierslot;
	pv->carrierINTLevel=intlevel;									//this is for VME system only. ignored by other Hytec carriers
    pv->carrier=carrier;
    pv->clock=clk;

    pv->irqdesc = NULL;												//clear interrupt descriptor at the beginning. Created when first intConnect is called.
    pv->intFlag = 0;

   /*register the IP memory space for this card*/
    res=regaddr(pv);												//sort out IP memory addresses
    if (res != OK) 
        return res;
    
    *cPrivate=(void*)pv;
    return OK;
}


/*******************************************************************************

Routine:
    Exit

Purpose:
    Tidy up files opened for device

Description:
    This routine tidies up the open files for a device when ioc quit.

Returns:
    none

*/
static void Exit(void *cPrivate)
{
    privinfo *cp = (privinfo *)cPrivate;
	
	if(cp->devHandler)
		close(cp->devHandler);
}

/*******************************************************************************

Routine:
    report

Purpose:
    Returns a status string for the requested slot

Description:
    This routine reports the interrupt level of the carrier card and
    the specified IP card interrupt setting.

Returns:
    A static string containing the slot's status.

*/
static char* report(void *cPrivate, ushort_t slot){
  /* Return string with giving status of this slot
     static char* report(ushort_t carrier, ushort_t slot){*/
    /*begin*/
    privinfo *cp = (privinfo *)cPrivate;
    static char output[IPAC_REPORT_LEN];
    sprintf(output, "Slot %d, INT0: %s, INT1: %s", 
	    slot,
        (cp->ipintsel & (1 << (slot*2))) ? "active" : "",
	    (cp->ipintsel & (1 << (slot*2+1))) ? "active" : "");
    return output;
}


/*******************************************************************************

Routine:
    baseAddr

Purpose:
    Returns the base address for the requested slot & address space

Description:
    Because we did all that hard work in the initialise routine, this 
    routine only has to do a table lookup in the private settings array.
    Note that no parameter checking is required - the IPAC driver which 
    calls this routine handles that.

Returns:
    The requested address, or NULL if the module has no memory.

*/

static void* baseAddr(void *cPrivate,
		      ushort_t slot,
		      ipac_addr_t space)
{
    privinfo* pv=(privinfo*)cPrivate;
    return (void*)pv->ipadresses[slot][space];
}

/*******************************************************************************

Routine:
    irqCmd

Purpose:
    Handles interrupter commands and status requests

Description:
    For Linux PCI/PCIe devices, the interrupt level is allocated by the BIOS.
    The only commands supported include enable/disable individual IP card 
	interrupt. All others are not supported at the moment.

Returns:
    ipac_irqEnable returns 0 = OK,
    ipac_irqDisable returns 0 = OK,
    other calls return S_IPAC_notImplemented.

*/

static int irqCmd(void *cPrivate, ushort_t slot,
		  ushort_t irqnum, ipac_irqCmd_t cmd)
{
    int retval=OK;
    privinfo* cp=(privinfo*)cPrivate;
	IOCTL_VME_BUF	ioctl_vme_buf;

    ioctl_vme_buf.lLength  = (unsigned long) cp->carrierINTLevel;				// interrupt level. This is only needed for 8002 VME arch
    ioctl_vme_buf.lSlot = cp->carrierSlot;
    ioctl_vme_buf.lSite = slot;

    /*begin*/
    /*irqnumber is 0 or 1.*/
    if (irqnum !=0 && irqnum!=1)return S_IPAC_badIntLevel;

    /*is the IP card valid*/
    if (slot>5) return S_IPAC_badAddress;
  
    switch(cmd)
    {
        /*We don't allow the IP driver to set the carrier's int level.
        It's set for the carrier in the init string*/
        case ipac_irqLevel0:
        case ipac_irqLevel1:
        case ipac_irqLevel2:
        case ipac_irqLevel3:
        case ipac_irqLevel4:
        case ipac_irqLevel5:
        case ipac_irqLevel6:/* Highest priority */
        case ipac_irqLevel7:/* Non-maskable, don't use */
			return S_IPAC_notImplemented;
            break;
        case ipac_irqGetLevel:
            /* Returns level set */
            retval=cp->carrierINTLevel; 
            break;
        case ipac_irqEnable:
            /* Required to use interrupts */
			if (irqnum==0)															//set the IP card interrupt in carrier CSR
				cp->ipintsel|=(1<<(slot*2));
			else
				cp->ipintsel|=(1<<(slot*2+1));
			retval = ioctl(cp->devHandler, OP_ENABLE_INTERRUPT, &ioctl_vme_buf);	//this guy does the real thing
			if(retval==1) retval=OK;
			if((cp->carrierType == 0x5331) || (cp->carrierType == 0x5332))
			{
				;		//need a bit extra
			}
            break;
        case ipac_irqDisable:
            /* Not necessarily supported */
            if (irqnum==0)															//clear interrupt settings in the carrier CSR
                cp->ipintsel&=(~(1<<(slot*2)));
            else
                cp->ipintsel&=(~(1<<(slot*2+1)));
			retval = ioctl(cp->devHandler, OP_DISABLE_INTERRUPT, &ioctl_vme_buf);	//this guy does the real thing
			if(retval==1) retval=OK;
            break;
        case ipac_irqPoll:
			/* need a bit more work */
			return S_IPAC_notImplemented;
            break;
        case ipac_irqSetEdge: /* Sets edge-triggered interrupts */
        case ipac_irqSetLevel:/* Sets level-triggered (default) */
        case ipac_irqClear:   /* Only needed if using edge-triggered */
			return S_IPAC_notImplemented;
            break;
        default:
            break;
    }/*switch*/

    return retval;
}

/*******************************************************************************

Routine:
    intConnect

Purpose:
    Associate interrupt handlers to vectors

Description:
    Each carrier private structure holds an irq structure that holds an callback
    function array and the parameter array. When the first IP card calls this, it
	creates this irq structure and creates the irq thread and registers the 
	callback and parameter pointers in the arrays. The subsequent IP cards will 
	only register their callbacks and parameters.

Returns:
    0 = OK
    <0 error

*/
static int intConnect(void *cPrivate, epicsUInt16 slot, epicsUInt16 vecNum,
		void (*routine)(int parameter), int parameter)
{
    pthread_attr_t attr;
    struct sched_param schp;
	IOCTL_BUF	ioctl_buf;

    ioctl_buf.sData  = (unsigned long) vecNum;						// interrupt vector

    /*begin*/
    privinfo* cp=(privinfo*)cPrivate;

	if(cp->irqdesc == NULL)											//first time called
 	{
		cp->irqdesc = (struct irq_desc*) malloc(sizeof(struct irq_desc));
    	if(cp->irqdesc==NULL) return S_IPAC_noMemory;

		memset(cp->irqdesc, 0, sizeof(struct irq_desc));
	    cp->irqdesc->irqfd = cp->devHandler;
	}

	/* save callback function and its parameter pointer as per the vector number */
    cp->irqdesc->driverp[vecNum] = (void*)parameter;
    cp->irqdesc->handler[vecNum] = (void*)routine;

	/* register vector to the device driver */	
	ioctl(cp->devHandler, OP_REGISTER_VECTOR, &ioctl_buf);			

	/* only need to create the interrupt thread once, at the first time */
	if(!cp->intFlag)
	{
		cp->intFlag = 1;											//mark thread has been created 
	    memset(&schp, 0, sizeof(schp));
    	schp.sched_priority = sched_get_priority_max(SCHED_FIFO);

    	pthread_attr_init(&attr);
    	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    	pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    	pthread_attr_setschedparam(&attr, &schp);
    	pthread_create(&cp->thread, &attr, interrupt_thread, cp);
	}

    return OK;
}


/*******************************************************************************

Routine:
    interrupt_thread

Purpose:
    To create an thread that listens to the block read function of the
	device driver for interrupts.

Description:
    The Hytec carrier devices have implemented the interrupt by block read function 
	in the device driver. This thread will listen to that read for any interrupt
	to come. Once there is one, it calls the correct callback function as per the
	vector number.

Returns:
    None. Foever loop doesn't return.

*/
static void *interrupt_thread(void *arg)
{
    privinfo* cp=(privinfo*)arg;
    int vec, err;
    int fd = cp->irqdesc->irqfd;

    printf("\nThread is running.\n\n");
    for (;;) 
    {
        err = read(fd, &vec, sizeof(vec));
        if (err < 0)
        {
            printf("Error la...%x\n", errno);
            switch(errno)
            {
                case EINTR:
                    printf("EINTR...%x\n", errno);
                    return 0;
                default:
                    printf("Interrupt device read returned EINVAL %x\n", errno);
                    /* NOTREACHED */
                    return 0;
                case EBUSY:
                {
                    printf("busy...%x\n", errno);
                    return 0;
                    continue;
                }
            }
        }else
        {
	    vec &= 0xFF;
/*printf("===vector=%x\n", vec);*/
            if((cp->irqdesc->handler[vec] != 0) && (cp->irqdesc->driverp[vec] != 0))
            	(void)(cp->irqdesc->handler[vec])(cp->irqdesc->driverp[vec]);
        }
    }
    /* NOTREACHED */
    return NULL;
}


/*THIS IS THE CARRIER JUMPTABLE */
ipac_carrier_t HyLinux9010={
    "Hytec IOCLinux9010",
    6,
    initialise,
    report,
    baseAddr,
    irqCmd,
    intConnect
};


/*******************************************************************************

Routine:
    ipacAddHyLinux9010

Purpose:
    shell command to be used in start up script

Description:
    The carrier board can be registered by using this function during 
    the system start up.

Parameters:
    The parameter "cardParams" is a string that should comprise at least
	two numbers seperated by comma plus optional 6 name/value pairs
	that are also seperated by commas without spaces. It has the following 
	format:

	ipacAddHyLinux9010(s,i,IPCLCKA=8,IPCLCKB=8,IPCLCKC=8,IPCLCKD=32,IPCLCKE=32,IPCLCKF=32)

	Where:
		s - The ID number of the carrier card when there are multiple 
			carriers such as PCIe or Micro-TCA chassis. This ID number is
			set by the on board jumpers. The number should be 0 ~ 31. When
			it is set to 99, it means it is a single carrier system (only
			one carrier such as IOC9010 or HY5331/5332).

		i - Interrupt level. It is not used unless it is VME system.
			For any other systems, it doesn't care. But should be within 0 ~ 7.

    	IPCLCKA ~ IPCLCKF - Defines IP slot (A ~ F) frequencies. The number 
			of each para can only be either 8 or 32 that represent 8MHz 
			or 32MHz respectively. These name/value pairs are optional. If 
			a slot doesn't have the correspondent name/value pair, it
			defaults to 8MHz.

    Examples:
        IPAC1 = ipacAddHyLinux9010("99,3,IPCLCKA=8,IPCLCKB=32,IPCLCF=8") 
            
        This configures a single carrier system with IP slot A,C,D,E,F 
		(if the system has) all set to 8MHz, and slot B set to 32MHz.

        IPAC1 = ipacAddHyLinux9010("2,3,IPCLCKA=32,IPCLCKB=32,IPCLCC=32") 
            
        This configures a multiple carrier system with carrier ID=2,
		IP slot A,B,C are set to 32MHz, and slot D,E,F set to 8MHz.

Returns:
    >=0 and < IPAC_MAX_CARRIERS (21): newly added carrier number
    > M_ipac(600 << 16): error code

*/
int ipacAddHyLinux9010(const char *cardParams) {
    int rt;
    rt = ipacAddCarrier(&HyLinux9010, cardParams);                  /* add IOC9010 Linux carrier */
   	return rt;                                                  	/* Otherwise return error code  */
}

static const iocshArg HyLinux9010Arg0 = { "cardParams",iocshArgString};
static const iocshArg * const HyLinux9010Args[1] = {&HyLinux9010Arg0};
static const iocshFuncDef HyLinux9010FuncDef = {"ipacAddHyLinux9010", 1, HyLinux9010Args};
static void HyLinux9010CallFunc(const iocshArgBuf *args) {
    ipacAddHyLinux9010(args[0].sval);
}


static void epicsShareAPI HyLinux9010Registrar(void) {
    iocshRegister(&HyLinux9010FuncDef, HyLinux9010CallFunc);
}

epicsExportRegistrar(HyLinux9010Registrar);


/***** Private function part *********/

/*******************************************************************************

Routine: internal function
    scanparm

Purpose:
    parsing parameters

Description:
    This function parses the parameter passed by ipacAddHyLinux9010 routine  
    to get the vme slot number, interrupt level, IP memory size, IP clcok
    setting, interrupt release type and memory offset for base address etc.

Parameters:
    Please refer to ipacAddHyLinux9010 routine.

Return:
    OK(0): if successful
    Error code otherwise.

*/
static int scanparm(char* cp,
		    int* carrierslot,
		    int* intlevel,
		    int* ipclcka,
		    int* ipclckb,
		    int* ipclckc,
		    int* ipclckd,
		    int* ipclcke,
		    int* ipclckf)
{
    int ipc=0;
    char *pstart;
    int crslot=0, itr=0;
    int skip=0;
    int count;

    /*set defaults: 8MHz clock*/
    *ipclcka=0;
    *ipclckb=0;
    *ipclckc=0;
    *ipclckd=0;
    *ipclcke=0;
    *ipclckf=0;

    if (cp == NULL || strlen(cp) == 0) {
        return OK;             
    }

    count = sscanf(cp, "%d,%d,%n", &crslot, &itr, &skip);
    if (count != 2){   
        printf("********Number error. %s  num:%d\n",cp, count);
        return S_IPAC_badAddress;     
    }

    /*vme slot number parsing*/
    if ((crslot<0 || crslot>21) && (crslot != MAGICNUM))
    {
        printf("********Slot error.\n");
        return S_IPAC_badAddress;
    }
    else
        *carrierslot = crslot;

    /*Interrupt level parsing*/
    if (itr<0 || itr>7)
        return S_IPAC_badAddress;
    else
        *intlevel = itr;

    cp += skip;

    /*parsing IP clock frequency*/
    if((pstart=strstr(cp, "IPCLCKA=")) != NULL){
        if((1 != sscanf(pstart+8, "%d", &ipc)) || (ipc !=8 && ipc !=32)){
            return S_IPAC_badAddress;
        }
        *ipclcka = ((ipc == 8) ? 0 : 1);    
    }
            
    /*parsing IP clock frequency*/
    if((pstart=strstr(cp, "IPCLCKB=")) != NULL){
        if((1 != sscanf(pstart+8, "%d", &ipc)) || (ipc !=8 && ipc !=32)){
            return S_IPAC_badAddress;
        }
        *ipclckb = ((ipc == 8) ? 0 : 1);    
    }
            
    /*parsing IP clock frequency*/
    if((pstart=strstr(cp, "IPCLCKC=")) != NULL){
        if((1 != sscanf(pstart+8, "%d", &ipc)) || (ipc !=8 && ipc !=32)){
            return S_IPAC_badAddress;
        }
        *ipclckc = ((ipc == 8) ? 0 : 1);    
    }
            
    /*parsing IP clock frequency*/
    if((pstart=strstr(cp, "IPCLCKD=")) != NULL){
        if((1 != sscanf(pstart+8, "%d", &ipc)) || (ipc !=8 && ipc !=32)){
            return S_IPAC_badAddress;
        }
        *ipclckd = ((ipc == 8) ? 0 : 1);    
    }
            
    /*parsing IP clock frequency*/
    if((pstart=strstr(cp, "IPCLCKE=")) != NULL){
        if((1 != sscanf(pstart+8, "%d", &ipc)) || (ipc !=8 && ipc !=32)){
            return S_IPAC_badAddress;
        }
        *ipclcke = ((ipc == 8) ? 0 : 1);    
    }
            
    /*parsing IP clock frequency*/
    if((pstart=strstr(cp, "IPCLCKF=")) != NULL){
        if((1 != sscanf(pstart+8, "%d", &ipc)) || (ipc !=8 && ipc !=32)){
            return S_IPAC_badAddress;
        }
        *ipclckf = ((ipc == 8) ? 0 : 1);    
    }
            
    return OK;
}


/*******************************************************************************

Routine: internal function
    regaddr

Purpose:
    register base address

Description:
    It registers the IP carrier card memory..

Parameters:
    *pv: private structure

Return:
    OK(0): if successful. 
    Error code otherwise.

*/
static int regaddr(privinfo* pv){

    volatile int addr;
    int* ipadr=(int*)(pv->ipadresses);
    int ip,ia;

    /*Clear all address variables*/
    for (ip=0;ip<NUMIPSLOTS;ip++)
        for (ia=0;ia<IPAC_ADDR_SPACES;ia++)*ipadr++ =0;
          
    /* init the ipac_addrIO and ipac_addrID spaces*/
    for(ip=0;ip<NUMIPSLOTS;ip++){
		addr = (int) pv->memoryBaseAddr + IP_IO_BASE_ADDR + IP_A_IO_BASE_ADDR + ((IP_B_ID_BASE_ADDR - IP_A_ID_BASE_ADDR) * ip);
        pv->ipadresses[ip][ipac_addrIO]=addr;
        pv->ipadresses[ip][ipac_addrID]=addr+0x80;
        addr = (int) pv->memoryBaseAddr + (IP_B_MEMORY_BASE_ADDR - IP_A_MEMORY_BASE_ADDR) * ip;
        pv->ipadresses[ip][ipac_addrMem]=addr;
    }

    return OK;
}


/*----------------------------------------------------------------------------------------------*/
/*Function:		Probe()																			*/
/*----------------------------------------------------------------------------------------------*/
/*Description:	Probe the hardware to determine either it is IOC9010 or 5331/5332/6335/7002		*/
/*Prototype:	int probe( void *cPrivate )														*/
/*				cPrivate  = pointer to private structure										*/
/*Return :		=9010, IOC9010																	*/
/*				=5331, 5331																		*/
/*				=5332, 5332																		*/
/*				=6335, PCIe 6335																*/
/*				=7002, uTCA 7002																*/
/*				=-1, driver hasn't been initialised												*/
/*				=-2, hardware not recognised													*/ 
/*----------------------------------------------------------------------------------------------*/
int probe( void *cPrivate )
{
    privinfo* cp=(privinfo*)cPrivate;

	// we only need to probe once
	if (cp->carrierType == 0)
    { 
        if (cp->devHandler != 0)
        {
           IOCTL_CONFIG	ioctl_config;
	       ioctl(cp->devHandler, OP_GET_CONFIG, &ioctl_config);
	       cp->carrierType = ioctl_config.sHardwareID;
        }else
           return S_IPAC_badDriver;
    }

	return cp->carrierType;                         // if already probed, return the type straight away
}


/*----------------------------------------------------------------------------------------------*/
/*Function:		Read Carrier Board Single Register												*/
/*----------------------------------------------------------------------------------------------*/
/*Description:	Executes a single action of 16 bit read from carrier card						*/
/*				registers.																		*/
/*Prototype:	long IOC9010CarrierRead( void *cPrivate, ushort_t add, ushort_t *data)				*/
/*				cPrivate  = pointer to private structure										*/
/*				add  = register address															*/
/*				*data  = pointer to hold the register value										*/
/*Return:		error code and also in m_ErrorStatus											*/
/*				if successful, return 0															*/
/*----------------------------------------------------------------------------------------------*/
long IOC9010CarrierRead( void *cPrivate, ushort_t add, ushort_t *data )
{
	IOCTL_BUF		ioctl_buf;
	unsigned short  local_data;
    privinfo* cp=(privinfo*)cPrivate;
	long status = OK;
	int carrier;

	if(cp->devHandler < 0) 
	{
		status = S_IPAC_badDriver;
		return status;							//device driver fault
	}

	carrier = probe(cPrivate);
	if ((carrier != 0x9010) && (carrier != 0x7002) && (carrier != 0x7003) &&(carrier != 0x6335))
	{
		status = S_IPAC_badModule;
		return status;							//wrong device
	}

	if (add > REG_CONFIG_2)
	{
		status = S_IPAC_badAddress;
		return status;							//bad address offset
	}

    ioctl_buf.lAddress = add;

	ioctl_buf.lLength  = 1;
	ioctl_buf.sData  = (unsigned long)(&local_data);
    ioctl(cp->devHandler, OP_CARRIER_READ_BLOCK, &ioctl_buf);
	
	*data = (ushort_t) local_data;
	return status;
}


/*----------------------------------------------------------------------------------------------*/
/*Function:		Write to single carrier board register											*/
/*----------------------------------------------------------------------------------------------*/
/*Description:	Executes a single action of 16 bit write to carrier card						*/
/*				registers.																		*/
/*Prototype:	long IOC9010CarrierWrite( void *cPrivate, ushort_t add, ushort_t data)				*/
/*				cPrivate  = pointer to private structure										*/
/*				data = data to write															*/
/*				add  = register address															*/
/*Return:		error code and also in m_ErrorStatus											*/
/*				if successful, return 0															*/
/*----------------------------------------------------------------------------------------------*/
long IOC9010CarrierWrite( void *cPrivate, ushort_t add, ushort_t data)
{


	IOCTL_BUF		ioctl_buf;
	unsigned short  local_data;
    privinfo* cp=(privinfo*)cPrivate;
	long status = OK;
	int carrier;

	if(cp->devHandler < 0) 
	{
		status = S_IPAC_badDriver;
		return status;							//device driver fault
	}

	carrier = probe(cPrivate);
	if ((carrier != 0x9010) && (carrier != 0x7002) && (carrier != 0x7003) &&(carrier != 0x6335))
	{
		status = S_IPAC_badModule;
		return status;							//wrong device
	}

	if (add > REG_CONFIG_2)
	{
		status = S_IPAC_badAddress;
		return status;							//bad address offset
	}

    ioctl_buf.lAddress = add;
	local_data = data;
	ioctl_buf.lLength  = 1;
	ioctl_buf.sData  = (unsigned long)(&local_data);
    ioctl(cp->devHandler, OP_CARRIER_WRITE_BLOCK, &ioctl_buf);
	
	return status;

}
