/*******************************************************************************

Project:
    LCLS 

File:
    drvTamc220.c

Description:
    EPICS Driver for the Acromag Tamc220 IPAC carrier 

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
#include <sys/mman.h>
#include <fcntl.h>
#ifdef NO_EPICS
#include <vme.h>
#include <sysLib.h>
#else
#include "devLib.h"
#include "drvSup.h"
#endif
#include "epicsThread.h"
#include "epicsInterrupt.h"
#include "drvIpac.h"
#include "drvTamc220.h"
//#include "xipIo.h"
#ifndef NO_EPICS
#include "epicsExport.h"
#include "iocsh.h"
#endif


/* Only used within this file */
LOCAL char *strdupn( const char *ct, size_t n );

/* Characteristics of the card */

#define SLOTS		3
#define	IO_SPACES	2	

/* Offsets from base address in VME A16 */
#define REGS_A		0x000000
#define PROM_A		0x000080
#define REGS_B		0x000100
#define PROM_B		0x000180
#define REGS_C		0x000200
#define PROM_C		0x000280
#define REGS_SIZE	128	
    
/* VME Interrupt levels */

#define IRQ_LEVEL 0x6

typedef void * private_t[IO_SPACES][SLOTS];

/* Carrier Private structure type, one instance per board */


struct privateTamc220
{
    int *pciConfigBase;
    private_t *memSpaces;
    struct configTamc220 *pconfig;
};

typedef struct _carrierIsr
{
     unsigned short carrier;
     struct _slots 
     {
       int (*ISR)(int param);
       int param;
     } slots[4];
} CARRIERISR;

static CARRIERISR carrierISR;


/*******************************************************************************

Routine:
    initialise

Purpose:
    Creates new private table for Acromag Tamc220 at addresses given by cardParams

Description:
    Checks the parameter string for the address of the card I/O space and 
    optional size of the memory space for the modules.  If both the I/O and
    memory base addresses can be reached from the CPU, a private table is 
    created for this board.  The private table is a 2-D array of pointers 
    to the base addresses of the various accessible parts of the IP module.

Parameters:
    The parameter string is unused at this time.

Examples:

Returns:
    0 = OK, 
    S_IPAC_badAddress = Parameter string error, or address not reachable

*/

