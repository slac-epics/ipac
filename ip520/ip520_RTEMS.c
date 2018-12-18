/*
 * RTEMS/EPICS driver for Acromag IP520 Octal UART
 *
 * Author: Christopher Ford
 * Based on Ron Sluiter's    "ip520.c".
 * Based on Andrew Johnson's "tyGSOctal".
 */

/*
Implentation Notes:

- The three Rx Error Flags (Overrun, Parity, Framing) in the LSR register
  are cleared whenever the CPU reads the LSR register. Therefore, Rx error
  processing must/should be done whenever the LSR register is read. The
  exception to this rule is IP520Report(). It is assumed that IP520Report()
  will only be called to identify a known problem.

- see README file for more info.

*/

#include <epicsStdio.h>
#include <epicsStdlib.h>
#include <epicsString.h>
#include <epicsThread.h>
#include <epicsExit.h>
#include <epicsInterrupt.h>
#include <epicsTypes.h>
#include <epicsExport.h>
#include <errlog.h>
#include <devLib.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>
#include <errno.h>
#include <iocsh.h>
#include <rtems/system.h>
#include <rtems/io.h>
#include <rtems/error.h>
#include <rtems/termiostypes.h>

#include "Acromag_ip_modules.h"
#include "IP520Ext.h"
#include "IP520Int.h"
#include "drvIpac.h"                          /* IP management (from drvIpac) */

/* enable interrupt support */
#define INCLUDE_IP520_INTERRUPT

/*
 * Macros
 */

#define isPower2(x) ((x) && !((x) & ((x) - 1)))

/*
 * Module variables
 */

static MOD_TABLE *IP520Modules;
static int        IP520MaxModules;
int               IP520LastModule;

rtems_device_major_number IP520Major;

static epicsUInt8 savedlcr;   /* Saved LCR value for EFROn & EFROff functions */

/*
 * Forward declarations
 */
static MOD_TABLE *IP520OctalFindQT(const char *);
static void       IP520InitChannel(MOD_TABLE *, int);
static void       IP520OptsSet(TY_IP520_DEV *, int);
static int        IP520CallbackPollWrite     (int, const char *, int);
static int        IP520CallbackInterruptWrite(int, const char *, int);
static int        IP520CallbackSetAttributes (int, const struct termios *);
static void       IP520RebootHook(void *);
static void       EFROn (REGMAP *);
static void       EFROff(REGMAP *);


void IP520Report(void)
{
    int mod;

    for (mod = 0; mod < IP520LastModule; mod++)
    {
        MOD_TABLE *pmod = &IP520Modules[mod];
        int        port;

        printf( "Module %d: carrier=%d slot=%d irqCnt=%u\n",
                mod, pmod->carrier, pmod->slot, pmod->irqCount );

        for (port = 0; port < 8; port++)
        {
            TY_IP520_DEV *dev  = &pmod->dev[port];
            REGMAP       *regs = dev->regs;

            if (dev->created)
            {
                printf( "  Port %d: %lu chars in, %lu chars out, %u overrun, %u parity, %u framing\n",
                        port, dev->readCount, dev->writeCount, dev->overCount,
                        dev->parityCount, dev->frameCount );
                printf( "           IER = 0x%2.2hhX, LSR = 0x%2.2hhX, MCR = 0x%2.2hhX, LCR = 0x%2.2hhX\n",
                        regs->u.read.ier, regs->u.read.lsr, regs->u.read.mcr,
                        regs->u.read.lcr);
            }
        }
    }
}

static void IP520RebootHook(void *arg)
{
    int mod;
    int key = epicsInterruptLock();                     /* disable interrupts */

    for (mod = 0; mod < IP520LastModule; mod++)
    {
        MOD_TABLE *pmod = &IP520Modules[mod];
        int        port;

        for (port=0; port < 8; port++)
        {
            TY_IP520_DEV *dev = &pmod->dev[port];

            if (dev->created)
            {
                dev->regs->u.write.ier  = 0;
                dev->regs->u.write.mcr &= ~(0x08);  /* Port interrupt disable */
            }
            ipmIrqCmd(pmod->carrier, pmod->slot, 0, ipac_irqDisable);
            ipmIrqCmd(pmod->carrier, pmod->slot, 1, ipac_irqDisable);

            ipmIrqCmd(pmod->carrier, pmod->slot, 0, ipac_statUnused);
        }
    }
    free(IP520Modules);

    epicsInterruptUnlock(key);
}

