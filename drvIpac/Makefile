# Makefile
TOP = ..
include $(TOP)/configure/CONFIG

DBD += drvIpac.dbd
DBD += drvApcie8650.dbd

INC += drvIpac.h
INC += drvApcie8650.h

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
LIBSRCS_Linux += drvApcie8650.c
#null stubs so other platforms don't have problems 
LIBSRCS_DEFAULT += nullHyLinuxCarrier.c
LIBSRCS_DEFAULT += nullTamc220.c

# MVME162 & MVME172: IPchip carrier driver (68k only)
LIBSRCS_vxWorks-68040 += drvIpMv162.c

# ISAbus: SBS ATC40 carrier driver (Intel only)
LIBSRCS_vxWorks-pentium += drvAtc40.c

LIBSRCS_vxWorks += $(LIBSRCS_$(T_A))

Ipac_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