LOCAL int initialise( const char *cardParams, void **pprivate, unsigned short carrier )
{
    unsigned short space, slot;
    int card = 0;
    unsigned long ctlStsBase;
    unsigned long ioBase;
    int uioDevFd;
    int uioClassPathCtlStsFd;
    int uioClassPathMMIOFd;
    int uioClassPathConfigFd;
    char uioDevName[33]; 
    char uioClassPath[64];
    char *tp;
    epicsThreadId ipTamc220WaitForIntr(struct configTamc220*);
    int uioDevNum;


    struct privateTamc220 *pTamc220;
    struct configTamc220  *pconfig;
    private_t *private;
    static const int offset[IO_SPACES][SLOTS] = 
    {
        { PROM_A, PROM_B, PROM_C },
        { REGS_A, REGS_B, REGS_C }
    };

    if (cardParams != NULL)
        sscanf(cardParams,"%d", &uioDevNum);

    errlogPrintf("cardparams %s uioDevNum %d\n", cardParams, uioDevNum);

    sprintf(uioDevName, "%s%d", UIODEVNAME, uioDevNum);

    uioDevFd = open(uioDevName, O_RDWR);
    if (uioDevFd < 0) 
    {
        perror("uio open:");
        return errno;
    }

/* get a FD to the config space */

    if ((tp = strdup(UIOCLASSPATH_CONFIG)) == NULL)
        return (S_IPAC_noMemory);
    sprintf(uioClassPath, tp, uioDevNum);
    free(tp);

    uioClassPathConfigFd = open(uioClassPath, O_RDWR);
    if (uioClassPathConfigFd < 0) 
    {
	perror("config open:");
        return errno;
    }
    
/* get a FD to the ctl/status space */

    if ((tp = strdup(UIOCLASSPATH_CTLSTS)) == NULL)
        return (S_IPAC_noMemory);
    sprintf(uioClassPath, tp, uioDevNum);
    free(tp);

    uioClassPathCtlStsFd = open(uioClassPath, O_RDWR|O_SYNC);
    if (uioClassPathCtlStsFd < 0) 
    {
	perror("ctl/sts open:");
        return errno;
    }

/* get a FD to the MMIO space */

    if ((tp = strdup(UIOCLASSPATH_MMIO)) == NULL)
        return (S_IPAC_noMemory);
    sprintf(uioClassPath, tp, uioDevNum);
    free(tp);

    uioClassPathMMIOFd = open(uioClassPath, O_RDWR|O_SYNC);
    if (uioClassPathMMIOFd < 0) 
    {
	perror("mmio open:");
        return errno;
    }

    if (sscanf(cardParams, "%d", &card) != 1)
        return -1;

    if ((pTamc220 = malloc(sizeof(struct privateTamc220))) == NULL)
        return (S_IPAC_noMemory);

    ctlStsBase = (unsigned long) mmap(NULL, TAMC220_CTLSTS_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, uioClassPathCtlStsFd, 0);
    if (ctlStsBase == -1)
        return S_IPAC_badAddress;

    ioBase = (unsigned long) mmap(NULL, TAMC220_IO_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, uioClassPathMMIOFd, 0);
    if (ioBase == -1)
        return S_IPAC_badAddress;

    private = malloc(sizeof (private_t));

    for( space = 0; space < IO_SPACES; space++ )
    {
        for( slot = 0; slot < SLOTS; slot++ )
            (*private)[space][slot] = (void *) (ioBase + offset[space][slot]);
    }

    pTamc220->memSpaces = private;

    /* what is this? */
    /* enabling interrupts? */
    *(unsigned char*) ioBase |= 0x0004;

    /* Setup parameters for Tamc220 configuration */
    if ((pconfig = malloc(sizeof(struct configTamc220))) == NULL)
        return (S_IPAC_noMemory);

    pconfig->initialized = FALSE;
    pconfig->card = card; 
    pconfig->initialized = TRUE;

    pconfig->uioDevFd = uioDevFd;
    pconfig->uioClassPathConfigFd = uioClassPathConfigFd;
    pconfig->uioClassPathCtlStsFd = uioClassPathCtlStsFd;
    pconfig->uioClassPathMMIOFd = uioClassPathMMIOFd;
    pconfig->ioBase = ioBase;
    pconfig->ctlStsBase = ctlStsBase;

    pTamc220->pconfig = pconfig;
    *pprivate      = pTamc220;

    pconfig->tid = epicsThreadCreate("ipTamc220WaitForIntr", 65, epicsThreadGetStackSize(epicsThreadStackMedium),
                     (EPICSTHREADFUNC) ipTamc220WaitForIntr, pconfig);
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
  struct privateTamc220 *p;

  p = (struct privateTamc220 *)private;
  return( (*p->memSpaces)[space][slot] );
}


/*******************************************************************************

Routine:
    irqCmd

Purpose:
    Handles interrupter commands and status requests

Description:
    The Tamc220 only supports one interrupt level for all
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
#if	1
static int irqCmd(void *cPrivate, ushort_t slot,
                  ushort_t irqnum, ipac_irqCmd_t cmd)
{
    int retval=OK;
    struct privateTamc220 *pp;
    struct configTamc220 *cp;
    ctlStatus *csrp;
    int i, *tp;

    pp=(struct privateTamc220 *) cPrivate;
    cp = pp->pconfig;
    csrp = cp->ctlStsBase;

#if	0
    IOCTL_VME_BUF   ioctl_vme_buf;

    ioctl_vme_buf.lLength  = (unsigned long) cp->carrierINTLevel;                               // interrupt level. This is only needed for 8002 VME arch
    ioctl_vme_buf.lSlot = cp->carrierSlot;
    ioctl_vme_buf.lSite = slot;
#endif

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
            retval=cp->level;
            break;
        case ipac_irqEnable:
            /* Required to use interrupts */
            tp = csrp;
            if (irqnum==0)  //set the IP card interrupt in carrier CSR
            {
                csrp->ipCtl[slot] |= TAMC220_CTLSTS_INT0_ENABLE;
                csrp->ipCtl[slot] |= TAMC220_CTLSTS_INT1_ENABLE;
            }
            else
                csrp->ipCtl[slot] |= TAMC220_CTLSTS_INT1_ENABLE;
            break;
        case ipac_irqDisable:
            /* Not necessarily supported */
            if (irqnum==0)  //set the IP card interrupt in carrier CSR
                csrp->ipCtl[slot] &= ~TAMC220_CTLSTS_INT0_ENABLE;
            else
                csrp->ipCtl[slot] &= ~TAMC220_CTLSTS_INT1_ENABLE;
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
#else

