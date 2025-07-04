/*
 * rbcParse.h --
 *
 *      TODO:Description
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCPARSE
#define _RBCPARSE

#include "tcl.h"

#define TCL_BRACKET_TERM 1

/*
 * A table used to classify input characters to assist in parsing
 * Tcl commands.  The table should be indexed with a signed character
 * using the CHAR_TYPE macro.  The character may have a negative
 * value.  The CHAR_TYPE macro takes a pointer to a signed character
 * and a pointer to the last character in the source string.  If the
 * src pointer is pointing at the terminating null of the string,
 * CHAR_TYPE returns TCL_COMMAND_END.
 */
#ifndef TCL_RESULT_SIZE
#define TCL_RESULT_SIZE 200
#endif

#define STATIC_STRING_SPACE 150
#define UCHAR(c) ((unsigned char)(c))
#define TCL_NORMAL 0x01
#define TCL_SPACE 0x02
#define TCL_COMMAND_END 0x04
#define TCL_QUOTE 0x08
#define TCL_OPEN_BRACKET 0x10
#define TCL_OPEN_BRACE 0x20
#define TCL_CLOSE_BRACE 0x40
#define TCL_BACKSLASH 0x80
#define TCL_DOLLAR 0x00

static unsigned char tclTypeTable[] = {
    /*
     * Negative character values, from -128 to -1:
     */

    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,

    /*
     * Positive character values, from 0-127:
     */

    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_SPACE,
    TCL_COMMAND_END,
    TCL_SPACE,
    TCL_SPACE,
    TCL_SPACE,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_SPACE,
    TCL_NORMAL,
    TCL_QUOTE,
    TCL_NORMAL,
    TCL_DOLLAR,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_COMMAND_END,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_OPEN_BRACKET,
    TCL_BACKSLASH,
    TCL_COMMAND_END,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_OPEN_BRACE,
    TCL_NORMAL,
    TCL_CLOSE_BRACE,
    TCL_NORMAL,
    TCL_NORMAL,

    /*
     * Large unsigned character values, from 128-255:
     */

    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
    TCL_NORMAL,
};

#define CHAR_TYPE(src, last) (((src) == (last)) ? TCL_COMMAND_END : (tclTypeTable + 128)[(int)*(src)])

/*
 *    The following data structure is used by various parsing
 *    procedures to hold information about where to store the
 *    results of parsing (e.g. the substituted contents of a quoted
 *    argument, or the result of a nested command).  At any given
 *    time, the space available for output is fixed, but a procedure
 *    may be called to expand the space available if the current
 *    space runs out.
 */
typedef struct ParseValueStruct ParseValue;

struct ParseValueStruct {
    char *buffer; /* Address of first character in
                   * output buffer. */
    char *next;   /* Place to store next character in
                   * output buffer. */
    char *end;    /* Address of the last usable character
                   * in the buffer. */
    void (*expandProc)(ParseValue *pvPtr, int needed);
    /* Procedure to call when space runs out;
     * it will make more space. */
    ClientData clientData; /* Arbitrary information for use of
                            * expandProc. */
};

typedef void **TclHandle;
typedef struct CallFrameStruct CallFrame;
typedef struct NamespaceStruct Namespace;
typedef struct ActiveVarTraceStruct ActiveVarTrace;
typedef struct ProcStruct Proc;
typedef struct TraceStruct Trace;

typedef struct TclRegexpStruct TclRegexp;
typedef struct ExecEnvStruct ExecEnv;

typedef struct LiteralEntryStruct LiteralEntry;
struct LiteralEntryStruct {
    LiteralEntry *nextPtr; /* Points to next entry in this
                            * hash bucket or NULL if end of
                            * chain. */
    Tcl_Obj *objPtr;       /* Points to Tcl object that
                            * holds the literal's bytes and
                            * length. */
    int refCount;          /* If in an interpreter's global
                            * literal table, the number of
                            * ByteCode structures that share
                            * the literal object; the literal
                            * entry can be freed when refCount
                            * drops to 0. If in a local literal
                            * table, -1. */
};

typedef struct {
    LiteralEntry **buckets; /* Pointer to bucket array. Each
                             * element points to first entry in
                             * bucket's hash chain, or NULL. */
    LiteralEntry *staticBuckets[TCL_SMALL_HASH_TABLE];
    /* Bucket array used for small
     * tables to avoid mallocs and
     * frees. */
    int numBuckets;  /* Total number of buckets allocated
                      * at **buckets. */
    int numEntries;  /* Total number of entries present
                      * in table. */
    int rebuildSize; /* Enlarge table when numEntries
                      * gets to be this large. */
    int mask;        /* Mask value used in hashing
                      * function. */
} LiteralTable;

