/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    evryfs.c

Abstract:

    EVRYFS filesystem logic: initialisation / auto-format, file write,
    file read, and directory listing. All metadata is cached in RAM and
    flushed to disk on every write.

    On-disk layout:
        LBA 0  -- EVRYFS_SUPER  (512 bytes)
        LBA 1  -- Root dir      (12 x EVRYFS_DIRENT, 480 bytes used)
        LBA 2+ -- File data     (sequential allocation)

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#include "evryfs.h"

/* ---- Module-private state ---------------------------------------------- */

static uint8_t s_superbuf[512];     /* cached superblock sector              */
static uint8_t s_dirbuf [512];      /* cached directory sector               */
static int     s_present = 0;       /* 1 after successful EvryFsInit         */

/* ---- Internal helpers -------------------------------------------------- */

static EVRYFS_SUPER* Super(void)
{
    return (EVRYFS_SUPER*)s_superbuf;
}

static EVRYFS_DIRENT* DirEntry(int i)
{
    return (EVRYFS_DIRENT*)(s_dirbuf + (unsigned)i * sizeof(EVRYFS_DIRENT));
}

static int FsStrEq(const char* a, const char* b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == 0 && *b == 0);
}

static void FsStrCpy(char* dst, const char* src, int n)
{
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ---- Public API -------------------------------------------------------- */

/*++

Routine Description:

    Detects the ATA drive and reads the superblock. If the magic number is
    absent (fresh disk) the volume is formatted in place: an empty superblock
    and an empty directory sector are written.

Arguments:

    None.

Return Value:

    0 on success. -1 if no ATA drive is detected.

--*/
int EvryFsInit(void)
{
    s_present = 0;

    if (AtaReadSector(EVRYFS_SUPER_LBA, s_superbuf) < 0)
        return -1;

    EVRYFS_SUPER* super = Super();

    if (super->magic != EVRYFS_MAGIC) {
        /* Fresh disk -- format */
        uint8_t zero[512];
        for (int i = 0; i < 512; i++) zero[i] = 0;

        for (int i = 0; i < 512; i++) s_superbuf[i] = 0;
        for (int i = 0; i < 512; i++) s_dirbuf [i] = 0;

        super->magic         = EVRYFS_MAGIC;
        super->version       = EVRYFS_VERSION;
        super->next_free_lba = EVRYFS_DATA_START;

        if (AtaWriteSector(EVRYFS_SUPER_LBA, s_superbuf) < 0) return -1;
        if (AtaWriteSector(EVRYFS_DIR_LBA,   s_dirbuf)   < 0) return -1;
    } else {
        if (AtaReadSector(EVRYFS_DIR_LBA, s_dirbuf) < 0) return -1;
    }

    s_present = 1;
    return 0;
}

/*++

Routine Description:

    Creates or overwrites a file on the EVRYFS volume. The file data is
    written to consecutive LBAs starting at next_free_lba; the directory
    and superblock are flushed afterwards.

Arguments:

    name - Null-terminated filename (max EVRYFS_NAME_LEN - 1 chars).
    data - Pointer to the file contents.
    len  - Length of data in bytes.

Return Value:

    0 on success. -1 if no disk is present or the directory is full.

--*/
int EvryFsWriteFile(const char* name, const uint8_t* data, int len)
{
    if (!s_present) return -1;

    /* Find a free slot, or an existing entry with the same name */
    int slot = -1;
    for (int i = 0; i < EVRYFS_MAX_FILES; i++) {
        EVRYFS_DIRENT* d = DirEntry(i);
        if ((d->flags & 1) && FsStrEq(d->name, name)) { slot = i; break; }
        if (!(d->flags & 1) && slot < 0)               slot = i;
    }
    if (slot < 0) return -1;    /* directory full */

    EVRYFS_SUPER*  s = Super();
    EVRYFS_DIRENT* d = DirEntry(slot);

    /* Allocate data LBAs */
    int      sectors   = (len + 511) / 512;
    uint32_t start_lba = s->next_free_lba;
    s->next_free_lba  += (uint32_t)sectors;

    /* Write data */
    uint8_t sector_buf[512];
    for (int sec = 0; sec < sectors; sec++) {
        for (int b = 0; b < 512; b++) sector_buf[b] = 0;
        int offset = sec * 512;
        int chunk  = len - offset;
        if (chunk > 512) chunk = 512;
        for (int b = 0; b < chunk; b++) sector_buf[b] = data[offset + b];
        if (AtaWriteSector(start_lba + (uint32_t)sec, sector_buf) < 0)
            return -1;
    }

    /* Update directory entry */
    FsStrCpy(d->name, name, EVRYFS_NAME_LEN);
    d->flags     = 1;
    d->start_lba = start_lba;
    d->size      = (uint32_t)len;

    /* Flush directory and superblock */
    if (AtaWriteSector(EVRYFS_DIR_LBA,   s_dirbuf)   < 0) return -1;
    if (AtaWriteSector(EVRYFS_SUPER_LBA, s_superbuf)  < 0) return -1;

    return 0;
}

/*++

Routine Description:

    Reads the contents of a named file into buf.

Arguments:

    name   - Null-terminated filename to look up.
    buf    - Destination buffer.
    maxlen - Maximum bytes to copy into buf.

Return Value:

    Number of bytes copied on success. -1 if the file was not found or
    no disk is present.

--*/
int EvryFsReadFile(const char* name, uint8_t* buf, int maxlen)
{
    if (!s_present) return -1;

    for (int i = 0; i < EVRYFS_MAX_FILES; i++) {
        EVRYFS_DIRENT* d = DirEntry(i);
        if (!(d->flags & 1))          continue;
        if (!FsStrEq(d->name, name))  continue;

        int     total   = (int)d->size;
        if (total > maxlen) total = maxlen;
        int     sectors = ((int)d->size + 511) / 512;
        uint8_t sector_buf[512];
        int     copied  = 0;

        for (int sec = 0; sec < sectors && copied < total; sec++) {
            if (AtaReadSector(d->start_lba + (uint32_t)sec, sector_buf) < 0)
                return -1;
            int chunk = total - copied;
            if (chunk > 512) chunk = 512;
            for (int b = 0; b < chunk; b++) buf[copied + b] = sector_buf[b];
            copied += chunk;
        }
        return copied;
    }
    return -1;
}

/*++

Routine Description:

    Returns all in-use directory entries as parallel name and size arrays.

Arguments:

    names - Caller-supplied array of EVRYFS_MAX_FILES string slots.
    sizes - Caller-supplied array of EVRYFS_MAX_FILES uint32_t slots.
    count - Receives the number of valid entries written.

Return Value:

    None. *count is zero if no disk is present or the volume is empty.

--*/
void EvryFsList(char names[][EVRYFS_NAME_LEN], uint32_t sizes[], int* count)
{
    *count = 0;
    if (!s_present) return;

    for (int i = 0; i < EVRYFS_MAX_FILES; i++) {
        EVRYFS_DIRENT* d = DirEntry(i);
        if (!(d->flags & 1)) continue;
        FsStrCpy(names[*count], d->name, EVRYFS_NAME_LEN);
        sizes[*count] = d->size;
        (*count)++;
    }
}