LOCAL int irqCmd( void *private, unsigned short slot, unsigned short irqNumber, ipac_irqCmd_t cmd )
{
    struct privateTamc220 *p;
    p = (struct privateTamc220 *)private;
    struct configTamc220 *pconfig = p->pconfig;
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
#endif

#if	0
LOCAL char *report(struct privateTamc220 *pprivate, unsigned short slot)
#else
LOCAL char *report(void *pprivate, unsigned short slot)
#endif
{
    struct configTamc220  *pconfig = ((struct privateTamc220 *) pprivate)->pconfig;
    volatile ipac_idProm_t  *ipmid = baseAddr(pprivate, slot, ipac_addrID);
    int bc = 0;
    static char buf[1204];

    if (ipmCheck(pconfig->card, slot) != S_IPAC_noModule)
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

#if	0
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
#endif


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

epicsThreadId ipTamc220WaitForIntr(struct configTamc220 *pconfig)
{
    int icount;
    int err;
    unsigned char command;
    int uioDevFd = pconfig->uioDevFd;
    int uioClassPathConfigFd = pconfig->uioClassPathConfigFd;
    unsigned short ipr;
    int slot;
    int oldicount = 10;
    char imsg[64];

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

        if (err != 4) 
        {
            perror("uio read:");
            return((epicsThreadId)-1);
        }

        ipr = *((unsigned short *)pconfig->ioBase + 0x8);

        /* figure out which slot */
        for (slot = 0; slot < SLOTS; slot++)
        {
            if (ipr & (0x03 << (slot * 2)))
            {
                if (carrierISR.slots[slot].ISR != NULL)
                    carrierISR.slots[slot].ISR(carrierISR.slots[slot].param);
            }
        }
    }
    return((epicsThreadId)0);
}
/*
    int (*intConnect)(void *cPrivate, unsigned short slot, unsigned short vecNum,
                void (*routine)(int parameter), int parameter);
*/
LOCAL int intConnect(void *cPrivate, unsigned short slot, unsigned short vec, void (*routine)(int param), int param )
{
  carrierISR.slots[slot].ISR = (int(*)()) routine;
  carrierISR.slots[slot].param = param;

  return(0);
}


int ipTamc220Report(int interest) 
{
    return (ipacReport(interest));
}

static const iocshArg tamc220ReportArg0 =
    {"interest", iocshArgInt};

static const iocshArg * const tamc220ReportArgs[2] =
    {&tamc220ReportArg0};

static const iocshFuncDef tamc220ReportFuncDef =
    {"ipTamc220Report", 1, tamc220ReportArgs};

static void tamc220ReportCallFunc(const iocshArgBuf *args) {
    ipTamc220Report(args[0].ival);
}

/* IPAC Carrier Table */

ipac_carrier_t tamc220 = {
    "TAMC220",
    SLOTS,
    initialise,
    report,
    baseAddr,
    irqCmd,
    intConnect 
};

int ipTamc220Add(const char *carrier) {
    return ipacAddCarrier(&tamc220, carrier);
}

/* iocsh command table and registrar */

static const iocshArg tamc220CreateArg0 =
    {"carrier", iocshArgString};

static const iocshArg * const tamc220CreateArgs[1] =
    {&tamc220CreateArg0};

static const iocshFuncDef tamc220CreateFuncDef =
    {"ipTamc220Add", 1, tamc220CreateArgs};

static void tamc220CreateCallFunc(const iocshArgBuf *args) {
    ipTamc220Add(args[0].sval);
}

static void epicsShareAPI tamc220Registrar(void) {
    iocshRegister(&tamc220ReportFuncDef, tamc220ReportCallFunc);
    iocshRegister(&tamc220CreateFuncDef, tamc220CreateCallFunc);
}

epicsExportRegistrar(tamc220Registrar);

