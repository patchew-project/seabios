#ifndef __PCI_H
#define __PCI_H

#include "types.h" // u32

#define PORT_PCI_CMD           0x0cf8
#define PORT_PCI_REBOOT        0x0cf9
#define PORT_PCI_DATA          0x0cfc
#define PORT_PXB_CMD_BASE      0x1000
#define PORT_PXB_DATA_BASE     0x1004

static inline u8 pci_bdf_to_bus(u16 bdf) {
    return bdf >> 8;
}
static inline u8 pci_bdf_to_devfn(u16 bdf) {
    return bdf & 0xff;
}
static inline u16 pci_bdf_to_busdev(u16 bdf) {
    return bdf & ~0x07;
}
static inline u8 pci_bdf_to_dev(u16 bdf) {
    return (bdf >> 3) & 0x1f;
}
static inline u8 pci_bdf_to_fn(u16 bdf) {
    return bdf & 0x07;
}
static inline u16 pci_to_bdf(int bus, int dev, int fn) {
    return (bus<<8) | (dev<<3) | fn;
}
static inline u16 pci_bus_devfn_to_bdf(int bus, u16 devfn) {
    return (bus << 8) | devfn;
}

/* for compatibility */
#define foreachbdf(BDF, BUS) foreachbdf_dom(BDF, BUS, 0)

#define foreachbdf_dom(BDF, BUS, DOMAIN)                                    \
    for (BDF=pci_next_dom(pci_bus_devfn_to_bdf((BUS), 0)-1, (BUS), (DOMAIN))  \
         ; BDF >= 0                                             \
         ; BDF=pci_next_dom(BDF, (BUS), (DOMAIN)))

#define pci_config_maskw(BDF, ADDR, OFF, ON) pci_config_maskw_dom((BDF), (ADDR), (OFF), (ON), 0)
#define pci_find_capability(BDF, CAP_ID, CAP) pci_find_capability_dom((BDF), (CAP_ID), (CAP), 0)
#define pci_next(BDF, BUS) pci_next_dom((BDF), (BUS), 0)

#define pci_config_writel(BDF, ADDR, VAL) pci_config_writel_dom((BDF), (ADDR), (VAL), 0)
#define pci_config_writew(BDF, ADDR, VAL) pci_config_writew_dom((BDF), (ADDR), (VAL), 0)
#define pci_config_writeb(BDF, ADDR, VAL) pci_config_writeb_dom((BDF), (ADDR), (VAL), 0)
#define pci_config_readl(BDF, ADDR) pci_config_readl_dom((BDF), (ADDR), 0)
#define pci_config_readw(BDF, ADDR) pci_config_readw_dom((BDF), (ADDR), 0)
#define pci_config_readb(BDF, ADDR) pci_config_readb_dom((BDF), (ADDR), 0)

void pci_config_writel_dom(u16 bdf, u32 addr, u32 val, int domain_nr);
void pci_config_writew_dom(u16 bdf, u32 addr, u16 val, int domain_nr);
void pci_config_writeb_dom(u16 bdf, u32 addr, u8 val, int domain_nr);
u32 pci_config_readl_dom(u16 bdf, u32 addr, int domain_nr);
u16 pci_config_readw_dom(u16 bdf, u32 addr, int domain_nr);
u8 pci_config_readb_dom(u16 bdf, u32 addr, int domain_nr);
void pci_config_maskw_dom(u16 bdf, u32 addr, u16 off, u16 on, int domain_nr);
u8 pci_find_capability_dom(u16 bdf, u8 cap_id, u8 cap, int domain_nr);
int pci_next_dom(int bdf, int bus, int domain_nr);
int pci_probe_host(void);
void pci_reboot(void);

#endif // pci.h
