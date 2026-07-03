/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    files.c

Abstract:

    Files window: displays the EVRYFS directory listing. Each visible
    row shows a filename and its size in bytes.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Userspace

--*/

#include "explorer.h"
#include "evryfs.h"

/* ---- Internal helpers -------------------------------------------------- */

/*++

Routine Description:

    Converts an unsigned 32-bit integer to a decimal ASCII string.

Arguments:

    n   - Value to convert.
    buf - Destination buffer (must hold at least 11 bytes).

Return Value:

    None.

--*/
static void FilesUintToStr(uint32_t n, char* buf)
{
    if (n == 0) { buf[0] = '0'; buf[1] = 0; return; }
    char  tmp[12];
    int   i = 0;
    while (n > 0) { tmp[i++] = (char)('0' + n % 10); n /= 10; }
    int j;
    for (j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[j] = 0;
}

/*++

Routine Description:

    Copies src into dst for at most n-1 characters, always null-terminating.

--*/
static void FilesStrCpy(char* dst, const char* src, int n)
{
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/*++

Routine Description:

    Returns the length of a null-terminated string.

--*/
static int FilesStrLen(const char* s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* ---- Public routines --------------------------------------------------- */

/*++

Routine Description:

    Draws the EVRYFS directory listing inside the Files window. Each entry
    is rendered as "filename   <N> B" on its own row. If the volume has no
    files, "Empty" is shown. If no disk is present, "No disk" is shown.

Arguments:

    None.

Return Value:

    None.

--*/
void FilesDraw(void)
{
    if (!FilesWin.visible || FilesWin.minimized) return;

    char     names[EVRYFS_MAX_FILES][EVRYFS_NAME_LEN];
    uint32_t sizes[EVRYFS_MAX_FILES];
    int      count = 0;

    EvryFsList(names, sizes, &count);

     // Each row is 18 pixels tall to accommodate the 16x16 folder icon.
     // The icon is drawn at the left edge; the filename + size text is
     // indented 18 pixels to the right, vertically centred at icon+5.
    int y = FilesWin.y + 14;

    if (count == 0) {
        DrawString(FilesWin.x + 4, y, "Empty", 0x07);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (y >= FilesWin.y + FilesWin.h - 2) break;
        IconDrawFolder(FilesWin.x + 2, y);

        /* Build row: "name   <size> B" */
        char row[48];
        char sizestr[12];

        FilesStrCpy(row, names[i], 32);

        /* Pad to column 20 */
        int len = FilesStrLen(row);
        while (len < 20 && len < 44) { row[len++] = ' '; }
        row[len] = 0;

        FilesUintToStr(sizes[i], sizestr);

        /* Append size + " B" */
        int slen = FilesStrLen(sizestr);
        for (int j = 0; j < slen && len < 44; j++) row[len++] = sizestr[j];
        if (len < 45) { row[len++] = ' '; }
        if (len < 46) { row[len++] = 'B'; }
        row[len] = 0;

        DrawString(FilesWin.x + 20, y + 5, row, 0x0F);
        y += 18;
    }
}