static void IsrErrMsg(epicsUInt8 lsr, TY_IP520_DEV *dev)
{
    static char  overrunErrMsg[] = "      : Rx overrun ctr = xxx\n";
    static char  parityErrMsg[]  = "      : Rx parity  ctr = xxx\n";
    static char  framingErrMsg[] = "      : Rx framing ctr = xxx\n";
    int          cnt;
    char        *errmsg;

    if      (lsr & 0x02)                              /* Check for Rx overrun */
    {
        cnt = ++dev->overCount;
        errmsg = overrunErrMsg;
    }
    else if (lsr & 0x04)                            /* Check for parity error */
    {
        cnt = ++dev->parityCount;
        errmsg = parityErrMsg;
    }
    else if (lsr & 0x08)                           /* Check for framing error */
    {
        cnt = ++dev->frameCount;
        errmsg = framingErrMsg;
    }
    else
        return;

    if (cnt <= 10 || isPower2(cnt))
    {
        int size;
        size = strlen(dev->pmod->moduleID);
        size = (size >= 6) ? 6 : (size - 1);
        strncpy(errmsg, dev->pmod->moduleID, size);

        errmsg[25] = '0' + (cnt / 100) % 10;
        errmsg[26] = '0' + (cnt /  10) % 10;
        errmsg[27] = '0' +  cnt        % 10;

        epicsInterruptContextMessage(errmsg);
    }
}

/*******************************************************************************
 * IP520Int - interrupt level processing
 *
 * Loop through each of the 8 ports, until no Rx or Tx processing required.
 *
 */
void IP520Int(int mod)
{
    MOD_TABLE *pmod = &IP520Modules[mod];
    REGMAP    *regs;
    volatile epicsUInt8 dummy, *flush = NULL;
    int scan = 0;

    pmod->irqCount++;

    while (scan <= 7)
    {
        epicsUInt8 isr, lsr, ier;
        TY_IP520_DEV *dev = &pmod->dev[scan];
        int key, work = 0;

        if (!dev->created)
        {
            scan++;
            continue;
        }

        regs = dev->regs;
        key  = epicsInterruptLock();                     /* Is this required? */
        isr  = regs->u.read.isr;
        ier  = regs->u.read.ier;
        lsr  = regs->u.read.lsr;

        if (lsr & 0x0E)         /* Check for overrun, parity or framing error */
            IsrErrMsg(lsr, dev);

        /*
         * If receiver is ready, read the character and push it up
         */
        while (lsr & 0x01)                     /* RBR has a character to read */
        {
            char inChar = regs->u.read.rbr;
            dev->readCount++;
            if (dev->tyDev)
              rtems_termios_enqueue_raw_characters(dev->tyDev, &inChar, 1);

            work = 1;

            lsr  = regs->u.read.lsr;
            if (lsr & 0x0E)     /* Check for overrun, parity or framing error */
                IsrErrMsg(lsr, dev);
        }

        /*
         * If transmiter is ready tell termios that character has been sent
         */
        if ((ier & 0x02) && (lsr & 0x20)) /* If Tx interrupts are enabled,
                                             AND, Tx FIFO is empty */
        {
            regs->u.write.ier &= ~(0x02);

            dev->writeCount++;
            if (dev->tyDev) rtems_termios_dequeue_characters(dev->tyDev, 1);

            work = 1;
        }

        if (work == 0) scan++;

        epicsInterruptUnlock(key);                       /* Is this required? */
    }

    if (flush) dummy = *flush;                      /* Flush last write cycle */
}

