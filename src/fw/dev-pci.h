#ifndef _PCI_CAP_H
#define _PCI_CAP_H

#include "types.h"

/*

QEMU-specific vendor(Red Hat)-specific capability.
It's intended to provide some hints for firmware to init PCI devices.

Its is shown below:

Header:

u8 id;       Standard PCI Capability Header field
u8 next;     Standard PCI Capability Header field
u8 len;      Standard PCI Capability Header field
u8 type;     Red Hat vendor-specific capability type:
               now only REDHAT_QEMU_CAP 1 exists
Data:

u16 non_prefetchable_16;     non-prefetchable memory limit

u8 bus_res;  minimum bus number to reserve;
             this is necessary for PCI Express Root Ports
             to support PCIE-to-PCI bridge hotplug

u8 io_8;     IO limit in case of 8-bit limit value
u32 io_32;   IO limit in case of 16-bit limit value
             io_8 and io_16 are mutually exclusive, in other words,
             they can't be non-zero simultaneously

u32 prefetchable_32;         non-prefetchable memory limit
                             in case of 32-bit limit value
u64 prefetchable_64;         non-prefetchable memory limit
                             in case of 64-bit limit value
                             prefetachable_32 and prefetchable_64 are
                             mutually exclusive, in other words,
                             they can't be non-zero simultaneously
If any field in Data section is 0,
it means that such kind of reservation
is not needed.

*/

/* Offset of vendor-specific capability type field */
#define PCI_CAP_VNDR_SPEC_TYPE  3

/* List of valid Red Hat vendor-specific capability types */
#define REDHAT_CAP_TYPE_QEMU    1


/* Offsets of QEMU capability fields */
#define QEMU_PCI_CAP_NON_PREF   4
#define QEMU_PCI_CAP_BUS_RES    6
#define QEMU_PCI_CAP_IO_8       7
#define QEMU_PCI_CAP_IO_32      8
#define QEMU_PCI_CAP_PREF_32    12
#define QEMU_PCI_CAP_PREF_64    16
#define QEMU_PCI_CAP_SIZE       24

#endif /* _PCI_CAP_H */
