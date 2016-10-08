/*******************************************************************************

Project:
    Hytec 8002 and 8004 Carrier Driver for EPICS

File:
    drvHy8002.c

Description:
    IPAC Carrier Driver for the Hytec VICB8002 and 8004 VME64x Quad IP Carrier
    boards. This file provides the board-specific interface between IPAC driver
    and the hardware. These carrier boards are 6U high and can support VME64x
    geographic addresses. The carrier has 4 IP card slots and can be configured
    to use any of the 7 interrupt levels (1 ~ 7).  The hotswap capability of the
    8002 board is not supported in this driver.

Authors:
    Andrew Johnson, Argonne National Laboratory
    Walter Scott (aka Scotty), HyTec Electronics Ltd
    Jim Chen, HyTec Electronics Ltd

Version:
    $Id$

Portions Copyright (c) 1995-1999 Andrew Johnson
Portions Copyright (c) 1999-2013 UChicago Argonne LLC,
    as Operator of Argonne National Laboratory
Portions Copyright (c) 2002-2010 Hytec Electronics Ltd, UK.

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
    20-Nov-2002  Walter Scott   First version with hotswap
    24-May-2010  Jim Chen       Second version without hotswap
    07-July-2010 Jim Chen       1.Removed VxWorks dependence
                                2.Fixed a couple of bugs in arguments parsing routine. 
                                3.Added comments. 
                                4.Changed Hy8002CarrierInfo to ipacHy8002CarrierInfo for consistency
                                5.ipacAddHy8002 now returns latest carrier number if successful
                                6.Tidy up the return value for success and errors
    20-August-2010 Jim Chen     Modified checkprom routine to check both configuration ROM space and GreenSpring spaces.
								Before this driver only checks the ID and model in GreenSpring space
								and other version of 8002 drivers check only in configuration ROM.
    24-August-2010 Jim Chen     Added interrupt connection routine. This used to be missing so that 
								all IP module drivers have to use devLib intConnectVME directly which 
								makes the drivers hardware dependent. Implementing here makes the drivers
								both Operating System Independent (OSI) and Hardware Architecture 
								Independent (HAI)
    10-November-2010 Jim Chen   Removed interrupt connection routine as Andrew Johnson suggested. Also changed 
								devEnableInterruptLevelVME call to devEnableInterruptLevel which makse the code 
                                more generic.
*******************************************************************************/

/* ANSI headers */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* EPICS headers */
#include <dbDefs.h>
#include <devLib.h>
#include <epicsTypes.h>
#include <epicsExit.h>
#include <iocsh.h>
#include <epicsExport.h>

/* Module headers */
#include "drvIpac.h"


/* Characteristics of the card */

#define SLOTS 4         /* Number of IP slots */
#define IO_SPACES 2     /* IP Address spaces in A16/A24 */
#define EXTENT 0x800    /* Total footprint in A16/A24 space */


/* Default configuration parameters */

#define DEFAULT_IPMEM   1
#define DEFAULT_IPCLOCK -1
#define DEFAULT_ROAK    0
#define DEFAULT_MEMBASE -1


/* Offsets from base address in VME A16 space */

#define IP_A_IO     0x0000
#define IP_A_ID     0x0080
#define IP_B_IO     0x0100
#define IP_B_ID     0x0180
#define IP_C_IO     0x0200
#define IP_C_ID     0x0280
#define IP_D_IO     0x0300
#define IP_D_ID     0x0380
#define REGISTERS   0x400
#define CONFIG_ROM  0x600

static const epicsUInt16 ipOffsets[IO_SPACES][SLOTS] = {
    { IP_A_ID, IP_B_ID, IP_C_ID, IP_D_ID },
    { IP_A_IO, IP_B_IO, IP_C_IO, IP_D_IO }
};


/* Carrier board registers */

typedef struct {
    epicsUInt16 ipstat;
    epicsUInt16 pad_02;
    epicsUInt16 memoff;
    epicsUInt16 pad_06;
    epicsUInt16 csr;
    epicsUInt16 pad_0a;
    epicsUInt16 intsel;
    epicsUInt16 pad_0e;
} ctrl_t;


/* Bit patterns for the CSR register */


/* Hytec IDs */
#define HYTECID    0x8003
#define PROM_MODEL 0x8002
#define PROM_MODEL_8003 0x8003
#define MANUFACTURER_HYTEC	0x80
#define HYTEC_PROM_MODEL	0x82
#define HYTEC_PROM_MODEL_8003	0x83

/* define individual bits in the carrier board's CSR register*/
#define CSR_RESET       0x0001
#define CSR_INTEN       0x0002
#define CSR_INTSEL0     0x0004
#define CSR_CLKSEL      0x0020
#define CSR_MEMMODE     0x0040
#define CSR_IPMS_1MB    0x0000
#define CSR_IPMS_2MB    0x0080
#define CSR_BADDSEL 	0x0080
#define CSR_IPMS_4MB    0x0100
#define CSR_IPMS_8MB    0x0180
#define CSR_INTRELS     0x0200
#define CSR_IPACLK      0x0400  /* 8004 only */
#define CSR_CD32    	0x4000
#define CSR_AB32    	0x8000

#define CSR_INTSELSHIFT 2
#define CSR_IPMEMSHIFT  7
#define CSROFF     ~(CSR_INTEN)



/* Offsets to fields in the VME64x Config ROM */

#define VME64CR_VALIDC  0x01F
#define VME64CR_VALIDR  0x023

#define VME64CR_MAN1    0x027
#define VME64CR_MAN2    0x02B
#define VME64CR_MAN3    0x02F

#define VME64CR_MOD1    0x033
#define VME64CR_MOD2    0x037
#define VME64CR_MOD3    0x03B
#define VME64CR_MOD4    0x03F

#define VME64CR_REVN    0x043
#define VME64CR_XIL1    0x047
#define VME64CR_XIL2    0x04B
#define VME64CR_XIL3    0x04F