/*
 *----------------------------------------------------------------
 * This structure defines an interpreter, which is a collection of
 * commands plus other state information related to interpreting
 * commands, such as variable storage. Primary responsibility for
 * this data structure is in tclBasic.c, but almost every Tcl
 * source file uses something in here.
 *----------------------------------------------------------------
 */

typedef struct {

    /*
     * Note:  the first three fields must match exactly the fields in
     * a Tcl_Interp struct (see tcl.h).  If you change one, be sure to
     * change the other.
     *
     * The interpreter's result is held in both the string and the
     * objResultPtr fields. These fields hold, respectively, the result's
     * string or object value. The interpreter's result is always in the
     * result field if that is non-empty, otherwise it is in objResultPtr.
     * The two fields are kept consistent unless some C code sets
     * the interpreter result directly. Programs should not access result and
     * objResultPtr directly; instead, they should always get and set the
     * result using procedures such as Tcl_SetObjResult, Tcl_GetObjResult,
     * and Tcl_GetStringResult. See the SetResult man page for details.
     */

    char *result;           /* If the last command returned a string
                             * result, this points to it. Should not be
                             * accessed directly; see comment above. */
    Tcl_FreeProc *freeProc; /* Zero means a string result is statically
                             * allocated. TCL_DYNAMIC means string
                             * result was allocated with ckalloc and
                             * should be freed with ckfree. Other values
                             * give address of procedure to invoke to
                             * free the string result. Tcl_Eval must
                             * free it before executing next command. */
    int errorLine;          /* When TCL_ERROR is returned, this gives
                             * the line number in the command where the
                             * error occurred (1 means first line). */
    Tcl_Obj *objResultPtr;  /* If the last command returned an object
                             * result, this points to it. Should not be
                             * accessed directly; see comment above. */

    TclHandle handle; /* Handle used to keep track of when this
                       * interp is deleted. */

    Namespace *globalNsPtr; /* The interpreter's global namespace. */
    Tcl_HashTable *hiddenCmdTablePtr;
    /* Hash table used by tclBasic.c to keep
     * track of hidden commands on a per-interp
     * basis. */
    ClientData interpInfo;       /* Information used by tclInterp.c to keep
                                  * track of master/slave interps on
                                  * a per-interp basis. */
    Tcl_HashTable mathFuncTable; /* Contains all the math functions currently
                                  * defined for the interpreter.  Indexed by
                                  * strings (function names); values have
                                  * type (MathFunc *). */

    /*
     * Information related to procedures and variables. See tclProc.c
     * and tclvar.c for usage.
     */

    int numLevels;          /* Keeps track of how many nested calls to
                             * Tcl_Eval are in progress for this
                             * interpreter.  It's used to delay deletion
                             * of the table until all Tcl_Eval
                             * invocations are completed. */
    int maxNestingDepth;    /* If numLevels exceeds this value then Tcl
                             * assumes that infinite recursion has
                             * occurred and it generates an error. */
    CallFrame *framePtr;    /* Points to top-most in stack of all nested
                             * procedure invocations.  NULL means there
                             * are no active procedures. */
    CallFrame *varFramePtr; /* Points to the call frame whose variables
                             * are currently in use (same as framePtr
                             * unless an "uplevel" command is
                             * executing). NULL means no procedure is
                             * active or "uplevel 0" is executing. */
    ActiveVarTrace *activeTracePtr;
    /* First in list of active traces for
     * interp, or NULL if no active traces. */
    int returnCode;  /* Completion code to return if current
                      * procedure exits with TCL_RETURN code. */
    char *errorInfo; /* Value to store in errorInfo if returnCode
                      * is TCL_ERROR.  Malloc'ed, may be NULL */
    char *errorCode; /* Value to store in errorCode if returnCode
                      * is TCL_ERROR.  Malloc'ed, may be NULL */

    /*
     * Information used by Tcl_AppendResult to keep track of partial
     * results.  See Tcl_AppendResult code for details.
     */

    char *appendResult; /* Storage space for results generated
                         * by Tcl_AppendResult.  Malloc-ed.  NULL
                         * means not yet allocated. */
    int appendAvl;      /* Total amount of space available at
                         * partialResult. */
    int appendUsed;     /* Number of non-null bytes currently
                         * stored at partialResult. */

    /*
     * A cache of compiled regular expressions.  See Tcl_RegExpCompile
     * in tclUtil.c for details.  THIS CACHE IS OBSOLETE and is only
     * retained for backward compatibility with Tcl_RegExpCompile.
     * New code should use the object interface so the Tcl_Obj caches
     * the compiled expression.
     */

#define NUM_REGEXPS 5
    char *patterns[NUM_REGEXPS]; /* Strings corresponding to compiled
                                  * regular expression patterns.  NULL
                                  * means that this slot isn't used.
                                  * Malloc-ed. */
    int patLengths[NUM_REGEXPS]; /* Number of non-null characters in
                                  * corresponding entry in patterns.
                                  * -1 means entry isn't used. */
    TclRegexp *regexps[NUM_REGEXPS];
    /* Compiled forms of above strings.  Also
     * malloc-ed, or NULL if not in use yet. */

    /*
     * Information about packages.  Used only in tclPkg.c.
     */

    Tcl_HashTable packageTable; /* Describes all of the packages loaded
                                 * in or available to this interpreter.
                                 * Keys are package names, values are
                                 * (Package *) pointers. */
    char *packageUnknown;       /* Command to invoke during "package
                                 * require" commands for packages that
                                 * aren't described in packageTable.
                                 * Malloc'ed, may be NULL. */

    /*
     * Miscellaneous information:
     */

    int cmdCount;              /* Total number of times a command procedure
                                * has been called for this interpreter. */
    int evalFlags;             /* Flags to control next call to Tcl_Eval.
                                * Normally zero, but may be set before
                                * calling Tcl_Eval.  See below for valid
                                * values. */
    int termOffset;            /* Offset of character just after last one
                                * compiled or executed by Tcl_EvalObj. */
    LiteralTable literalTable; /* Contains LiteralEntry's describing all
                                * Tcl objects holding literals of scripts
                                * compiled by the interpreter. Indexed by
                                * the string representations of literals.
                                * Used to avoid creating duplicate
                                * objects. */
    int compileEpoch;          /* Holds the current "compilation epoch"
                                * for this interpreter. This is
                                * incremented to invalidate existing
                                * ByteCodes when, e.g., a command with a
                                * compile procedure is redefined. */
    Proc *compiledProcPtr;     /* If a procedure is being compiled, a
                                * pointer to its Proc structure; otherwise,
                                * this is NULL. Set by ObjInterpProc in
                                * tclProc.c and used by tclCompile.c to
                                * process local variables appropriately. */
    char *scriptFile;          /* NULL means there is no nested source
                                * command active;  otherwise this points to
                                * the name of the file being sourced (it's
                                * not malloc-ed:  it points to an argument
                                * to Tcl_EvalFile. */
    int flags;                 /* Various flag bits.  See below. */
    long randSeed;             /* Seed used for rand() function. */
    Trace *tracePtr;           /* List of traces for this interpreter. */
    Tcl_HashTable *assocData;  /* Hash table for associating data with
                                * this interpreter. Cleaned up when
                                * this interpreter is deleted. */
    ExecEnv *execEnvPtr;       /* Execution environment for Tcl bytecode
                                * execution. Contains a pointer to the
                                * Tcl evaluation stack. */
    Tcl_Obj *emptyObjPtr;      /* Points to an object holding an empty
                                * string. Returned by Tcl_ObjSetVar2 when
                                * variable traces change a variable in a
                                * gross way. */
    char resultSpace[TCL_RESULT_SIZE + 1];
    /* Static space holding small results. */
    Tcl_ThreadId threadId; /* ID of thread that owns the interpreter */

    /*
     * Statistical information about the bytecode compiler and interpreter's
     * operation.
     */
#ifdef TCL_COMPILE_STATS
    ByteCodeStats stats; /* Holds compilation and execution
                          * statistics for this interpreter. */
#endif                   /* TCL_COMPILE_STATS */
} Interp;

#define TclParseBraces Rbc_ParseBraces
#define TclExpandParseValue Rbc_ExpandParseValue
#define TclParseNestedCmd Rbc_ParseNestedCmd
#define TclParseQuotes Rbc_ParseQuotes

void Rbc_ExpandParseValue(ParseValue *parsePtr, int needed);
int Rbc_ParseNestedCmd(Tcl_Interp *interp, char *string, int flags, char **termPtr, ParseValue *parsePtr);
int Rbc_ParseBraces(Tcl_Interp *interp, char *string, char **termPtr, ParseValue *parsePtr);
int Rbc_ParseQuotes(Tcl_Interp *interp, char *string, int termChar, int flags, char **termPtr, ParseValue *parsePtr);

#endif /* _RBCPARSE */