/*******************************************************************************
 * IP520ModuleInit - initialize an IP module
 *
 * The routine initializes the specified IP module. Each module is
 * characterized by its model name, interrupt vector, carrier board number,
 * and slot number on the board. No new setup is done if a MOD_TABLE entry
 * already exists with the same carrier and slot numbers.
 *
 * For example:
 * .CS
 *    int idx;
 *    idx = IP520ModuleInit("SBS232-1", "232", 0x60, 0, 1);
 * .CE
 *
 *
 * RETURNS: Index into module table, or -1 if the driver is not installed,
 * the channel is invalid, or the device already exists.
 *
 * SEE ALSO: IP520Drv()
*/
int IP520ModuleInit
    (
    const char * moduleID,       /* IP module name */
    const char * type,           /* IP module type 232/422/485 */
    int          int_num,        /* Interrupt vector */
    int          carrier,        /* carrier number */
    int          slot            /* slot number */
    )
{
    static char *fn_nm = "IP520ModuleInit";
    int modelID, status, mod;
    MOD_TABLE *pmod;

    /*
     * Check for the driver being installed.
     */
    if (IP520Major == 0)
    {
        errno = ENODEV;
        printf("%s: returning ENODEV", fn_nm);
        return -1;
    }

    if (!moduleID || !type)
    {
        errno = EINVAL;
        printf("%s: returning EINVAL", fn_nm);
        return -1;
    }

    /*
     * Check the IP module type.
     */
    if (strstr(type, "232"))
        modelID = IP520_OCTAL232;
/* For Future use.
    else if (strstr(type, "422"))
        modelID = IP520_OCTAL422;
    else if (strstr(type, "485"))
        modelID = IP520_OCTAL485;
*/
    else
    {
        printf("%s: Unsupported module type: %s", fn_nm, type);
        errno = EINVAL;
        return -1;
    }

    /*
     * Validate the IP module location and type.
     */
    status = ipmValidate(carrier, slot, ACROMAG_ID, modelID);
    if (status)
    {
        printf("%s: IPAC Module validation failed\n"
            "    Carrier:%d slot:%d modelID:0x%x\n",
            fn_nm, carrier, slot, modelID);

        switch (status)
        {
            case S_IPAC_badAddress:
                printf("    Bad carrier or slot number\n");
                break;
            case S_IPAC_noModule:
                printf("    No module installed\n");
                break;
            case S_IPAC_noIpacId:
                printf("    IPAC identifier not found\n");
                break;
            case S_IPAC_badCRC:
                printf("    CRC Check failed\n");
                break;
            case S_IPAC_badModule:
                printf("    Manufacturer or model IDs wrong\n");
                break;
            default:
                printf("    Unknown status code: 0x%x\n", status);
                break;
        }
        errno = status;
        return -1;
    }

    /* See if the associated IP module has already been set up */
    for (mod = 0; mod < IP520LastModule; mod++)
    {
        pmod = &IP520Modules[mod];
        if (pmod->carrier == carrier && pmod->slot == slot)
            break;
    }

    /* Create a new quad table entry if not there */
    if (mod >= IP520LastModule)
    {
        void *addrIO;
        char *ID = epicsStrDup(moduleID);
        REGMAP *preg;
        int port;

        if (IP520LastModule >= IP520MaxModules)
        {
            printf("%s: Maximum module count exceeded!", fn_nm);
            errno = ENOSPC;
            return -1;
        }

        pmod = &IP520Modules[IP520LastModule];
        pmod->modelID = modelID;
        pmod->carrier = carrier;
        pmod->slot = slot;
        pmod->moduleID = ID;

        addrIO = ipmBaseAddr(carrier, slot, ipac_addrIO);
        preg = (REGMAP *) addrIO;

        for (port = 0; port < 8; port++)
        {
            pmod->dev[port].created = 0;
            pmod->dev[port].regs = &preg[port];
            pmod->dev[port].pmod= pmod;
            pmod->dev[port].regs->u.write.ier = 0;
            pmod->dev[port].regs->u.write.scr = int_num;
        }

#ifdef INCLUDE_IP520_INTERRUPT
        if (ipmIntConnect(carrier, slot, int_num, IP520Int, IP520LastModule))
        {
            printf("%s: Unable to connect ISR", fn_nm);
            return -1;
        }

        ipmIrqCmd(carrier, slot, 0, ipac_irqEnable);
        ipmIrqCmd(carrier, slot, 1, ipac_irqEnable);
        ipmIrqCmd(carrier, slot, 0, ipac_statActive);
#endif
    }

    return IP520LastModule++;
}