#define VME64CR_SER1    0x0CB
#define VME64CR_SER2    0x0CF
#define VME64CR_SER3    0x0D3
#define VME64CR_SER4    0x0D7
#define VME64CR_SER5    0x0DB
#define VME64CR_SER6    0x0DF


/* ID values used in the ROM */

#define IEEE_MANUFACTURER_HYTEC 0x008003
#define HYTEC_MODEL_8002        0x80020000u
#define HYTEC_MODEL_8004        0x80040000u

/* prototype define */
typedef unsigned short word;
typedef unsigned int longword;

/* private structure used to keep track of a carrier card.
 We also keep a list of all Hy8002 carriers (carlist)
 for Hotswap support (scanning) */
typedef struct privinfo{
  struct privinfo* next;
  word vmeslotnum;                              /* vme slot */
  word carrier;                                 /* carrier number */
  word IPintlevel;                              /* interrupt level */
  int baseadr;                                  /* base address */
  int model;                                    /* carrier model, 8002/8003 */
  int clock;                                    /* carrier clock frequency */
  int intrels;                                  /* interrupt release on acknowledgement or on register read */
  word ipmemmode;                               /* 1,2,4 or 8 MB RAM per IP slot */
  word isgeomem;                                /* the card uses geographical IP card addressing. Please note, 
                                                due to the desing issue, Hytec 8002 cannot disable geographical addressing
                                                if VME64x crate is used. Yet if VME64 or VME is used, then a set of jumpers
                                                on board can be set for the base address of the carrier */
  /*these reflect the hardware registers*/
  word memoffs;                                 /* memory offset if non-geographical addressing is used */
  word csrcb;                                   /* CSR register */
  word ipintsel;                                /* IP cards interrupt settings in CSR */
  int ipadresses[NUMIPSLOTS][IPAC_ADDR_SPACES]; /* address mapping */
}privinfo;


/************GLOBAL VARIABLES********************/
static privinfo* carlist=NULL;
static char* charid="drvHy8002";

/*these are all offsets from the A16 base address*/
#define CARR_IPSTAT  0x00
#define CARR_MEMOFF  0x04                       /* added by JC 08-04-10 for memory offset register */
#define CARR_CSR     0x08
#define CARR_INTSEL  0x0C
#define CARR_HOTSWAP 0x10

#define CARR_IDENT   0x81
#define CARR_MANID   0x89
#define CARR_MODID   0x8B
#define CARR_REVN    0x8D
#define CARR_DRID1   0x91
#define CARR_DRID2   0x93
#define CARR_NUMB    0x95
#define CARR_CRC     0x97


/* Carrier Private structure, one instance per board */

typedef struct private_t {
    struct private_t * next;
    int carrier;
    volatile ctrl_t * regs;
    volatile epicsUInt8 * prom;
    void * addr[IPAC_ADDR_SPACES][SLOTS];
} private_t;


/* Module Variables */

static private_t *list_head = NULL;
static const char * const drvname = "drvHy8002";

/* function prototype */
static int regaddr(privinfo* pv);
static int checkVMEprom(unsigned int base);
static int scanparm(char* cp, int* vmeslotnum, int* IPintlevel, int* ipmem, int* ipclck, int* roak, int* domemreg, int* memoffs);
int ipacHy8002CarrierInfo(epicsUInt16 carrier);

/*******************************************************************************

Routine: internal function
    scanparm

Purpose:
    Parses configuration parameters

Description:
    This function parses the configuration parameter string to extract the
    mandatory VME slot number and interrupt level. Following these the string
    may also include settings for IP memory size, clock frequency, interrupt
    release and/or the IP memory base address; default values are set for any
    of these optional parameters that are not included.

Parameters:

    The parameter string starts with two decimal integers separated by a comma,
    which give the board's VME slot number (0 through 21) and the VME interrupt
    level to be used (0 through 7, 0 means interrupts are disabled). If a VME64x
    backplane is not being used to provide geographical addressing information,
    the slot number parameter must match the value set in jumpers J6 through
    J10, with J6 being the LSB and an installed jumper giving a 1 bit.

    After the above mandatory parameters, any of the following optional
    parameters may appear in any order. Space characters cannot be used on
    either side of the equals sign:

    IPMEM=<size>
        Sets the extent of the memory space allocated for each IP slot. The size
        parameter is a single digit 1, 2, 4 or 8 which gives the memory size to
        be used, expressed in MegaBytes. If this parameter is not provided the
        default value of 1 will be used.

    MEMBASE=<address>
        Specifies the top 16 bits of the base address of the A32 memory area to
        be used for IP slot memory space, overriding the jumper-selected or
        VME64x geographical address that will be used if this parameter is not
        provided. The address string should be a decimal integer or a number hex
        with a leading 0x. The resulting base address must be a multiple of 4
        times the slot memory size; an error message will be printed if this
        restriction is not met.

    IPCLK=<frequency>
        Specifies the frequency in MHz of the IP clock passed to all slots. The
        frequency parameter must be either 8 or 32. If not specified for a 8002
        carrier the IP clock will default to 8MHz, while for a 8004 carrier the
        driver will then configure each slot individually based on information
        from the ID prom of the module installed; specifying this parameter for
        an 8004 board overrides the module's configuration for all slots.

    ROAK=<release>
        This parameter controls when an 8004 carrier board will releases the
        interrupt request to the VMEbus. Giving release as 1 will cause the
        request to be released by the VME Interrupt Acknowledge cycle (ROAK
        protocol). If release is given as 0 or the parameter is not specified at
        all, the interrupt request will be released by the module's interrupt
        service routine clearing the module's reason for the interrupt request
        (RORA protocol) or disabling the interrupt using ipmIrqCmd(carrier,
        slot, irqn, ipac_irqDisable).

Examples:
    ipacAddHy8002("3,2")
        The board is in slot 3 of a VME64x crate, or jumpers are installed in
        positions J6 and J7. Module interrupts use VMEbus level 2. IP slots get
        1MB of memory in the block starting at A32:0x00c00000. IP clocks use the
        default described above.

    ipacAddHy8002("3,2 IPMEM=2, MEMBASE=0x9000,IPCLCK=32 ROAK=1")
        This modifies the above configuration to give each slot 2MB of memory in
        the block starting at A32:0x90000000, drives all IP clocks at 32MHz, and
        uses the ROAK protocol for releasing interrupt requests.

Return:
    0 = OK if successful
    S_IPAC_badAddress = Error in string

*/

