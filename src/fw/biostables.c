// Support for manipulating bios tables (pir, mptable, acpi, smbios).
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "byteorder.h" // le32_to_cpu
#include "config.h" // CONFIG_*
#include "hw/pci.h" // pci_config_writeb
#include "malloc.h" // malloc_fseg
#include "memmap.h" // SYMBOL
#include "output.h" // dprintf
#include "romfile.h" // romfile_find
#include "std/acpi.h" // struct rsdp_descriptor
#include "std/mptable.h" // MPTABLE_SIGNATURE
#include "std/pirtable.h" // struct pir_header
#include "std/smbios.h" // struct smbios_entry_point
#include "string.h" // memcpy
#include "util.h" // copy_table
#include "list.h" // hlist_*
#include "x86.h" // outb

struct pir_header *PirAddr VARFSEG;

void
copy_pir(void *pos)
{
    struct pir_header *p = pos;
    if (p->signature != PIR_SIGNATURE)
        return;
    if (PirAddr)
        return;
    if (p->size < sizeof(*p))
        return;
    if (checksum(pos, p->size) != 0)
        return;
    void *newpos = malloc_fseg(p->size);
    if (!newpos) {
        warn_noalloc();
        return;
    }
    dprintf(1, "Copying PIR from %p to %p\n", pos, newpos);
    memcpy(newpos, pos, p->size);
    PirAddr = newpos;
}

void
copy_mptable(void *pos)
{
    struct mptable_floating_s *p = pos;
    if (p->signature != MPTABLE_SIGNATURE)
        return;
    if (!p->physaddr)
        return;
    if (checksum(pos, sizeof(*p)) != 0)
        return;
    u32 length = p->length * 16;
    u16 mpclength = ((struct mptable_config_s *)p->physaddr)->length;
    if (length + mpclength > BUILD_MAX_MPTABLE_FSEG) {
        dprintf(1, "Skipping MPTABLE copy due to large size (%d bytes)\n"
                , length + mpclength);
        return;
    }
    // Allocate final memory location.  (In theory the config
    // structure can go in high memory, but Linux kernels before
    // v2.6.30 crash with that.)
    struct mptable_floating_s *newpos = malloc_fseg(length + mpclength);
    if (!newpos) {
        warn_noalloc();
        return;
    }
    dprintf(1, "Copying MPTABLE from %p/%x to %p\n", pos, p->physaddr, newpos);
    memcpy(newpos, pos, length);
    newpos->physaddr = (u32)newpos + length;
    newpos->checksum -= checksum(newpos, sizeof(*newpos));
    memcpy((void*)newpos + length, (void*)p->physaddr, mpclength);
}


/****************************************************************
 * ACPI
 ****************************************************************/

static int
get_acpi_rsdp_length(void *pos, unsigned size)
{
    struct rsdp_descriptor *p = pos;
    if (p->signature != RSDP_SIGNATURE)
        return -1;
    u32 length = 20;
    if (length > size)
        return -1;
    if (checksum(pos, length) != 0)
        return -1;
    if (p->revision > 1) {
        length = p->length;
        if (length > size)
            return -1;
        if (checksum(pos, length) != 0)
            return -1;
    }
    return length;
}

struct rsdp_descriptor *RsdpAddr;

void
copy_acpi_rsdp(void *pos)
{
    if (RsdpAddr)
        return;
    int length = get_acpi_rsdp_length(pos, -1);
    if (length < 0)
        return;
    void *newpos = malloc_fseg(length);
    if (!newpos) {
        warn_noalloc();
        return;
    }
    dprintf(1, "Copying ACPI RSDP from %p to %p\n", pos, newpos);
    memcpy(newpos, pos, length);
    RsdpAddr = newpos;
}

void *find_acpi_rsdp(void)
{
    unsigned long start = SYMBOL(zonefseg_start);
    unsigned long end = SYMBOL(zonefseg_end);
    unsigned long pos;

    for (pos = ALIGN(start, 0x10); pos <= ALIGN_DOWN(end, 0x10); pos += 0x10)
        if (get_acpi_rsdp_length((void *)pos, end - pos) >= 0)
            return (void *)pos;

    return NULL;
}

