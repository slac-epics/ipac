/*******************************************************************************

Project:
    Stub for non-Linux non-uTCA platforms, to avoid undefined symbols

File:
    nullTamc220.c

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

#ifndef NO_EPICS
#include "epicsExport.h"
#include "iocsh.h"
#endif


static void epicsShareAPI tamc220Registrar(void) {
    return;
}

epicsExportRegistrar(tamc220Registrar);

