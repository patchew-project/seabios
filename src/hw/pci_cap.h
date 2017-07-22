#ifndef _PCI_CAP_H
#define _PCI_CAP_H

#include "types.h"

struct vendor_pci_cap {
    u8 id;
    u8 next;
    u8 len;
};

struct redhat_pci_bridge_cap {
    struct vendor_pci_cap hdr;
    u8 bus_res;
    u32 pref_lim_upper;
    u16 pref_lim;
    u16 mem_lim;
    u16 io_lim_upper;
    u8 io_lim;
    u8 padd;
};

#endif /* _PCI_CAP_H */