void *
find_acpi_table(u32 signature)
{
    dprintf(4, "rsdp=%p\n", RsdpAddr);
    if (!RsdpAddr || RsdpAddr->signature != RSDP_SIGNATURE)
        return NULL;
    struct rsdt_descriptor_rev1 *rsdt = (void*)RsdpAddr->rsdt_physical_address;
    struct xsdt_descriptor_rev2 *xsdt =
        RsdpAddr->xsdt_physical_address >= 0x100000000
        ? NULL : (void*)(u32)(RsdpAddr->xsdt_physical_address);
    dprintf(4, "rsdt=%p\n", rsdt);
    dprintf(4, "xsdt=%p\n", xsdt);

    if (xsdt && xsdt->signature == XSDT_SIGNATURE) {
        void *end = (void*)xsdt + xsdt->length;
        int i;
        for (i=0; (void*)&xsdt->table_offset_entry[i] < end; i++) {
            if (xsdt->table_offset_entry[i] >= 0x100000000)
                continue; /* above 4G */
            struct acpi_table_header *tbl = (void*)(u32)xsdt->table_offset_entry[i];
            if (!tbl || tbl->signature != signature)
                continue;
            dprintf(1, "table(%x)=%p (via xsdt)\n", signature, tbl);
            return tbl;
        }
    }

    if (rsdt && rsdt->signature == RSDT_SIGNATURE) {
        void *end = (void*)rsdt + rsdt->length;
        int i;
        for (i=0; (void*)&rsdt->table_offset_entry[i] < end; i++) {
            struct acpi_table_header *tbl = (void*)rsdt->table_offset_entry[i];
            if (!tbl || tbl->signature != signature)
                continue;
            dprintf(1, "table(%x)=%p (via rsdt)\n", signature, tbl);
            return tbl;
        }
    }

    dprintf(4, "no table %x found\n", signature);
    return NULL;
}

u32
find_resume_vector(void)
{
    struct fadt_descriptor_rev1 *fadt = find_acpi_table(FACP_SIGNATURE);
    if (!fadt)
        return 0;
    struct facs_descriptor_rev1 *facs = (void*)fadt->firmware_ctrl;
    dprintf(4, "facs=%p\n", facs);
    if (! facs || facs->signature != FACS_SIGNATURE)
        return 0;
    // Found it.
    dprintf(4, "resume addr=%d\n", facs->firmware_waking_vector);
    return facs->firmware_waking_vector;
}

static struct acpi_20_generic_address acpi_reset_reg;
static u8 acpi_reset_val;
u32 acpi_pm1a_cnt VARFSEG;
u16 acpi_pm_base = 0xb000;

#define acpi_ga_to_bdf(addr) pci_to_bdf(0, (addr >> 32) & 0xffff, (addr >> 16) & 0xffff)

void
acpi_reboot(void)
{
    // Check it passed the sanity checks in acpi_set_reset_reg() and was set
    if (acpi_reset_reg.register_bit_width != 8)
        return;

    u64 addr = le64_to_cpu(acpi_reset_reg.address);

    dprintf(1, "ACPI hard reset %d:%llx (%x)\n",
            acpi_reset_reg.address_space_id, addr, acpi_reset_val);

    switch (acpi_reset_reg.address_space_id) {
    case 0: // System Memory
        writeb((void *)(u32)addr, acpi_reset_val);
        break;
    case 1: // System I/O
        outb(acpi_reset_val, addr);
        break;
    case 2: // PCI config space
        pci_config_writeb(acpi_ga_to_bdf(addr), addr & 0xffff, acpi_reset_val);
        break;
    }
}

static void
acpi_set_reset_reg(struct acpi_20_generic_address *reg, u8 val)
{
    if (!reg || reg->address_space_id > 2 ||
        reg->register_bit_width != 8 || reg->register_bit_offset)
        return;

    acpi_reset_reg = *reg;
    acpi_reset_val = val;
}

void
find_acpi_features(void)
{
    struct fadt_descriptor_rev1 *fadt = find_acpi_table(FACP_SIGNATURE);
    if (!fadt)
        return;
    u32 pm_tmr = le32_to_cpu(fadt->pm_tmr_blk);
    u32 pm1a_cnt = le32_to_cpu(fadt->pm1a_cnt_blk);
    dprintf(4, "pm_tmr_blk=%x\n", pm_tmr);
    if (pm_tmr)
        pmtimer_setup(pm_tmr);
    if (pm1a_cnt)
        acpi_pm1a_cnt = pm1a_cnt;

    // Theoretically we should check the 'reset_reg_sup' flag, but Windows
    // doesn't and thus nobody seems to *set* it. If the table is large enough
    // to include it, let the sanity checks in acpi_set_reset_reg() suffice.
    if (fadt->length >= 129) {
        void *p = fadt;
        acpi_set_reset_reg(p + 116, *(u8 *)(p + 128));
    }
}