/******************************************************************************
 * IP520DevCreate - create a device for a serial port on an IP module
 *
 * This routine creates a device on a specified serial port.  Each port
 * to be used should have exactly one device associated with it by calling
 * this routine.
 *
 * For instance, to create the device "/SBS/0,1/3", with buffer sizes
 * of 512 bytes, the proper calls would be:
 * .CS
 *    if (IP520ModuleInit("232-1", "232", 0x60, 0, 1) != -1) {
 *       char *nam = IP520DevCreate ("/SBS/0,1/3", "232-1", 3, 512, 512);
 * }
 * .CE
 *
 * Calling this routine with a negative port number creates all eight devices.
 *
 * RETURNS: Pointer to device name, or NULL if the driver is not
 * installed, the channel is invalid, or the device already exists.
 *
 * SEE ALSO: IP520Drv()
*/
const char * IP520DevCreate
    (
    char *       name,           /* name to use for this device          */
    const char * moduleID,       /* IP module name                       */
    int          port,           /* port on module for this device [0-7] */
                                 /* (or -1 for all ports)                */
    int          rdBufSize,      /* read buffer size, in bytes (ignored) */
    int          wrtBufSize      /* write buffer size, in bytes (ignored)*/
    )
{
    TY_IP520_DEV *dev;
    MOD_TABLE *pmod = IP520OctalFindQT(moduleID);
    int minor;
    rtems_status_code sc;
    struct termios termios;

    if (!name || !pmod) {
        printf("ERROR: IP520DevCreate() returning NULL\n");
        return NULL;
    }

    if (port < 0) {
        char nameBuf[256];
        for (port = 0 ; port < 8 ; port++) {
            epicsSnprintf(nameBuf, sizeof nameBuf, "%s%d", name, port);
            IP520DevCreate(nameBuf, moduleID, port, rdBufSize, wrtBufSize);
        }
        return name;
    }

    /* if this doesn't represent a valid port, don't do it */
    if (port < 0 || port > 7)
        return NULL;

    minor = ((pmod - IP520Modules) * 8) + port;

    dev = &pmod->dev[port];

    /* if there is a device already on this channel, don't do it */
    if (dev->created)
        return NULL;

    /* initialize the channel hardware (9600-8N1) */
    termios.c_iflag = 0;
    termios.c_oflag = 0;
    termios.c_lflag = 0;
    termios.c_cflag = CS8;
    cfsetispeed(&termios, B9600);
    cfsetospeed(&termios, B9600);
    IP520CallbackSetAttributes(minor, &termios);

    /* initialize dev registers */
    IP520InitChannel(pmod, port);

    /* mark the device as created, and add the device to the I/O system */
    dev->created = 1;

    sc = rtems_io_register_name(name, IP520Major, minor);
    if (sc != RTEMS_SUCCESSFUL) {
        printf("rtems_io_register_name(\"%s\", %d, %d) failed: %s\n",
                    name, (int)IP520Major, minor, rtems_status_text(sc));
        return NULL;
    }

    return name;
}

/******************************************************************************
 *
 * IP520OctalFindQT - Find a named module quadtable
 *
 * NOMANUAL
 */
static MOD_TABLE * IP520OctalFindQT(const char *moduleID)
{
    int mod;

    if (!moduleID)
        return NULL;

    for (mod = 0; mod < IP520LastModule; mod++)
        if (strcmp(moduleID, IP520Modules[mod].moduleID) == 0)
            return &IP520Modules[mod];

    return NULL;
}

