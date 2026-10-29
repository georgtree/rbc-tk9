# RBC.graph.signal.M.tcl --
#
###Abstract
# Visual manual tests for graph signal.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide graph.signal

package require rbc
namespace import rbc::*

namespace eval graph.signal {
    # FFT then inverse FFT reconstructs the real signal: red reconstruction should
    # cover blue input. After: apply Hann window; both traces acquire the same envelope.
    proc RBC.graph.signal.M.1.1.Setup {} {
        vector create samples signal windowed reconstructed imaginary weights
        vector create spectrum -type complex
        samples seq 0 63 1
        signal expr {sin(2*acos(-1)*samples/16)}
        signal fft spectrum
        spectrum ifft -real reconstructed -imag imaginary
        graph .graph1 -width 720 -height 480
        pack .graph1 -fill both -expand yes
        .graph1 line create restored -xdata samples -ydata reconstructed -color red -symbol none -linewidth 2
        .graph1 line create original -xdata samples -ydata signal -color blue -symbol none -linewidth 4
    }

    proc RBC.graph.signal.M.1.1.Body {} {
        weights window hann 64 -periodic yes
        windowed expr {signal * weights}
        windowed fft spectrum
        spectrum ifft -real reconstructed -imag imaginary
        .graph1 element configure original -ydata windowed
    }

    proc RBC.graph.signal.M.1.1.Cleanup {} {
        destroy .graph1
        vector destroy samples signal windowed reconstructed imaginary weights spectrum
    }

    # Simplified red trace follows the dense blue reference. After: loosen tolerance;
    # fewer red vertices are kept while the original blue samples remain unchanged.
    proc RBC.graph.signal.M.1.2.Setup {} {
        vector create sx sy reducedX reducedY
        sx seq 0 10 0.01
        sy expr {sin(sx)}
        rbc::vector::simplify sx sy reducedX reducedY 0.02
        graph .graph1 -width 720 -height 480
        pack .graph1 -fill both -expand yes
        .graph1 line create dense -xdata sx -ydata sy -color blue -symbol none
        .graph1 line create reduced -xdata reducedX -ydata reducedY -color red -symbol circle -pixels 4
    }

    proc RBC.graph.signal.M.1.2.Body {} {
        rbc::vector::simplify sx sy reducedX reducedY 0.15
    }

    proc RBC.graph.signal.M.1.2.Cleanup {} {
        destroy .graph1
        vector destroy sx sy reducedX reducedY
    }

}