/****************************************************************
 * SMBIOS
 ****************************************************************/

// Iterator for each sub-table in the smbios blob.
void *
smbios_next(struct smbios_entry_point *smbios, void *prev)
{
    if (!smbios)
        return NULL;
    void *start = (void*)smbios->structure_table_address;
    void *end = start + smbios->structure_table_length;

    if (!prev) {
        prev = start;
    } else {
        struct smbios_structure_header *hdr = prev;
        if (prev + sizeof(*hdr) > end)
            return NULL;
        prev += hdr->length + 2;
        while (prev < end && (*(u8*)(prev-1) != '\0' || *(u8*)(prev-2) != '\0'))
            prev++;
    }
    struct smbios_structure_header *hdr = prev;
    if (prev >= end || prev + sizeof(*hdr) >= end || prev + hdr->length >= end)
        return NULL;
    return prev;
}

struct smbios_entry_point *SMBiosAddr;

void
copy_smbios(void *pos)
{
    if (SMBiosAddr)
        return;
    struct smbios_entry_point *p = pos;
    if (p->signature != SMBIOS_SIGNATURE)
        return;
    if (checksum(pos, 0x10) != 0)
        return;
    if (memcmp(p->intermediate_anchor_string, "_DMI_", 5))
        return;
    if (checksum(pos+0x10, p->length-0x10) != 0)
        return;
    struct smbios_entry_point *newpos = malloc_fseg(p->length);
    if (!newpos) {
        warn_noalloc();
        return;
    }
    dprintf(1, "Copying SMBIOS entry point from %p to %p\n", pos, newpos);
    memcpy(newpos, pos, p->length);
    SMBiosAddr = newpos;
}

void
display_uuid(void)
{
    struct smbios_type_1 *tbl = smbios_next(SMBiosAddr, NULL);
    int minlen = offsetof(struct smbios_type_1, uuid) + sizeof(tbl->uuid);
    for (; tbl; tbl = smbios_next(SMBiosAddr, tbl))
        if (tbl->header.type == 1 && tbl->header.length >= minlen) {
            u8 *uuid = tbl->uuid;
            u8 empty_uuid[sizeof(tbl->uuid)] = { 0 };
            if (memcmp(uuid, empty_uuid, sizeof(empty_uuid)) == 0)
                return;

            /*
             * According to SMBIOS v2.6 the first three fields are encoded in
             * little-endian format.  Versions prior to v2.6 did not specify
             * the encoding, but we follow dmidecode and assume big-endian
             * encoding.
             */
            if (SMBiosAddr->smbios_major_version > 2 ||
                (SMBiosAddr->smbios_major_version == 2 &&
                 SMBiosAddr->smbios_minor_version >= 6)) {
                printf("Machine UUID"
                       " %02x%02x%02x%02x"
                       "-%02x%02x"
                       "-%02x%02x"
                       "-%02x%02x"
                       "-%02x%02x%02x%02x%02x%02x\n"
                       , uuid[ 3], uuid[ 2], uuid[ 1], uuid[ 0]
                       , uuid[ 5], uuid[ 4]
                       , uuid[ 7], uuid[ 6]
                       , uuid[ 8], uuid[ 9]
                       , uuid[10], uuid[11], uuid[12]
                       , uuid[13], uuid[14], uuid[15]);
            } else {
                printf("Machine UUID"
                       " %02x%02x%02x%02x"
                       "-%02x%02x"
                       "-%02x%02x"
                       "-%02x%02x"
                       "-%02x%02x%02x%02x%02x%02x\n"
                       , uuid[ 0], uuid[ 1], uuid[ 2], uuid[ 3]
                       , uuid[ 4], uuid[ 5]
                       , uuid[ 6], uuid[ 7]
                       , uuid[ 8], uuid[ 9]
                       , uuid[10], uuid[11], uuid[12]
                       , uuid[13], uuid[14], uuid[15]);
            }

            return;
        }
}

#define set_str_field_or_skip(type, field, value)                       \
    do {                                                                \
        int size = (value != NULL) ? strlen(value) + 1 : 0;             \
        if (size > 1) {                                                 \
            memcpy(end, value, size);                                   \
            end += size;                                                \
            p->field = ++str_index;                                     \
        } else {                                                        \
            p->field = 0;                                               \
        }                                                               \
    } while (0)

