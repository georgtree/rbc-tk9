# Shared numerical reference and fixtures for FFT/IFFT vector tests.
# See license.terms for details.
namespace eval vector.fft {
    proc Cleanup {} {
        foreach name [::rbc::vector names ::Fft*] {
            ::rbc::vector destroy $name
        }
        catch {namespace delete ::FftNames}
    }

    proc Setup {} {
        Cleanup
        ::rbc::vector create ::FftInput ::FftReal ::FftImag ::FftImagInput
        ::rbc::vector create ::FftOut ::FftComplex -type complex
    }

    # Direct O(N^2) DFT is independent of the radix-2 implementation.
    proc Dft {values {inverse 0}} {
        set n [llength $values]
        set result {}
        set sign [expr {$inverse ? 1.0 : -1.0}]
        set scale [expr {$inverse ? 1.0/$n : 1.0}]
        for {set k 0} {$k < $n} {incr k} {
            set re 0.0
            set im 0.0
            set j 0
            foreach z $values {
                if {[llength $z] == 1} {
                    set z [list $z 0.0]
                }
                lassign $z a b
                set angle [expr {$sign*2.0*acos(-1.0)*$j*$k/$n}]
                set re [expr {$re+$a*cos($angle)-$b*sin($angle)}]
                set im [expr {$im+$a*sin($angle)+$b*cos($angle)}]
                incr j
            }
            lappend result [list [expr {$re*$scale}] [expr {$im*$scale}]]
        }
        return $result
    }

    proc Near {actual expected {tolerance 1e-10}} {
        if {[llength $actual] != [llength $expected]} {
            return 0
        }
        foreach a $actual e $expected {
            if {[llength $a] == 1} {
                set a [list $a 0.0]
            }
            if {[llength $e] == 1} {
                set e [list $e 0.0]
            }
            foreach x $a y $e {
                if {abs($x-$y) > $tolerance*(1.0+abs($y))} {
                    return 0
                }
            }
        }
        return 1
    }

    proc SplitValues {} {
        set values {}
        foreach re [::FftReal range 0 end] im [::FftImag range 0 end] {
            lappend values [list $re $im]
        }
        return $values
    }
}