static int scanparm (
    const char *params,
    int *vmeslot,
    int *intlevel,
    int *ipmem,
    int *ipclock,
    int *roak,
    int *membase
) {
    int vme = 0, itr = 0;
    int skip = 0;
    const char *pstart;
    int count;

    if (params == NULL || *params == 0)
        return S_IPAC_badAddress;

    count = sscanf(params, "%d, %d %n", &vme, &itr, &skip);
    if (count != 2) {
        printf("%s: Error parsing card configuration '%s'\n", drvname, params);
        return S_IPAC_badAddress;
    }

    if (vme < 0 || vme > 21) {
        printf("%s: Bad VME slot number %d\n", drvname, vme);
        return S_IPAC_badAddress;
    }
    else
        *vmeslot = vme;

    if (itr < 0 || itr > 7) {
        printf("%s: Bad VME interrupt level %d\n", drvname, itr);
        return S_IPAC_badAddress;
    }
    else
        *intlevel = itr;

    params += skip;

    /* Parse IP memory size */
    if ((pstart = strstr(params, "IPMEM=")) != NULL) {
        int ipm;

        if ((1 != sscanf(pstart+6, "%d", &ipm)) ||
            (ipm != 1 && ipm != 2 && ipm != 4 && ipm != 8))
            return S_IPAC_badAddress;
        *ipmem = ipm;
    }

    /* Parse memory base address */
    if ((pstart = strstr(params, "MEMBASE=")) != NULL) {
        int mem;

        if ((1 != sscanf(pstart+8, "%i", &mem)) ||
            mem < 0 || mem + (*ipmem << 6) > 0xffff)
            return S_IPAC_badAddress;
        *membase = mem;
    }

    /* Parse IP clock frequency */
    if ((pstart = strstr(params, "IPCLCK=")) != NULL) {
        int ipc;

        if ((1 != sscanf(pstart+7, "%d", &ipc)) ||
            (ipc != 8 && ipc != 32))
            return S_IPAC_badAddress;
        *ipclock = ipc;
    }

    /* Parse ROAK setting */
    if ((pstart = strstr(params, "ROAK=")) != NULL) {
        int ro;

        if ((1 != sscanf(pstart+5, "%d", &ro)) ||
            (ro !=0 && ro !=1))
            return S_IPAC_badAddress;
        *roak = ro;
    }

    return OK;
}


/*******************************************************************************

Routine:
    checkVMEprom

Purpose:
    Ensure the card is an 8002 or 8004 carrier

Description:
    This function checks that addressed VME slot has a configuration ROM
    at the expected location, and makes sure it identifies the module as
    being a Hytec 8002 or 8004 card.

Returns:
    0 = OK
    S_IPAC_noModule = Bus Error accessing card ROM
    S_IPAC_badModule = ROM contents did not match

*/

static int checkVMEprom(volatile epicsUInt8 *prom)
{
    epicsUInt32 id;
    char valid;

    /* Make sure the Configuration ROM exists */
    if (devReadProbe(1, &prom[VME64CR_VALIDC], &valid)) {
        printf("%s: Bus Error accessing card, check configuration\n", drvname);
        return S_IPAC_noModule;
    }
    if (valid != 'C' || prom[VME64CR_VALIDR] != 'R') {
        printf("%s: Configuration ROM not found, check address\n", drvname);
        return S_IPAC_badModule;
    }

    /* Manufacturer ID */
    id = (prom[VME64CR_MAN1] << 16) +
        (prom[VME64CR_MAN2] << 8) + prom[VME64CR_MAN3];

    if (id != IEEE_MANUFACTURER_HYTEC) {
        printf("%s: Manufacturer ID is %x, expected %x\n",
            drvname, id, IEEE_MANUFACTURER_HYTEC);
        return S_IPAC_badModule;
    }

    /* Board ID */
    id = (prom[VME64CR_MOD1] << 24) + (prom[VME64CR_MOD2] << 16) +
        (prom[VME64CR_MOD3] << 8) + prom[VME64CR_MOD4];

    if (id != HYTEC_MODEL_8002 && id != HYTEC_MODEL_8004) {
        printf("%s: Board ID is %x, expected %x or %x\n",
            drvname, id, HYTEC_MODEL_8002, HYTEC_MODEL_8004);
        return S_IPAC_badModule;
    }

    return OK;
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
    sprintf(output, "INT Level %d, INT0: %s, INT1: %s", 
	    cp->IPintlevel,
        (cp->ipintsel & (1 << slot)) ? "active" : "",
	    (cp->ipintsel & (1 << (slot+4))) ? "active" : "");
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
    shutdown

Purpose:
    Disable interrupts on IOC reboot

Description:
    This function is registered as an epicsAtExit routine which disables
    interrupts from the carrier when the IOC is shuttind down.

Returns:
    N/A

*/

static void shutdown(void *r) {
    volatile ctrl_t *regs = (ctrl_t *)r;

    regs->csr &= ~CSR_INTEN;
}

/*******************************************************************************

Routine:
    irqCmd

Purpose:
    Handles interrupter commands and status requests

Description:
    The carrier board provides a switch to select from 5 default interrupt
    level settings, and a control register to allow these to be overridden.
    The commands supported include setting and fetching the current interrupt
    level associated with a particular slot and interrupt number, enabling
    interrupts by making sure the VMEbus interrupter is listening on the
    relevent level, and the abilty to ask whether a particular slot interrupt
    is currently pending or not.

Returns:
    ipac_irqLevel0-7 return 0 = OK,
    ipac_irqGetLevel returns the current interrupt level,
    ipac_irqEnable returns 0 = OK,
    ipac_irqPoll returns 0 = no interrupt or 1 = interrupt pending,
    other calls return S_IPAC_notImplemented.

*/

static int irqCmd(void *cPrivate, ushort_t slot,
		  ushort_t irqnum, ipac_irqCmd_t cmd)
{
    int retval=S_IPAC_notImplemented;
    privinfo* cp=(privinfo*)cPrivate;
    word ipstat,mymask;
    word dodump=0;
    /*begin*/
    /*irqnumber is 0 or 1.*/
    if (irqnum !=0 && irqnum!=1)return S_IPAC_notImplemented;

    /*is the IP card valid*/
    if (slot>3) return S_IPAC_badAddress;
  
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
            break;
        case ipac_irqGetLevel:
            /* Returns level set (or hard-coded) */
            retval=cp->IPintlevel;
            break;
        case ipac_irqEnable:
            /* Required to use interrupts */
            if (irqnum==0)
                cp->ipintsel|=(1<<(slot));
            else
                cp->ipintsel|=(1<<(slot+4));

            cp->csrcb|=CSR_INTEN;dodump=1;
            retval=OK;
            break;
        case ipac_irqDisable:
            /* Not necessarily supported */
            cp->csrcb&=CSROFF;dodump=1;
            retval=OK;
            break;
        case ipac_irqPoll:
            /* Returns interrupt state */
            ipstat=*((word*)(cp->baseadr+CARR_IPSTAT));
            mymask=1<<(4+slot)|(1<<slot);
            retval=ipstat&mymask;
            break;
        case ipac_irqSetEdge: /* Sets edge-triggered interrupts */
        case ipac_irqSetLevel:/* Sets level-triggered (default) */
        case ipac_irqClear:   /* Only needed if using edge-triggered */
            break;
        default:
            break;
    }/*switch*/

    if (dodump){
        *((word*)(cp->baseadr+CARR_CSR   )) =cp->csrcb;
        *((word*)(cp->baseadr+CARR_INTSEL)) =cp->ipintsel;
    }
    return retval;
}

