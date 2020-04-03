#include "config.h" // CONFIG_DEBUG_LEVEL
#include "malloc.h" // free
#include "output.h" // dprintf
#include "stacks.h" // run_thread
#include "string.h" // memset
#include "virtio-pci.h"
#include "virtio-ring.h"
#include "virtio-mmio.h"

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

void virtio_mmio_setup(void)
{
    u32 magic, version, devid;
    void *mmio;
    int i;

    for (i = 0; i < ARRAY_SIZE(devs); i++) {
        if (devs[i] == 0)
            return;
        mmio = (void*)(u32)(devs[i]);
        magic = readl(mmio);
        if (magic != 0x74726976)
            continue;
        version = readl(mmio+4);
        if (version != 1 /* legacy */ &&
            version != 2 /* 1.0 */)
            continue;
        devid = readl(mmio+8);
        dprintf(1, "virtio-mmio: %llx: device id %x%s\n",
                devs[i], devid, version == 1 ? " (legacy)" : "");
        switch (devid) {
        case 2: /* blk */
            /* TODO */
            break;
        case 8: /* scsi */
            /* TODO */
            break;
        default:
            break;
        }
    }
}

void vp_init_mmio(struct vp_device *vp, void *mmio)
{
    memset(vp, 0, sizeof(*vp));
    vp->use_mmio = 1;
    vp->common.mode = VP_ACCESS_MMIO;
    vp->common.memaddr = mmio;
    vp->device.mode = VP_ACCESS_MMIO;
    vp->device.memaddr = mmio + 0x100;
    vp_reset(vp);
    vp_set_status(vp, VIRTIO_CONFIG_S_ACKNOWLEDGE |
                  VIRTIO_CONFIG_S_DRIVER);
}
