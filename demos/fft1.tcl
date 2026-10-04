# ------------------------------------------------------------------------------
#  RBC Demo fft1.tcl
# ------------------------------------------------------------------------------
#  Real-valued signal, windowing, amplitude spectrum, PSD, and IFFT reconstruction.
#
#  Place this file in the RBC demos directory beside scripts/common.tcl.
#
#  The forward FFT is unnormalized. For X = FFT(signal * window):
#
#      amplitude = oneSidedFactor * abs(X) / sum(window)
#      PSD       = oneSidedFactor * abs(X)^2 / (sampleRate * sum(window^2))
#
#  The one-sided factor is 2 except at DC and, for even FFT lengths, Nyquist.
#  Window sums use the original samples, excluding FFT zero padding.
# ------------------------------------------------------------------------------

package require Tk
package require rbc::graphtoolbar
namespace import rbc::graphtoolbar
namespace import rbc::vector

set DemoDir [file normalize [file dirname [info script]]]
source [file join $DemoDir scripts common.tcl]

proc Calculate {} {
    variable count
    variable rate
    variable window
    variable fftLength
    variable addNoise
    variable status

    set dt [expr {1.0/$rate}]
    signal expr {clean+$addNoise*noise}
    # periodic windows are appropriate for spectral analysis.
    weights window $window $count -periodic yes
    windowed expr {signal*weights}
    # zero padding changes frequency-grid spacing, not observation duration # or the ability to resolve nearby tones.
    windowed fft spectrum -length $fftLength
    set coherentSum [vector expr {sum(weights)}]
    set windowEnergy [vector expr {sum(weights*weights)}]
    # keep DC through positive Nyquist. Do not double either endpoint.
    set last [expr {$fftLength/2}]
    bins seq 0 $last
    frequency fftfreq $fftLength -delta $dt -onesided yes
    factor expr {2-(bins==0)-(($fftLength%2==0) && (bins==$last)) }
    # peak-amplitude scaling for isolated, bin-centered sinusoidal tones. This is not RMS amplitude or a
    # noise-density calibration.
    amplitude expr "abs(spectrum(0:$last))/$coherentSum*factor"
    # one-sided periodogram, in V^2/Hz. No mean removal is performed: the DC component is retained.
    psd expr "abs(spectrum(0:$last))^2*$dt/$windowEnergy*factor"
    # apply a floor only to the displayed copy for the logarithmic axis.
    psdDisplay expr {psd+(psd<1.0e-16)*(1.0e-16-psd)}
    # inverse calculation
    spectrum ifft inverse
    recovered expr "real(inverse(0:[expr {$count-1}]))"
    difference expr {abs(recovered-windowed)}
    set maxError [vector expr {max(difference)}]
    # check for this window-normalized PSD: sum(PSD)*df == sum((signal*window)^2)/sum(window^2).
    set spectralPower [vector expr {sum(psd)*$rate/$fftLength}]
    set timePower [vector expr {sum(windowed*windowed)/$windowEnergy}]
    set status [format\
                        {FFT spacing: %.2f Hz    PSD integral: %.6f V²    Window-weighted mean square: %.6f V²    \
                                 Maximum IFFT error: %.2g V}\
                        [expr {$rate/$fftLength}] $spectralPower $timePower $maxError]
}

proc MakeGraph {path title xTitle yTitle} {
    set graph [graphtoolbar $path -width 520 -height 220 -type graph -controlmode context -zoom -zoomtitle\
                       -zoommark -crosshairs -crosshairsmode closest -crosshairsclosestopts {-interpolate no} -pan\
                       -zoomwheel -activelegend]
    $graph graph configure -title $title
    $graph graph xaxis configure -title $xTitle -loose no
    $graph graph yaxis configure -title $yTitle
    $graph graph legend configure -hide yes -background {}
    $graph graph grid on
    return $graph
}

set count 1024
set rate 4096.0
set window hann
set fftLength 4096
set addNoise 1
set status {}

vector create indices sampleTime clean noise signal weights windowed
vector create frequency bins factor amplitude psd psdDisplay
vector create recovered difference
vector create spectrum inverse -type complex

indices seq 0 [expr {$count-1}]
sampleTime expr {indices/$rate}

# the tones fall exactly on bins of the unpadded FFT:
# DC = 0.25 V; 256 Hz = 1 V peak; 640 Hz = 0.4 V peak.
set pi [expr {acos(-1.0)}]
clean expr {0.25+sin(2*$pi*256*sampleTime)+0.4*cos(2*$pi*640*sampleTime)}

# generate one reproducible Gaussian noise realization, with 0.05 V standard deviation.
expr {srand(12345)}
set values {}
for {set i 0} {$i<$count} {incr i} {
    set u [expr {max(rand(), 1.0e-15)}]
    lappend values [expr {0.05*sqrt(-2*log($u))*cos(2*$pi*rand())}]
}
noise set $values
unset values u i pi

wm title . {RBC: FFT amplitude, PSD, and reconstruction}
set HeaderText [MakeLine {
    |A real signal sampled at 4096 Hz: 0.25 V DC, a 1 V peak tone at
    |256 Hz, and a 0.4 V peak tone at 640 Hz, with optional Gaussian noise.
    |Compare windows and FFT zero padding while preserving amplitude and
    |PSD calibration. The IFFT reconstructs the windowed signal.
}]
CommonHeader .header $HeaderText 4 $DemoDir

