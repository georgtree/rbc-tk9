/*
 * rbcUtil.c --
 *
 *      This module implements utility procedures for the rbc
 *      toolkit.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcInt.h"
#include <ctype.h>
#include <stdarg.h>

#ifndef HAVE_STRTOLOWER
void
strtolower(s)
register char *s;
{
    while (*s != '\0') {
	*s = tolower(UCHAR(*s));
	s++;
    }
}
#endif /* !HAVE_STRTOLOWER */

#ifndef HAVE_STRCASECMP

static unsigned char caseTable[] = {
    (unsigned char)'\000', (unsigned char)'\001',
    (unsigned char)'\002', (unsigned char)'\003',
    (unsigned char)'\004', (unsigned char)'\005',
    (unsigned char)'\006', (unsigned char)'\007',
    (unsigned char)'\010', (unsigned char)'\011',
    (unsigned char)'\012', (unsigned char)'\013',
    (unsigned char)'\014', (unsigned char)'\015',
    (unsigned char)'\016', (unsigned char)'\017',
    (unsigned char)'\020', (unsigned char)'\021',
    (unsigned char)'\022', (unsigned char)'\023',
    (unsigned char)'\024', (unsigned char)'\025',
    (unsigned char)'\026', (unsigned char)'\027',
    (unsigned char)'\030', (unsigned char)'\031',
    (unsigned char)'\032', (unsigned char)'\033',
    (unsigned char)'\034', (unsigned char)'\035',
    (unsigned char)'\036', (unsigned char)'\037',
    (unsigned char)'\040', (unsigned char)'\041',
    (unsigned char)'\042', (unsigned char)'\043',
    (unsigned char)'\044', (unsigned char)'\045',
    (unsigned char)'\046', (unsigned char)'\047',
    (unsigned char)'\050', (unsigned char)'\051',
    (unsigned char)'\052', (unsigned char)'\053',
    (unsigned char)'\054', (unsigned char)'\055',
    (unsigned char)'\056', (unsigned char)'\057',
    (unsigned char)'\060', (unsigned char)'\061',
    (unsigned char)'\062', (unsigned char)'\063',
    (unsigned char)'\064', (unsigned char)'\065',
    (unsigned char)'\066', (unsigned char)'\067',
    (unsigned char)'\070', (unsigned char)'\071',
    (unsigned char)'\072', (unsigned char)'\073',
    (unsigned char)'\074', (unsigned char)'\075',
    (unsigned char)'\076', (unsigned char)'\077',
    (unsigned char)'\100', (unsigned char)'\141',
    (unsigned char)'\142', (unsigned char)'\143',
    (unsigned char)'\144', (unsigned char)'\145',
    (unsigned char)'\146', (unsigned char)'\147',
    (unsigned char)'\150', (unsigned char)'\151',
    (unsigned char)'\152', (unsigned char)'\153',
    (unsigned char)'\154', (unsigned char)'\155',
    (unsigned char)'\156', (unsigned char)'\157',
    (unsigned char)'\160', (unsigned char)'\161',
    (unsigned char)'\162', (unsigned char)'\163',
    (unsigned char)'\164', (unsigned char)'\165',
    (unsigned char)'\166', (unsigned char)'\167',
    (unsigned char)'\170', (unsigned char)'\171',
    (unsigned char)'\172', (unsigned char)'\133',
    (unsigned char)'\134', (unsigned char)'\135',
    (unsigned char)'\136', (unsigned char)'\137',
    (unsigned char)'\140', (unsigned char)'\141',
    (unsigned char)'\142', (unsigned char)'\143',
    (unsigned char)'\144', (unsigned char)'\145',
    (unsigned char)'\146', (unsigned char)'\147',
    (unsigned char)'\150', (unsigned char)'\151',
    (unsigned char)'\152', (unsigned char)'\153',
    (unsigned char)'\154', (unsigned char)'\155',
    (unsigned char)'\156', (unsigned char)'\157',
    (unsigned char)'\160', (unsigned char)'\161',
    (unsigned char)'\162', (unsigned char)'\163',
    (unsigned char)'\164', (unsigned char)'\165',
    (unsigned char)'\166', (unsigned char)'\167',
    (unsigned char)'\170', (unsigned char)'\171',
    (unsigned char)'\172', (unsigned char)'\173',
    (unsigned char)'\174', (unsigned char)'\175',
    (unsigned char)'\176', (unsigned char)'\177',
    (unsigned char)'\200', (unsigned char)'\201',
    (unsigned char)'\202', (unsigned char)'\203',
    (unsigned char)'\204', (unsigned char)'\205',
    (unsigned char)'\206', (unsigned char)'\207',
    (unsigned char)'\210', (unsigned char)'\211',
    (unsigned char)'\212', (unsigned char)'\213',
    (unsigned char)'\214', (unsigned char)'\215',
    (unsigned char)'\216', (unsigned char)'\217',
    (unsigned char)'\220', (unsigned char)'\221',
    (unsigned char)'\222', (unsigned char)'\223',
    (unsigned char)'\224', (unsigned char)'\225',
    (unsigned char)'\226', (unsigned char)'\227',
    (unsigned char)'\230', (unsigned char)'\231',
    (unsigned char)'\232', (unsigned char)'\233',
    (unsigned char)'\234', (unsigned char)'\235',
    (unsigned char)'\236', (unsigned char)'\237',
    (unsigned char)'\240', (unsigned char)'\241',
    (unsigned char)'\242', (unsigned char)'\243',
    (unsigned char)'\244', (unsigned char)'\245',
    (unsigned char)'\246', (unsigned char)'\247',
    (unsigned char)'\250', (unsigned char)'\251',
    (unsigned char)'\252', (unsigned char)'\253',
    (unsigned char)'\254', (unsigned char)'\255',
    (unsigned char)'\256', (unsigned char)'\257',
    (unsigned char)'\260', (unsigned char)'\261',
    (unsigned char)'\262', (unsigned char)'\263',
    (unsigned char)'\264', (unsigned char)'\265',
    (unsigned char)'\266', (unsigned char)'\267',
    (unsigned char)'\270', (unsigned char)'\271',
    (unsigned char)'\272', (unsigned char)'\273',
    (unsigned char)'\274', (unsigned char)'\275',
    (unsigned char)'\276', (unsigned char)'\277',
    (unsigned char)'\300', (unsigned char)'\341',
    (unsigned char)'\342', (unsigned char)'\343',
    (unsigned char)'\344', (unsigned char)'\345',
    (unsigned char)'\346', (unsigned char)'\347',
    (unsigned char)'\350', (unsigned char)'\351',
    (unsigned char)'\352', (unsigned char)'\353',
    (unsigned char)'\354', (unsigned char)'\355',
    (unsigned char)'\356', (unsigned char)'\357',
    (unsigned char)'\360', (unsigned char)'\361',
    (unsigned char)'\362', (unsigned char)'\363',
    (unsigned char)'\364', (unsigned char)'\365',
    (unsigned char)'\366', (unsigned char)'\367',
    (unsigned char)'\370', (unsigned char)'\371',
    (unsigned char)'\372', (unsigned char)'\333',
    (unsigned char)'\334', (unsigned char)'\335',
    (unsigned char)'\336', (unsigned char)'\337',
    (unsigned char)'\340', (unsigned char)'\341',
    (unsigned char)'\342', (unsigned char)'\343',
    (unsigned char)'\344', (unsigned char)'\345',
    (unsigned char)'\346', (unsigned char)'\347',
    (unsigned char)'\350', (unsigned char)'\351',
    (unsigned char)'\352', (unsigned char)'\353',
    (unsigned char)'\354', (unsigned char)'\355',
    (unsigned char)'\356', (unsigned char)'\357',
    (unsigned char)'\360', (unsigned char)'\361',
    (unsigned char)'\362', (unsigned char)'\363',
    (unsigned char)'\364', (unsigned char)'\365',
    (unsigned char)'\366', (unsigned char)'\367',
    (unsigned char)'\370', (unsigned char)'\371',
    (unsigned char)'\372', (unsigned char)'\373',
    (unsigned char)'\374', (unsigned char)'\375',
    (unsigned char)'\376', (unsigned char)'\377',
};