/*THIS IS THE CARRIER JUMPTABLE */
ipac_carrier_t Hy8002={
    "Hytec VICB8002",
    4,
    initialise,
    report,
    baseAddr,
    irqCmd,
    NULL
};


/*******************************************************************************

Routine:
    initialise

Purpose:
    Registers a new Hy8002 or 8004 carrier board

Description:
    Parses the parameter string for the card settings, initializes the card,
    sets its interrupt level and memory base address and creates a new private
    table with the necessary address information.

Parameters:
    See the scanparm routine above for a description of the parameter string.

Returns:
    0 = OK,
    S_IPAC_badAddress = Parameter string error
    S_IPAC_noMemory = malloc() failed
    S_dev_ errors returned by devLib

*/

static int initialise(const char *cp, void **cPrivate, epicsUInt16 carrier)
{
    int vmeslotnum, IPintlevel;
    unsigned int carbase;
    size_t ccbase;
    int res,ipmem,ipclck,roak,domemreg,memoffs;
    privinfo* pv;
    word csr;
    long status;
    /*begin*/

    res=scanparm(cp, &vmeslotnum, &IPintlevel,
	       &ipmem,&ipclck,&roak,&domemreg,&memoffs);
    if (res!=OK) return res;

    ccbase= (size_t)((vmeslotnum<<11)+(1<<10));
    res=devRegisterAddress(charid,atVMEA16,
			 ccbase,
			 VME_MEM_SIZE,(void*)(&carbase));
    if (res!=OK) 
        return S_IPAC_badAddress;

    /*see if this really is a HyTec 8002*/
    res = checkVMEprom(carbase);
    if (res!=OK){
        res=devUnregisterAddress(atVMEA16,ccbase,charid);
        return res;
    }

    pv=(privinfo*)calloc(1,sizeof(privinfo));
    if(pv==NULL) return S_IPAC_noMemory;

    /*determine the CSR*/
    csr=IPintlevel<<CSR_INTSELSHIFT;

    /* if (domemreg)csr|=CSR_BADDSEL; */ /* a bug, to use memory offset, need to set bit6, not 8. JC 08-04-10 */
    if (domemreg) csr|=CSR_MEMMODE;
    if (ipclck == 32) csr|=CSR_CLKSEL;                  /* this clock bit was missing before. JC 26-05-2010 */
    if (roak) csr|=CSR_INTRELS;                         /* this ROAK or RORA bit was also missing before. JC 26-05-2010 */

    switch (ipmem){
        case 1:
            break;
        case 2:
            csr|=(1<<CSR_IPMEMSHIFT);
            break;
        case 4:
            csr|=(2<<CSR_IPMEMSHIFT);
            break;
        case 8:
            csr|=(3<<CSR_IPMEMSHIFT);
            break;
        default:
            printf("%s: INTERNAL ERROR 1\n",charid);
            return S_IPAC_badAddress;
    }

    /*in the ipmem==2 with geographical addressing,
    vmeslotnum must be [0..15] */
    if (ipmem==2 && vmeslotnum>15){
        printf("%s: vmeslot number must be <16 when geographical addressing with 2MB IP RAM size",charid);
        return S_IPAC_badAddress;
    }
    if (ipmem>=4 && domemreg==0){
        printf("%s: geographical adressing is not supported with 4MB IP RAM size",charid);
        return S_IPAC_badAddress;
    }

    pv->next=carlist;carlist=pv;

    pv->vmeslotnum=vmeslotnum;
    pv->carrier=carrier;
    pv->IPintlevel=IPintlevel;
    pv->baseadr=carbase;
    pv->memoffs=memoffs;
    pv->clock=ipclck;
    pv->isgeomem=(domemreg==0);
    pv->csrcb=csr;
    pv->ipintsel=0;
    pv->ipmemmode=ipmem;

    *((word*)(pv->baseadr+CARR_CSR   )) =pv->csrcb;                 /* set csr register */
    if(!pv->isgeomem)                                               /* This part is missing before. added by JC 08-04-10 */
        *((word*)(pv->baseadr+CARR_MEMOFF   )) =pv->memoffs;        /* set memroy offset */

    /*register the IP memory space for this card*/
    res=regaddr(pv);
    if (res != OK) return res;

    status=devEnableInterruptLevel(intVME, IPintlevel);
    if(status) return S_IPAC_badIntLevel;

    *((word*)(pv->baseadr+CARR_INTSEL)) =pv->ipintsel;
    
    *cPrivate=(void*)pv;
    return OK;
}