ExpandableText .details 1000 {Scaling and graph interactions} {
Amplitude:
    Divide the FFT magnitude by the window sum, then double positive-frequency bins except Nyquist. DC is not
    doubled. The plotted amplitude is peak volts. Disable noise to inspect the known tone amplitudes. Off-bin tones can
    show scalloping loss; coherent-gain correction does not eliminate leakage.

PSD:
    Divide squared FFT magnitude by sample rate times window energy, then apply the same one-sided factors. Units are
    V²/Hz. This is a single periodogram, so the noise floor fluctuates; no averaging or detrending is performed. Values
    below 1e-16 V²/Hz are floored only for logarithmic display.

Padding and reconstruction:
    All window sums exclude zero padding. Padding samples the spectrum more densely without improving the resolution of
    the 0.25-second record. IFFT reconstructs the windowed input, not the original unwindowed signal. The status line
    compares the PSD integral with its time-domain equivalent.

Interactions:
    Select a zoom box with the left mouse button;
    Use Ctrl + mouse wheel to zoom and Shift + left drag to pan;
    Use the context menu to restore the full view.
}

ttk::frame .controls
ttk::label .controls.windowLabel -text Window:
ttk::combobox .controls.window -textvariable window -values {rectangular hann hamming blackman bartlett}\
        -state readonly -width 12
ttk::label .controls.lengthLabel -text {FFT length:}
ttk::combobox .controls.length -textvariable fftLength -values {1024 2048 4096} -state readonly -width 7
ttk::checkbutton .controls.noise -text {Add noise (0.05 V standard deviation)} -variable addNoise -command Calculate
bind .controls.window <<ComboboxSelected>> {Calculate}
bind .controls.length <<ComboboxSelected>> {Calculate}
grid .controls.windowLabel .controls.window .controls.lengthLabel .controls.length .controls.noise -padx 6 -pady 4\
        -sticky w
ttk::label .status -textvariable status -anchor w
bind .status <Configure> {
    %W configure -wraplength [expr {max(1,%w-16)}]
}
option add *HighlightThickness 0
option add *Element.ScaleSymbols no

Calculate

### Time-domain plots
set originalGraph [MakeGraph .original {Original signal} {Time (s)} {Voltage (V)}]
$originalGraph graph element create signal -x sampleTime -y signal -symbol none -color navy -linewidth 1
set windowGraph [MakeGraph .windowed {Signal with selected window applied} {Time (s)} {Voltage (V)}]
$windowGraph graph element create windowed -x sampleTime -y windowed -symbol none -color forestgreen -linewidth 1

###  Frequency-domain plots
set amplitudeGraph [MakeGraph .amplitude {One-sided amplitude spectrum} {Frequency (Hz)} {Peak amplitude (V)}]
$amplitudeGraph graph xaxis configure -min 0 -max [expr {$rate/2}]
$amplitudeGraph graph yaxis configure -min 0
$amplitudeGraph graph element create amplitude -x frequency -y amplitude -symbol none -color blue -linewidth 1
set psdGraph [MakeGraph .psd {One-sided power spectral density} {Frequency (Hz)} {PSD (V²/Hz)}]
$psdGraph graph xaxis configure -min 0 -max [expr {$rate/2}]
$psdGraph graph yaxis configure -logscale yes -min 1.0e-16
$psdGraph graph element create psd -x frequency -y psdDisplay -symbol none -color purple -linewidth 1

### Reconstruction plot
set inverseGraph [MakeGraph .inverse {IFFT reconstruction: curves should overlap} {Time (s)} {Voltage (V)}]
$inverseGraph graph legend configure -hide no
$inverseGraph graph element create reference -label {Windowed input} -x sampleTime -y windowed -symbol none -color navy\
        -linewidth 2
$inverseGraph graph element create reconstructed -label {Real part of IFFT} -x sampleTime -y recovered -symbol none\
        -color darkorange -linewidth 1 -dashes {4 4}

### Map everything
grid .header -row 0 -column 0 -columnspan 2 -sticky ew
grid .details -row 1 -column 0 -columnspan 2 -sticky ew -padx 10
grid .controls -row 2 -column 0 -columnspan 2 -sticky ew -padx 10
grid $originalGraph -row 3 -column 0 -sticky nsew -padx 4 -pady 4
grid $windowGraph -row 3 -column 1 -sticky nsew -padx 4 -pady 4
grid $amplitudeGraph -row 4 -column 0 -sticky nsew -padx 4 -pady 4
grid $psdGraph -row 4 -column 1 -sticky nsew -padx 4 -pady 4
grid $inverseGraph -row 5 -column 0 -columnspan 2 -sticky nsew -padx 4 -pady 4
grid .status -row 6 -column 0 -columnspan 2 -sticky ew -padx 12 -pady 6
grid columnconfigure . {0 1} -weight 1 -uniform plots
grid rowconfigure . {3 4 5} -weight 1 -uniform plots


