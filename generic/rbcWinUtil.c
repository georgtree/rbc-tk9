/*
 * rbcWinUtil.c --
 *
 *      This module contains WIN32 routines not included in the Tcl/Tk
 *      libraries.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcInt.h"

/*
 *--------------------------------------------------------------
 *
 * Rbc_GetPlatformId --
 *
 *      TODO: Description
 *
 * Parameters:
 *      None
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int Rbc_GetPlatformId(void) {
    static int platformId = 0;
    if (platformId == 0) {
        OSVERSIONINFO opsysInfo;

        opsysInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
        if (GetVersionEx(&opsysInfo)) {
            platformId = opsysInfo.dwPlatformId;
        }
    }
    return platformId;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_LastError --
 *
 *      TODO: Description
 *
 * Parameters:
 *      None
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
char *Rbc_LastError(void) {
    static char buffer[1024];
    DWORD error;
    DWORD length;

    error = GetLastError();

    length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error,
                            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, (DWORD)sizeof(buffer), NULL);
    if (length == 0) {
        snprintf(buffer, sizeof(buffer), "Windows error %lu", (unsigned long)error);
        return buffer;
    }
    /*
     * FormatMessage normally terminates system messages with CR/LF.
     */
    while ((length > 0) && ((buffer[length - 1] == '\r') || (buffer[length - 1] == '\n'))) {
        buffer[--length] = '\0';
    }
    return buffer;
}