/*******************************************************************************

Routine:
    ipacAddHy8002

Purpose:
    shell command to be used in start up script

Description:
    The carrier board can be registered by using this function during 
    the system start up.

Parameters:
    The parameter "cardParams" is a string that should comprise 2 (the first two are mandatory) 
    to 6 parameters that are seperated by commas.   

    - first parameter is the VME slot number (decimal string)
    - second parameter is the VME interrupt level (decimal string)
    - third parameter is a name/value pair defines the IP memory size. 
      "IPMEM=" followed by a number "1", "2" ,"4" or "8" for 1M, 2M, 4M or 8M respectively
    - fourth parameter is a name/value pair defines the IP module clock. 
      "IPCLCK=" followed number either "8" or "32" for 8M or 32M respectively
    - fiveth parameter is a name/value pair defines the type of releasing interrupt. 
      "ROAK=1" means to release interrupt upon acknowledgement; "ROAK=0" means to release by ISR.
    - sixth parameter defines IP memory mapping base address offset when neither geographical 
      addressing nor jumpers are used. "MEMOFFS=128". Please refer to the user manual.

    Examples:
        IPAC1 = ipacAddHy8002("3,2") 
            
        where: 3 = vme slot number 3
               2 = interrupt level 2

        IPAC1 = ipacAddHy8002("3,2,IPMEM=1,IPCLCK=8,ROAK=1,MEMOFFS=2048)

        where: 3 -> vme slot number 3
               2 -> interrupt level 2
               IPMEM=1 -> IP memory space as 1M
               IPCLCK=8 -> IP clock is 8MHz
               ROAK=1 -> release the interrupt on acknowledgement
               MEMOFFS=2048 -> IP memory mapping base address offset when not using 
                   geographical addressing (Please see manual for detail usage)

        Please note, no space allowed in the parameter string

Returns:
    >=0 and < IPAC_MAX_CARRIERS (21): newly added carrier number
    > M_ipac(600 << 16): error code

*/
int ipacAddHy8002(const char *cardParams)
{
    int rt;
    rt = ipacAddCarrier(&Hy8002, cardParams);                       /* add 8002 carrier */
    if(rt == OK)
        return ipacLatestCarrier();                                 /* If added OK, return the latest carrier number */
    else
    	return rt;                                                  /* Otherwise return error code  */
}

static const iocshArg Hy8002Arg0 = { "cardParams",iocshArgString};
static const iocshArg * const Hy8002Args[1] = {&Hy8002Arg0};
static const iocshFuncDef Hy8002FuncDef = {"ipacAddHy8002", 1, Hy8002Args};
static void Hy8002CallFunc(const iocshArgBuf *args) {
    ipacAddHy8002(args[0].sval);
}

static const iocshArg Hy8002InfoArg0 = {"carrier", iocshArgInt};
static const iocshArg * const Hy8002InfoArgs[1] =  {&Hy8002InfoArg0};
static const iocshFuncDef Hy8002InfoFuncDef = {"ipacHy8002CarrierInfo", 1, Hy8002InfoArgs};
static void Hy8002InfoCallFunc(const iocshArgBuf *args)
{
    ipacHy8002CarrierInfo(args[0].ival);
}

static void epicsShareAPI Hy8002Registrar(void) {
    iocshRegister(&Hy8002FuncDef, Hy8002CallFunc);
    iocshRegister(&Hy8002InfoFuncDef, Hy8002InfoCallFunc);
}

epicsExportRegistrar(Hy8002Registrar);




/***** Private function part *********/



/*******************************************************************************

Routine:
    report

Purpose:
    Returns a status string for the requested slot

Description:
    The status indicates if the module is asserting its error signal, then
    for each interrupt line it shows whether the interrupt is enabled and
    if that interrupt signal is currently active.

Returns:
    A static string containing the slot's current status.

*/
#if 0	/* Conflicts w/ SLAC version of same function */
static char* report (
    void *p,
    epicsUInt16 slot
) {
    private_t *private = (private_t *) p;
    volatile ctrl_t *regs = private->regs;
    volatile epicsUInt8 *prom = private->prom;
    int ipstat = regs->ipstat;
    int intsel = regs->intsel;
    static char output[IPAC_REPORT_LEN];

    switch (prom[VME64CR_MOD2]) {
    case 0x02:
        sprintf(output, "%sInt0: %sabled%s    Int1: %sabled%s",
            (ipstat & (0x100 << slot) ? "Slot Error    " : ""),
            (intsel & (0x001 << slot) ? "en" : "dis"),
            (ipstat & (0x001 << slot) ? ", active" : ""),
            (intsel & (0x010 << slot) ? "en" : "dis"),
            (ipstat & (0x010 << slot) ? ", active" : ""));
        break;
    case 0x04:
        sprintf(output, "%sInt0: %sabled%s    Int1: %sabled%s",
            (ipstat & 0x100 ? "IP Error    " : ""),
            (intsel & (0x001 << slot) ? "en" : "dis"),
            (ipstat & (0x001 << slot) ? ", active" : ""),
            (intsel & (0x010 << slot) ? "en" : "dis"),
            (ipstat & (0x010 << slot) ? ", active" : ""));
        break;
    }
    return output;
}
#endif


