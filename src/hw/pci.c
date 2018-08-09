// PCI config space access functions.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "output.h" // dprintf
#include "pci.h" // pci_config_writel
#include "pci_regs.h" // PCI_VENDOR_ID
#include "util.h" // udelay
#include "x86.h" // outl

void pci_config_writel_dom(u16 bdf, u32 addr, u32 val, int domain_nr)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc),
         domain_nr ? PORT_PXB_CMD_BASE + ((domain_nr - 1) << 3) : PORT_PCI_CMD);
    outl(val, (domain_nr ? PORT_PXB_DATA_BASE + ((domain_nr - 1) << 3) : PORT_PCI_DATA));
}

void pci_config_writew_dom(u16 bdf, u32 addr, u16 val, int domain_nr)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc),
         domain_nr == 0 ? PORT_PCI_CMD : PORT_PXB_CMD_BASE + (domain_nr << 3));
    outw(val, (domain_nr ? PORT_PXB_DATA_BASE + ((domain_nr - 1) << 3) : PORT_PCI_DATA) + (addr & 2));
}

void pci_config_writeb_dom(u16 bdf, u32 addr, u8 val, int domain_nr)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc),
         domain_nr ? PORT_PXB_CMD_BASE + ((domain_nr - 1) << 3) : PORT_PCI_CMD);
    outb(val, (domain_nr ? PORT_PXB_DATA_BASE + ((domain_nr - 1) << 3) : PORT_PCI_DATA) + (addr & 3));
}

u32 pci_config_readl_dom(u16 bdf, u32 addr, int domain_nr)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc),
         domain_nr ? PORT_PXB_CMD_BASE + ((domain_nr - 1) << 3) : PORT_PCI_CMD);
    return inl((domain_nr ? PORT_PXB_DATA_BASE + ((domain_nr - 1) << 3) : PORT_PCI_DATA));
}

u16 pci_config_readw_dom(u16 bdf, u32 addr, int domain_nr)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc),
         domain_nr ? PORT_PXB_CMD_BASE + ((domain_nr - 1) << 3) : PORT_PCI_CMD);
    return inw((domain_nr ? PORT_PXB_DATA_BASE + ((domain_nr - 1) << 3) : PORT_PCI_DATA) + (addr & 2));
}

u8 pci_config_readb_dom(u16 bdf, u32 addr, int domain_nr)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc),
         domain_nr ? PORT_PXB_CMD_BASE + ((domain_nr - 1) << 3) : PORT_PCI_CMD);
    return inb((domain_nr ? PORT_PXB_DATA_BASE + ((domain_nr - 1) << 3) : PORT_PCI_DATA) + (addr & 3));
}

void
pci_config_maskw_dom(u16 bdf, u32 addr, u16 off, u16 on, int domain_nr)
{
    u16 val = pci_config_readw_dom(bdf, addr, domain_nr);
    val = (val & ~off) | on;
    pci_config_writew_dom(bdf, addr, val, domain_nr);
}

u8 pci_find_capability_dom(u16 bdf, u8 cap_id, u8 cap, int domain_nr)
{
    int i;
    u16 status = pci_config_readw_dom(bdf, PCI_STATUS, domain_nr);

    if (!(status & PCI_STATUS_CAP_LIST))
        return 0;

    if (cap == 0) {
        /* find first */
        cap = pci_config_readb_dom(bdf, PCI_CAPABILITY_LIST, domain_nr);
    } else {
        /* find next */
        cap = pci_config_readb_dom(bdf, cap + PCI_CAP_LIST_NEXT, domain_nr);
    }
    for (i = 0; cap && i <= 0xff; i++) {
        if (pci_config_readb_dom(bdf, cap + PCI_CAP_LIST_ID, domain_nr) == cap_id)
            return cap;
        cap = pci_config_readb_dom(bdf, cap + PCI_CAP_LIST_NEXT, domain_nr);
    }

    return 0;
}

// Helper function for foreachbdf() macro - return next device
int
pci_next_dom(int bdf, int bus, int domain_nr)
{
    if (pci_bdf_to_fn(bdf) == 0
        && (pci_config_readb_dom(bdf, PCI_HEADER_TYPE, domain_nr) & 0x80) == 0)
        // Last found device wasn't a multi-function device - skip to
        // the next device.
        bdf += 8;
    else
        bdf += 1;

    for (;;) {
        if (pci_bdf_to_bus(bdf) != bus)
            return -1;

        u16 v = pci_config_readw_dom(bdf, PCI_VENDOR_ID, domain_nr);
        if (v != 0x0000 && v != 0xffff)
            // Device is present.
            return bdf;

        if (pci_bdf_to_fn(bdf) == 0)
            bdf += 8;
        else
            bdf += 1;
    }
}

// Check if PCI is available at all
int
pci_probe_host(void)
{
    outl(0x80000000, PORT_PCI_CMD);
    if (inl(PORT_PCI_CMD) != 0x80000000) {
        dprintf(1, "Detected non-PCI system\n");
        return -1;
    }
    return 0;
}

void
pci_reboot(void)
{
    u8 v = inb(PORT_PCI_REBOOT) & ~6;
    outb(v|2, PORT_PCI_REBOOT); /* Request hard reset */
    udelay(50);
    outb(v|6, PORT_PCI_REBOOT); /* Actually do the reset */
    udelay(50);
}
