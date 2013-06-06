/*******************************************************************************

Project:
    LCLS 

File:
    drvApcie8650.c

Description:
    EPICS Driver for the Acromag Apcie8650 IPAC carrier 

Author:
    Richard Dabney <rdabney@slac.stanford.edu>

Created:
      12th February 2013

*******************************************************************************/

#ifdef NO_EPICS
#include <vxWorks.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#ifdef linux
#include <sys/mman.h>
#endif
#include "apce8650.h"
#ifdef NO_EPICS
#include <vme.h>
#include <sysLib.h>
#else
#include "devLib.h"
#include "drvSup.h"
#endif
#include "epicsThread.h"
#include "drvIpac.h"
#include "drvApcie8650.h"
#include "xipIo.h"
#ifndef NO_EPICS
#include "epicsExport.h"
#include "iocsh.h"
#endif


/* Only used within this file */
LOCAL char *strdupn( const char *ct, size_t n );

/* Characteristics of the card */

#define SLOTS		4
#define	IO_SPACES	2	

/* Offsets from base address in VME A16 */
#define REGS_A		0x0180
#define PROM_A		0x0040
#define REGS_B		0x0200
#define PROM_B		0x0080
#define REGS_C		0x0280
#define PROM_C		0x00C0
#define REGS_D		0x0300
#define PROM_D		0x0100
#define REGS_SIZE	0x0400
    
/* VME Interrupt levels */

#define IRQ_LEVEL 0x6

typedef void * private_t[IO_SPACES][SLOTS];

/* Carrier Private structure type, one instance per board */


struct privateApcie8650
{
    int *pciConfigBase;
    private_t *memSpaces;
    struct configApcie8650 *pconfig;
};

typedef struct _carrierIsr
{
     unsigned short carrier;
     struct _slots {
       int (*ISR)(int param);
       int param;
     } slots[4];
} CARRIERISR;

static CARRIERISR carrierISR;


/*******************************************************************************

Routine:
    initialise

Purpose:
    Creates new private table for XYCOM Apcie8650 at addresses given by cardParams

Description:
    Checks the parameter string for the address of the card I/O space and 
    optional size of the memory space for the modules.  If both the I/O and
    memory base addresses can be reached from the CPU, a private table is 
    created for this board.  The private table is a 2-D array of pointers 
    to the base addresses of the various accessible parts of the IP module.

Parameters:
    The parameter string should comprise a hex number (the 0x or 0X at the 
    start is optional) optionally followed by a comma and a decimal integer.  
    The first number is the I/O base address of the card in the VME A16 
    address space (the factory default is 0x0000).  If present the second 
    number gives the memory space in Kbytes allocated to each IP module.  

Examples:
    "0x6000" 
	This indicates that the carrier board has its I/O base set to 
	0x6000, and none of the slots provide memory space.
    "1000,128"
	Here the I/O base is set to 0x1000, and there is 128Kbytes of 
	memory on each module, with the IP module A memory at 0x100000,
	module B at 0x120000, module C at 0x140000 and D at 0x160000.
    "7000,1024"
	The I/O base is at 0x7000, and hence the carrier memory base is 
	0x700000.  However because the memory size is set to 1024 Kbytes, 
	modules A, B and C cannot be selected (1024 K = 0x100000, so they 
	are decoded at 0x400000, 0x500000 and 0x600000 but can't be accessed
	because these are below the base address).

Returns:
    0 = OK, 
    S_IPAC_badAddress = Parameter string error, or address not reachable

*/

