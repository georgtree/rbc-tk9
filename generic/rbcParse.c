/*
 * rbcParse.c --
 *
 *      TODO: Description
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcParse.h"
#include <string.h>
#include <limits.h>

/*
 *--------------------------------------------------------------
 *
 * Rbc_ExpandParseValue --
 *
 *      This procedure is commonly used as the value of the
 *      expandProc in a ParseValue.  It uses malloc to allocate
 *      more space for the result of a parse.
 *
 * Parameters:
 *      ParseValue *parsePtr - Information about buffer that must be expanded.  If the clientData in the structure is 
 *                             non-zero, it means that the current buffer is dynamically allocated.
 *      int needed - Minimum amount of additional space to allocate.
 *
 * Results:
 *      The buffer space in *parsePtr is reallocated to something
 *      larger, and if parsePtr->clientData is non-zero the old
 *      buffer is freed.  Information is copied from the old
 *      buffer to the new one.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------
 */
void Rbc_ExpandParseValue(ParseValue *parsePtr, int needed) {
    int size;
    char *buffer;

    /*
     * Either double the size of the buffer or add enough new space
     * to meet the demand, whichever produces a larger new buffer.
     */
    size = (parsePtr->end - parsePtr->buffer) + 1;
    if (size < needed) {
        size += needed;
    } else {
        size += size;
    }
    buffer = (char *)ckalloc((unsigned int)size);

    /*
     * Copy from old buffer to new, free old buffer if needed, and
     * mark new buffer as malloc-ed.
     */
    memcpy((void *)buffer, (void *)parsePtr->buffer, (size_t)(parsePtr->next - parsePtr->buffer));
    parsePtr->next = buffer + (parsePtr->next - parsePtr->buffer);
    if (parsePtr->clientData != 0) {
        ckfree((char *)parsePtr->buffer);
    }
    parsePtr->buffer = buffer;
    parsePtr->end = buffer + size - 1;
    parsePtr->clientData = (ClientData)1;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_ParseNestedCmd --
 *
 *      This procedure parses a nested Tcl command between
 *      brackets, returning the result of the command.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter to use for nested command evaluations and error messages.
 *      char *string - Character just after opening bracket.
 *      int flags - Flags to pass to nested Tcl_Eval.
 *      char **termPtr - Store address of terminating character here.
 *      ParseValue *parsePtr - Information about where to place result of command.
 *
 * Results:
 *      The return value is a standard Tcl result, which is
 *      TCL_OK unless there was an error while executing the
 *      nested command.  If an error occurs then the interpreter result
 *      contains a standard error message.  *TermPtr is filled
 *      in with the address of the character just after the
 *      last one processed;  this is usually the character just
 *      after the matching close-bracket, or the null character
 *      at the end of the string if the close-bracket was missing
 *      (a missing close bracket is an error).  The result returned
 *      by the command is stored in standard fashion in *parsePtr,
 *      null-terminated, with parsePtr->next pointing to the null
 *      character.
 *
 * Side effects:
 *      The storage space at *parsePtr may be expanded.
 *--------------------------------------------------------------
 */
int Rbc_ParseNestedCmd(Tcl_Interp *interp, char *string, int flags, char **termPtr, ParseValue *parsePtr) {
    Tcl_Parse parse;
    Tcl_Obj *resultObj;
    const char *scanPtr;
    const char *commandEnd;
    const char *closePtr;
    const char *value;
    Tcl_Size scriptLength;
    Tcl_Size length;
    Tcl_Size available;
    Tcl_Size needed;
    int result;

    scanPtr = string;
    closePtr = NULL;
    /*
     * Find the close bracket using Tcl's public command parser.
     *
     * A command substitution may contain multiple commands, so parse
     * one command at a time until Tcl reports a command terminated by
     * the outer ']'.
     */
    for (;;) {
        result = Tcl_ParseCommand(interp, scanPtr, -1, 1, &parse);
        if (result != TCL_OK) {
            *termPtr = string + strlen(string);
            return result;
        }
        if (parse.commandSize <= 0) {
            Tcl_FreeParse(&parse);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("missing close-bracket", -1));
            *termPtr = string + strlen(string);
            return TCL_ERROR;
        }
        commandEnd = parse.commandStart + parse.commandSize;
        if ((commandEnd > parse.commandStart) && (commandEnd[-1] == ']')) {
            closePtr = commandEnd - 1;
            Tcl_FreeParse(&parse);
            break;
        }
        Tcl_FreeParse(&parse);
        if (*commandEnd == '\0') {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("missing close-bracket", -1));
            *termPtr = (char *)commandEnd;
            return TCL_ERROR;
        }
        scanPtr = commandEnd;
    }
    /*
     * The caller expects termPtr to point immediately after the
     * matching close bracket.
     */
    *termPtr = (char *)(closePtr + 1);
    /*
     * Evaluate exactly the script inside the brackets.  This replaces
     * the old TCL_BRACKET_TERM manipulation of Tcl's private Interp
     * structure.
     */
    scriptLength = (Tcl_Size)(closePtr - string);
    result = Tcl_EvalEx(interp, string, scriptLength, flags);
    if (result != TCL_OK) {
        return result;
    }

    /*
     * Copy the command result through the public Tcl object API.
     */
    resultObj = Tcl_GetObjResult(interp);
    value = Tcl_GetStringFromObj(resultObj, &length);
    available = (Tcl_Size)(parsePtr->end - parsePtr->next);
    if (length > available) {
        needed = length - available;
        /*
         * ParseValue's legacy expansion interface still takes int.
         * Do not narrow a Tcl_Size silently.
         */
        if (needed > INT_MAX) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("vector expression substitution result is too large", -1));
            return TCL_ERROR;
        }
        (*parsePtr->expandProc)(parsePtr, (int)needed);
    }
    if (length > 0) {
        memcpy(parsePtr->next, value, (size_t)length);
        parsePtr->next += length;
    }
    *parsePtr->next = '\0';
    Tcl_ResetResult(interp);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_ParseBraces --
 *
 *      This procedure scans the information between matching
 *      curly braces.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter to use for nested command evaluations and error messages.
 *      char *string - Character just after opening bracket.
 *      char **termPtr - Store address of terminating character here.
 *      ParseValue *parsePtr - Information about where to place result of command.
 *
 * Results:
 *      The return value is a standard Tcl result, which is
 *      TCL_OK unless there was an error while parsing string.
 *      If an error occurs then the interpreter result contains a
 *      standard error message.  *TermPtr is filled
 *      in with the address of the character just after the
 *      last one successfully processed;  this is usually the
 *      character just after the matching close-brace.  The
 *      information between curly braces is stored in standard
 *      fashion in *parsePtr, null-terminated with parsePtr->next
 *      pointing to the terminating null character.
 *
 * Side effects:
 *      The storage space at *parsePtr may be expanded.
 *
 *--------------------------------------------------------------
 */
