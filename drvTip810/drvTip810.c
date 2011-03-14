/*******************************************************************************

Project:
    CAN Bus Driver for EPICS

File:
    drvTip810.c

Description:
    CAN Bus driver for TEWS TIP810 Industry-Pack Module.

Author:
    Andrew Johnson <anjohnson@iee.org>
Created:
    20 July 1995
Version:
    drvTip810.c,v 1.18 2004/12/16 18:56:41 anj Exp

Copyright (c) 1995-2003 Andrew Johnson

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

*******************************************************************************/

#include "drvTip810.h"
#include "drvIpac.h"
#include "pca82c200.h"


#ifdef NO_EPICS
#include <vxWorks.h>
#include <iv.h>
#include <intLib.h>
#include <rebootLib.h>
#include <semLib.h>
#define SEM_TAKE	semTake
#define SEM_FOREVER	NO_WAIT
#include <logLib.h>
#include <sysLib.h>
#include <msgQLib.h>
#include <taskLib.h>
#else
/* OK, this is not really OSI - epics has no message queue :-(
 * we make our life easy by using RTEMS message queues...
 */
#if defined(__vxworks) || defined(__vxworks__) || defined(vxWorks)
#include <vxWorks.h>
#include <msgQLib.h>
#elif defined(__rtems__)
#include <rtems.h>
#define  MSG_Q_ID	rtems_id
#else
#error "no message queue available on this system"
#endif
#include "devLib.h"
#include "epicsMutex.h"
#include "epicsTimer.h"
#include "epicsThread.h"
#include "epicsInterrupt.h"
#include "epicsEvent.h"
#define  semGive(id) \
		epicsEventSignal((id))
#define  SEM_TAKE(id,timeout) \
		(epicsEventWaitOK == ( (errno=-1, (timeout) >= 0.) ? \
			   epicsEventWaitWithTimeout((id),(timeout))   :  \
			   epicsEventWait((id))) ? 0 : -1)
#define  SEM_FOREVER	(-1.)

#ifndef OK
#define OK 0
#endif
#ifndef ERROR
#define ERROR -1
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* Some local magic numbers */
#define T810_MAGIC_NUMBER 81001
#ifdef NO_EPICS
#define RECV_TASK_PRIO 55	/* vxWorks task priority */
#else
#define RECV_TASK_PRIO epicsThreadPriorityMax
#endif
#define RECV_TASK_STACK 20000	/* task stack size */
#define RECV_Q_SIZE 1000	/* Num messages to buffer */

/* These are the IPAC IDs for this module */
#define IP_MANUFACTURER_TEWS 0xb3 
#define IP_MODEL_TEWS_TIP810 0x01


#ifndef NO_EPICS
# include "drvSup.h"
# include "iocsh.h"
# include "epicsExport.h"

/* EPICS Driver Support Entry Table */

struct drvet drvTip810 = {
    2,
    (DRVSUPFUN) t810Report,
    (DRVSUPFUN) t810Initialise
};
epicsExportAddress(drvet, drvTip810);

epicsTimerQueueId canWdTimerQ=0;

#endif /* NO_EPICS */


typedef void callback_t(void *pprivate, long parameter);

typedef struct callbackTable_s {
    struct callbackTable_s *pnext;	/* linked list ... */
    void *pprivate;			/* reference for callback routine */
    callback_t *pcallback;		/* registered routine */
} callbackTable_t;


typedef struct t810Dev_s {
    struct t810Dev_s *pnext;	/* To next device. Must be first member */
    int magicNumber;		/* device pointer confirmation */
    char *pbusName;		/* Bus identification */
    unsigned short card;		/* Industry Pack address */
    unsigned short slot;		/*     "     "      "    */
    unsigned short irqNum;		/* interrupt vector number */
    unsigned int busRate;		/* bit rate of bus in Kbits/sec */
    pca82c200_t *pchip;		/* controller registers */
#ifdef NO_EPICS
    SEM_ID txSem;		/* Transmit buffer protection */
#else
    epicsEventId txSem;
#endif
    unsigned int txCount;		/* messages transmitted */
    unsigned int rxCount;		/* messages received */
    unsigned int overCount;		/* overrun - lost messages */
    unsigned int unusedCount;		/* messages without callback */
    unsigned short unusedId;		/* last ID received without a callback */
    unsigned int errorCount;		/* Times entered Error state */
    unsigned int busOffCount;		/* Times entered Bus Off state */
#ifdef NO_EPICS
    SEM_ID readSem;		/* canRead task Mutex */
#else
    epicsMutexId readSem;
#endif
    canMessage_t *preadBuffer;	/* canRead destination buffer */
#ifdef NO_EPICS
    SEM_ID rxSem;		/* canRead message arrival signal */
#else
    epicsEventId    rxSem;
#endif
    callbackTable_t *pmsgHandler[CAN_IDENTIFIERS];	/* message callbacks */
    callbackTable_t *psigHandler;	/* error signal callbacks */
} t810Dev_t;

typedef struct {
   t810Dev_t *pdevice;
   canMessage_t message;
} t810Receipt_t;


LOCAL t810Dev_t *pt810First = NULL;

int canSilenceErrors = FALSE;	/* Really for EPICS device support use */

LOCAL MSG_Q_ID receiptQueue = 0;
int t810maxQueued = 0;		/* not static so may be reset by operator */

/*******************************************************************************

Routine:
    t810Status

Purpose:
    Return status of given t810 device

Description:
    Returns the status of the t810 device identified by the input parameter, 
    or -1 if not a device ID.

Returns:
    Bit-pattern (0..255) or -1.

*/

int t810Status (
    void *canBusID
) {
    t810Dev_t *pdevice = (t810Dev_t *)canBusID;
    if (canBusID != 0 &&
    	pdevice->magicNumber == T810_MAGIC_NUMBER) {
    	return pdevice->pchip->status;
    } else {
    	return -1;
    }
}