static void *
smbios_new_type_0(void *start,
                  const char *vendor, const char *version, const char *date)
{
    struct smbios_type_0 *p = (struct smbios_type_0 *)start;
    char *end = (char *)start + sizeof(struct smbios_type_0);
    int str_index = 0;

    p->header.type = 0;
    p->header.length = sizeof(struct smbios_type_0);
    p->header.handle = 0;

    set_str_field_or_skip(0, vendor_str, vendor);
    set_str_field_or_skip(0, bios_version_str, version);
    p->bios_starting_address_segment = 0xe800;
    set_str_field_or_skip(0, bios_release_date_str, date);

    p->bios_rom_size = 0; /* FIXME */

    /* BIOS characteristics not supported */
    memset(p->bios_characteristics, 0, 8);
    p->bios_characteristics[0] = 0x08;

    /* Enable targeted content distribution (needed for SVVP) */
    p->bios_characteristics_extension_bytes[0] = 0;
    p->bios_characteristics_extension_bytes[1] = 4;

    p->system_bios_major_release = 0;
    p->system_bios_minor_release = 0;
    p->embedded_controller_major_release = 0xFF;
    p->embedded_controller_minor_release = 0xFF;

    *end = 0;
    end++;
    if (!str_index) {
        *end = 0;
        end++;
    }

    return end;
}

#define BIOS_NAME "SeaBIOS"
#define BIOS_DATE "04/01/2014"

static int
smbios_romfile_setup(void)
{
    struct romfile_s *f_anchor = romfile_find("etc/smbios/smbios-anchor");
    struct romfile_s *f_tables = romfile_find("etc/smbios/smbios-tables");
    struct smbios_entry_point ep;
    struct smbios_type_0 *t0;
    u16 qtables_len, need_t0 = 1;
    u8 *qtables, *tables;

    if (!f_anchor || !f_tables || f_anchor->size != sizeof(ep))
        return 0;

    f_anchor->copy(f_anchor, &ep, f_anchor->size);

    if (f_tables->size != ep.structure_table_length)
        return 0;

    qtables = malloc_tmphigh(f_tables->size);
    if (!qtables) {
        warn_noalloc();
        return 0;
    }
    f_tables->copy(f_tables, qtables, f_tables->size);
    ep.structure_table_address = (u32)qtables; /* for smbios_next(), below */

    /* did we get a type 0 structure ? */
    for (t0 = smbios_next(&ep, NULL); t0; t0 = smbios_next(&ep, t0))
        if (t0->header.type == 0) {
            need_t0 = 0;
            break;
        }

    qtables_len = ep.structure_table_length;
    if (need_t0) {
        /* common case: add our own type 0, with 3 strings and 4 '\0's */
        u16 t0_len = sizeof(struct smbios_type_0) + strlen(BIOS_NAME) +
                     strlen(VERSION) + strlen(BIOS_DATE) + 4;
        ep.structure_table_length += t0_len;
        if (t0_len > ep.max_structure_size)
            ep.max_structure_size = t0_len;
        ep.number_of_structures++;
    }

    /* allocate final blob and record its address in the entry point */
    if (ep.structure_table_length > BUILD_MAX_SMBIOS_FSEG)
        tables = malloc_high(ep.structure_table_length);
    else
        tables = malloc_fseg(ep.structure_table_length);
    if (!tables) {
        warn_noalloc();
        free(qtables);
        return 0;
    }
    ep.structure_table_address = (u32)tables;

    /* populate final blob */
    if (need_t0)
        tables = smbios_new_type_0(tables, BIOS_NAME, VERSION, BIOS_DATE);
    memcpy(tables, qtables, qtables_len);
    free(qtables);

    /* finalize entry point */
    ep.checksum -= checksum(&ep, 0x10);
    ep.intermediate_checksum -= checksum((void *)&ep + 0x10, ep.length - 0x10);

    copy_smbios(&ep);
    return 1;
}

void
smbios_setup(void)
{
    if (smbios_romfile_setup())
        return;
    smbios_legacy_setup();
}

void
copy_table(void *pos)
{
    copy_pir(pos);
    copy_mptable(pos);
    copy_acpi_rsdp(pos);
    copy_smbios(pos);
}

/****************************************************************
 * DSDT parser
 ****************************************************************/

struct acpi_device {
    struct hlist_node node;
    char name[16];
    u8 *hid_aml;
    u8 *sta_aml;
    u8 *crs_data;
    int crs_size;
};
static struct hlist_head acpi_devices;

