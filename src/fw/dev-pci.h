#ifndef _PCI_CAP_H
#define _PCI_CAP_H

#include "types.h"

/*

QEMU-specific vendor(Red Hat)-specific capability.
It's intended to provide some hints for firmware to init PCI devices.

Its structure is shown below:

Header:

u8 id;       Standard PCI Capability Header field
u8 next;     Standard PCI Capability Header field
u8 len;      Standard PCI Capability Header field
u8 type;     Red Hat vendor-specific capability type:
               now only REDHAT_CAP_TYP_QEMU=1 exists
Data:

u32 bus_res;            minimum bus number to reserve;
                        this is necessary for PCI Express Root Ports
                        to support PCIE-to-PCI bridge hotplug
u64 io;                 IO space to reserve
u64 mem;                non-prefetchable memory space to reserve
u64 prefetchable_mem;   prefetchable memory space to reserve

If any field value in Data section is -1,
it means that such kind of reservation
is not needed and must be ignored.

*/

/* Offset of vendor-specific capability type field */
#define PCI_CAP_REDHAT_TYPE  3

/* List of valid Red Hat vendor-specific capability types */
#define REDHAT_CAP_TYPE_QEMU    1


/* Offsets of QEMU capability fields */
#define QEMU_PCI_CAP_BUS_RES        4
#define QEMU_PCI_CAP_LIMITS_OFFSET  8
#define QEMU_PCI_CAP_IO             8
#define QEMU_PCI_CAP_MEM            16
#define QEMU_PCI_CAP_PREF_MEM       24
#define QEMU_PCI_CAP_SIZE           32

#endif /* _PCI_CAP_H */