/*******************************************************************************

Routine:
    baseAddr

Purpose:
    Returns a pointer to the requested slot & address space

Description:
    Because we did all the calculations in the initialise routine, this code
    only has to do a table lookup from an array in the private area. Note that
    no parameter checking is required since we have array entries for all
    combinations of slot and space - the IPAC driver which calls this routine
    handles that.

Returns:
    The requested pointer, or NULL if the module has no memory.

*/

static void * baseAddr (
    void *p,
    epicsUInt16 slot,
    ipac_addr_t space
) {
    private_t *private = (private_t *) p;
    return private->addr[space][slot];
}

/*******************************************************************************

Routine:
    irqCmd

Purpose:
    Handles interrupter commands and status requests

Description:
    The board supports one interrupt level for all IP modules which cannot be
    changed after initialisation.  This routine returns the level for module
    drivers to use, and allows interrupt signals to be individually enabled,
    disabled and polled.

Returns:
    ipac_irqGetLevel returns the interrupt level,
    ipac_irqEnable and ipac_irqDisable return 0 = OK,
    ipac_irqPoll returns non-zero if an interrupt pending, else 0,
    other calls return S_IPAC_notImplemented.

*/

static int irqCmd (
    void *p,
    epicsUInt16 slot,
    epicsUInt16 irqNumber,
    ipac_irqCmd_t cmd
) {
    private_t* private = (private_t *) p;
    volatile ctrl_t *regs = private->regs;
    int irqBit = 1 << (4 * irqNumber + slot);

    switch(cmd) {
        case ipac_irqGetLevel:
            return (regs->csr / CSR_INTSEL0) & 7;

        case ipac_irqEnable:
            regs->intsel |= irqBit;
            return OK;

        case ipac_irqDisable:
            regs->intsel &= ~ irqBit;
            return OK;

        case ipac_irqPoll:
            return regs->ipstat & irqBit;

        default:
            return S_IPAC_notImplemented;
    }
}



/*******************************************************************************

Routine:
    ipacHy8002CarrierInfo

Purpose:
    Print model and serial numbers from carrier card ROM

Description:
    Display model and serial number information from the carrier PROM for a
    selected carrier, or for all registered carriers when the carrier number
    requested is > 20.

Return:
    0 = OK

*/

int ipacHy8002CarrierInfo (
    epicsUInt16 carrier
) {
    private_t *private = list_head;

    if (private == NULL) {
        printf("No Hy8002/8004 carriers registered.\n");
        return OK;
    }

    while (private != NULL) {
        volatile epicsUInt8 *prom = private->prom;

        if ((private->carrier == carrier) || (carrier > 20)) {
            printf("PROM manufacturer ID: 0x%06x.\n",
                (prom[VME64CR_MAN1] << 16) + (prom[VME64CR_MAN2] << 8) +
                 prom[VME64CR_MAN3]);

            printf("PROM model #: 0x%04x, board rev. 0x%02x\n",
                (prom[VME64CR_MOD1] << 8) + prom[VME64CR_MOD2],
                prom[VME64CR_REVN]);

            printf("PROM Xilinx rev.: 0x%02x, 0x%02x, 0x%02x\n",
                prom[VME64CR_XIL1], prom[VME64CR_XIL2], prom[VME64CR_XIL3]);

            printf("PROM Serial #: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
                prom[VME64CR_SER1], prom[VME64CR_SER2], prom[VME64CR_SER3],
                prom[VME64CR_SER4], prom[VME64CR_SER5], prom[VME64CR_SER6]);

            if (private->carrier == carrier)
                break;
        }
        private = private->next;
    }
    return OK;
}


/******************************************************************************/

/* IPAC Carrier Table */

static ipac_carrier_t Hy8002 = {
    "Hytec VICB8002/8004",
    SLOTS,
    initialise,
    report,
    baseAddr,
    irqCmd,
    NULL
};


int ipacAddHy8002(const char *cardParams) {
    return ipacAddCarrier(&Hy8002, cardParams);
}


/* iocsh Command Table and Registrar */

static const iocshArg argParams =
    {"cardParams", iocshArgString};
static const iocshArg argCarrier =
    {"carrier", iocshArgInt};

static const iocshArg * const addArgs[] =
    {&argParams};
static const iocshArg * const infoArgs[] =
    {&argCarrier};

static const iocshFuncDef Hy8002FuncDef =
    {"ipacAddHy8002", NELEMENTS(addArgs), addArgs};

static void Hy8002CallFunc(const iocshArgBuf *args) {
    ipacAddHy8002(args[0].sval);
}

static const iocshFuncDef Hy8002InfoFuncDef =
    {"ipacHy8002CarrierInfo", NELEMENTS(infoArgs), infoArgs};

static void Hy8002InfoCallFunc(const iocshArgBuf *args) {
    ipacHy8002CarrierInfo(args[0].ival);
}

static void epicsShareAPI Hy8002Registrar(void) {
    iocshRegister(&Hy8002FuncDef, Hy8002CallFunc);
    iocshRegister(&Hy8002InfoFuncDef, Hy8002InfoCallFunc);
}

epicsExportRegistrar(Hy8002Registrar);

/*******************************************************************************

Routine: internal function
    checkVMEprom

Purpose:
    check if the carrier is 8002 or 8003

Description:
    This function checks the carrier against a valid Hytec 8002 or 8003 card.  

Parameters:
    base: VME carrier base address.

Return:
    OK(0): if it is 8002 or 8003
    Error code otherwise.

*/