/******************************************************************************
 *
 * IP520BaudSet - set channel baud rate
 *
 * NOMANUAL
 */

static int IP520BaudSet(TY_IP520_DEV *dev, int baud)
{
    int rtnstat = 0;
    REGMAP *regs = dev->regs;
    epicsUInt8 llcr, lmcr, dlm, dll;

    if (dev->baud == baud)              /* Any changes? */
        return(rtnstat);                /* No. Exit.    */

    EFROn(regs);
    regs->u.write.lcr = savedlcr;       /* Restore LCR to saved value for following MCR write, but
                                           don't disable writes to enhanced functions (EF's). */
    if (baud == 57600)
        regs->u.write.mcr |=   0x80;    /* Only 57600 requires MCR bit#7 = 1; crystal freq. divide by 4.*/
    else
        regs->u.write.mcr &= ~(0x80);   /* MCR bit#7 = 0; crystal freq. divide by 1. */
    lmcr = regs->u.read.mcr;            /* Read MCR to flush posted writes. */
    EFROff(regs);

    regs->u.write.lcr |= 0x80;          /* Expose DLL/DLM; hide RBR/THR/IER. */
    llcr = regs->u.read.lcr;            /* Read LCR to flush posted writes. */

    switch (baud)
    {
        case 1200:
            dlm = 0x03; /* DLM */
            dll = 0x00; /* DLL */
            break;
        case 2400:
            dlm = 0x01; /* DLM */
            dll = 0x80; /* DLL */
            break;
        case 4800:
            dlm = 0x00; /* DLM */
            dll = 0xC0; /* DLL */
            break;
        case 9600:
            dlm = 0x00; /* DLM */
            dll = 0x60; /* DLL */
            break;
        case 19200:
            dlm = 0x00; /* DLM */
            dll = 0x30; /* DLL */
            break;
        case 38400:
            dlm = 0x00; /* DLM */
            dll = 0x18; /* DLL */
            break;
        case 57600:
            dlm = 0x00; /* DLM */
            dll = 0x04; /* DLL */
            break;
        case 115200:
            dlm = 0x00; /* DLM */
            dll = 0x08; /* DLL */
            break;
        case 230400:
            dlm = 0x00; /* DLM */
            dll = 0x04; /* DLL */
            break;
        default:
            errno = EINVAL;
            rtnstat = -1;
    }

    if (rtnstat != -1)
    {
        regs->u.write.ier = dlm; /* DLM */
        regs->u.write.thr = dll; /* DLL */
        dev->baud = baud;
    }

    regs->u.write.lcr &= ~(0x80); /* Hide DLL/DLM; expose RBR/THR. */
    llcr = regs->u.read.lcr;      /* Read to flush posted writes. */

    return rtnstat;
}

/******************************************************************************
 *
 * IP520InitChannel - initialize a single channel
 *
 * NOMANUAL
 */
static void IP520InitChannel(MOD_TABLE *pmod, int port)
{
    TY_IP520_DEV *dev = &pmod->dev[port];
    REGMAP *regs      = dev->regs;
    int key;
    epicsUInt8 status;

    key = epicsInterruptLock();    /* disable interrupts during init */

    regs->u.write.ier = 0x0;   /* disable interrupts */
    status = regs->u.read.isr; /* clear interrupt status bits */

/*
 * Set up the default port configuration:
 * 9600 baud, no parity, 1 stop bit, 8 bits per char, no flow control
 */
    IP520BaudSet(dev, 9600);
    IP520OptsSet(dev, CS8 | CLOCAL);

#ifdef INCLUDE_IP520_INTERRUPT
    regs->u.write.ier |= 0x05;      /* enable FIFO and Rx interrupts */
    regs->u.write.mcr |= 0x08;      /* enable port interrupts */
#endif

    epicsInterruptUnlock(key);
}

/*
 * TERMIOS callback routines
 */

static int
IP520CallbackSetAttributes(int minor, const struct termios *termios)
{
    static char *fn_nm = "IP520CallbackSetAttributes";

    /* TODO */

    printf("*** Entered %s ***\n", fn_nm);

    return RTEMS_SUCCESSFUL;
}