int Rbc_ParseBraces(Tcl_Interp *interp, char *string, char **termPtr, ParseValue *parsePtr) {
    int level;
    register char *src, *dest, *end;
    register char c;
    char *lastChar = string + strlen(string);

    src = string;
    dest = parsePtr->next;
    end = parsePtr->end;
    level = 1;

    /*
     * Copy the characters one at a time to the result area, stopping
     * when the matching close-brace is found.
     */

    for (;;) {
        c = *src;
        src++;

        if (dest == end) {
            parsePtr->next = dest;
            (*parsePtr->expandProc)(parsePtr, 20);
            dest = parsePtr->next;
            end = parsePtr->end;
        }
        *dest = c;
        dest++;

        if (CHAR_TYPE(src - 1, lastChar) == TCL_NORMAL) {
            continue;
        } else if (c == '{') {
            level++;
        } else if (c == '}') {
            level--;
            if (level == 0) {
                dest--; /* Don't copy the last close brace. */
                break;
            }
        } else if (c == '\\') {
            int count;

            /*
             * Must always squish out backslash-newlines, even when in
             * braces.  This is needed so that this sequence can appear
             * anywhere in a command, such as the middle of an expression.
             */

            if (*src == '\n') {
                //                dest[-1] = Tcl_UtfBackslash(src - 1, &count);
                Tcl_UtfBackslash(src - 1, &count, dest - 1);
                src += count - 1;
            } else {
                char dummy[4];
                Tcl_UtfBackslash(src - 1, &count, dummy);
                while (count > 1) {
                    if (dest == end) {
                        parsePtr->next = dest;
                        (*parsePtr->expandProc)(parsePtr, 20);
                        dest = parsePtr->next;
                        end = parsePtr->end;
                    }
                    *dest = *src;
                    dest++;
                    src++;
                    count--;
                }
            }
        } else if (c == '\0') {
            Tcl_AppendResult(interp, "missing close-brace", (char *)NULL);
            *termPtr = string - 1;
            return TCL_ERROR;
        }
    }

    *dest = '\0';
    parsePtr->next = dest;
    *termPtr = src;
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_ParseQuotes --
 *
 *      This procedure parses a double-quoted string such as a
 *      quoted Tcl command argument or a quoted value in a Tcl
 *      expression.  This procedure is also used to parse array
 *      element names within parentheses, or anything else that
 *      needs all the substitutions that happen in quotes.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter to use for nested command evaluations and error messages.
 *      char *string - Character just after opening double-quote.
 *      int termChar - Character that terminates "quoted" string (usually double-quote, but sometimes right-paren or 
 *                     something else).
 *      int flags - Flags to pass to nested Tcl_Eval calls.
 *      char **termPtr - Store address of terminating character here.
 *      ParseValue *parsePtr - Information about where to place fully-substituted result of parse.
 *
 * Results:
 *      The return value is a standard Tcl result, which is
 *      TCL_OK unless there was an error while parsing the
 *      quoted string.  If an error occurs then the interpreter result
 *      contains a standard error message.  *TermPtr is filled
 *      in with the address of the character just after the
 *      last one successfully processed;  this is usually the
 *      character just after the matching close-quote.  The
 *      fully-substituted contents of the quotes are stored in
 *      standard fashion in *parsePtr, null-terminated with
 *      parsePtr->next pointing to the terminating null character.
 *
 * Side effects:
 *      The buffer space in parsePtr may be enlarged by calling its
 *      expandProc.
 *
 *--------------------------------------------------------------
 */
int Rbc_ParseQuotes(Tcl_Interp *interp, char *string, int termChar, int flags, char **termPtr, ParseValue *parsePtr) {
    register char *src, *dest, c;
    char *lastChar = string + strlen(string);

    src = string;
    dest = parsePtr->next;

    for (;;) {
        if (dest == parsePtr->end) {
            /*
             * Target buffer space is about to run out.  Make more space.
             */
            parsePtr->next = dest;
            (*parsePtr->expandProc)(parsePtr, 1);
            dest = parsePtr->next;
        }
        c = *src;
        src++;
        if (c == termChar) {
            *dest = '\0';
            parsePtr->next = dest;
            *termPtr = src;
            return TCL_OK;
        } else if (CHAR_TYPE(src - 1, lastChar) == TCL_NORMAL) {
        copy:
            *dest = c;
            dest++;
            continue;
        } else if (c == '$') {
            int length;
            const char *value;
            /* (const char**) */
            value = Tcl_ParseVar(interp, src - 1, (const char **)termPtr);
            if (value == NULL) {
                return TCL_ERROR;
            }
            src = *termPtr;
            length = strlen(value);
            if ((parsePtr->end - dest) <= length) {
                parsePtr->next = dest;
                (*parsePtr->expandProc)(parsePtr, length);
                dest = parsePtr->next;
            }
            strcpy(dest, value);
            dest += length;
            continue;
        } else if (c == '[') {
            int result;

            parsePtr->next = dest;
            result = Rbc_ParseNestedCmd(interp, src, flags, termPtr, parsePtr);
            if (result != TCL_OK) {
                return result;
            }
            src = *termPtr;
            dest = parsePtr->next;
            continue;
        } else if (c == '\\') {
            int nRead, nWritten;

            src--;
            //            *dest = Tcl_Backslash(src, &nRead);
            //            dest++;
            nWritten = Tcl_UtfBackslash(src, &nRead, dest);
            dest += nWritten;
            src += nRead;
            continue;
        } else if (c == '\0') {
            char buf[30];

            Tcl_ResetResult(interp);
            snprintf(buf, sizeof(buf), "missing %c", termChar);
            Tcl_SetResult(interp, buf, TCL_VOLATILE);
            *termPtr = string - 1;
            return TCL_ERROR;
        } else {
            goto copy;
        }
    }
}