/*******************************************************************************

Routine:
    t810Report

Purpose:
    Report status of all t810 devices

Description:
    Prints a list of all the t810 devices created, their IP carrier &
    slot numbers and the bus name string. For interest > 0 it gives
    additional information about each device.

Returns:
    OK, or
    S_t810_badDevice if device list corrupted.

*/

int t810Report (
    int interest
) {
    t810Dev_t *pdevice = pt810First;
    unsigned short id, printed;
    unsigned char status;

    if (interest > 0) {
	printf("  Receive queue holds %d messages, max %d = %d %% used.\n", 
		RECV_Q_SIZE, t810maxQueued, 
		(100 * t810maxQueued) / RECV_Q_SIZE);
    }

    while (pdevice != NULL) {
	if (pdevice->magicNumber != T810_MAGIC_NUMBER) {
	    printf("t810 device list is corrupt\n");
	    return S_t810_badDevice;
	}

	printf("  '%s' : IP Carrier %hd Slot %hd, Bus rate %d Kbits/sec\n", 
		pdevice->pbusName, pdevice->card, pdevice->slot, 
		pdevice->busRate);

	switch (interest) {
	    case 1:
		printf("\tMessages Sent       : %5d\n", pdevice->txCount);
		printf("\tMessages Received   : %5d\n", pdevice->rxCount);
		printf("\tMessage Overruns    : %5d\n", pdevice->overCount);
		printf("\tDiscarded Messages  : %5d\n", pdevice->unusedCount);
		if (pdevice->unusedCount > 0) {
		    printf("\tLast Discarded ID   : %#5x\n", pdevice->unusedId);
		}
		printf("\tError Interrupts    : %5d\n", pdevice->errorCount);
		printf("\tBus Off Events      : %5d\n", pdevice->busOffCount);
		break;

	    case 2:
		printed = 0;
		printf("\tCallbacks registered: ");
		for (id=0; id < CAN_IDENTIFIERS; id++) {
		    if (pdevice->pmsgHandler[id] != NULL) {
			if (printed % 10 == 0) {
			    printf("\n\t    ");
			}
			printf("0x%-3hx  ", id);
			printed++;
		    }
		}
		if (printed == 0) {
		    printf("None.");
		}
		printf("\n\tcanRead Status : %s\n", 
			pdevice->preadBuffer ? "Active" : "Idle");
		break;

	    case 3:
		printf("    pca82c200 Chip Status:\n");
		status = pdevice->pchip->status;

		printf("\tBus Status             : %s\n", 
			status & PCA_SR_BS ? "Bus-Off" : "Bus-On");
		printf("\tError Status           : %s\n",
			status & PCA_SR_ES ? "Error" : "Ok");
		printf("\tData Overrun           : %s\n",
			status & PCA_SR_DO ? "Overrun" : "Ok");
		printf("\tReceive Status         : %s\n",
			status & PCA_SR_RS ? "Receiving" : "Idle");
		printf("\tReceive Buffer Status  : %s\n",
			status & PCA_SR_RBS ? "Full" : "Empty");
		printf("\tTransmit Status        : %s\n",
			status & PCA_SR_TS ? "Transmitting" : "Idle");
		printf("\tTransmission Complete  : %s\n",
			status & PCA_SR_TCS ? "Complete" : "Incomplete");
		printf("\tTransmit Buffer Access : %s\n",
			status & PCA_SR_TBS ? "Released" : "Locked");
		break;
	}
	pdevice = pdevice->pnext;
    }
    return OK;
}


/*******************************************************************************

Routine:
    t810Create

Purpose:
    Register a new TIP810 device

Description:
    Checks that the given name and card/slot numbers are unique, then
    creates a new device table, initialises it and adds it to the end
    of the linked list.

Returns:
    

Example:
    t810Create "CAN1", 0, 0, 0x60, 500

*/

