# Normalize Doxygen XML variants before Breathe reads them. Tcl core only.
# See license.terms for copyright and redistribution terms.
namespace eval ::rbcDocBuild {}

proc ::rbcDocBuild::normalizeCallbackXml {xml} {
    # Some Doxygen versions put the pointer's closing parenthesis in <type>:
    #   <type>void(*)</type> ... <argsstring>(int)</argsstring>
    # Breathe joins type, name and argsstring, producing invalid C. Move only
    # that delimiter to argsstring, yielding void(* name )(int). Already split
    # declarations and non-callback members must remain byte-for-byte intact.
    set offset 0
    set result {}
    while {[regexp -indices -start $offset {<memberdef\s[^>]*>} $xml range]} {
        lassign $range first headerEnd
        set closing [string first </memberdef> $xml [expr {$headerEnd+1}]]
        if {$closing < 0} {
            break
        }
        set last [expr {$closing+[string length </memberdef>]-1}]
        append result [string range $xml $offset [expr {$first-1}]]
        set member [string range $xml $first $last]
        if {[regexp {^<memberdef\s[^>]*\mkind="variable"} $member] && [regexp {<argsstring>\s*\(} $member] &&\
                    [regsub {(<type>[^<]*\(\s*\*)\s*\)(\s*</type>)} $member {\1\2} fixed]} {
            regsub {(<argsstring>)\s*(\()} $fixed {\1)\2} member
        }
        append result $member
        set offset [expr {$last+1}]
    }
    append result [string range $xml $offset end]
    return $result
}