static int IP520CallbackPollWrite(int minor, const char *buf, int n)
{
    static char *fn_nm = "IP520CallbackPollWrite";

    /* TODO */

    printf("*** Entered %s ***\n", fn_nm);

#if 0
    /* Write */
    for (i = 0; i < n; ++i) {
        my_driver_write_char(e, buf [i]);
    }
#endif

    return n;
}

static int IP520CallbackInterruptWrite(int minor, const char *buf, int n)
{
    MOD_TABLE *qt = &IP520Modules[minor/8];
    TY_IP520_DEV *dev = &qt->dev[minor%8];
    REGMAP *regs = dev->regs;
    int key;

    epicsUInt8 lsr;

    /*
     * Tell the device to transmit some characters from buf (less than
     * or equal to n).  When the device is finished it should raise an
     * interrupt.  The interrupt handler will notify Termios that these
     * characters have been transmitted and this may trigger this write
     * function again.  You may have to store the number of outstanding
     * characters in the device data structure.
     */

    lsr = regs->u.read.lsr;

    if ((lsr & 0x20) && (n > 0)) {
        key = epicsInterruptLock();

        regs->u.write.thr  = *buf;
        regs->u.write.ier |= 0x02;

        epicsInterruptUnlock( key );
    }

    /* Future optimization: write multiple chars (FIFO depth is 64) */

    return 0;
}

static int IP520CallbackPollRead(int minor)
{
    static char *fn_nm = "IP520CallbackPollRead";

    /* TODO */

    printf("*** Entered %s ***\n", fn_nm);

#if 0
    /* Check if a character is available */
    if (my_driver_can_read_char(e)) {
        /* Return the character */
        return my_driver_read_char(e);
    } else {
        /* Return an error status */
        return -1;
    }
#endif
    return -1;
}

/*
 * RTEMS driver points
 */
static rtems_device_driver IP520Open(rtems_device_major_number major,
                                         rtems_device_minor_number minor,
                                         void *arg)
{
    rtems_libio_open_close_args_t *args = (rtems_libio_open_close_args_t *)arg;
    rtems_status_code sc;

    /* interrupt driven */
    static const rtems_termios_callbacks interrupt_callbacks = {
        NULL, /* firstOpen */
        NULL, /* lastClose */
        NULL, /* read */
        IP520CallbackInterruptWrite,
        IP520CallbackSetAttributes,
        NULL, /* stopRemoteTx */
        NULL, /* startRemoteTx */
        TERMIOS_IRQ_DRIVEN
    };

    /* polled */
    static const rtems_termios_callbacks polled_callbacks = {
        NULL, /* firstOpen */
        NULL, /* lastClose */
        IP520CallbackPollRead,
        IP520CallbackPollWrite,
        IP520CallbackSetAttributes,
        NULL, /* stopRemoteTx */
        NULL, /* startRemoteTx */
        TERMIOS_POLLED
    };

#ifdef INCLUDE_IP520_INTERRUPT
    sc = rtems_termios_open(major, minor, arg, &interrupt_callbacks);
#else
    sc = rtems_termios_open(major, minor, arg, &polled_callbacks);
#endif
    (IP520Modules+(minor/8))->dev[minor%8].tyDev = args->iop->data1;
    return sc;
}

static rtems_device_driver IP520Close(rtems_device_major_number major,
                                          rtems_device_minor_number minor,
                                          void *arg)
{
    return rtems_termios_close(arg);
}

static rtems_device_driver IP520Read(rtems_device_major_number major,
                                         rtems_device_minor_number minor,
                                         void *arg)
{
    return rtems_termios_read(arg);
}

static rtems_device_driver IP520Write(rtems_device_major_number major,
                                          rtems_device_minor_number minor,
                                          void *arg)
{
    return rtems_termios_write(arg);
}

static rtems_device_driver IP520Control(rtems_device_major_number major,
                                            rtems_device_minor_number minor,
                                            void *arg)
{
    return rtems_termios_ioctl(arg);
}