int t810Create (
    char *pbusName,	/* Unique Identifier for this device */
    unsigned short card,	/* Ipac Driver card .. */
    unsigned short slot,	/* .. and slot number */
    unsigned short irqNum,	/* interrupt vector number */
    unsigned int busRate	/* in Kbits/sec */
) {
    static const struct {
	unsigned int rate;
	unsigned char busTiming0;
	unsigned char busTiming1;
    } rateTable[] = {
	{ 5,    PCA_BTR0_5K,    PCA_BTR1_5K	},
	{ 10,   PCA_BTR0_10K,   PCA_BTR1_10K	},
	{ 20,   PCA_BTR0_20K,   PCA_BTR1_20K	},
	{ 50,   PCA_BTR0_50K,   PCA_BTR1_50K	},
	{ 100,  PCA_BTR0_100K,  PCA_BTR1_100K	},
	{ 125,  PCA_BTR0_125K,  PCA_BTR1_125K	},
	{ 250,  PCA_BTR0_250K,  PCA_BTR1_250K	},
	{ 500,  PCA_BTR0_500K,  PCA_BTR1_500K	},
	{ 1000, PCA_BTR0_1M0,   PCA_BTR1_1M0	},
	{ 1600, PCA_BTR0_1M6,   PCA_BTR1_1M6	},
	{ -125, PCA_KVASER_125K, PCA_BTR1_KVASER},
	{ -250, PCA_KVASER_250K, PCA_BTR1_KVASER},
	{ -500, PCA_KVASER_500K, PCA_BTR1_KVASER},
	{ -1000,PCA_KVASER_1M0,  PCA_BTR1_KVASER},
	{ 0,	0,		0		}
    };
    t810Dev_t *pdevice, *plist = (t810Dev_t *) &pt810First;
    int status, rateIndex, id;

    status = ipmValidate(card, slot, IP_MANUFACTURER_TEWS, 
			 IP_MODEL_TEWS_TIP810);
    if (status) {
	return status;
    }
    /* Slot contains a real TIP810 module */

    if (busRate == 0) {
	return S_t810_badBusRate;
    }
    for (rateIndex = 0; rateTable[rateIndex].rate != busRate; rateIndex++) {
	if (rateTable[rateIndex].rate == 0) {
	    return S_t810_badBusRate;
	}
    }
    /* Bus rate is legal and we now know the right chip settings */

    while (plist->pnext != NULL) {
	plist = plist->pnext;
	if (strcmp(plist->pbusName, pbusName) == 0 || 
	    (plist->card == card && 
	     plist->slot == slot)) {
	    return S_t810_duplicateDevice;
	}
    }
    /* plist now points to the last item in the list */

    pdevice = malloc(sizeof (t810Dev_t));
    if (pdevice == NULL) {
	return errno;
    }
    /* pdevice is our new device table */

    pdevice->pnext       = NULL;
    pdevice->magicNumber = T810_MAGIC_NUMBER;
    pdevice->pbusName    = pbusName;
    pdevice->card        = card;
    pdevice->slot        = slot;
    pdevice->irqNum      = irqNum;
    pdevice->busRate     = busRate;
    pdevice->pchip       = (pca82c200_t *) ipmBaseAddr(card, slot, ipac_addrIO);
    pdevice->preadBuffer = NULL;
    pdevice->psigHandler = NULL;

    for (id=0; id<CAN_IDENTIFIERS; id++) {
	pdevice->pmsgHandler[id] = NULL;
    }

#ifdef NO_EPICS
    pdevice->txSem   = semBCreate(SEM_Q_PRIORITY, SEM_FULL);
    pdevice->rxSem   = semBCreate(SEM_Q_PRIORITY, SEM_EMPTY);
    pdevice->readSem = semMCreate(SEM_Q_PRIORITY | 
				  SEM_INVERSION_SAFE | 
				  SEM_DELETE_SAFE);
#else
    errno = -1;  /* in case mutex/event alloc fails */
    pdevice->txSem   = epicsEventCreate(epicsEventFull);
    pdevice->rxSem   = epicsEventCreate(epicsEventEmpty);
    pdevice->readSem = epicsMutexCreate();
#endif
    if (pdevice->txSem == NULL ||
	pdevice->rxSem == NULL ||
	pdevice->readSem == NULL) {
	free(pdevice);		/* Ought to free those semaphores, but... */
	return errno;
    }

    plist->pnext = pdevice;
    /* device table interface stuff filled in and added to list */

    pdevice->pchip->control        = PCA_CR_RR;	/* Reset state */
    pdevice->pchip->acceptanceCode = 0;
    pdevice->pchip->acceptanceMask = 0xff;
    pdevice->pchip->busTiming0     = rateTable[rateIndex].busTiming0;
    pdevice->pchip->busTiming1     = rateTable[rateIndex].busTiming1;
    pdevice->pchip->outputControl  = PCA_OCR_OCM_NORMAL |
				     PCA_OCR_OCT0_PUSHPULL |
				     PCA_OCR_OCT1_PUSHPULL;
    /* chip now initialised, but held in the Reset state */

    ipmIrqCmd(card, slot, 0, ipac_statActive);
    return OK;
}


/*******************************************************************************

Routine:
    t810Shutdown

Purpose:
    Reboot hook routine

Description:
    Stops interrupts and resets the CAN controller chip.

Returns:
    void

*/

int t810Shutdown (
    int startType
) {
    t810Dev_t *pdevice = pt810First;

    while (pdevice != NULL) {
	if (pdevice->magicNumber != T810_MAGIC_NUMBER) {
	    /* Whoops! */
	    return S_t810_badDevice;
	}

	pdevice->pchip->control = PCA_CR_RR;	/* Reset, interrupts off */
	ipmIrqCmd(pdevice->card, pdevice->slot, 0, ipac_statUnused);
	
	pdevice = pdevice->pnext;
    }
    return OK;
}


/*******************************************************************************

Routine:
    getRxMessage

Purpose:
    Copy a received message from chip to memory

Description:
    Reads a message from the chip receive buffer into the message buffer 
    and flags to chip to release the buffer for further input.

Returns:
    void

*/

LOCAL void getRxMessage (
    pca82c200_t *pchip,
    canMessage_t *pmessage
) {
    unsigned char desc0, desc1, i;

    desc0 = pchip->rxBuffer.descriptor0;
    desc1 = pchip->rxBuffer.descriptor1;

    pmessage->identifier = (desc0 << PCA_MSG_ID0_RSHIFT) |
			   ((desc1 & PCA_MSG_ID1_MASK) >> PCA_MSG_ID1_LSHIFT);
    pmessage->length     = desc1 & PCA_MSG_DLC_MASK;

    if (desc1 & PCA_MSG_RTR) {
	pmessage->rtr = RTR;
    } else {
	pmessage->rtr = SEND;
	for (i=0; i<pmessage->length; i++) {
	    pmessage->data[i] = pchip->rxBuffer.data[i];
	}
    }

    pchip->command = PCA_CMR_RRB;	/* Finished with chip buffer */
}


/*******************************************************************************

Routine:
    putTxMessage

Purpose:
    Copy a message from memory to the chip

Description:
    Copies a message from the message buffer into the chip receive buffer 
    and flags to chip to transmit the message.

Returns:
    void

*/

LOCAL void putTxMessage (
    pca82c200_t *pchip,
    canMessage_t *pmessage
) {
    unsigned char desc0, desc1, i;

    desc0  = pmessage->identifier >> PCA_MSG_ID0_RSHIFT;
    desc1  = (pmessage->identifier << PCA_MSG_ID1_LSHIFT) & PCA_MSG_ID1_MASK;
    desc1 |= pmessage->length & PCA_MSG_DLC_MASK;

    if (pmessage->rtr == SEND) {
	for (i=0; i<pmessage->length; i++) {
	    pchip->txBuffer.data[i] = pmessage->data[i];
	}
    } else {
	desc1 |= PCA_MSG_RTR;
    }

    pchip->txBuffer.descriptor0 = desc0;
    pchip->txBuffer.descriptor1 = desc1;

    pchip->command = PCA_CMR_TR;
}