/*
 *----------------------------------------------------------------------
 *
 * strcasecmp --
 *
 *      Compare two strings, disregarding case.
 *
 * Results:
 *      Returns a signed integer representing the following:
 *
 *        zero      - two strings are equal
 *        negative  - first string is less than second
 *        positive  - first string is greater than second
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int
strcasecmp(s1, s2)
    CONST char *s1;
    CONST char *s2;
{
    unsigned char *s = (unsigned char *)s1;
    unsigned char *t = (unsigned char *)s2;

    for ( /* empty */ ; (caseTable[*s] == caseTable[*t]); s++, t++) {
	if (*s == '\0') {
	    return 0;
	}
    }
    return (caseTable[*s] - caseTable[*t]);
}

/*
 *----------------------------------------------------------------------
 *
 * strncasecmp --
 *
 *      Compare two strings, disregarding case, up to a given length.
 *
 * Results:
 *      Returns a signed integer representing the following:
 *
 *        zero      - two strings are equal
 *        negative  - first string is less than second
 *        positive  - first string is greater than second
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int
strncasecmp(s1, s2, length)
    CONST char *s1;
    CONST char *s2;
    size_t length;
{
    register unsigned char *s = (unsigned char *)s1;
    register unsigned char *t = (unsigned char *)s2;

    for ( /* empty */ ; (length > 0); s++, t++, length--) {
	if (caseTable[*s] != caseTable[*t]) {
	    return (caseTable[*s] - caseTable[*t]);
	}
	if (*s == '\0') {
	    return 0;
	}
    }
    return 0;
}