#define VME_CARR_MAN1  0x22B
#define VME_CARR_MAN2  0x22F

#define VME_CARR_MOD1  0x233
#define VME_CARR_MOD2  0x237

#define VME_CARR_REVN  0x243
#define VME_CARR_XIL1  0x247
#define VME_CARR_XIL2  0x24B
#define VME_CARR_XIL3  0x24F

#define VME_CARR_SER1  0x2CB
#define VME_CARR_SER2  0x2CF
#define VME_CARR_SER3  0x2D3
#define VME_CARR_SER4  0x2D7
#define VME_CARR_SER5  0x2DB
#define VME_CARR_SER6  0x2DF


static int checkVMEprom(unsigned int base)
{
    char* hytecstr=" (HyTec Electronics Ltd., Reading, UK)";
    int manid,ismodel,modelnum,ishytec;

	/* This checks the ID in Configuration ROM */
    manid=(*((char*)(base+VME_CARR_MAN1))<<8)+
        (*((char*)(base+VME_CARR_MAN2)));

/* bug fix PHO 29-1-02 
*  manid gets sign extended on a 167
*/
    manid &= 0xffff;
    ishytec=(manid==HYTECID);

	/* If ID in Configuration ROM fails, also check GreenSpring space */
	if (!ishytec)
	{
		manid = (int) (*((char *) (base + CARR_MANID)));
		manid &= 0xff;
		ishytec = (manid == MANUFACTURER_HYTEC);
	}

	/* This checks the model in Configuration ROM  */
    modelnum=((int)(*((char*)base+VME_CARR_MOD1))<<8)+
        (int)(*((char*)base+VME_CARR_MOD2));

/* bug fix PHO 29-1-02 as for manid
*/
    modelnum &= 0xffff;
    ismodel=((modelnum==PROM_MODEL) || (modelnum==PROM_MODEL_8003));

	/* If model in Configuration ROM fails, also check GreenSpring space */
	if(!ismodel)	
	{
		modelnum = (int) (*((char *) base + CARR_MODID));
		modelnum &= 0xff;
		ismodel=((modelnum==HYTEC_PROM_MODEL) || (modelnum==HYTEC_PROM_MODEL_8003));
	}

    if (!ishytec)
		printf("PROM UNSUPPORTED MANUFACTURER ID:%x;\nPROM EXPECTED 0x%08X, %s\n",
	       manid,HYTECID,hytecstr);
    if(!ismodel)
        printf("PROM UNSUPPORTED BOARD MODEL NUMBER:%x EXPECTED 0x%04hx or 0x%04hx\n",
	       modelnum, PROM_MODEL, PROM_MODEL_8003);
    return (ishytec && ismodel) ? OK : S_IPAC_badModule;
}