/*******************************************************************************

Routine:
    doCallbacks

Purpose:
    calls all routines in the given list

Description:
    

Returns:
    void

*/

LOCAL void doCallbacks (
    callbackTable_t *phandler,
    long parameter
) {
    while (phandler != NULL) {
	(*phandler->pcallback)(phandler->pprivate, parameter);
	phandler = phandler->pnext;
    }
}


/*******************************************************************************

Routine:
    t810ISR

Purpose:
    Interrupt Service Routine

Description:
    

Returns:
    void

*/

LOCAL void t810ISR (
    t810Dev_t *pdevice
) {
    unsigned char intSource = pdevice->pchip->interrupt;

    /* disable interrupts for this carrier and slot */
    if (ipmIrqCmd(pdevice->card, pdevice->slot, 0,
                  ipac_irqDisable) == S_IPAC_badAddress) {    
#ifdef NO_EPICS
      logMsg("t810ISR: Error in card or slot number\n", 0, 0, 0, 0, 0, 0);
#else
      epicsInterruptContextMessage("t810ISR: Error in card or slot number");
#endif
    }
    
    if (intSource & PCA_IR_OI) {		/* Overrun Interrupt */
        pdevice->overCount++;
        canBusStop(pdevice->pbusName);		/* Reset the chip but not */
        canBusRestart(pdevice->pbusName);	/* all the counters */

	intSource = pdevice->pchip->interrupt;	/* Rescan interrupts */
    }

    if (intSource & PCA_IR_RI) {		/* Receive Interrupt */
        t810Receipt_t qmsg;

	/* Take a local copy of the message */
        qmsg.pdevice = pdevice;
	getRxMessage(pdevice->pchip, &qmsg.message);

        /* Send it to the servicing task */
#ifdef __rtems__
		if (RTEMS_SUCCESSFUL !=
			rtems_message_queue_send(receiptQueue, &qmsg, sizeof(t810Receipt_t)))
#else
        if (msgQSend(receiptQueue, (char *)&qmsg, sizeof(t810Receipt_t), 
                     NO_WAIT, MSG_PRI_NORMAL) == ERROR)
#endif
#ifdef NO_EPICS
           logMsg("Warning: CANbus receive queue overflow\n", 0, 0, 0, 0, 0, 0);
#else
	       epicsInterruptContextMessage("Warning: CANbus receive queue overflow");
#endif
           
    }

    if (intSource & PCA_IR_EI) {		/* Error Interrupt */
	callbackTable_t *phandler = pdevice->psigHandler;
	unsigned short status;

	switch (pdevice->pchip->status & (PCA_SR_ES | PCA_SR_BS)) {
	    case PCA_SR_ES:
		status = CAN_BUS_ERROR;
		pdevice->errorCount++;
#ifdef NO_EPICS
                logMsg("t810ISR: CANbus error event\n", 0, 0, 0, 0, 0, 0);
#elif DOMESSAGES
	        epicsInterruptContextMessage("t810ISR: CANbus error event");
#endif
		break;
	    case PCA_SR_BS:
	    case PCA_SR_BS | PCA_SR_ES:
		status = CAN_BUS_OFF;
		pdevice->busOffCount++;
		semGive(pdevice->txSem);		/* Release transmit */
                /*pdevice->pchip->control &= ~PCA_CR_RR;*/	/* Clear Reset state */
#ifdef NO_EPICS
                logMsg("t810ISR: CANbus off event\n", 0, 0, 0, 0, 0, 0);
#elif DOMESSAGES 
	        epicsInterruptContextMessage("t810ISR: CANbus off event");
#endif
		break;
	    default:
		status = CAN_BUS_OK;
#ifdef NO_EPICS
                logMsg("t810ISR: CANbus error event\n", 0, 0, 0, 0, 0, 0);
#elif DOMESSAGES
	        epicsInterruptContextMessage("t810ISR: CANbus OK");
#endif
		break;
	}

	doCallbacks(phandler, status);
    }

    if (intSource & PCA_IR_TI) {		/* Transmit Interrupt */
	pdevice->txCount++;
	semGive(pdevice->txSem);
    }

    if (intSource & PCA_IR_WUI) {		/* Wake-up Interrupt */
#ifdef NO_EPICS
	logMsg("Wake-up Interrupt from CANbus '%s'\n", 
	       (int) pdevice->pbusName, 0, 0, 0, 0, 0);
#else
	epicsInterruptContextMessage("Wake-up Interrupt from CANbus");
#endif
    }
    
    /* Clear and Enable Interrupt from Carrier Board Registers */
    if (ipmIrqCmd(pdevice->card, pdevice->slot, 0,
                  ipac_irqClear) == S_IPAC_badAddress) {    
#ifdef NO_EPICS
      logMsg("t810ISR: Error in card or slot number\n", 0, 0, 0, 0, 0, 0);
#else
      epicsInterruptContextMessage("t810ISR: Error in card or slot number");
#endif
    }
}


/*******************************************************************************

Routine:
    t810RecvTask

Purpose:
    Receive task

Description:
    This routine is a background task started by t810Initialise. It
    takes messages out of the receive queue one by one and runs the
    callbacks registered against the relevent message ID.

Returns:
    int

*/