static int parse_error = 0;
static int parse_dumptree = 0;
static char parse_name[32];
static struct acpi_device *parse_dev;

static void parse_termlist(u8 *ptr, int offset, int pkglength);

static void hex(const u8 *ptr, int count, int lvl, const char *item)
{
    int l = 0, i;

    do {
        dprintf(lvl, "%s: %04x:  ", item, l);
        for (i = l; i < l+16; i += 4)
            dprintf(lvl, "%02x %02x %02x %02x  ",
                    ptr[i+0], ptr[i+1], ptr[i+2], ptr[i+3]);
        for (i = l; i < l+16; i++)
            dprintf(lvl, "%c", (ptr[i] > 0x20 && ptr[i] < 0x80) ? ptr[i] : '.');
        dprintf(lvl, "\n");
        l += 16;
    } while (l < count);
}

static u64 parse_resource_int(u8 *ptr, int count)
{
    u64 value = 0;
    int index = 0;

    for (index = 0; index < count; index++)
        value |= (u64)ptr[index] << (index * 8);
    return value;
}

static int parse_resource_bit(u8 *ptr, int count)
{
    int bit;

    for (bit = 0; bit < count*8; bit++)
        if (ptr[bit/8] & (1 << (bit%8)))
            return bit;
    return 0;
}

static int parse_resource(u8 *ptr, int length, int *type, u64 *min, u64 *max)
{
    int rname, rsize;
    u64 len;

    *type = -1;
    *min = 0;
    *max = 0;
    len = 0;
    if (!(ptr[0] & 0x80)) {
        /* small resource */
        rname = (ptr[0] >> 3) & 0x0f;
        rsize = ptr[0] & 0x07;
        rsize++;
        switch (rname) {
        case 0x04: /* irq */
            *min = parse_resource_bit(ptr + 1, rsize);
            *max = *min;
            *type = 3;
            break;
        case 0x0f: /* end marker */
            return 0;
        case 0x08: /* io */
            *min = parse_resource_int(ptr + 2, 2);
            *max = parse_resource_int(ptr + 4, 2);
            if (*min == *max) {
                *max = *min + ptr[7] - 1;
                *type = 1;
            }
            break;
        case 0x09: /* fixed io */
            *min = parse_resource_int(ptr + 2, 2);
            *max = *min + ptr[4] - 1;
            *type = 1;
            break;
        default:
            dprintf(3, "%s: small: 0x%x (len %d)\n",
                    __func__, rname, rsize);
            break;
        }
    } else {
        /* large resource */
        rname = ptr[0] & 0x7f;
        rsize = ptr[2] << 8 | ptr[1];
        rsize += 3;
        switch (rname) {
        case 0x06: /* 32-bit Fixed Location Memory Range Descriptor */
            *min = parse_resource_int(ptr + 4, 4);
            len = parse_resource_int(ptr + 8, 4);
            *max = *min + len - 1;
            *type = 0;
            break;
        case 0x07: /* DWORD Address Space Descriptor */
            *min = parse_resource_int(ptr + 10, 4);
            *max = parse_resource_int(ptr + 14, 4);
            *type = ptr[3];
            break;
        case 0x08: /* WORD Address Space Descriptor */
            *min = parse_resource_int(ptr +  8, 2);
            *max = parse_resource_int(ptr + 10, 2);
            *type = ptr[3];
            break;
        case 0x09: /* irq */
            *min = parse_resource_int(ptr +  5, 4);
            *max = *min;
            *type = 3;
            break;
        case 0x0a: /* QWORD Address Space Descriptor */
            *min = parse_resource_int(ptr + 14, 8);
            *max = parse_resource_int(ptr + 22, 8);
            *type = ptr[3];
            break;
        default:
            dprintf(3, "%s: large: 0x%x (len %d)\n", __func__, rname, rsize);
            break;
        }
    }
    return rsize;
}

static int find_resource(u8 *ptr, int len, int kind, u64 *min, u64 *max)
{
    int type, size, offset = 0;

    do {
        size = parse_resource(ptr + offset, len - offset,
                              &type, min, max);
        if (kind == type)
            return 0;
        offset += size;
    } while (size > 0 && offset < len);
    return -1;
}