/******************************************************************************
 *
 * IP520Drv - initialize the IP520 tty driver
 *
 * This routine connects the driver with RTEMS
 *
 * This routine should be called exactly once, before any reads, writes, or
 * calls to IP520DevCreate().
 *
 * This routine takes as an argument the maximum number of IP modules
 * to support.
 * For example:
 * .CS
 *    int status;
 *    status = IP520Drv(4);
 * .CE
 *
 * RETURNS: 0, or -1 if the driver cannot be installed.
 *
 * SEE ALSO: IP520DevCreate()
*/

int IP520Drv(int maxModules)
{
    rtems_status_code sc;
    static rtems_driver_address_table IP520DriverTable = {
        NULL,       /* initialization */
        IP520Open,
        IP520Close,
        IP520Read,
        IP520Write,
        IP520Control
    };
    if (IP520Major > 0)
        return 0;
    IP520MaxModules = maxModules;
    IP520LastModule = 0;
    IP520Modules = (MOD_TABLE *)calloc(maxModules, sizeof(MOD_TABLE));
    if (!IP520Modules) {
        printf("Memory allocation failed!");
        return -1;
    }
    epicsAtExit(IP520RebootHook, NULL);

    sc = rtems_io_register_driver(0, &IP520DriverTable, &IP520Major);
    if (sc != RTEMS_SUCCESSFUL) {
        printf("Can't register driver: %s\n", rtems_status_text(sc));
        return -1;
    }
    return 0;
}

/******************************************************************************
 *
 * IP520OptsSet - set channel serial options
 *
 * NOMANUAL
 */

static void IP520OptsSet(TY_IP520_DEV * dev, int opts)
{
    REGMAP *regs    = dev->regs;
    epicsUInt8 llcr, lefr, lmcr, lisr, lfcr;
    int mask = (CSIZE | PARENB | PARODD | CLOCAL);

    switch (opts & CSIZE)
    {
        case CS8:
        default:
            llcr = 3;
            break;
    }

    /* default is 1 stop bit */

    /* default is no parity */

    regs->u.write.lcr = llcr;
    llcr = regs->u.read.lcr;    /* Read to flush posted writes. */

    dev->mode = RS232;

    dev->opts = opts & mask;

    lfcr = 0xA1;                    /* Set Rx FIFO trigger level = 60. */

    regs->u.write.fcr  = 0x00;      /* Clear FIFO's. */
    regs->u.write.fcr  = lfcr;      /* Set Rx FIFO trigger level based on baudrate,
                                     * Set Tx FIFO trigger level to 8 charaters. */
    EFROn(regs);
    lefr = regs->u.read.isr;        /* Read EFR.*/

    lefr &= ~(0xC0);                /* Disable RTS/CTS flow control. */

    regs->u.write.fcr = lefr;       /* Write to EFR. */
    lisr = regs->u.read.isr;        /* Read ISR to flush FCR posted writes. */
    EFROff(regs);

    lmcr = regs->u.read.mcr;

    lmcr &= ~(0x02);                /* Set RTS off. */

    regs->u.write.mcr = lmcr;
    lmcr = regs->u.read.mcr;        /* Read to flush posted writes. */
}

/* EFROn - Enable Enhanced Functions */
static void EFROn(REGMAP *regs)
{
    epicsUInt8 llcr, lefr;

    savedlcr = regs->u.read.lcr;        /* Save LCR. */
    regs->u.write.lcr = 0xBF;           /* Expose EFR/Xon-1/Xon-2/Xoff-1/Xoff-2; hide ISR/FCR/MCR/LSR/MSR/SCR. */
    llcr = regs->u.read.lcr;            /* Read LCR to flush posted writes. */
    regs->u.write.fcr |= 0x10;          /* Write to EFR; enable writes to enhanced functions. */
    lefr = regs->u.read.isr;            /* Read EFR to flush posted writes. */
}