LOCAL int t810RecvTask() {
   t810Receipt_t rmsg;
   callbackTable_t *phandler;
   int numQueued;

   if (receiptQueue == 0) {
      fprintf(stderr, "CANbus Receive queue does not exist, task exiting.\n");
      return ERROR;
   }
   printf("CANbus receive task started\n");

   while (TRUE) {
#ifdef __rtems__
	  rtems_message_queue_get_number_pending(
					  receiptQueue,
					  &numQueued);
#else
      numQueued = msgQNumMsgs(receiptQueue); 
#endif
      if (numQueued > t810maxQueued) t810maxQueued = numQueued;
      
#ifdef __rtems__
	  { rtems_unsigned32 s=sizeof(t810Receipt_t);
	  rtems_message_queue_receive(
					  receiptQueue,
					  &rmsg,
					  &s,
					  RTEMS_WAIT,
					  RTEMS_NO_TIMEOUT); /* wait forever */
	  }
#else
      msgQReceive(receiptQueue, (char *)&rmsg, sizeof(t810Receipt_t), 
      		  WAIT_FOREVER);
#endif
      rmsg.pdevice->rxCount++;
      
      /* Look up the message ID and do the message callbacks */
      phandler = rmsg.pdevice->pmsgHandler[rmsg.message.identifier];
      if (phandler == NULL) {
          rmsg.pdevice->unusedId = rmsg.message.identifier;
          rmsg.pdevice->unusedCount++;
      } else {
          doCallbacks(phandler, (long) &rmsg.message);
      }

      /* If canRead is waiting for this ID, give it the message and kick it */
      if (rmsg.pdevice->preadBuffer != NULL &&
          rmsg.pdevice->preadBuffer->identifier == rmsg.message.identifier) {
          memcpy(rmsg.pdevice->preadBuffer, &rmsg.message, 
                 sizeof(canMessage_t));
          rmsg.pdevice->preadBuffer = NULL;
          semGive(rmsg.pdevice->rxSem);
      }
   }
}

/*******************************************************************************

Routine:
    t810Initialise

Purpose:
    Initialise driver and all registered hardware

Description:
    Under EPICS this routine is called by iocInit, which must occur
    after all t810Create calls in the startup script.  It completes the
    initialisation of the CAN controller chip and interrupt vector
    registers for all known TIP810 devices and starts the chips
    running.  The receive queue is created and its processing task is
    started to handle incoming data.  A reboot hook is used to make
    sure all interrupts are turned off if the OS is shut down.

Returns:
    int

*/

