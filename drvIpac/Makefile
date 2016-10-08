# Makefile
TOP = ..
include $(TOP)/configure/CONFIG

DBD += drvIpac.dbd

INC += drvIpac.h

HTMLS_DIR = .
HTMLS += index.html
HTMLS += logo101.gif
HTMLS += drvIpac.html
HTMLS += ipacRelease.html

LIBRARY_IOC = Ipac

LIBSRCS += drvIpac.c

# Any VMEbus: VIPC/TVME/XVME carrier drivers
LIBSRCS += drvVipc310.c
LIBSRCS += drvVipc610.c
LIBSRCS += drvVipc616.c
LIBSRCS += drvTvme200.c
LIBSRCS += drvXy9660.c
LIBSRCS += drvHy8002.c

#uTCA carriers supported on Linux-x86
LIBSRCS_Linux += drvHyLinuxCarrier.c
LIBSRCS_Linux += drvTamc220.c
#null stubs so other platforms don't have problems 
LIBSRCS_DEFAULT += nullHyLinuxCarrier.c
LIBSRCS_DEFAULT += nullTamc220.c

# MVME162 & MVME172: IPchip carrier driver
LIBSRCS_vxWorks += drvIpMv162.c

# ISAbus: SBS ATC40 carrier driver (Intel-based systems only!)
LIBSRCS_vxWorks += drvAtc40.c

Ipac_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
