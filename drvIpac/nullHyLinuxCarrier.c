/*******************************************************************************

Project:
    Stub for non-Linux non-uTCA platforms, to avoid undefined symbols

File:
    drvHyLinuxCarrier.c

Description:
    The driver is defined in the dbd file, which will cause problems if
    there is no function built for a particular platform.

Author:
    Oringinal developer:   Jim Chen, HyTec Electronics Ltd,
    Reading, Berks., UK
    http://www.hytec-electronics.co.uk
    The code is based on drvHy8002 code which is also based on
	Andrew Johnson's <anjohnson@iee.org> drvTvme200.c.
Created:
    27/10/2010 first version.

Version:
    $Id: drvHyLinuxCarrier.c,v 1.2 2014/10/08 23:22:57 rdabney Exp $

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
#include "iocsh.h"

#include <drvIpac.h>

static void epicsShareAPI HyLinux9010Registrar(void) {
    return;
}

epicsExportRegistrar(HyLinux9010Registrar);