LOCAL int initialise( const char *cardParams, void **pprivate, unsigned short carrier )
{
    unsigned short space, slot;
    int handle;
    int card = 0;
    unsigned long ioBase;
    unsigned char command;
    int status;
    int uioDevFd, err;
    int uioClassPathMMIOFd;
    int uioClassPathConfigFd;
    char uioDevName[33]; 
    char uioClassPath[64];
    char *tp;
    void *p;
    epicsThreadId ipApcie8650WaitForInts(int);

    struct privateApcie8650 *pApcie8650;
    struct configApcie8650  *pconfig;
    private_t *private;
    static const int offset[IO_SPACES][SLOTS] = 
    {
        { PROM_A, PROM_B, PROM_C, PROM_D },
        { REGS_A, REGS_B, REGS_C, REGS_D }
    };

    sprintf(uioDevName, "%s%d", UIODEVNAME, carrier);

    uioDevFd = open(uioDevName, O_RDWR);
    if (uioDevFd < 0) 
    {
        perror("uio open:");
        return errno;
    }

/* get a FD to the config space */

    if ((tp = strdup(UIOCLASSPATH_CONFIG)) == NULL)
        return (S_IPAC_noMemory);
    sprintf(uioClassPath, tp, carrier);
    free(tp);

    uioClassPathConfigFd = open(uioClassPath, O_RDWR);
    if (uioClassPathConfigFd < 0) 
    {
	perror("config open:");
        return errno;
    }
    
/* get a FD to the MMIO space */

    if ((tp = strdup(UIOCLASSPATH_MMIO)) == NULL)
        return (S_IPAC_noMemory);
    sprintf(uioClassPath, tp, carrier);
    free(tp);

    uioClassPathMMIOFd = open(uioClassPath, O_RDWR);
    if (uioClassPathMMIOFd < 0) 
    {
	perror("mmap open:");
        return errno;
    }

    if (sscanf(cardParams, "%d", &card) != 1)
        return -1;

    if ((pApcie8650 = malloc(sizeof(struct privateApcie8650))) == NULL)
        return (S_IPAC_noMemory);

   
#ifndef	linux	
#ifdef NO_EPICS
    if (sysBusToLocalAdrs(VME_AM_SUP_SHORT_IO, (char *) ioBase, (char **) &ioBase)
#else
    if (devRegisterAddress("APCIE8650Ipac", atVMEA16, ioBase, REGS_SIZE, (void*)&ioBase))
#endif
#else /* linux */
    ioBase = (unsigned long) mmap(NULL, APC8650_IO_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, uioClassPathMMIOFd, 0);
    if (ioBase == -1)
#endif /* linux */
        return S_IPAC_badAddress;

    private = malloc(sizeof (private_t));

    for( space = 0; space < IO_SPACES; space++ )
    {
        for( slot = 0; slot < SLOTS; slot++ )
            (*private)[space][slot] = (void *) (ioBase + offset[space][slot]);
    }

    pApcie8650->memSpaces = private;

    /* what is this? */
    *(unsigned char*) ioBase |= 0x0004;

    /* Setup parameters for Apcie8650 configuration */
    if ((pconfig = malloc(sizeof(struct configApcie8650))) == NULL)
        return (S_IPAC_noMemory);

    pconfig->initialized = FALSE;
    pconfig->card = card; 
    pconfig->initialized = TRUE;

    pconfig->uioDevFd = uioDevFd;
    pconfig->uioClassPathConfigFd = uioClassPathConfigFd;
    pconfig->uioClassPathMMIOFd = uioClassPathMMIOFd;
    pconfig->ioBase = ioBase;

    pApcie8650->pconfig = pconfig;
    *pprivate      = pApcie8650;

    pconfig->tid = epicsThreadCreate("ipApcie8650WaitForInts", 65, epicsThreadGetStackSize(epicsThreadStackMedium),
                     ipApcie8650WaitForInts, pconfig);
    if (pconfig->tid == NULL)
        return(!OK);

    
    return OK;
}


/*******************************************************************************

Routine:
    baseAddr

Purpose:
    Returns the base address for the requested slot & address space

Description:
    Because we did all that hard work in the initialise routine, this 
    routine only has to do a table lookup in the private array.
    Note that no parameter checking is required - the IPAC driver which 
    calls this routine handles that.

Returns:
    The requested address, or NULL if the module has no memory.

*/

LOCAL void *baseAddr( void *private, unsigned short slot, ipac_addr_t space )
{
  struct privateApcie8650 *p;

  p = (struct privateApcie8650 *)private;
  return( (*p->memSpaces)[space][slot] );
}


/*******************************************************************************

Routine:
    irqCmd

Purpose:
    Handles interrupter commands and status requests

Description:
    The Apcie8650 only supports one interrupt level for all
    attached IP modules. The only commands supported are
    a request of the interrupt level, enable interrupt for a module,
    disable interrupt for a module/slot,
    and clear and enable interrupt for a module/slot. 

Returns:
    ipac_irqLevel0  returns 0 = OK, disables global interrupts.
    ipac_irqLevel6  returns 0 = OK, enables global interrupts.
    ipac_irqGetLevel returns the interrupt level,
    ipac_irqEnable  returns 0 = OK,
    ipac_irqClear   returns 0 = OK,
    ipac_irqDisable returns 0 = OK,
    other calls return S_IPAC_notImplemented.

*/

LOCAL int irqCmd( void *private, unsigned short slot, unsigned short irqNumber, ipac_irqCmd_t cmd )
{
    struct privateApcie8650 *p;
    struct mapApcie8650     *carrier;
    p = (struct privateApcie8650 *)private;
    struct configApcie8650 *pconfig = p->pconfig;
    volatile unsigned long ioBase = pconfig->ioBase;
    
    switch (cmd) {
	case ipac_irqLevel0:
	    return OK;

	case ipac_irqLevel6:
	    return OK;

	case ipac_irqGetLevel:
	    return IRQ_LEVEL;

	case ipac_irqClear: /* no 'clear' on APCI8650 so just enable */
        case ipac_irqEnable:
            switch(slot)
            {
              case 0:
              case 1:
              case 2:
              case 3:
                  *((char *) ioBase) |= 0x04;
                  break;
             }
                
#ifndef linux
#ifdef NO_EPICS
            sysIntEnable(IRQ_LEVEL);
#else
            devEnableInterruptLevelVME(IRQ_LEVEL);
#endif
#endif
            return OK;

	case ipac_irqDisable:
            /* Disable interrupts by writing 0 to bit of Interrupt Enable  */
            /* Register which controls interrupts for this slot            */
            /* Boards in other slots will continue to interrupt            */
            switch( slot )
            {
              case 0:
              case 1:
              case 2:
              case 3:
                  *((char *) ioBase) &= ~0x04;
                  break;
                
              default:
                break;
            }
	    return OK;

	default:
	    return S_IPAC_notImplemented;
    }
}


LOCAL char *report(struct privateApcie8650 *pprivate, unsigned short slot)
{
    struct configApcie8650  *pconfig = pprivate->pconfig;
    volatile ipac_idProm_t  *ipmid = baseAddr(pprivate, slot, ipac_addrID);
    int i, x; 
    int bc = 0;
    static char buf[1204];
    int card;

    card = pconfig->card;
    if (ipmCheck(card, slot) != S_IPAC_noModule)
    {
        bc += sprintf(buf+bc, "\n");
        bc += sprintf(buf+bc, "Identification:\t\t%c%c%c%c\n", ipmid->asciiI, ipmid->asciiP, ipmid->asciiA, ipmid->asciiC);
        bc += sprintf(buf+bc, "Manufacturers ID:\t%x\n", ipmid->manufacturerId & 0xff);
        bc += sprintf(buf+bc, "Model ID:\t\t%x\n", ipmid->modelId & 0xff);
        bc += sprintf(buf+bc, "Revision:\t\t%x\n", ipmid->revision & 0xff);
        bc += sprintf(buf+bc, "Reserved:\t\t%x\n", ipmid->reserved & 0xff);
        bc += sprintf(buf+bc, "Driver ID Low:\t\t%x\n", ipmid->driverIdLow & 0xff);
        bc += sprintf(buf+bc, "Driver ID High\t\t%x\n", ipmid->driverIdHigh & 0xff);
        bc += sprintf(buf+bc, "ID PROM length:\t\t%x\n", ipmid->bytesUsed & 0xff);
        bc += sprintf(buf+bc, "ID PROM CRC:\t\t%x\n", ipmid->CRC & 0xff);
    }
    return(buf);
}

int xipIoParse( char *str, xipIo_t *ptr, char flag )
{
  char *name;
  char *end;

  if( str == NULL || ptr == NULL )
    return 1;

  while( !isalnum(*str) )
  {
    if( *str++ == '\0')
      return 1;
  }

  name = str;

  str = strpbrk(str, " ");
  if( str == NULL || *str == '\0' )
    return 1;

  ptr->name = strdupn(name, str - name);
  if( ptr->name == NULL )
    return 1;

  if( flag == 'A' )
  {
    end = strchr(str,'C');
    if( end )
    {
      str = end + 1;
      sscanf(str,"%d", &ptr->channel);
    }
    else
      return 1;
  }
  else if( flag == 'B' )
  {
    end = strchr(str,'P');
    if( end )
    {
      str = end + 1;
      sscanf(str,"%d", &ptr->port);
      end = strchr(str,'B');
      if( end )
      {
        str = end + 1;
        sscanf(str,"%d", &ptr->bit);
      }
      else
        return 1;
    }
    else
      return 1;
  }
  return(0);
}


LOCAL char *strdupn( const char *ct, size_t n )
{
  char *duplicate;

  duplicate = (char *)malloc(n+1);
  if( !duplicate )
    return NULL;

  memcpy(duplicate, ct, n);
  duplicate[n] = '\0';

  return duplicate;
}

epicsThreadId ipApcie8650WaitForInts(int param)
{
    int icount;
    int err;
    int status;
    unsigned char command;
    struct configApcie8650 *pconfig = (struct config *) param;
    int uioDevFd = pconfig->uioDevFd;
    int uioClassPathConfigFd = pconfig->uioClassPathConfigFd;
    unsigned short ipr;
    int slot;
    int oldicount = 10;
    char imsg[64];

    status =  pread(uioClassPathConfigFd, &command, 1, 7);
    printf("command 0x%x\n", command);
    status =  pread(uioClassPathConfigFd, &command, 1, 5);
    printf("command 0x%x\n", command);

    printf("carrier %d\n", pconfig->card);

    while(1)
    {
        command &= ~0x4;
        pwrite(uioClassPathConfigFd, &command, 1, 5);
        /* Wait for next interrupt. */
        err = read(uioDevFd, &icount, 4);
        if (oldicount+1 < icount)
        {
            sprintf(imsg, "we missed %d interrupts\n", (icount-oldicount)+1);
            epicsInterruptContextMessage(imsg);
        }
        oldicount = icount;

        if (err != 4) {
            perror("uio read:");
            return(-1);
        }
        /* figure out which slot */
        for (slot = 0; slot < 4; slot++)
        {
            ipr = *((unsigned short *)pconfig->ioBase + 0x1);
            if (ipr & (0x03 << (slot * 2)))
            {
                if (0)
                {
                    sprintf(imsg, "Calling ISR for slot %d IPR 0x%x\n", slot, ipr & 0xff);
                    epicsInterruptContextMessage(imsg);
                }
                if (carrierISR.slots[slot].ISR != NULL)
                    carrierISR.slots[slot].ISR(carrierISR.slots[slot].param);
            }
        }
    }
    return(0);
}
/*
    int (*intConnect)(void *cPrivate, unsigned short slot, unsigned short vecNum,
                void (*routine)(int parameter), int parameter);
*/
LOCAL intConnect(unsigned short carrier, unsigned short slot, unsigned short vec, void (*routine)(int param), int param )
{
  carrierISR.slots[slot].ISR = (int(*)()) routine;
  carrierISR.slots[slot].param = param;

  return(0);
}


int ipApcie8650Report(int interest) {
    return (ipacReport(interest) == NULL?-1:0);
}

static const iocshArg apcie8650ReportArg0 =
    {"interest", iocshArgInt};

static const iocshArg * const apcie8650ReportArgs[2] =
    {&apcie8650ReportArg0};

static const iocshFuncDef apcie8650ReportFuncDef =
    {"ipApcie8650Report", 1, apcie8650ReportArgs};

static void apcie8650ReportCallFunc(const iocshArgBuf *args) {
    ipApcie8650Report(args[0].ival);
}

/* IPAC Carrier Table */

ipac_carrier_t apcie8650 = {
    "APCIE8650",
    SLOTS,
    initialise,
    report,
    baseAddr,
    irqCmd,
    intConnect 
};

int ipApcie8650Add(const char *cardParams) {
    return ipacAddCarrier(&apcie8650, cardParams);
}

/* iocsh command table and registrar */

static const iocshArg apcie8650CreateArg0 =
    {"VMEaddress", iocshArgString};

static const iocshArg * const apcie8650CreateArgs[1] =
    {&apcie8650CreateArg0};

static const iocshFuncDef apcie8650CreateFuncDef =
    {"ipApcie8650Add", 1, apcie8650CreateArgs};

static void apcie8650CreateCallFunc(const iocshArgBuf *args) {
    ipApcie8650Add(args[0].sval);
}

static void epicsShareAPI apcie8650Registrar(void) {
    iocshRegister(&apcie8650ReportFuncDef, apcie8650ReportCallFunc);
    iocshRegister(&apcie8650CreateFuncDef, apcie8650CreateCallFunc);
}

epicsExportRegistrar(apcie8650Registrar);