static int print_resources(const char *prefix, u8 *ptr, int len)
{
    static const char *typename[] = { "mem", "i/o", "bus" };
    int type, size, offset = 0;
    u64 min, max;

    do {
        size = parse_resource(ptr + offset, len - offset,
                              &type, &min, &max);
        switch (type) {
        case 0:
        case 1:
        case 2:
            dprintf(1, "%s%s 0x%llx -> 0x%llx\n",
                    prefix, typename[type], min, max);
            break;
        case 3:
            dprintf(1, "%sirq %lld\n", prefix, min);
            break;
        }
        offset += size;
    } while (size > 0 && offset < len);
    return -1;
}

static int parse_nameseg(u8 *ptr, char **dst)
{
    if (dst && *dst) {
        *(dst[0]++) = ptr[0];
        if (ptr[1] != '_')
            *(dst[0]++) = ptr[1];
        if (ptr[2] != '_')
            *(dst[0]++) = ptr[2];
        if (ptr[3] != '_')
            *(dst[0]++) = ptr[3];
        *(dst[0]) = 0;
    }
    return 4;
}

static int parse_namestring(u8 *ptr, const char *item)
{
    char *dst = parse_name;
    int offset = 0;
    int i, count;

again:
    switch (ptr[offset]) {
    case 0: /* null name */
        offset++;
        *(dst++) = 0;
        break;
    case 0x2e:
        offset++;
        offset += parse_nameseg(ptr + offset, &dst);
        *(dst++) = '.';
        offset += parse_nameseg(ptr + offset, &dst);
        break;
    case 0x2f:
        offset++;
        count = ptr[offset];
        offset++;
        for (i = 0; i < count; i++) {
            if (i)
                *(dst++) = '.';
            offset += parse_nameseg(ptr + offset, &dst);
        }
        break;
    case '\\':
        *(dst++) = '\\';
        offset++;
        goto again;
    case '^':
        *(dst++) = '^';
        offset++;
        goto again;
    case 'A' ... 'Z':
    case '_':
        offset += parse_nameseg(ptr, &dst);
        break;
    default:
        hex(ptr, 16, 3, __func__);
        parse_error = 1;
        break;
    }
    dprintf(5, "%s: %s '%s'\n", __func__, item, parse_name);
    return offset;
}

static int parse_termarg_int(u8 *ptr, u64 *dst)
{
    u64 value;
    int offset = 1;

    switch (ptr[0]) {
    case 0x00: /* zero */
        value = 0;
        break;
    case 0x01: /* one */
        value = 1;
        break;
    case 0x0a: /* byte prefix */
        value = ptr[1];
        offset++;
        break;
    case 0x0b: /* word prefix */
        value = ptr[1] |
            ((unsigned long)ptr[2] << 8);
        offset += 2;
        break;
    case 0x0c: /* dword prefix */
        value = ptr[1] |
            ((unsigned long)ptr[2] << 8) |
            ((unsigned long)ptr[3] << 16) |
            ((unsigned long)ptr[4] << 24);
        offset += 4;
        break;
    default:
        value = 0;
        hex(ptr, 16, 3, __func__);
        parse_error = 1;
        break;
    }

    if (dst)
        *dst = value;
    dprintf(5, "%s: 0x%llx\n", __func__, value);
    return offset;
}

static int parse_pkglength(u8 *ptr, int *pkglength)
{
    int offset = 2;

    *pkglength = 0;
    switch (ptr[0] >> 6) {
    case 3:
        *pkglength |= ptr[3] << 20;
        offset++;
    case 2:
        *pkglength |= ptr[2] << 12;
        offset++;
    case 1:
        *pkglength |= ptr[1] << 4;
        *pkglength |= ptr[0] & 0x0f;
        return offset;
    case 0:
    default:
        *pkglength |= ptr[0] & 0x3f;
        return 1;
    }
}

static int parse_pkg_common(u8 *ptr, const char *item, int *pkglength)
{
    int offset;

    offset = parse_pkglength(ptr, pkglength);
    offset += parse_namestring(ptr + offset, item);
    return offset;
}

static int parse_pkg_scope(u8 *ptr)
{
    int offset, pkglength;

    offset = parse_pkg_common(ptr, "skope", &pkglength);
    parse_termlist(ptr, offset, pkglength);
    return pkglength;
}

static int parse_pkg_device(u8 *ptr)
{
    int offset, pkglength;

    offset = parse_pkg_common(ptr, "device", &pkglength);

    parse_dev = malloc_high(sizeof(*parse_dev));
    if (!parse_dev) {
        warn_noalloc();
        parse_error = 1;
        return pkglength;
    }

    memset(parse_dev, 0, sizeof(*parse_dev));
    hlist_add_head(&parse_dev->node, &acpi_devices);
    strtcpy(parse_dev->name, parse_name, sizeof(parse_dev->name));

    parse_termlist(ptr, offset, pkglength);
    return pkglength;
}