#endif /* !HAVE_STRCASECMP */

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DictionaryCompare
 *
 *      This function compares two strings as if they were being used in
 *      an index or card catalog.  The case of alphabetic characters is
 *      ignored, except to break ties.  Thus "B" comes before "b" but
 *      after "a".  Also, integers embedded in the strings compare in
 *      numerical order.  In other words, "x10y" comes after "x9y", not
 *      before it as it would when using strcmp().
 *
 * Results:
 *      A negative result means that the first element comes before the
 *      second, and a positive result means that the second element
 *      should come first.  A result of zero means the two elements
 *      are equal and it doesn't matter which comes first.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
int
Rbc_DictionaryCompare(left, right)
    char *left;
    char *right;
{
    Tcl_UniChar uniLeft, uniRight, uniLeftLower, uniRightLower;
    int diff, zeros;
    int secondaryDiff = 0;

    for (;;) {
	if ((isdigit(UCHAR(*right))) && (isdigit(UCHAR(*left)))) {
	    /*
	     * There are decimal numbers embedded in the two
	     * strings.  Compare them as numbers, rather than
	     * strings.  If one number has more leading zeros than
	     * the other, the number with more leading zeros sorts
	     * later, but only as a secondary choice.
	     */

	    zeros = 0;
	    while ((*right == '0') && (isdigit(UCHAR(right[1])))) {
		right++;
		zeros--;
	    }
	    while ((*left == '0') && (isdigit(UCHAR(left[1])))) {
		left++;
		zeros++;
	    }
	    if (secondaryDiff == 0) {
		secondaryDiff = zeros;
	    }

	    /*
	     * The code below compares the numbers in the two
	     * strings without ever converting them to integers.  It
	     * does this by first comparing the lengths of the
	     * numbers and then comparing the digit values.
	     */

	    diff = 0;
	    for (;;) {
		if (diff == 0) {
		    diff = UCHAR(*left) - UCHAR(*right);
		}
		right++;
		left++;

		/* Ignore commas in numbers. */
		if (*left == ',') {
		    left++;
		}
		if (*right == ',') {
		    right++;
		}

		if (!isdigit(UCHAR(*right))) { /* INTL: digit */
		    if (isdigit(UCHAR(*left))) { /* INTL: digit */
			return 1;
		    } else {
			/*
			 * The two numbers have the same length. See
			 * if their values are different.
			 */

			if (diff != 0) {
			    return diff;
			}
			break;
		    }
		} else if (!isdigit(UCHAR(*left))) { /* INTL: digit */
		    return -1;
		}
	    }
	    continue;
	}

	/*
	 * Convert character to Unicode for comparison purposes.  If either
	 * string is at the terminating null, do a byte-wise comparison and
	 * bail out immediately.
	 */
	if ((*left != '\0') && (*right != '\0')) {
	    left += Tcl_UtfToUniChar(left, &uniLeft);
	    right += Tcl_UtfToUniChar(right, &uniRight);
	    /*
	     * Convert both chars to lower for the comparison, because
	     * dictionary sorts are case insensitve.  Convert to lower, not
	     * upper, so chars between Z and a will sort before A (where most
	     * other interesting punctuations occur)
	     */
	    uniLeftLower = Tcl_UniCharToLower(uniLeft);
	    uniRightLower = Tcl_UniCharToLower(uniRight);
	} else {
	    diff = UCHAR(*left) - UCHAR(*right);
	    break;
	}

	diff = uniLeftLower - uniRightLower;
	if (diff) {
	    return diff;
	} else if (secondaryDiff == 0) {
	    if (Tcl_UniCharIsUpper(uniLeft) &&
		    Tcl_UniCharIsLower(uniRight)) {
		secondaryDiff = -1;
	    } else if (Tcl_UniCharIsUpper(uniRight)
		       && Tcl_UniCharIsLower(uniLeft)) {
		secondaryDiff = 1;
	    }
	}
    }
    if (diff == 0) {
	diff = secondaryDiff;
    }
    return diff;
}

