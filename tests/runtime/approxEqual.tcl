# Source inside the test namespace, which supplies atol and rtol.
proc containsNumber {value} {
    if {[string is double -strict $value]} {
        return true
    }
    if {[catch {llength $value} len]} {
        return false
    }
    # Single-element and empty values are treated as atoms here.
    if {$len<=1} {
        return false
    }
    for {set i 0} {$i<$len} {incr i} {
        if {[containsNumber [lindex $value $i]]} {
            return true
        }
    }
    return false
}

proc approxNestedEqual {a b} {
    variable atol
    variable rtol
    set aIsNum [string is double -strict $a]
    set bIsNum [string is double -strict $b]
    # Numeric atoms: approximate comparison
    if {$aIsNum && $bIsNum} {
        return [expr {abs($a-$b)<=($atol+$rtol*abs($b))}]
    }
    # One numeric, one non-numeric
    if {$aIsNum!=$bIsNum} {
        return false
    }
    set aHasNum [containsNumber $a]
    set bHasNum [containsNumber $b]
    # If neither side contains numbers, compare as exact strings.
    # This preserves spaces, tabs, etc.
    if {!$aHasNum && !$bHasNum} {
        return [string equal $a $b]
    }
    # If only one side contains numeric structure, they differ.
    if {$aHasNum != $bHasNum} {
        return false
    }
    # Both contain numbers somewhere, so compare structurally as lists.
    if {[catch {llength $a} lenA]} {
        return false
    }
    if {[catch {llength $b} lenB]} {
        return false
    }
    if {$lenA != $lenB} {
        return false
    }
    for {set i 0} {$i<$lenA} {incr i} {
        set elemA [lindex $a $i]
        set elemB [lindex $b $i]
        if {![approxNestedEqual $elemA $elemB]} {
            return false
        }
    }
    return true
}
