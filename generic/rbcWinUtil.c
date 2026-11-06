/**
 * @file rbcWinUtil.c
 * @brief Windows system-error formatting.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcInt.h"

/**
 * @brief Format the current Windows error in borrowed static storage.
 * @ingroup windows_bitmap_images
 *
 * Captures GetLastError before formatting. Uses FormatMessageA with the system
 * default language and removes trailing CR/LF. Falls back to a numeric error
 * string if formatting fails. The returned 1024-byte buffer is overwritten by
 * later calls, is not thread-local, and must not be freed. Text is in the
 * Windows ANSI encoding rather than guaranteed UTF-8; copy/convert if retained
 * or exposed through a Tcl API expecting UTF-8.
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
