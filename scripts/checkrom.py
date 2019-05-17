#!/usr/bin/env python
# Script to check a bios image and report info on it.
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

import sys, struct
import layoutrom, buildrom

from python23compat import as_bytes

def subst(data, offset, new):
    return data[:offset] + new + data[offset + len(new):]

def checksum(data, start, size, csum):
    sumbyte = buildrom.checksum(data[start:start+size])
    return subst(data, start+csum, sumbyte)

def check_windows_bios_date(rawdata):
    dates = []
    for i in xrange(len(rawdata)):
        if (rawdata[i+0:i+2].isdigit() and
                rawdata[i+2] == '/' and
                rawdata[i+3:i+5].isdigit() and
                rawdata[i+5] == '/' and
                rawdata[i+6:i+8].isdigit()):
            dates.append(rawdata[i:i+8])
    if len(dates) > 1:
        print("Warning!  More than one date was detected in rom.")
        print("   This may cause Windows OS to report incorrect date:")
        print("   %s" % (dates, ))
    if len(dates) == 0:
        print("Warning!  No dates were detected in rom.")
        print("   This may cause Windows OS to report incorrect date.")

def check_windows_bios_version(rawdata):
    versions = []
    for i in xrange(len(rawdata)):
        if (rawdata[i+0].isdigit() and
                rawdata[i+1] == '.' and
                rawdata[i+2].isdigit()):
            p = i-1
            while ((p >= 0) and (p > i+2-127)
                   and ord(rawdata[p]) >= ord(' ')
                   and rawdata[p] != '$'):
                p -= 1
            version = rawdata[p+1:i+3]
            for s in ["Ver", "Rev", "Rel",
                      "v0", "v1", "v2", "v3", "v4",
                      "v5", "v6", "v7", "v8", "v9",
                      "v 0", "v 1", "v 2", "v 3", "v 4",
                      "v 5", "v 6", "v 7", "v 8", "v 9"]:
                if s in version:
                    versions.append((rawdata[p+1:i+3], i))
                    break
    if len(versions) > 0:
        print("Warning!  Version strings were detected in rom.")
        print("   This may cause Windows OS to report incorrect version:")
        print("   %s" % (versions, ))

def main():
    # Get args
    objinfo, finalsize, rawfile, outfile = sys.argv[1:]

    # Read in symbols
    objinfofile = open(objinfo, 'r')
    symbols = layoutrom.parseObjDump(objinfofile, 'in')[1]

    # Read in raw file
    f = open(rawfile, 'rb')
    rawdata = f.read()
    f.close()
    datasize = len(rawdata)
    finalsize = int(finalsize) * 1024
    if finalsize == 0:
        finalsize = 64*1024
        if datasize > 64*1024:
            finalsize = 128*1024
            if datasize > 128*1024:
                finalsize = 256*1024
    if datasize > finalsize:
        print("Error!  ROM doesn't fit (%d > %d)" % (datasize, finalsize))
        print("   You have to either increase the size (CONFIG_ROM_SIZE)")
        print("   or turn off some features (such as hardware support not")
        print("   needed) to make it fit.  Trying a more recent gcc version")
        print("   might work too.")
        sys.exit(1)

    # Sanity checks
    check_windows_bios_date(rawdata)
    check_windows_bios_version(rawdata)

    start = symbols['code32flat_start'].offset
    end = symbols['code32flat_end'].offset
    expend = layoutrom.BUILD_BIOS_ADDR + layoutrom.BUILD_BIOS_SIZE
    if end != expend:
        print("Error!  Code does not end at 0x%x (got 0x%x)" % (
            expend, end))
        sys.exit(1)
    if datasize > finalsize:
        print("Error!  Code is too big (0x%x vs 0x%x)" % (
            datasize, finalsize))
        sys.exit(1)
    expdatasize = end - start
    if datasize != expdatasize:
        print("Error!  Unknown extra data (0x%x vs 0x%x)" % (
            datasize, expdatasize))
        sys.exit(1)

    # Fix up CSM Compatibility16 table
    if 'csm_compat_table' in symbols and 'entry_csm' in symbols:
        # Field offsets within EFI_COMPATIBILITY16_TABLE
        ENTRY_FIELD_OFS = 14 # Compatibility16CallOffset (UINT16)
        SIZE_FIELD_OFS = 5   # TableLength (UINT8)
        CSUM_FIELD_OFS = 4   # TableChecksum (UINT8)

        tableofs = symbols['csm_compat_table'].offset - symbols['code32flat_start'].offset
        entry_addr = symbols['entry_csm'].offset - layoutrom.BUILD_BIOS_ADDR
        entry_addr = struct.pack('<H', entry_addr)
        rawdata = subst(rawdata, tableofs+ENTRY_FIELD_OFS, entry_addr)

        tsfield = tableofs+SIZE_FIELD_OFS
        tablesize = ord(rawdata[tsfield:tsfield+1])
        rawdata = checksum(rawdata, tableofs, tablesize, CSUM_FIELD_OFS)

    # Print statistics
    runtimesize = end - symbols['code32init_end'].offset
    print("Total size: %d  Fixed: %d  Free: %d (used %.1f%% of %dKiB rom)" % (
        datasize, runtimesize, finalsize - datasize
        , (datasize / float(finalsize)) * 100.0
        , int(finalsize / 1024)))

    # Write final file
    f = open(outfile, 'wb')
    f.write((as_bytes("\0") * (finalsize - datasize)) + rawdata)
    f.close()

if __name__ == '__main__':
    main()