/* EFROff - Disable Enhanced Functions */
static void EFROff(REGMAP * regs)
{
    epicsUInt8 llcr, lefr;

    regs->u.write.fcr &= ~(0x10);       /* Write to EFR:4; disable writes to enhanced functions.
                                         * Expose RBR/THR/IER; hide DLL/DLM, AND,
                                         * Expose ISR/FCR/MCR/LSR/MSR/SCR; hide EFR/Xon-1/Xon-2/Xoff-1/Xoff-2. */
    lefr = regs->u.read.isr;            /* Read EFR to flush posted writes. */
    regs->u.write.lcr = savedlcr;       /* Restore LCR to save value. */
    llcr = regs->u.read.lcr;            /* Read LCR to flush posted writes. */
}

/******************************************************************************
 *
 * Command Registration with iocsh
 */

/* IP520Drv */
static const iocshArg IP520DrvArg0 = {"maxModules", iocshArgInt};
static const iocshArg * const IP520DrvArgs[1] = {&IP520DrvArg0};
static const iocshFuncDef IP520DrvFuncDef = {"IP520Drv", 1, IP520DrvArgs};
static void IP520DrvCallFunc(const iocshArgBuf *args)
{
    IP520Drv(args[0].ival);
}

/* IP520Report */
static const iocshFuncDef IP520ReportFuncDef = {"IP520Report", 0, NULL};
static void IP520ReportCallFunc(const iocshArgBuf *args)
{
    IP520Report();
}

/* IP520ModuleInit */
static const iocshArg IP520ModuleInitArg0 = {"moduleID",  iocshArgString};
static const iocshArg IP520ModuleInitArg1 = {"RS<nnn>",   iocshArgString};
static const iocshArg IP520ModuleInitArg2 = {"intVector", iocshArgInt};
static const iocshArg IP520ModuleInitArg3 = {"carrier#",  iocshArgInt};
static const iocshArg IP520ModuleInitArg4 = {"slot",      iocshArgInt};

static const iocshArg * const IP520ModuleInitArgs[5] = {&IP520ModuleInitArg0, &IP520ModuleInitArg1, &IP520ModuleInitArg2,
                                                        &IP520ModuleInitArg3, &IP520ModuleInitArg4};
static const iocshFuncDef IP520ModuleInitFuncDef = {"IP520ModuleInit", 5, IP520ModuleInitArgs};
static void IP520ModuleInitCallFunc(const iocshArgBuf *args)
{
    IP520ModuleInit(args[0].sval, args[1].sval, args[2].ival, args[3].ival, args[4].ival);
}

/* IP520DevCreate */
static const iocshArg IP520DevCreateArg0 = {"devName",   iocshArgString};
static const iocshArg IP520DevCreateArg1 = {"moduleID",  iocshArgString};
static const iocshArg IP520DevCreateArg2 = {"port",      iocshArgInt};
static const iocshArg IP520DevCreateArg3 = {"rdBufSize", iocshArgInt};
static const iocshArg IP520DevCreateArg4 = {"wrBufSize", iocshArgInt};

static const iocshArg * const IP520DevCreateArgs[5] = {&IP520DevCreateArg0, &IP520DevCreateArg1, &IP520DevCreateArg2,
                                                       &IP520DevCreateArg3, &IP520DevCreateArg4};
static const iocshFuncDef IP520DevCreateFuncDef = {"IP520DevCreate", 5, IP520DevCreateArgs};
static void IP520DevCreateCallFunc(const iocshArgBuf *arg)
{
    IP520DevCreate(arg[0].sval, arg[1].sval, arg[2].ival, arg[3].ival, arg[4].ival);
}

static void IP520Registrar(void) {
    iocshRegister(&IP520DrvFuncDef,IP520DrvCallFunc);
    iocshRegister(&IP520ReportFuncDef,IP520ReportCallFunc);
    iocshRegister(&IP520ModuleInitFuncDef,IP520ModuleInitCallFunc);
    iocshRegister(&IP520DevCreateFuncDef,IP520DevCreateCallFunc);
}
epicsExportRegistrar(IP520Registrar);
