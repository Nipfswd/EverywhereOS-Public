/*++

Copyright (c) Everywhere Computing, Inc. All Rights Reserved.

Module Name:

    mouclass.h

Abstract:

    Public header for the mouse class driver

Author:

    Noah Juopperi <nipfswd@gmail.com>

Environment:

    Kernel-mode only.

--*/

#ifndef _MOUCLASS_H_
#define _MOUCLASS_H_

#include <stdint.h>

/* /// Convential on-disk identity*/
#define MOUCLASS_SYS_NAME   "MOUCLASS.SYS"
#define MOUCLASS_SYS_PATH   "C:\\Everywhere\\System32\\drivers\\mouclass.sys"

// Statically-linked build API

extern int mouse_x;
extern int mouse_y;
extern int mouse_buttons;
extern int mouse_prev_buttons;

void InitMouse(void);
void UpdateMouse(void);
void DrawMouseCursor(void);
void MouseIsr(void);

#endif /* _MOUCLASS_H_ */