/*******************************************************************************

Routine: internal function
    scanparm

Purpose:
    parsing parameters

Description:
    This function parses the parameter passed by ipacAddHy8002 routine  
    to get the vme slot number, interrupt level, IP memory size, IP clcok
    setting, interrupt release type and memory offset for base address etc.

Parameters:
    Please refer to ipacAddHy8002 routine.

Return:
    OK(0): if successful
    Error code otherwise.

*/
static int scanparm(char* cp,
		    int* vmeslotnum,
		    int* IPintlevel,
		    int* ipmem,
		    int* ipclck,
            int* roak,
		    int* domemreg,
		    int* memoffs
		    )
{
    int vme=0, itr=0, ipm=0, ipc=0, ro=0, mem=0;
    int skip=0;
    char *pstart;
    int count;

    if (cp == NULL || strlen(cp) == 0) {
        return S_IPAC_badAddress;             
    }

    count = sscanf(cp, "%d,%d,%n", &vme, &itr, &skip);
    if (count != 2){   
        printf("********Number error. %s  num:%d\n",cp, count);
        return S_IPAC_badAddress;     
    }

    /*vme slot number parsing*/
    if (vme<0 || vme>21)
    {
        printf("********Slot error.\n");
        return S_IPAC_badAddress;
    }
    else
        *vmeslotnum = vme;

    /*Interrupt level parsing*/
    if (itr<0 || itr>7)
        return S_IPAC_badAddress;
    else
        *IPintlevel = itr;

    cp += skip;

    /*set defaults: 1M memeory, 8MHz clock, ROAK, do not use geographical addressing*/
    *ipmem=1;
    *ipclck=8;
    *roak=0;
    *domemreg=*memoffs=0;

    /*parsing IP memory size*/
    if((pstart=strstr(cp, "IPMEM=")) != NULL){
        if((1 != sscanf(pstart+6, "%d", &ipm)) || (ipm !=1 && ipm !=2 && ipm !=4 && ipm !=8)){
            return S_IPAC_badAddress;
        }
        *ipmem = ipm;    
    }
            
    /*parsing IP clock frequency*/
    if((pstart=strstr(cp, "IPCLCK=")) != NULL){
        if((1 != sscanf(pstart+7, "%d", &ipc)) || (ipc !=8 && ipc !=32)){
            return S_IPAC_badAddress;
        }
        *ipclck = ipc;    
    }
            
    /*parsing ROAK request*/
    if((pstart=strstr(cp, "ROAK=")) != NULL){
        if((1 != sscanf(pstart+5, "%d", &ro)) || (ro !=0 && ro !=1)){
            return S_IPAC_badAddress;
        }
        *roak = ro;    
    }
            
    /*parsing memory offset*/
    if((pstart=strstr(cp, "MEMOFFS=")) != NULL){
        if((1 != sscanf(pstart+8, "%d", &mem)) || mem <0 || mem >(1<<17)){
            return S_IPAC_badAddress;
        }
        *domemreg = 1;
        *memoffs = mem;
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
    int* ipadr=(int*)(pv->ipadresses);
    int vmeslotnum=pv->vmeslotnum;
    int ip,ia,ipinc;
    int memspace;
    size_t basetmp=0;
    /*  int retval=(int)NULL;*/  /* Who would initialize like this??? */
    volatile longword retval = 0;
    int space;
    int moffs,status;
    /*begin*/
    for (ip=0;ip<NUMIPSLOTS;ip++)
        for (ia=0;ia<IPAC_ADDR_SPACES;ia++)*ipadr++ =0;
  
    /* init the ipac_addrIO and ipac_addrID spaces*/
    for(ip=0;ip<NUMIPSLOTS;ip++){
        basetmp=(size_t)((vmeslotnum<<11)+(ip<<8));
        status=devRegisterAddress(charid, atVMEA16,
			      basetmp,
			      IP_MEM_SIZE,
			      (void *) &retval);
        if (status!= OK){
            return S_IPAC_badAddress;
        }
        pv->ipadresses[ip][ipac_addrIO]=retval;
        pv->ipadresses[ip][ipac_addrID]=retval+0x80;
    }

    /*IP RAM space. This depends on the memory mode.
     See section 2.2.1 in the VICB8802 User's Manual.
     The 32 bit dual slot case is handled the same
     way as the 16 bit case but has larger memory space*/
    ipinc=1;
    for(ip=0;ip<NUMIPSLOTS;ip+=ipinc){
        space=ipac_addrMem;

        if (pv->isgeomem) {
        /*geographic addressing*/
            switch (pv->ipmemmode) {
                case 1:
	               basetmp=(size_t)((vmeslotnum<<22)|(ip<<20));
	               break;
                case 2:
	               basetmp=(size_t)((vmeslotnum<<23)|(ip<<21));
	               break;
                case 4:
	           /*shouldn't happen, catch this case in initialise()*/
	               break;
                case 8:
	               basetmp=(size_t)((vmeslotnum<<27)|(ip<<23));
	               break;
                default:
	               printf("INTERNAL ERROR: unknown ipmemmode %d\n",pv->ipmemmode);
	               break;
            }/*switch*/
        } else {
            /*use the memory base register*/
            moffs=(pv->memoffs>>6);
            switch (pv->ipmemmode) {
                case 1:
	               basetmp=(size_t)((moffs<<22)|(ip<<20));
	               break;
                case 2:
	               basetmp=(size_t)((moffs<<23)|(ip<<21));
	               break;
                case 4:
	               basetmp=(size_t)((moffs<<24)|(ip<<22));
	               break;
                case 8:
	               basetmp=(size_t)((moffs<<25)|(ip<<23));
	               break;
                default:
	               printf("INTERNAL ERROR: unknown ipmemmode %d\n",pv->ipmemmode);
	               break;
            }
        }
        /*now register the address*/
        if ((int)basetmp==0)
             return S_IPAC_badAddress;

        if (space==ipac_addrMem) memspace=ONEMB;
        else/*double wide space*/memspace=2*ONEMB;

        /* printf("%s: Try to map address=0x%x\n)",charid, basetmp); */

        status=devRegisterAddress(charid, atVMEA32,
			      basetmp,
			      memspace,
			      (void *)&(retval));
        if (status!=OK) {
            return S_IPAC_badAddress;
        }

        /* printf("%s: Mapped address=0x%x\n)",charid, retval); */
        pv->ipadresses[ip][space]=retval;
    } 
    return OK;
}

/*******************************************************************************

Routine: 
    ipacHy8002CarrierInfo

Purpose:
    print ROM info of the carrier card

Description:

    This is an. It registers the IP carrier card memory..

Parameters:
    carrier: carrier card number

Return:
    OK(0): if successful. 
    Error code otherwise .

*/
/*return carrier PROM info of specified carrier or all if argument carrier is 0xFFFF */
int ipacHy8002CarrierInfo(epicsUInt16 carrier)
{
    privinfo *cp=carlist; 
    char* hytecstr=" (HyTec Electronics Ltd., Reading, UK)";
    int manid,modelnum;

    if(carlist == NULL){
        printf("No carrier is registered.");
        return S_IPAC_badAddress;
    }
 
    while(cp!=NULL){																	/* loop all carriers */
        if((cp->carrier == carrier) || (carrier == 0xFFFF)){                            /* print when carrier matches specified or all */
            /*begin*/
            manid=((*((char*)(cp->baseadr+VME_CARR_MAN1))<<8)+
            (*((char*)(cp->baseadr+VME_CARR_MAN2)))) & 0xffff;

            printf("PROM manufacturer ID: 0x%02X. %s\n",manid, hytecstr);
        
            modelnum=(((int)(*((char*)cp->baseadr+VME_CARR_MOD1))<<8)+
                (int)(*((char*)cp->baseadr+VME_CARR_MOD2))) & 0xffff;

            printf("\nPROM model #: 0x%02hx, board rev. 0x%02hx\n",
	           modelnum,(int)(*((char*)(cp->baseadr+VME_CARR_REVN))));
            printf("PROM Xilinx rev.: 0x%02hx, 0x%02hx, 0x%02hx\n",
	           (int)(*((char*)(cp->baseadr+VME_CARR_XIL1))),
	           (int)(*((char*)(cp->baseadr+VME_CARR_XIL2))),
	           (int)(*((char*)(cp->baseadr+VME_CARR_XIL3))));

            printf("PROM Serial #: 0x%02hx 0x%02hx 0x%02hx 0x%02hx 0x%02hx 0x%02hx\n",
	           *((char*)(cp->baseadr+VME_CARR_SER1)),
	           *((char*)(cp->baseadr+VME_CARR_SER2)),
	           *((char*)(cp->baseadr+VME_CARR_SER3)),
	           *((char*)(cp->baseadr+VME_CARR_SER4)),
	           *((char*)(cp->baseadr+VME_CARR_SER5)),
	           *((char*)(cp->baseadr+VME_CARR_SER6)));

            if(cp->carrier == carrier) break;
        }
        cp=cp->next;
    }
    return OK;
}