#ifndef NDEBUG
/*
 *--------------------------------------------------------------
 *
 * Rbc_Assert --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
Rbc_Assert(testExpr, fileName, lineNumber)
    char *testExpr;
    char *fileName;
    int lineNumber;
{
#ifdef WINDEBUG
    PurifyPrintf("line %d of %s: Assert \"%s\" failed\n", lineNumber,
		 fileName, testExpr);
#endif
    fprintf(stderr, "line %d of %s: Assert \"%s\" failed\n",
	    lineNumber, fileName, testExpr);
    fflush(stderr);
    abort();
}
#endif

/*
 *--------------------------------------------------------------
 *
 * Rbc_Panic --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
Rbc_Panic (char *format, ...)
{
    va_list argList;

    va_start(argList, format);
    vfprintf(stderr, format, argList);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(argList);
    abort();
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_DStringAppendElements --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
Rbc_DStringAppendElements (Tcl_DString *dsPtr, ...)
{
    va_list argList;
    register const char *elem;

    va_start(argList, dsPtr);
    while ((elem = va_arg(argList, const char *)) != NULL) {
	Tcl_DStringAppendElement(dsPtr, elem);
    }
    va_end(argList);
}

static char stringRep[200];

/*
 *--------------------------------------------------------------
 *
 * Rbc_Itoa --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
char *
Rbc_Itoa(value)
    int value;
{
    sprintf(stringRep, "%d", value);
    return stringRep;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_Utoa --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
char *
Rbc_Utoa(value)
    unsigned int value;
{
    sprintf(stringRep, "%u", value);
    return stringRep;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_Dtoa --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
char *
Rbc_Dtoa(
    Tcl_Interp *interp,
    double value)
{
    Tcl_PrintDouble(interp, value, stringRep);
    return stringRep;
}

#undef fopen
/*
 *--------------------------------------------------------------
 *
 * Rbc_OpenUtfFile --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
FILE *
Rbc_OpenUtfFile(fileName, mode)
    char *fileName;
    char *mode;
{
    Tcl_DString dString;
    FILE *f;

    fileName = Tcl_UtfToExternalDString(NULL, fileName, -1, &dString);
    f = fopen(fileName, mode);
    Tcl_DStringFree(&dString);
    return f;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_InitHexTable --
 *
 *      Table index for the hex values. Initialized once, first time.
 *      Used for translation value or delimiter significance lookup.
 *
 *      We build the table at run time for several reasons:
 *
 *        1.  portable to non-ASCII machines.
 *        2.  still reentrant since we set the init flag after setting
 *            table.
 *        3.  easier to extend.
 *        4.  less prone to bugs.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
Rbc_InitHexTable(hexTable)
    char hexTable[];
{
    hexTable['0'] = 0;
    hexTable['1'] = 1;
    hexTable['2'] = 2;
    hexTable['3'] = 3;
    hexTable['4'] = 4;
    hexTable['5'] = 5;
    hexTable['6'] = 6;
    hexTable['7'] = 7;
    hexTable['8'] = 8;
    hexTable['9'] = 9;
    hexTable['a'] = hexTable['A'] = 10;
    hexTable['b'] = hexTable['B'] = 11;
    hexTable['c'] = hexTable['C'] = 12;
    hexTable['d'] = hexTable['D'] = 13;
    hexTable['e'] = hexTable['E'] = 14;
    hexTable['f'] = hexTable['F'] = 15;
}

/*
 * The hash table below is used to keep track of all the Rbc_Uids created
 * so far.
 */
