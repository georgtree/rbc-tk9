# Shared error-bar fixture for hit testing and drawing tests.
namespace eval ::rbcTest::errors {
    namespace export Create CapCreate
    proc Create {kind} {
        ::rbc::$kind .ce -width 400 -height 400
        pack .ce
        .ce axis configure x -min -1 -max 6 -loose no
        .ce axis configure y -min -1 -max 6 -loose no
        .ce element create e -x {1 3 5} -y {1 3 5}
        update idletasks
    }
    proc CapCreate {kind inverted descending logarithmic errors} {
        Create $kind
        .ce configure -invertxy $inverted
        foreach axis {x y} {
            .ce axis configure $axis -min 1 -max 6 -loose no \
                -descending $descending -logscale $logarithmic
        }
        .ce element configure e -data {3 3} -errorbarcolor magenta -errorbarcap 16 -errorbarwidth 2
        if {$errors eq "symmetric"} {
            .ce element configure e -xerror 1 -yerror 1
        } else {
            .ce element configure e -xlow 2 -xhigh 4 -ylow 2 -yhigh 4
        }
        update
    }
}