static int parse_pkg_buffer(u8 *ptr)
{
    u64 blen;
    int pkglength, offset;

    offset = parse_pkglength(ptr, &pkglength);
    offset += parse_termarg_int(ptr + offset, &blen);
    if (strcmp(parse_name, "_CRS") == 0) {
        parse_dev->crs_data = ptr + offset;
        parse_dev->crs_size = blen;
    }
    return pkglength;
}

static int parse_pkg_skip(u8 *ptr, int op, int name)
{
    int pkglength, offset;
    char item[8];

    snprintf(item, sizeof(item), "op %x", op);
    offset = parse_pkglength(ptr, &pkglength);
    if (name) {
        parse_namestring(ptr + offset, item);
    } else {
        dprintf(5, "%s: %s (%d)\n", __func__, item, pkglength);
    }
    return pkglength;
}

static int parse_termobj(u8 *ptr)
{
    int offset = 1;

    switch (ptr[0]) {
    case 0x00: /* zero */
        break;
    case 0x01: /* one */
        break;
    case 0x08: /* name op */
        offset += parse_namestring(ptr + offset, "name");
        offset += parse_termobj(ptr + offset);
        if (strcmp(parse_name, "_HID") == 0)
            parse_dev->hid_aml = ptr;
        if (strcmp(parse_name, "_STA") == 0)
            parse_dev->sta_aml = ptr;
        break;
    case 0x0a: /* byte prefix */
        offset++;
        break;
    case 0x0b: /* word prefix */
        offset += 2;
        break;
    case 0x0c: /* dword prefix */
        offset += 4;
        break;
    case 0x0d: /* string prefix */
        while (ptr[offset])
            offset++;
        offset++;
        break;
    case 0x10: /* scope op */
        offset += parse_pkg_scope(ptr + offset);
        break;
    case 0x11: /* buffer op */
        offset += parse_pkg_buffer(ptr + offset);
        break;
    case 0x12: /* package op */
    case 0x13: /* var package op */
        offset += parse_pkg_skip(ptr + offset, ptr[0], 0);
        break;
    case 0x14: /* method op */
        offset += parse_pkg_skip(ptr + offset, ptr[0], 1);
        if (strcmp(parse_name, "_STA") == 0)
            parse_dev->sta_aml = ptr;
        break;
    case 0x5b: /* ext op prefix */
        offset++;
        switch (ptr[1]) {
        case 0x01: /* mutex op */
            offset += parse_namestring(ptr + offset, "mutex");
            offset++; /* sync flags */
            break;
        case 0x80: /* op region op */
            offset += parse_namestring(ptr + offset, "op region");
            offset++; /* region space */
            offset += parse_termarg_int(ptr + offset, NULL);
            offset += parse_termarg_int(ptr + offset, NULL);
            break;
        case 0x81: /* field op */
        case 0x83: /* processor op */
        case 0x84: /* power resource op */
        case 0x85: /* thermal zone op */
            offset += parse_pkg_skip(ptr + offset, 0x5b00 | ptr[1], 1);
            break;
        case 0x82: /* device op */
            offset += parse_pkg_device(ptr + offset);
            break;
        default:
            hex(ptr, 16, 3, __func__);
            parse_error = 1;
            break;
        }
        break;
    default:
        hex(ptr, 16, 3, __func__);
        parse_error = 1;
        break;
    }

    return offset;
}

static void parse_termlist(u8 *ptr, int offset, int pkglength)
{
    for (;;) {
        offset += parse_termobj(ptr + offset);
        if (offset == pkglength)
            return;
        if (offset > pkglength) {
            dprintf(1, "%s: overrun: %d/%d\n", __func__,
                    offset, pkglength);
            parse_error = 1;
            return;
        }
        if (parse_error) {
            dprintf(1, "%s: parse error, skip from %d/%d\n", __func__,
                    offset, pkglength);
            parse_error = 0;
            return;
        }
    }
}

static struct acpi_device *acpi_dsdt_find(struct acpi_device *prev, const u8 *aml, int size)
{
    struct acpi_device *dev;
    struct hlist_node *node;

    if (!prev)
        node = acpi_devices.first;
    else
        node = prev->node.next;