static Tcl_HashTable uidTable;
static int uidInitialized = 0;

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetUid --
 *
 *      Given a string, returns a unique identifier for the string.
 *      A reference count is maintained, so that the identifier
 *      can be freed when it is not needed any more. This can be used
 *      in many places to replace Tcl_GetUid.
 *
 * Results:
 *      This procedure returns a Rbc_Uid corresponding to the "string"
 *      argument.  The Rbc_Uid has a string value identical to string
 *      (strcmp will return 0), but it's guaranteed that any other
 *      calls to this procedure with a string equal to "string" will
 *      return exactly the same result (i.e. can compare Rbc_Uid
 *      *values* directly, without having to call strcmp on what they
 *      point to).
 *
 * Side effects:
 *      New information may be entered into the identifier table.
 *
 *----------------------------------------------------------------------
 */
Rbc_Uid
Rbc_GetUid(string)
    const char *string; /* String to convert. */
{
    int isNew;
    Tcl_HashEntry *hPtr;
    Tcl_Size refCount;

    if (!uidInitialized) {
	Tcl_InitHashTable(&uidTable, TCL_STRING_KEYS);
	uidInitialized = 1;
    }
    hPtr = Tcl_CreateHashEntry(&uidTable, string, &isNew);
    if (isNew) {
	refCount = 0;
    } else {
	refCount = (Tcl_Size)Tcl_GetHashValue(hPtr);
    }
    refCount++;
    Tcl_SetHashValue(hPtr, (ClientData)refCount);
    return (Rbc_Uid)Tcl_GetHashKey(&uidTable, hPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreeUid --
 *
 *      Frees the Rbc_Uid if there are no more clients using this
 *      identifier.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The identifier may be deleted from the identifier table.
 *
 *----------------------------------------------------------------------
 */
void
Rbc_FreeUid(uid)
    Rbc_Uid uid; /* Identifier to release. */
{
    Tcl_HashEntry *hPtr;

    if (!uidInitialized) {
	Tcl_InitHashTable(&uidTable, TCL_STRING_KEYS);
	uidInitialized = 1;
    }
    hPtr = Tcl_FindHashEntry(&uidTable, uid);
    if (hPtr) {
	Tcl_Size refCount;

	refCount = (Tcl_Size)Tcl_GetHashValue(hPtr);
	refCount--;
	if (refCount == 0) {
	    Tcl_DeleteHashEntry(hPtr);
	} else {
	    Tcl_SetHashValue(hPtr, (ClientData)refCount);
	}
    } else {
	fprintf(stderr, "tried to release unknown identifier \"%s\"\n", uid);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FindUid --
 *
 *      Returns a Rbc_Uid associated with a given string, if one
 *      exists.
 *
 * Results:
 *      A Rbc_Uid for the string if one exists. Otherwise NULL.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Rbc_Uid
Rbc_FindUid(string)
    char *string; /* String to find. */
{
    Tcl_HashEntry *hPtr;

    if (!uidInitialized) {
	Tcl_InitHashTable(&uidTable, TCL_STRING_KEYS);
	uidInitialized = 1;
    }
    hPtr = Tcl_FindHashEntry(&uidTable, string);
    if (hPtr == NULL) {
	return NULL;
    }
    return (Rbc_Uid) Tcl_GetHashKey(&uidTable, hPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetOpFromObj --
 *
 *      Find the command operation given a string name.  This is
 *      useful where a group of command operations have the same
 *      argument signature.
 *
 * Results:
 *      If found, a pointer to the procedure (function pointer) is
 *      returned.  Otherwise NULL is returned and an error message
 *      containing a list of the possible commands is returned in
 *      the interpreter result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Rbc_Op
Rbc_GetOpFromObj(
    Tcl_Interp *interp,		/* Interpreter to report errors to */
    Rbc_OpSpec specArr[],	/* Op specification array */
    int operPos,		/* Position of operation in argument list. */
    int objc,			/* Number of arguments in the argument vector.
				 * This includes any prefixed arguments */
    Tcl_Obj *const objv[])	/* Argument vector */
{
    Rbc_Op result = NULL;
    int index;

    if (objc <= operPos) {
	Tcl_WrongNumArgs(interp, objc, objv, "subcommand ?arg ...?");
	goto done;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[operPos], specArr,
	sizeof(Rbc_OpSpec), "subcommand", 0, &index) != TCL_OK) {
	goto done;
    }

    if ((objc < specArr[index].minArgs) || (
	    (specArr[index].maxArgs > 0) &&
	    (objc > specArr[index].maxArgs))) {
	Tcl_WrongNumArgs(interp, operPos+1, objv, specArr[index].usage);
	goto done;
    }
    result = specArr[index].proc;

done:
    return result;
}