int t810Initialise (
    void
) {
    t810Dev_t *pdevice = pt810First;
    int status = OK, err;

#ifdef NO_EPICS
    rebootHookAdd(t810Shutdown);
#endif

#ifdef __rtems__
	if (RTEMS_SUCCESSFUL != rtems_message_queue_create(
								rtems_build_name('C','a','n','Q'),
								RECV_Q_SIZE,
								sizeof(t810Receipt_t),
								RTEMS_FIFO,
								&receiptQueue))
			return -1;
#else
    receiptQueue = msgQCreate(RECV_Q_SIZE, sizeof(t810Receipt_t), MSG_Q_FIFO);
#endif
    if (
#ifndef __rtems__
		(receiptQueue == NULL) ||
#endif
#if defined(NO_EPICS) || defined(TASK_TODO)
	taskSpawn("canRecvTask", RECV_TASK_PRIO, VX_FP_TASK, RECV_TASK_STACK, 
		  t810RecvTask, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == ERROR
#else
	/* initialize our watchdog timer queue */
	( (errno=-1), ! (canWdTimerQ=epicsTimerQueueAllocate(
				1, /* ok to share queue */
				epicsThreadPriorityLow) )) ||
	0==epicsThreadCreate(
			"canRecvTask",
			RECV_TASK_PRIO,
			RECV_TASK_STACK,
			(EPICSTHREADFUNC)t810RecvTask,
			0)
#endif
	) {
	return errno;
    }

    while (pdevice != NULL) {
	pdevice->txCount     = 0;
	pdevice->rxCount     = 0;
	pdevice->overCount   = 0;
	pdevice->unusedCount = 0;
	pdevice->errorCount  = 0;
	pdevice->busOffCount = 0;

	/* Hmm - I thought that we should use the carrier driver routine here ??
     * T.S. 3/2002
	 */
#if defined(IRQ_TODO)
	if (intConnect(INUM_TO_IVEC((int)pdevice->irqNum), t810ISR, (int)pdevice)) {
	    status = errno;
	}
#else
	if ((err=ipmIntConnect(pdevice->card, pdevice->slot, pdevice->irqNum, (void(*)())t810ISR, (int)pdevice))) {
		status = err;
	}
#endif
        pdevice->pchip->irqNum = pdevice->irqNum;

	ipmIrqCmd(pdevice->card, pdevice->slot, 0, ipac_irqEnable);

	pdevice->pchip->control = PCA_CR_OIE |
				  PCA_CR_EIE |
				  PCA_CR_TIE |
				  PCA_CR_RIE;

	pdevice = pdevice->pnext;
    }
    return status;
}


/*******************************************************************************

Routine:
    canOpen

Purpose:
    Return device pointer for given CAN bus name

Description:
    Searches through the linked list of known t810 devices for one
    which matches the name given, and returns the device pointer
    associated with the relevant device table.

Returns:
    OK, or S_can_noDevice if no match found.

Example:
    void *can1;
    status = canOpen("CAN1", &can1);

*/

int canOpen (
    const char *pbusName,
    void **ppdevice
) {
    t810Dev_t *pdevice = pt810First;

    while (pdevice != NULL) {
	if (strcmp(pdevice->pbusName, pbusName) == 0) {
	    *ppdevice = pdevice;
	    return OK;
	}
	pdevice = pdevice->pnext;
    }
    return S_can_noDevice;
}


/*******************************************************************************

Routine:
    canBusReset

Purpose:
    Reset named CANbus

Description:
    Resets the chip and connected to the named bus and all counters

Returns:
    OK, or S_can_noDevice if no match found.

Example:
    status = canBusReset("CAN1");

*/

int canBusReset (
    const char *pbusName
) {
    t810Dev_t *pdevice;
    int status = canOpen(pbusName, (void **) &pdevice);
    
    if (status) return status;
    
    pdevice->pchip->control |=  PCA_CR_RR;    /* Reset the chip */
    pdevice->txCount   = 0;
    pdevice->rxCount   = 0;
    pdevice->overCount   = 0;
    pdevice->unusedCount = 0;
    pdevice->errorCount  = 0;
    pdevice->busOffCount = 0;
    semGive(pdevice->txSem);
    pdevice->pchip->control = PCA_CR_OIE |
			    PCA_CR_EIE |
			    PCA_CR_TIE |
			    PCA_CR_RIE;

    return OK;
}


/*******************************************************************************

Routine:
    canBusStop

Purpose:
    Stop I/O on named CANbus

Description:
    Holds the chip for the named bus in Reset state

Returns:
    OK, or S_can_noDevice if no match found.

Example:
    status = canBusStop("CAN1");

*/

int canBusStop (
    const char *pbusName
) {
    t810Dev_t *pdevice;
    int status = canOpen(pbusName, (void **) &pdevice);
    
    if (status) return status;
    
    pdevice->pchip->control |=  PCA_CR_RR;    /* Reset the chip */
    return OK;
}


/*******************************************************************************
 
Routine:
    canBusRestart

Purpose: 
    Restart I/O on named CANbus

Description:
    Restarts the chip for the named bus after a canBusStop

Returns: 
    OK, or S_can_noDevice if no match found.

Example: 
    status = canBusRestart("CAN1");

*/

int canBusRestart (
    const char *pbusName
) {
    t810Dev_t *pdevice;
    int status = canOpen(pbusName, (void **) &pdevice);
    
    if (status) return status;
    
    semGive(pdevice->txSem);
    pdevice->pchip->control = PCA_CR_OIE |
			    PCA_CR_EIE |
			    PCA_CR_TIE |
			    PCA_CR_RIE;

    return OK;
}


/*******************************************************************************

Routine:
    strdupn

Purpose:
    duplicate n characters of a string and return pointer to new substring

Description:
    Copies n characters from the input string to a newly malloc'ed memory
    buffer, and adds a trailing '\0', then returns the new string pointer.

Returns:
    char *newString, or NULL if malloc failed.

*/

LOCAL char* strdupn (
    const char *ct,
    size_t n
) {
    char *duplicate;

    duplicate = malloc(n+1);
    if (duplicate == NULL) {
	return NULL;
    }

    memcpy(duplicate, ct, n);
    duplicate[n] = '\0';

    return duplicate;
}


/*******************************************************************************

Routine:
    canIoParse

Purpose:
    Parse a CAN address string into a canIo_t structure

Description:
    canString which must match the format below is converted by this routine
    into the relevent fields of the canIo_t structure pointed to by pcanIo:

    	busname{/timeout}:id{+n}{.offset} parameter
    
    where
    	busname is alphanumeric, all other fields are hex, decimal or octal
    	timeout is in milliseconds
	id and any number of +n components are summed to give the CAN Id
	offset is the byte offset into the message
	parameter is a string or integer for use by device support

Returns:
    OK, or
    S_can_badAddress for illegal input strings,
    S_can_noDevice for an unregistered bus name.

Example:
    canIoParse("CAN1/20:0126+4+1.4 0xfff", &myIo);

*/

int canIoParse (
    char *canString, 
    canIo_t *pcanIo
) {
    char separator;
    char *name;

    pcanIo->canBusID = NULL;

    if (canString == NULL ||
	pcanIo == NULL) {
	return S_can_badAddress;
    }

    /* Get rid of leading whitespace and non-alphanumeric chars */
    while (!isalnum(*canString)) {
	if (*canString++ == '\0') {
	    return S_can_badAddress;
	}
    }

    /* First part of string is the bus name */
    name = canString;

    /* find the end of the busName */
    canString = strpbrk(canString, "/:");
    if (canString == NULL ||
	*canString == '\0') {
	return S_can_badAddress;
    }

    /* now we're at character after the end of the busName */
    pcanIo->busName = strdupn(name, canString - name);
    if (pcanIo->busName == NULL) {
	return errno;
    }
    separator = *canString++;

    /* Handle /<timeout> if present, convert from ms to ticks */
    if (separator == '/') {
#if defined(NO_EPICS) || defined(TODO)
	pcanIo->timeout = strtol(canString, &canString, 0) * sysClkRateGet();
	pcanIo->timeout = ((pcanIo->timeout + 500) / 1000);
#else
	/* leave timeout in s */
	pcanIo->timeout = ((double)strtol(canString, &canString, 0))/1000.;
#endif
	separator = *canString++;
    } else {
#ifdef NO_EPICS
	pcanIo->timeout = WAIT_FOREVER;
#else
	pcanIo->timeout = -1.;
#endif
    }

    /* String must contain :<canID> */
    if (separator != ':') {
	return S_can_badAddress;
    }
    pcanIo->identifier = strtoul(canString, &canString, 0);
    separator = *canString++;
    
    /* Handle any number of optional +<n> additions to the ID */
    while (separator == '+') {
    	pcanIo->identifier += strtol(canString, &canString, 0);
	separator = *canString++;
    }

    /* Handle .<offset> if present */
    if (separator == '.') {
	pcanIo->offset = strtoul(canString, &canString, 0);
	if (pcanIo->offset >= CAN_DATA_SIZE) {
	    return S_can_badAddress;
	}
	separator = *canString++;
    } else {
	pcanIo->offset = 0;
    }

    /* Final parameter is separated by whitespace */
    if (separator != ' ' &&
	separator != '\t') {
	return S_can_badAddress;
    }
    pcanIo->parameter = strtol(canString, &canString, 0);
    pcanIo->paramStr = strdupn(canString, strlen(canString));
    if (pcanIo->paramStr == NULL) {
	return errno;
    }

    /* Ok, finally look up the bus name */
    return canOpen(pcanIo->busName, &pcanIo->canBusID);
}


/*******************************************************************************

Routine:
    canWrite

Purpose:
    writes a CAN message to the bus

Description:
    Sends the message described by pmessage out through the bus identified by
    canBusID.  After some simple argument checks it obtains exclusive access to
    the transmit registers, then copies the message to the chip.  The timeout
    value allows task recovery in the event that exclusive access is not
    available within a the given number of vxWorks clock ticks.

Returns:
    OK, 
    S_can_badMessage for bad identifier, message length or rtr value,
    S_can_badDevice for bad device pointer,
    S_objLib_OBJ_TIMEOUT indicates timeout,
    S_t810_transmitterBusy indicates an internal error.

Example:
    

*/

int canWrite (
    void *canBusID,
    canMessage_t *pmessage,
    TimeOut timeout
) {
    t810Dev_t *pdevice = (t810Dev_t *) canBusID;
    int status;

    if (pdevice->magicNumber != T810_MAGIC_NUMBER) {
	return S_t810_badDevice;
    }

    if (pmessage->identifier >= CAN_IDENTIFIERS ||
	pmessage->length > CAN_DATA_SIZE ||
	(pmessage->rtr != SEND && pmessage->rtr != RTR)) {
	return S_can_badMessage;
    }

    status = SEM_TAKE(pdevice->txSem, timeout);
    if (status) {
	return errno;
    }

    if (pdevice->pchip->status & PCA_SR_TBS) {
	putTxMessage(pdevice->pchip, pmessage);
	return OK;
    } else {
	semGive(pdevice->txSem);
	return S_t810_transmitterBusy;
    }
}


/*******************************************************************************

Routine:
    canMessage

Purpose:
    Register CAN message callback

Description:
    Adds a new callback routine for the given CAN message ID on the
    given device.  There can be any number of callbacks for the same ID,
    and all are called in turn when a message with this ID is
    received.  As a result, the callback routine must not change the
    message at all - it is only permitted to examine it.  The callback
    is called from vxWorks Interrupt Context, thus there are several
    restrictions in what the routine can perform (see vxWorks User
    Guide for details of these).  The callback routine should be
    declared of type canMsgCallback_t
	void callback(void *pprivate, can_Message_t *pmessage); 
    The pprivate value supplied to canMessage is passed to the callback
    routine with each message to allow it to identify its context.

Returns:
    OK, 
    S_can_badMessage for bad identifier or NULL callback routine,
    S_t810_badDevice for bad device pointer.

Example:
    

*/

int canMessage (
    void *canBusID,
    unsigned short identifier,
    canMsgCallback_t *pcallback,
    void *pprivate
) {
    t810Dev_t *pdevice = (t810Dev_t *) canBusID;
    callbackTable_t *phandler, *plist;

    if (pdevice->magicNumber != T810_MAGIC_NUMBER) {
	return S_t810_badDevice;
    }

    if (identifier >= CAN_IDENTIFIERS ||
	pcallback == NULL) {
	return S_can_badMessage;
    }

    phandler = malloc(sizeof (callbackTable_t));
    if (phandler == NULL) {
	return errno;
    }

    phandler->pnext     = NULL;
    phandler->pprivate  = pprivate;
    phandler->pcallback = (callback_t *) pcallback;

    plist = (callbackTable_t *) (&pdevice->pmsgHandler[identifier]);
    while (plist->pnext != NULL) {
	plist = plist->pnext;
    }
    /* plist now points to the last handler in the list */

    plist->pnext = phandler;
    return OK;
}


/*******************************************************************************

Routine:
    canMsgDelete

Purpose:
    Delete registered CAN message callback

Description:
    Deletes an existing callback routine for the given CAN message ID
    on the given device.  The first matching callback found in the list
    is deleted.  To match, the parameters to canMsgDelete must be
    identical to those given to canMessage.

Returns:
    OK, 
    S_can_badMessage for bad identifier or NULL callback routine,
    S_can_noMessage for no matching message callback,
    S_t810_badDevice for bad device pointer.

Example:
    

*/

int canMsgDelete (
    void *canBusID,
    unsigned short identifier,
    canMsgCallback_t *pcallback,
    void *pprivate
) {
    t810Dev_t *pdevice = (t810Dev_t *) canBusID;
    callbackTable_t *phandler, *plist;

    if (pdevice->magicNumber != T810_MAGIC_NUMBER) {
	return S_t810_badDevice;
    }

    if (identifier >= CAN_IDENTIFIERS ||
	pcallback == NULL) {
	return S_can_badMessage;
    }

    plist = (callbackTable_t *) (&pdevice->pmsgHandler[identifier]);
    while (plist->pnext != NULL) {
    	phandler = plist->pnext;
    	if (((canMsgCallback_t *)phandler->pcallback == pcallback) &&
    	    (phandler->pprivate  == pprivate)) {
    	    plist->pnext = phandler->pnext;
    	    phandler->pnext = NULL;		/* Just in case... */
    	    free(phandler);
    	    return OK;
    	}
	plist = phandler;
    }

    return S_can_noMessage;
}


/*******************************************************************************

Routine:
    canSignal

Purpose:
    Register CAN error signal callback

Description:
    Adds a new callback routine for the CAN error reports.  There can be
    any number of error callbacks, and all are called in turn when the
    controller chip reports an error or bus Off The callback is called
    from vxWorks Interrupt Context, thus there are restrictions in what
    the routine can perform (see vxWorks User Guide for details of
    these).  The callback routine should be declared a canSigCallback_t
	void callback(void *pprivate, unsigned short status);
    The pprivate value supplied to canSignal is passed to the callback
    routine with the error status to allow it to identify its context.
    Status values will be one of
	CAN_BUS_OK,
	CAN_BUS_ERROR or
	CAN_BUS_OFF.
    If the chip goes to the Bus Off state, the driver will attempt to
    restart it.

Returns:
    OK, 
    S_t810_badDevice for bad device pointer.

Example:
    

*/

int canSignal (
    void *canBusID,
    canSigCallback_t *pcallback,
    void *pprivate
) {
    t810Dev_t *pdevice = (t810Dev_t *) canBusID;
    callbackTable_t *phandler, *plist;

    if (pdevice->magicNumber != T810_MAGIC_NUMBER) {
	return S_t810_badDevice;
    }

    phandler = malloc(sizeof (callbackTable_t));
    if (phandler == NULL) {
	return errno;
    }

    phandler->pnext     = NULL;
    phandler->pprivate  = pprivate;
    phandler->pcallback = (callback_t *) pcallback;

    plist = (callbackTable_t *) (&pdevice->psigHandler);
    while (plist->pnext != NULL) {
	plist = plist->pnext;
    }
    /* plist now points to the last handler in the list */

    plist->pnext = phandler;
    return OK;
}


/*******************************************************************************

Routine:
    canRead

Purpose:
    read incoming CAN message, any ID number

Description:
    The simplest way to implement this is have canRead take a message
    ID in the buffer, send an RTR and look for the returned value of
    this message.  This is in keeping with the CAN philosophy and makes
    it useful for simple software interfaces.  More complex ones ought
    to use the canMessage callback functions.

Returns:
    OK, or
    S_t810_badDevice for bad bus ID, 
    S_can_badMessage for bad message Identifier or length,
    S_objLib_OBJ_TIMEOUT for timeout

Example:
    canMessage_t myBuffer = {
	139,	// Can ID
	0,	// RTR
	4	// Length
    };
    int status = canRead(canID, &myBuffer, WAIT_FOREVER);

*/

int canRead (
    void *canBusID,
    canMessage_t *pmessage,
    TimeOut timeout
) {
    t810Dev_t *pdevice = (t810Dev_t *) canBusID;
    int status;

    if (pdevice->magicNumber != T810_MAGIC_NUMBER) {
	return S_t810_badDevice;
    }

    if (pmessage->identifier >= CAN_IDENTIFIERS ||
	pmessage->length > CAN_DATA_SIZE) {
	return S_can_badMessage;
    }

    /* This semaphore is so only one task canRead simultaneously */
#ifdef NO_EPICS
    status = semTake(pdevice->readSem, timeout);
    if (status) {
	return errno;
    }
#else
    if (timeout < 0.0)
    {
      if (epicsMutexLockOK != epicsMutexLock(pdevice->readSem)) return -1;
    }
    else
    {
      TimeOut timeElapsed = 0;
      epicsMutexLockStatus lockStatus;
      while(1) {
        lockStatus = epicsMutexTryLock(pdevice->readSem);
        if (lockStatus == epicsMutexLockOK) break;
        if ((epicsMutexLockError == lockStatus) || (timeElapsed >= timeout)) return -1;
        epicsThreadSleep(.05);
        timeElapsed += .05;
      }
    }
#endif

    pdevice->preadBuffer = pmessage;

    /* All set for the reply, now send the request */
    pmessage->rtr = RTR;

    status = canWrite(canBusID, pmessage, timeout);
    if (status == OK) {
	/* Wait for the message to be recieved */
	status = SEM_TAKE(pdevice->rxSem, timeout);
	if (status) {
	    status = errno;
	}
    }
    if (status) {
	/* Problem (timeout) sending the RTR or receiving the reply */
	pdevice->preadBuffer = NULL;
	SEM_TAKE(pdevice->rxSem, SEM_FOREVER);	/* Must leave this EMPTY */
    }
#ifdef NO_EPICS
    semGive(pdevice->readSem);
#else
    epicsMutexUnlock(pdevice->readSem);
#endif
    return status;
}


/*******************************************************************************

Routine:
    canTest

Purpose:
    Test routine, sends a single message to the named bus.

Description:
    This routine is intended for use from the vxWorks shell.

Returns:
    Ok, or ERROR

Example:
    

*/

int canTest (
    char *pbusName,
    unsigned short identifier,
    unsigned short rtr,
    unsigned char length,
    char *data
) {
    void *canBusID;
    canMessage_t message;
    int status;

    if (pbusName == NULL) {
	printf("Usage: canTest \"busname\", id, rtr, len, \"data\"\n");
	return ERROR;
    }

    status = canOpen(pbusName, &canBusID);
    if (status) {
	printf("Error %d opening CAN bus '%s'\n", status, pbusName);
	return ERROR;
    }

    message.identifier = identifier;
    message.rtr        = rtr ? RTR : SEND;
    message.length     = length;

    if (rtr == 0) {
	memcpy(&message.data[0], data, length);
    }

    status = canWrite(canBusID, &message, 0);
    if (status) {
	printf("Error %d writing message\n", status);
	return ERROR;
    }
    return OK;
}


/*******************************************************************************
* EPICS iocsh Command registry
*/

#ifndef NO_EPICS

/* t810Report(int interest) */
static const iocshArg t810ReportArg0 = {"interest", iocshArgInt};
static const iocshArg * const t810ReportArgs[1] = {&t810ReportArg0};
static const iocshFuncDef t810ReportFuncDef =
    {"t810Report",1,t810ReportArgs};
static void t810ReportCallFunc(const iocshArgBuf *args)
{
    t810Report(args[0].ival);
}

/* t810Create(char *pbusName, int card, int slot, int irqNum, int busRate) */
static const iocshArg t810CreateArg0 = {"busName",iocshArgPersistentString};
static const iocshArg t810CreateArg1 = {"carrier", iocshArgInt};
static const iocshArg t810CreateArg2 = {"slot", iocshArgInt};
static const iocshArg t810CreateArg3 = {"intVector", iocshArgInt};
static const iocshArg t810CreateArg4 = {"busRate", iocshArgInt};
static const iocshArg * const t810CreateArgs[5] = {
    &t810CreateArg0, &t810CreateArg1, &t810CreateArg2, &t810CreateArg3,
    &t810CreateArg4};
static const iocshFuncDef t810CreateFuncDef =
    {"t810Create",5,t810CreateArgs};
static void t810CreateCallFunc(const iocshArgBuf *arg)
{
    t810Create(arg[0].sval, arg[1].ival, arg[2].ival, arg[3].ival, 
	       arg[4].ival);
}

static void drvTip810Registrar(void) {
    iocshRegister(&t810ReportFuncDef,t810ReportCallFunc);
    iocshRegister(&t810CreateFuncDef,t810CreateCallFunc);
}
epicsExportRegistrar(drvTip810Registrar);

#endif
