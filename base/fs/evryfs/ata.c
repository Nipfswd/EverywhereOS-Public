/*++

Copyright (c) 2026  The EverywhereOS Authors. All Rights Reserved.

Module Name:

    ata.c

Abstract:

    ATA PIO 28-bit LBA single-sector read and write for the EVRYFS
    filesystem driver. Targets the primary ATA channel (base 0x1F0).

    Floating-bus detection: if the status register reads 0xFF the drive
    is absent and the call returns -1 immediately.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only

--*/

#include "evryfs.h"

/* ---- Port-level helpers ------------------------------------------------ */

static inline uint8_t AtaInb(uint16_t port)
{
    uint8_t r;
    __asm__ __volatile__("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static inline void AtaOutb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t AtaInw(uint16_t port)
{
    uint16_t r;
    __asm__ __volatile__("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static inline void AtaOutw(uint16_t port, uint16_t val)
{
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* ---- ATA register map (primary channel) -------------------------------- */

#define ATA_REG_DATA     0x1F0
#define ATA_REG_FEATURES 0x1F1
#define ATA_REG_SECCNT   0x1F2
#define ATA_REG_LBA0     0x1F3
#define ATA_REG_LBA1     0x1F4
#define ATA_REG_LBA2     0x1F5
#define ATA_REG_DRIVE    0x1F6
#define ATA_REG_STATUS   0x1F7
#define ATA_REG_CMD      0x1F7

#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30

#define ATA_SR_BSY       0x80
#define ATA_SR_DRDY      0x40
#define ATA_SR_DRQ       0x08
#define ATA_SR_ERR       0x01

#define ATA_NO_DRIVE     0xFF   /* floating bus */

/* ---- Internal helpers -------------------------------------------------- */

/*++

Routine Description:

    Spins until the ATA BSY bit clears. Returns -1 on timeout or if the
    drive is absent (floating-bus 0xFF).

--*/
static int AtaWaitBsy(void)
{
    uint8_t  s;
    int      timeout = 0x100000;

    do {
        s = AtaInb(ATA_REG_STATUS);
        if (s == ATA_NO_DRIVE) return -1;
        if (--timeout == 0)    return -1;
    } while (s & ATA_SR_BSY);

    if (s & ATA_SR_ERR) return -1;
    return 0;
}

/*++

Routine Description:

    Spins until DRQ is set (drive is ready to transfer data). Returns -1
    on error or if the drive is absent.

--*/
static int AtaWaitDrq(void)
{
    uint8_t  s;
    int      timeout = 0x100000;

    do {
        s = AtaInb(ATA_REG_STATUS);
        if (s == ATA_NO_DRIVE)       return -1;
        if (s & ATA_SR_ERR)          return -1;
        if (--timeout == 0)          return -1;
    } while (!(s & ATA_SR_DRQ));

    return 0;
}

/*++

Routine Description:

    Programs LBA28 registers for the primary master drive.

--*/
static void AtaSetupLba28(uint32_t lba)
{
    AtaOutb(ATA_REG_DRIVE,  0xE0 | (uint8_t)((lba >> 24) & 0x0F));
    AtaOutb(ATA_REG_SECCNT, 1);
    AtaOutb(ATA_REG_LBA0,   (uint8_t)(lba));
    AtaOutb(ATA_REG_LBA1,   (uint8_t)(lba >>  8));
    AtaOutb(ATA_REG_LBA2,   (uint8_t)(lba >> 16));
}

/* ---- Public interface -------------------------------------------------- */

/*++

Routine Description:

    Reads one 512-byte sector at LBA lba into buf via PIO.

Arguments:

    lba - 28-bit logical block address.
    buf - Destination buffer; must be at least 512 bytes.

Return Value:

    0 on success, -1 if the drive is absent or an ATA error occurs.

--*/
int AtaReadSector(uint32_t lba, uint8_t* buf)
{
    if (AtaWaitBsy()    < 0) return -1;
    AtaSetupLba28(lba);
    AtaOutb(ATA_REG_CMD, ATA_CMD_READ);
    if (AtaWaitDrq()    < 0) return -1;

    uint16_t* w = (uint16_t*)buf;
    for (int i = 0; i < 256; i++) w[i] = AtaInw(ATA_REG_DATA);

    return 0;
}

/*++

Routine Description:

    Writes one 512-byte sector at LBA lba from buf via PIO.

Arguments:

    lba - 28-bit logical block address.
    buf - Source buffer; must be at least 512 bytes.

Return Value:

    0 on success, -1 if the drive is absent or an ATA error occurs.

--*/
int AtaWriteSector(uint32_t lba, const uint8_t* buf)
{
    if (AtaWaitBsy()    < 0) return -1;
    AtaSetupLba28(lba);
    AtaOutb(ATA_REG_CMD, ATA_CMD_WRITE);
    if (AtaWaitDrq()    < 0) return -1;

    const uint16_t* w = (const uint16_t*)buf;
    for (int i = 0; i < 256; i++) AtaOutw(ATA_REG_DATA, w[i]);

    /* Wait for write to complete */
    return AtaWaitBsy();
}
