# Makefile
TOP = ..
include $(TOP)/configure/CONFIG

DBD += drvIpac.dbd
DBD += drv8002.dbd
DBD += drvXy9660.dbd
DBD += drvApcie8650.dbd

INC += drvIpac.h
INC += drvXy9660.h
INC += xipIo.h

HTMLS_DIR = .
HTMLS += index.html
HTMLS += logo101.gif
HTMLS += drvIpac.html
HTMLS += ipacRelease.html

LIBRARY_IOC_RTEMS   = Ipac
LIBRARY_IOC_vxWorks = Ipac
LIBRARY_IOC = Ipac

# Disable hotswap option in drvHy8002.c
USR_CFLAGS += -Wshadow -Wpointer-arith -Wbad-function-cast -Wno-unused
USR_CFLAGS += -Wredundant-decls -Wnested-externs -Winline
USR_CFLAGS += -D__NO_HOTSWAP__

USR_CFLAGS_RTEMS += -DIPAC_FORCELINK_TAB

LIBSRCS += drvIpac.c

ifeq ($(TA), RTEMS-beatnik)
# Any VMEbus: SBS VIPC carrier drivers
LIBSRCS += drvVipc310.c
LIBSRCS += drvVipc610.c
LIBSRCS += drvVipc616.c
LIBSRCS_vxWorks += drvTvme200.c

# Hytec VME bus carrier driver
LIBSRCS += drvHy8002.c

# Xycom (and acromag) VME bus carrier driver
LIBSRCS += drvXy9660.c
endif

ifeq ($(TA), vxWorks)

# MVME162 & MVME172: IPchip carrier driver
LIBSRCS_vxWorks += drvIpMv162.c

# ISAbus: SBS ATC40 carrier driver (Intel-based systems only!)
LIBSRCS_vxWorks += drvAtc40.c
endif

# Acromag APCIE8650 PCIE IPAC carrier
LIBSRCS += drvApcie8650.c


include $(TOP)/configure/RULES