    for (; node != NULL; node = dev->node.next) {
        dev = container_of(node, struct acpi_device, node);
        if (!aml)
            return dev;
        if (!dev->hid_aml)
            continue;
        if (memcmp(dev->hid_aml + 5, aml, size) == 0)
            return dev;
    }
    return NULL;
}

static int acpi_dsdt_present(struct acpi_device *dev)
{
    if (!dev)
        return 0; /* no */
    if (!dev->sta_aml)
        return 1; /* yes */
    if (dev->sta_aml[0] == 0x14)
        return -1; /* unknown (can't evaluate method) */
    if (dev->sta_aml[0] == 0x08) {
        u64 value = 0;
        parse_termarg_int(dev->sta_aml + 5, &value);
        if (value == 0)
            return 0; /* no */
        else
            return 1; /* yes */
    }
    return -1; /* unknown (should not happen) */
}

/****************************************************************
 * DSDT parser, public interface
 ****************************************************************/

struct acpi_device *acpi_dsdt_find_string(struct acpi_device *prev, const char *hid)
{
    if (!CONFIG_ACPI_PARSE)
        return NULL;

    u8 aml[10];
    int len = snprintf((char*)aml, sizeof(aml), "\x0d%s", hid);
    return acpi_dsdt_find(prev, aml, len);
}

struct acpi_device *acpi_dsdt_find_eisaid(struct acpi_device *prev, u16 eisaid)
{
    if (!CONFIG_ACPI_PARSE)
        return NULL;
    u8 aml[] = {
        0x0c, 0x41, 0xd0,
        eisaid >> 8,
        eisaid & 0xff
    };
    return acpi_dsdt_find(prev, aml, 5);
}

char *acpi_dsdt_name(struct acpi_device *dev)
{
    if (!CONFIG_ACPI_PARSE || !dev)
        return NULL;
    return dev->name;
}

int acpi_dsdt_find_io(struct acpi_device *dev, u64 *min, u64 *max)
{
    if (!CONFIG_ACPI_PARSE || !dev || !dev->crs_data)
        return -1;
    return find_resource(dev->crs_data, dev->crs_size,
                         1 /* I/O */, min, max);
}

int acpi_dsdt_find_mem(struct acpi_device *dev, u64 *min, u64 *max)
{
    if (!CONFIG_ACPI_PARSE || !dev || !dev->crs_data)
        return -1;
    return find_resource(dev->crs_data, dev->crs_size,
                         0 /* mem */, min, max);
}

int acpi_dsdt_find_irq(struct acpi_device *dev, u64 *irq)
{
    u64 max;
    if (!CONFIG_ACPI_PARSE || !dev || !dev->crs_data)
        return -1;
    return find_resource(dev->crs_data, dev->crs_size,
                         3 /* irq */, irq, &max);
}

int acpi_dsdt_present_eisaid(u16 eisaid)
{
    if (!CONFIG_ACPI_PARSE)
        return -1; /* unknown */

    struct acpi_device *dev = acpi_dsdt_find_eisaid(NULL, eisaid);
    return acpi_dsdt_present(dev);
}

void acpi_dsdt_parse(void)
{
    if (!CONFIG_ACPI_PARSE)
        return;

    struct fadt_descriptor_rev1 *fadt = find_acpi_table(FACP_SIGNATURE);
    if (!fadt)
        return;
    u8 *dsdt = (void*)(fadt->dsdt);
    if (!dsdt)
        return;

    u32 length = *(u32*)(dsdt + 4);
    u32 offset = 0x24;
    dprintf(1, "ACPI: parse DSDT at %p (len %d)\n", dsdt, length);
    parse_termlist(dsdt, offset, length);

    if (parse_dumptree) {
        struct acpi_device *dev;
        dprintf(1, "ACPI: dumping dsdt devices\n");
        for (dev = acpi_dsdt_find(NULL, NULL, 0);
             dev != NULL;
             dev = acpi_dsdt_find(dev, NULL, 0)) {
            dprintf(1, "    %s", acpi_dsdt_name(dev));
            if (dev->hid_aml)
                dprintf(1, ", hid");
            if (dev->sta_aml)
                dprintf(1, ", sta (0x%x)", dev->sta_aml[0]);
            if (dev->crs_data)
                dprintf(1, ", crs");
            dprintf(1, "\n");
            if (dev->crs_data)
                print_resources("        ", dev->crs_data, dev->crs_size);
        }
    }
}
