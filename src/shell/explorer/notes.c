/*++

Copyright (c) 2026  Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    notes.c

Abstract:

    Notes window text buffer and drawing.

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Userspace

--*/

#include "explorer.h"

char notes_buf[NOTES_BUF_SIZE];
int  notes_len = 0;

/*++

Routine Description:

    Draws the notes text inside the notes window.

Arguments:

    None.

Return Value:

    None.

--*/

void NotesDraw(void) {
    if (!NotesWin.visible || NotesWin.minimized) return;
    DrawString(NotesWin.x + 4, NotesWin.y + 14, notes_buf, 0x0F);
}
