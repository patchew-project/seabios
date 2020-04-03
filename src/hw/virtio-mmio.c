#include "config.h" // CONFIG_DEBUG_LEVEL
#include "malloc.h" // free
#include "output.h" // dprintf
#include "virtio-pci.h"
#include "virtio-ring.h"

/* qemu microvm supports 8 virtio-mmio devices */
static u64 devs[8];

void virtio_mmio_register(u64 mmio)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(devs); i++) {
        if (devs[i] == mmio) {
            /*
             * This can happen in case we have multiple scsi devices
             * attached to a single virtio-scsi controller
             */
            dprintf(3, "virtio-mmio: duplicate device at 0x%llx, ignoring\n", mmio);
            return;
        }
        if (devs[i] == 0) {
            dprintf(3, "virtio-mmio: register device at 0x%llx\n", mmio);
            devs[i] = mmio;
            return;
        }
    }
    dprintf(1, "virtio-mmio: device list full\n");
}
