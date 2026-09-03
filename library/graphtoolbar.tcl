package require argparse
package require Tk
package require rbc

namespace eval ::rbc::graphtoolbar {
    variable libDir [file dirname [file normalize [info script]]]

    option add *Element.ScaleSymbols no widgetDefault
    # zoom title default options
    option add *gtbZoomTitleFont {Arial 18} widgetDefault
    option add *gtbZoomTitleShadow yellow4 widgetDefault
    option add *gtbZoomTitleForeground black widgetDefault
    option add *gtbZoomTitleCoords {-Inf Inf} widgetDefault
    option add *gtbZoomTitleAnchor nw widgetDefault

    # zoom box default options
    option add *gtbZoomOutlineDashes 4 widgetDefault
    option add *gtbZoomOutlineLineWidth 1 widgetDefault
    option add *gtbZoomOutlineXor no widgetDefault

    # zoom box corner markers default options
    option add *gtbZoomTextFont {ArialNarrow 8} widgetDefault
    option add *gtbZoomTextAnchor ne widgetDefault
    option add *gtbZoomTextForeground black widgetDefault
    option add *gtbZoomTextJustify left widgetDefault
    option add *gtbZoomTextPadX 4 widgetDefault
    option add *gtbZoomTextPadY 4 widgetDefault
    option add *gtbZoomTextXFormat .4g widgetDefault
    option add *gtbZoomTextYFormat .4g widgetDefault

    # zoom box corner markers box default options
    option add *gtbZoomTextBoxFill #FFEB3B widgetDefault
    option add *gtbZoomTextBoxOutline grey widgetDefault
    option add *gtbZoomTextBoxLineWidth 1 widgetDefault

    # crosshairs style default options
    option add *gtbCrosshairsLineWidth 1 widgetDefault
    option add *gtbCrosshairsColor grey widgetDefault
    option add *gtbCrosshairsDashes {} widgetDefault

    # crosshairs markers default options
    option add *gtbCrosshairsTextFont {ArialNarrow 8} widgetDefault
    option add *gtbCrosshairsTextAnchor nw widgetDefault
    option add *gtbCrosshairsTextForeground black widgetDefault
    option add *gtbCrosshairsTextJustify left widgetDefault
    option add *gtbCrosshairsTextPadX 4 widgetDefault
    option add *gtbCrosshairsTextPadY 4 widgetDefault
    option add *gtbCrosshairsTextXFormat .4g widgetDefault
    option add *gtbCrosshairsTextYFormat .4g widgetDefault
    option add *gtbCrosshairsTextParamFormat .4g widgetDefault

    # crosshairs markers box default options
    option add *gtbCrosshairsTextBoxFill #FFEB3B widgetDefault
    option add *gtbCrosshairsTextBoxOutline grey widgetDefault
    option add *gtbCrosshairsTextBoxLineWidth 1 widgetDefault

    # crosshairs markers bar default options
    option add *gtbCrosshairsBarLineOutline black widgetDefault
    option add *gtbCrosshairsBarLineWidth 1 widgetDefault
    option add *gtbCrosshairsBarLineArrowShape {8 10 3} widgetDefault
}

namespace eval ::rbc::graphtoolbar::icons {
    image create photo [namespace current]::removeAllMarkersIcon -data {
        R0lGODlhEAAQAIIAAASC/PwCBMQCBEQCBIQCBAAAAAAAAAAAACH5BAEAAAAA
        LAAAAAAQABAAAAMuCLrc/hCGFyYLQjQsquLDQ2ScEEJjZkYfyQKlJa2j7AQn
        MM7NfucLze1FLD78CQAh/mhDcmVhdGVkIGJ5IEJNUFRvR0lGIFBybyB2ZXJz
        aW9uIDIuNQ0KqSBEZXZlbENvciAxOTk3LDE5OTguIEFsbCByaWdodHMgcmVz
        ZXJ2ZWQuDQpodHRwOi8vd3d3LmRldmVsY29yLmNvbQA7
    }
    image create photo [namespace current]::revertZoomIcon -data {
        R0lGODlhEAAQAIUAAPwCBBxSHBxOHMTSzNzu3KzCtBRGHCSKFIzCjLzSxBQ2
        FAxGHDzCLCyeHBQ+FHSmfAwuFBxKLDSCNMzizISyjJzOnDSyLAw+FAQSDAQe
        DBxWJAwmDAQOBKzWrDymNAQaDAQODAwaDDyKTFSyXFTGTEy6TAQCBAQKDAwi
        FBQyHAwSFAwmHAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
        AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAALAAAAAAQABAAAAZ1
        QIBwSCwaj0hiQCBICpcDQsFgGAaIguhhi0gohIsrQEDYMhiNrRfgeAQC5fMC
        AolIDhD2hFI5WC4YRBkaBxsOE2l/RxsHHA4dHmkfRyAbIQ4iIyQlB5NFGCAA
        CiakpSZEJyinTgAcKSesACorgU4mJ6uxR35BACH+aENyZWF0ZWQgYnkgQk1Q
        VG9HSUYgUHJvIHZlcnNpb24gMi41DQqpIERldmVsQ29yIDE5OTcsMTk5OC4g
        QWxsIHJpZ2h0cyByZXNlcnZlZC4NCmh0dHA6Ly93d3cuZGV2ZWxjb3IuY29t
        ADs=
    }
    image create photo [namespace current]::resetZoomIcon -data {
        iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAAXNSR0IArs4c
        6QAAAXRJREFUOI2VkyFPw1AQx397ObGQF1Ix8eREQxATyIpmqZjYR+AjoAkf
        AMEHQCCQSCRyAtGQigoEgpCFTEwQUjFRgahodgjapS2jCefu3f1/d7m7Bx3z
        j3wNp6HaQ6vdWK+F01DFiD6/r/T180OTt1cVIxpOw16QqcXRbM46z3HOUciQ
        bOhx/5ERzebUkNLIL9gAQIzo+ivHWQ+AeJOxzHJeNjmnkzHzkaMA0rrbbTnY
        dWAPrS6WS5z1SKukaOQ4mxzjCcSrrCW+7HbgH/n6EMfkMqQQSEeOqAoGDWEt
        fmxUBxjYA6t3aQrWY10AAsXxeAfpE7dmsNhkLFYZRVkAQ67Dk131AJA9Yqi2
        EIQB8c0tc98x9lxLXJtsy/67qO+gNKKJEZ0ZUTGiV5VfVn4vBH52PWsky1Oy
        gyRPST+krt59b3bSC6mr74tJBbg4v2hBpJu4b1UA5bYcREY0eP4ZbxzHRFGk
        LcBfq2pC0iT93y/dZ9IY8jcCl7dntGRDqgAAAABJRU5ErkJggg==
    }
    image create photo [namespace current]::makeSnapshotIcon -data {
        R0lGODlhEAAQAIYAAPz+BFxaXNze3Nza3Nza1LS2tJyanPz+/PTu5GRqZOTi
        3HRydMzOzDQyNJQOFDxKfKTW5OTy/OT23MTaxISKhGxmZERCRCwuLJQSFPzG
        pLweHJTO3ESu3DRilNz61FymXCxKRMTCxPTy7Ly2rIyKhDwCBORiRKQWFEy2
        3CyKvJTWhDSOPCRONDQaHNROPNQ2JLRubHTC5DR6rHR+jFSqRCRyLGxqbOzm
        3LyulCwqLMySjMQ2JMyqrERijJSenFSGXGRmXMS6tPTq3Ozi1MS+vMxCNKyq
        rHR2dPTm1OzezOSKfNTe5KSmpIyGjLy2pOzWvLyqjIRqZIyKjHR2fExKTHx2
        bLyynOzexLymhERGRGRmZKSWhMS6pOTOpLyedMTCtMS6rMSulLymfAAAAAAA
        AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
        AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAA
        LAAAAAAQABAAAAfcgACCAAGFhoaDiQECAwICBAUBBgGJggEEB5kHCAkHk5UB
        CpqZCwyeDYIOAA8QERITmRQLFRYXABgZGhscHR4fICEiAiMkDSUmJicoKQ8q
        KywLAwgKIxctLi8wMTIzNDUJNiEINzgNOTo7PBA9Pj/QQEFCQzg5F0RFEQQ+
        RhRHswVIkpS7oEMJgiUhCjBpcqSCkyFPoNjKEYVJCCNSpgSgUsXKlSdYUAG4
        kCOLlgpAgFTZwuVJFyy2Bi0gEsSJzZYuvYi09OULGC44wmAR48VLDkU5zClt
        0OACUz+BAAAh/mhDcmVhdGVkIGJ5IEJNUFRvR0lGIFBybyB2ZXJzaW9uIDIu
        NQ0KqSBEZXZlbENvciAxOTk3LDE5OTguIEFsbCByaWdodHMgcmVzZXJ2ZWQu
        DQpodHRwOi8vd3d3LmRldmVsY29yLmNvbQA7
    }
    image create photo [namespace current]::postScriptDialogIcon -data {
        R0lGODlhEAAQAIYAAPwCBFxWVFRWVFxaXOTi3Nze3LS2tKSajPT29Pz+/Ozq
        7GxqbJyanISChPz6/HRydNTS1CwqLMTCxISGhOzu7ERCRDQyNERGRLy2rIyK
        hExOTAQCBKyqrOTm5KSmpHR2dNza3AQGBNzSxOzm3LyulBweHBQSFKSilOza
        xAwKDOzezDw6PJSSlDw+PKSelNzOvLymhAwODNzGrOTStLyqjExKRHRqZLym
        fMTCvMS+tMS6rLyynMS6pOTOpLyifMTCtMS+rMS2pMSynLyidLyedAAAAAAA
        AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
        AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
        AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
        AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAA
        LAAAAAAQABAAAAfPgACCAAECA4aIA4OLAgSOBQQGAgcBi4KNCAmaCgsJDAKW
        jQkNCA4JDxCeEYwSDQ4TFKMPCxUWAA0BEQ0GDggQDxcIBBgZthobExwICB0e
        FRsSHcOrER8QCwYIIA8bIRwiIyS2JSYVHgsQzhsbJygEJKsl6ykeH9zrJyIq
        4gAp6xsrWLT45+IFChi2YqwLwcLEvxgHZMygsarBOgsCBGioUcNGxBk3bA0S
        gCOHjh0oeczo4WPVoAA/fgAJIoQGjSFEiIgUNCCCBZ8Wggq14CcQACH+aENy
        ZWF0ZWQgYnkgQk1QVG9HSUYgUHJvIHZlcnNpb24gMi41DQqpIERldmVsQ29y
        IDE5OTcsMTk5OC4gQWxsIHJpZ2h0cyByZXNlcnZlZC4NCmh0dHA6Ly93d3cu
        ZGV2ZWxjb3IuY29tADs=
    }
}

# x / y                 physical widget pixels, exactly like Tk %x / %y
# xPixel / yPixel       pixel coordinate along the X/Y data dimension
#                       (swapped relative to x/y when -invertxy yes)
# xValue / yValue       data value on a particular named X/Y axis

oo::configurable create ::rbc::graphtoolbar::graphtoolbar {
    variable Subwidgets
    initialize {
        variable CrosshairsModeNames
        const CrosshairsModeNames {current closest none disabled}
        variable CrosshairsModeLabels
        const CrosshairsModeLabels [dict create current {Current point} closest {Closest point} none {No marker}\
                                            disabled Disabled]
        variable CoordMarkModes
        const CoordMarkModes {auto axis complex polar gamma normalizedimpedance normalizedimpedanceri\
                                      normalizedadmittance normalizedadmittanceri}
        variable CoordClosestMarkModes
        const CoordClosestMarkModes {axis complex polar gamma normalizedimpedance impedance normalizedadmittance\
                                             admittance}
        variable CoordClosestMarkLabels
        const CoordClosestMarkLabels [dict create axis {Axis} complex {Complex} polar {Polar} gamma {Gamma}\
                                              normalizedimpedance {Normalized impedance} impedance {Impedance}\
                                              normalizedadmittance {Normalized admittance} admittance {Admittance}]
    }
    property crosshairsmode -set {
        classvariable CrosshairsModeNames
        if {$value ni $CrosshairsModeNames} {
            return -code error "bad crosshairs mode '$value': must be [join $CrosshairsModeNames {, }]"
        }
        set crosshairsmode $value
        set CrosshairsSelector [my CrosshairsModeLabel $value]
        if {[info exists Subwidgets(crosshairsComBox)]} {
            my ApplyCrosshairsMode
        }
    }
    property coordmark -set {
        classvariable CoordMarkModes
        if {$value ni $CoordMarkModes} {
            return -code error "bad coordinate marker mode '$value': must be [join $CoordMarkModes {, }]"
        }
        set coordmark $value
    }
    property coordclosestmark -set {
        classvariable CoordClosestMarkModes
        if {[info exists Subwidgets(graph)]} {
            set modes [my ClosestCoordinateModes]
        } else {
            set modes $CoordClosestMarkModes
        }
        if {$value ni $modes} {
            return -code error "bad closest coordinate marker mode '$value': must be [join $modes {, }]"
        }
        set coordclosestmark $value
        set ClosestCoordSelector [my ClosestCoordinateLabel $value]
    }
    property zoomtitle
    property zoomtitleopts -set {
        if {[info exists zoomtitleopts]} {
            dict for {option optionValue} $value {
                if {[dict exists $zoomtitleopts $option]} {
                    dict set zoomtitleopts $option $optionValue
                } else {
                    return -code error "option with name '$option' does not exists in zoomtitleopts"
                }
            }
            
        } else {
            set zoomtitleopts\
                    [argparse -inline [list [list -font= -key -font -default\
                                                     [option get $Subwidgets(graph) gtbZoomTitleFont GtbZoomTitleFont]]\
                                               [list -foreground= -key -foreground -default\
                                                        [option get $Subwidgets(graph) gtbZoomTitleForeground\
                                                                 GtbZoomTitleForeground]]\
                                               [list -shadow= -key -shadow -default\
                                                        [option get $Subwidgets(graph) gtbZoomTitleShadow\
                                                                 GtbZoomTitleShadow]]\
                                               [list -anchor= -key -anchor -default\
                                                        [option get $Subwidgets(graph) gtbZoomTitleAnchor\
                                                                 GtbZoomTitleAnchor]]\
                                               [list -coords= -key -coords -default\
                                                        [option get $Subwidgets(graph) gtbZoomTitleCoords\
                                                                 GtbZoomTitleCoords]]]\
                             $value]
        }
    }
    property zoomboxopts  -set {
        if {[info exists zoomboxopts]} {
            dict for {option optionValue} $value {
                if {[dict exists $zoomboxopts $option]} {
                    dict set zoomboxopts $option $optionValue
                } else {
                    return -code error "option with name '$option' does not exists in zoomboxopts"
                }
            }
        } else {
            set zoomboxopts\
                    [argparse -inline [list [list -dashes= -key -dashes -default\
                                                     [option get $Subwidgets(graph) gtbZoomOutlineDashes\
                                                              GtbZoomOutlineDashes]]\
                                               [list -linewidth= -key -linewidth -default\
                                                        [option get $Subwidgets(graph) gtbZoomOutlineLineWidth\
                                                                 GtbZoomOutlineLineWidth]]\
                                               [list -xor= -key -xor -default\
                                                        [option get $Subwidgets(graph) gtbZoomOutlineXor\
                                                                 GtbZoomOutlineXor]]]\
                             $value]
        }
    }
    property zoommarkopts -set {
        if {[info exists zoommarkopts]} {
            dict for {option optionValue} $value {
                if {[dict exists $zoommarkopts $option]} {
                    dict set zoommarkopts $option $optionValue
                } else {
                    return -code error "option with name '$option' does not exists in zoommarkopts"
                }
            }
        } else {
            set zoommarkopts\
                [argparse -inline [list [list -font= -key -font -default\
                                                 [option get $Subwidgets(graph) gtbZoomTextFont GtbZoomTextFont]]\
                                           [list -anchor= -key -anchor -default\
                                                    [option get $Subwidgets(graph) gtbZoomTextAnchor\
                                                             GtbZoomTextAnchor]]\
                                           [list -justify=  -key -justify -default\
                                                    [option get $Subwidgets(graph) gtbZoomTextJustify\
                                                             GtbZoomTextJustify]]\
                                           [list -foreground= -key -foreground -default\
                                                    [option get $Subwidgets(graph) gtbZoomTextForeground\
                                                             GtbZoomTextForeground]]\
                                           [list -padx=  -key -padx -default\
                                                    [option get $Subwidgets(graph) gtbZoomTextPadX\
                                                             GtbZoomTextPadX]]\
                                           [list -formatx= -key -formatx -default\
                                                    [option get $Subwidgets(graph) gtbZoomTextXFormat\
                                                             GtbZoomTextXFormat]]\
                                           [list -formaty= -key -formaty -default\
                                                    [option get $Subwidgets(graph) gtbZoomTextYFormat\
                                                             GtbZoomTextYFormat]]\
                                           [list -pady=  -key -pady -default\
                                                    [option get $Subwidgets(graph) gtbZoomTextPadY GtbZoomTextPadY]]]\
                         $value]
        }
    }
    property zoommarkboxopts -set {
        if {[info exists zoommarkboxopts]} {
            dict for {option optionValue} $value {
                if {[dict exists $zoommarkboxopts $option]} {
                    dict set zoommarkboxopts $option $optionValue
                } else {
                    return -code error "option with name '$option' does not exists in zoommarkboxopts"
                }
            }
        } else {
            set zoommarkboxopts\
                    [argparse -inline [list [list -fill= -key -fill -default\
                                                     [option get $Subwidgets(graph) gtbZoomTextBoxFill\
                                                              GtbZoomTextBoxFill]]\
                                               [list -outline= -key -outline -default\
                                                        [option get $Subwidgets(graph) gtbZoomTextBoxOutline\
                                                                 GtbZoomTextBoxOutline]]\
                                               [list -linewidth= -key -linewidth -default\
                                                        [option get $Subwidgets(graph) gtbZoomTextBoxLineWidth\
                                                                 GtbZoomTextBoxLineWidth]]]\
                 $value]
        }
    }
    property crosshairsopts -set {
        set crosshairsopts\
                [argparse -inline\
                         [list\
                                  [list -linewidth= -key -linewidth -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsLineWidth\
                                                    GtbCrosshairsLineWidth]]\
                                  [list -color= -key -color -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsColor GtbCrosshairsColor]]\
                                  [list -dashes= -key -dashes -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsDashes GtbCrosshairsDashes]]]\
                         $value]
    }
    property crosshairsclosestopts -set {
        if {[info exists crosshairsclosestopts]} {
            dict for {option optionValue} $value {
                if {[dict exists $crosshairsclosestopts $option]} {
                    dict set crosshairsclosestopts $option $optionValue
                } else {
                    return -code error "option with name '$option' does not exists in crosshairsclosestopts"
                }
            }
        } else {
            set crosshairsclosestopts [argparse -inline {
                {-interpolate= -default yes}
                {-halo= -default 10}
                {-single= -default yes}
                {-hide= -default yes}} $value]
        }
    }
    property crosshairsmarkopts -set {
        if {[info exists crosshairsmarkopts]} {
            dict for {option optionValue} $value {
                if {[dict exists $crosshairsmarkopts $option]} {
                    dict set crosshairsmarkopts $option $optionValue
                } else {
                    return -code error "option with name '$option' does not exists in crosshairsmarkopts"
                }
            }
        } else {
            set crosshairsmarkopts\
                    [argparse -inline [list [list -font= -key -font -default\
                                                     [option get $Subwidgets(graph) gtbCrosshairsTextFont\
                                                              GtbCrosshairsTextFont]]\
                                               [list -anchor= -key -anchor -default\
                                                        [option get $Subwidgets(graph) gtbCrosshairsTextAnchor\
                                                                 GtbCrosshairsTextAnchor]]\
                                               [list -justify=  -key -justify -default\
                                                        [option get $Subwidgets(graph) gtbCrosshairsTextJustify\
                                                                 GtbCrosshairsTextJustify]]\
                                               [list -foreground= -key -foreground -default\
                                                        [option get $Subwidgets(graph) gtbCrosshairsTextForeground\
                                                                 GtbCrosshairsTextForeground]]\
                                               [list -formatx= -key -formatx -default\
                                                        [option get $Subwidgets(graph) gtbCrosshairsTextXFormat\
                                                                 GtbCrosshairsTextXFormat]]\
                                               [list -formaty= -key -formaty -default\
                                                        [option get $Subwidgets(graph) gtbCrosshairsTextYFormat\
                                                                 GtbCrosshairsTextYFormat]]\
                                               [list -formatparam= -key -formatparam -default\
                                                        [option get $Subwidgets(graph) gtbCrosshairsTextParamFormat\
                                                                 GtbCrosshairsTextParamFormat]]\
                                               [list -padx=  -key -padx -default\
                                                        [option get $Subwidgets(graph) gtbCrosshairsTextPadX\
                                                                 GtbCrosshairsTextPadX]]\
                                               [list -pady=  -key -pady -default\
                                                        [option get $Subwidgets(graph) gtbCrosshairsTextPadY\
                                                                 GtbCrosshairsTextPadY]]]\
                             $value]
        }
    }
    property crosshairsmarkboxopts -set {
        if {[info exists crosshairsmarkboxopts]} {
            dict for {option optionValue} $value {
                if {[dict exists $crosshairsmarkboxopts $option]} {
                    dict set crosshairsmarkboxopts $option $optionValue
                } else {
                    return -code error "option with name '$option' does not exists in crosshairsmarkboxopts"
                }
            }
        } else {
            set crosshairsmarkboxopts\
                    [argparse -inline [list [list -fill= -key -fill -default\
                                                     [option get $Subwidgets(graph) gtbCrosshairsTextBoxFill\
                                                              GtbCrosshairsTextBoxFill]]\
                                               [list -outline= -key -outline -default\
                                                        [option get $Subwidgets(graph) gtbCrosshairsTextBoxOutline\
                                                                 GtbCrosshairsTextBoxOutline]]\
                                               [list -linewidth= -key -linewidth -default\
                                                        [option get $Subwidgets(graph) gtbCrosshairsTextBoxLineWidth\
                                                                 GtbCrosshairsTextBoxLineWidth]]]\
                             $value]
        }
    }
    property crosshairsbarlineopts -set {
        if {[info exists crosshairsbarlineopts]} {
            dict for {option optionValue} $value {
                if {[dict exists $crosshairsbarlineopts $option]} {
                    dict set crosshairsbarlineopts $option $optionValue
                } else {
                    return -code error "option with name '$option' does not exists in crosshairsbarlineopts"
                }
            }
        } else {
        set crosshairsbarlineopts\
            [argparse -inline [list [list -outline= -key -outline -default\
                                             [option get $Subwidgets(graph) gtbCrosshairsBarLineOutline\
                                                      GtbCrosshairsBarLineOutline]]\
                                       [list -linewidth= -key -linewidth -default\
                                                [option get $Subwidgets(graph) gtbCrosshairsBarLineWidth\
                                                         GtbCrosshairsBarLineWidth]]\
                                       [list -arrowshape= -key -arrowshape -default\
                                                [option get $Subwidgets(graph) gtbCrosshairsBarLineArrowShape\
                                                         GtbCrosshairsBarLineArrowShape]]]\
                     $value]
        }
    }
    variable PsData ZoomInfo ZoomMod zoomtitle ZoomMark zoomtitleopts zoomboxopts zoommarkopts ZoomTransientChecks\
            zoommarkboxopts GraphType coordmark coordclosestmark crosshairsmode PanInfo PanTransientChecks
    variable CrosshairsSelector crosshairsmarkopts crosshairsmarkboxopts crosshairsclosestopts crosshairsopts
    variable crosshairsbarlineopts CrosshairsMarkerInfo
    variable CrosshairsSelector ClosestCoordSelector
    variable AxisScaleInfo SavedToolbarStates
    classmethod unknown {w args} {
        if {[string match .* $w]} {
            [self] new $w {*}$args
            return $w
        }
        next $w {*}$args
    }
    constructor {args} {
        classvariable CrosshairsModes CoordClosestMarkModes
        set arguments [argparse -inline -pfirst {
            path
            {-width= -default 800}
            {-height= -default 600}
            {-type= -default graph -enum {graph barchart stripchart polar}}
            {-representation= -default polar -enum {polar smith}}
            {-smithgrid= -default impedance -enum {impedance admittance both}}
            {-coordmark= -default auto}
            {-coordclosestmark= -default axis}
            {-toolbarside= -default bottom -enum {bottom top}}
            -zoom
            {-zoomstartbut= -default {ButtonPress-1}}
            {-zoomendbut= -default {ButtonRelease-1}}
            {-zoombackbut= -default {ButtonPress-3}}
            {-zoommod= -default {Any-}}
            {-zoomwheel -require zoom}
            {-zoomwheelscale= -require zoomwheel -default 1.1}
            {-zoomwheelmod= -require zoomwheel -default {Control-}}
            {-zoomtitle -require zoom}
            {-zoomtitleopts= -require zoom -type dict -default {}}
            {-zoomboxopts= -require zoom -type dict -default {}}
            {-zoommark -require zoom}
            {-zoommarkopts= -require zoom -type dict -default {}}
            {-zoommarkboxopts= -require zoom -type dict -default {}}
            {-pan -require zoom}
            {-panstartbut= -require pan -default {ButtonPress-1}}
            {-panendbut= -require pan -default {ButtonRelease-1}}
            {-panmod= -require pan -default {Shift-}}
            -crosshairs
            {-crosshairsmode= -require crosshairs -default closest}
            {-crosshairsopts= -require crosshairs -type dict -default {}}
            {-crosshairsmarkopts= -require crosshairs -type dict -default {}}
            {-crosshairsmarkboxopts= -require crosshairs -type dict -default {}}
            {-crosshairsclosestopts= -require crosshairs -type dict -default {}}
            {-crosshairsbarlineopts= -require crosshairs -type dict -default {}}
            {-scaletoggle= -type list}
            -activelegend
        }]
        set ZoomMod [dict get $arguments zoommod]
        set GraphType [dict get $arguments type]
        set currentNamespace [namespace current]
        set frameName [dict get $arguments path]
        ttk::frame $frameName
        set Subwidgets(toolbarFrame) [ttk::frame $frameName.toolbarFr]
        set Subwidgets(graph) [::rbc::$GraphType $frameName.graph -width [dict get $arguments width]\
                                       -height [dict get $arguments height]]
        if {$GraphType eq {polar}} {
            $Subwidgets(graph) configure -representation [dict get $arguments representation]\
                    -smithgrid [dict get $arguments smithgrid]
        }
        my configure -coordmark [dict get $arguments coordmark] -coordclosestmark [dict get $arguments coordclosestmark]
        grid $Subwidgets(graph) -row [expr {[dict get $arguments toolbarside] eq {bottom} ? 0 : 1}] -column 0\
                -sticky nsew
        grid $Subwidgets(toolbarFrame) -row [expr {[dict get $arguments toolbarside] eq {bottom} ? 1 : 0}] -column 0\
                -sticky ew
        set butCount -1
        set Subwidgets(makeSnapshotBut) [ttk::button $Subwidgets(toolbarFrame).makeSnapshotBut -width 14\
                                                 -image ::rbc::graphtoolbar::icons::makeSnapshotIcon\
                                                 -command [namespace code {my MakeSnapshot}]]
        grid $Subwidgets(makeSnapshotBut) -row 0 -column [incr butCount] -sticky ns
        set Subwidgets(postScriptDialogBut) [ttk::button $Subwidgets(toolbarFrame).postScriptDialogBut -width 14\
                                                     -image ::rbc::graphtoolbar::icons::postScriptDialogIcon\
                                                     -command [namespace code {my PostScriptDialog}]]
        grid $Subwidgets(postScriptDialogBut) -row 0 -column [incr butCount] -sticky ns

        ##### zoom activation
        if {[dict exists $arguments zoom]} {
            if {[dict exists $arguments zoomtitle]} {
                my configure -zoomtitle yes
            } else {
                my configure -zoomtitle no
            }
            if {[dict exists $arguments zoommark]} {
                set ZoomMark yes
            } else {
                set ZoomMark no
            }
            my configure -zoomtitleopts [dict get $arguments zoomtitleopts]
            my configure -zoomboxopts [dict get $arguments zoomboxopts]
            my configure -zoommarkopts [dict get $arguments zoommarkopts]
            my configure -zoommarkboxopts [dict get $arguments zoommarkboxopts]
            my EnableZoom [dict get $arguments zoomstartbut] [dict get $arguments zoomendbut]\
                    [dict get $arguments zoombackbut]
            if {[dict exists $arguments pan]} {
                my EnablePan [dict get $arguments panstartbut] [dict get $arguments panendbut]\
                        [dict get $arguments panmod]
            }
            if {[dict exists $arguments zoomwheel]} {
                my EnableWheelZoom [dict get $arguments zoomwheelmod] [dict get $arguments zoomwheelscale]
            }
            set Subwidgets(resetZoomBut) [ttk::button $Subwidgets(toolbarFrame).resetZoomBut -width 14\
                                                  -image ::rbc::graphtoolbar::icons::resetZoomIcon\
                                                  -command [namespace code {my ResetAllZoom}]]
            grid $Subwidgets(resetZoomBut) -row 0 -column [incr butCount] -sticky ns
            set Subwidgets(revertZoomBut) [ttk::button $Subwidgets(toolbarFrame).revertZoomBut -width 14\
                                                   -image ::rbc::graphtoolbar::icons::revertZoomIcon\
                                                   -command [namespace code {my ResetZoom}]]
            grid $Subwidgets(revertZoomBut) -row 0 -column [incr butCount] -sticky ns
        }

        ##### crosshairs activation
        if {[dict exists $arguments crosshairs]} {
            if {$GraphType eq {polar}} {
                if {[dict get $arguments representation] eq {smith}} {
                    if {[dict get $arguments smithgrid] eq {impedance}} {
                        my configure -coordclosestmark normalizedimpedance
                    } elseif {[dict get $arguments smithgrid] eq {admittance}} {
                        my configure -coordclosestmark normalizedadmittance
                    } else {
                        my configure -coordclosestmark normalizedimpedance
                    }
                }
            }
            my configure -crosshairsopts [dict get $arguments crosshairsopts]
            my configure -crosshairsmarkopts [dict get $arguments crosshairsmarkopts]
            my configure -crosshairsclosestopts [dict get $arguments crosshairsclosestopts]
            my configure -crosshairsbarlineopts [dict get $arguments crosshairsbarlineopts]
            my configure -crosshairsmarkboxopts [dict get $arguments crosshairsmarkboxopts]
            my configure -crosshairsmode [dict get $arguments crosshairsmode]

            set crosshairsModeWidths [list]
            foreach label [my CrosshairsModeLabels] {
                lappend crosshairsModeWidths [string length $label]
            }
            set Subwidgets(crosshairsComBox) \
                    [ttk::combobox $Subwidgets(toolbarFrame).crosshairsComBox -values [my CrosshairsModeLabels]\
                             -width [expr {[::tcl::mathfunc::max {*}$crosshairsModeWidths]}]\
                             -textvariable [self namespace]::CrosshairsSelector \
                             -state readonly -postcommand [namespace code {my UpdateCrosshairsModes}]]
            bind $Subwidgets(crosshairsComBox) <<ComboboxSelected>> [namespace code {my SelectCrosshairsMode}]
            grid $Subwidgets(crosshairsComBox) -row 0 -column [incr butCount] -sticky ns

            set closestCoordWidths [list]
            foreach mode $CoordClosestMarkModes {
                lappend closestCoordWidths [string length $mode]
            }
            set Subwidgets(closestCoordComBox)\
                [ttk::combobox $Subwidgets(toolbarFrame).closestCoordComBox -values [my ClosestCoordinateLabels]\
                         -width [expr {[::tcl::mathfunc::max {*}$closestCoordWidths]}]\
                         -textvariable [self namespace]::ClosestCoordSelector -state disabled\
                         -postcommand [namespace code {my UpdateClosestCoordinateModes}]]
            bind $Subwidgets(closestCoordComBox) <<ComboboxSelected>> [namespace code {my SelectClosestCoordinateMode}]
            grid $Subwidgets(closestCoordComBox) -row 0 -column [incr butCount] -sticky ns
            my UpdateCrosshairsModes
            my UpdateClosestCoordinateModes
            my ApplyCrosshairsMode
        }
        #### axes scales toggle activation
        if {[dict exists $arguments scaletoggle]} {
            my setAxisActiveScale [dict get $arguments scaletoggle]
        }

        #### active legend activation
        if {[dict exists $arguments activelegend]} {
            my EnableActiveLegend
        }
        grid columnconfigure $frameName 0 -weight 1
        if {[dict get $arguments toolbarside] eq "bottom"} {
            grid rowconfigure $frameName 0 -weight 1
        } else {
            grid rowconfigure $frameName 1 -weight 1
        }

        rename ::$frameName ::$frameName.fr
        rename [self] ::$frameName

        bind $frameName <Destroy> +[list [self] destroy]
    }
    destructor {
        foreach timer {titleTimer} {
            if {[info exists ZoomInfo($timer)]} {
                after cancel $ZoomInfo($timer)
            }
        }
    }
    method names {} {
        foreach widget [array names Subwidgets] {
            lappend names $widget
        }
        return $names
    }
    method subwidget {name} {
        if {[info exists Subwidgets($name)]} {
            return $Subwidgets($name)   
        } else {
            return -code error "Widget with name '$name' does not exist in a list of availible subwidgets"
        }
    }
    method graph {args} {
        tailcall $Subwidgets(graph) {*}$args
    }
    method widgetcommand {args} {
        set widget [lindex $args 0]
        if {![info exists Subwidgets($widget)]} {
            return -code error "Widget with name '$widget' does not exist in a list of availible subwidgets"
        }
        [set Subwidgets($widget)] {*}[lrange $args 1 end]
    }

    #### general private methods
    method AddBitmapPoint {name xValue yValue {mapx {}} {mapy {}}} {
        set mapopts [list]
        if {$mapx ne {}} {
            lappend mapopts -mapx $mapx
        }
        if {$mapy ne {}} {
            lappend mapopts -mapy $mapy
        }
        $Subwidgets(graph) marker create bitmap -name $name -coords [list $xValue $yValue]\
                -bitmap "@[file join $::rbc::graphtoolbar::libDir pointer.xbm]"\
                -mask "@[file join $::rbc::graphtoolbar::libDir pointer_mask.xbm]" -under no {*}$mapopts
    }
    method AddBindTag {widget tag {after {}}} {
        set tags [bindtags $widget]
        # remove existing occurrence, allowing this operation to reposition it.
        set tags [lsearch -all -inline -not -exact $tags $tag]
        if {$after eq {}} {
            set index 0
        } else {
            set pos [lsearch -exact $tags $after]
            if {$pos < 0} {
                return -code error "bindtag '$after' is not present on '$widget'"
            }
            set index [expr {$pos + 1}]
        }
        bindtags $widget [linsert $tags $index $tag]
    }
    method RemoveBindTag {widget tag} {
        set oldTagList [bindtags $widget]
        set index [lsearch $oldTagList $tag]
        if {$index >= 0} {
            bindtags $widget [lreplace $oldTagList $index $index]
        }
    }
    method CheckBindTagExistence {widget tag} {
        if {[lsearch -exact [bindtags $widget] $tag]<0} {
            return false
        } else {
            return true
        }
    }
    method ChangeToolbarState {action} {
        if {$action eq {restore}} {
            foreach w [winfo children $Subwidgets(toolbarFrame)] {
                if {[info exists SavedToolbarStates($w)]} {
                    $w configure -state $SavedToolbarStates($w)
                    unset SavedToolbarStates($w)
                }
            }
        } elseif {$action eq {disable}} {
            foreach w [winfo children $Subwidgets(toolbarFrame)] {
                if {[catch {$w cget -state} state]} {
                    continue
                }
                set SavedToolbarStates($w) $state
                $w configure -state disabled
            }
        } else {
            return -code error "Action '$action' is unknown"
        }
    }
    method TextSize {text options} {
        # Calculates size of a text marker in physical widget pixels.
        # Result:
        #   {width height}
        set graph $Subwidgets(graph)
        set font [dict get $options -font]
        set padx [winfo pixels $graph [dict get $options -padx]]
        set pady [winfo pixels $graph [dict get $options -pady]]
        set width 0
        set lines [split $text \n]
        foreach line $lines {
            set lineWidth [font measure $font -displayof $graph $line]
            if {$lineWidth>$width} {
                set width $lineWidth
            }
        }
        set height [expr {[llength $lines]*[font metrics $font -displayof $graph -linespace]}]
        incr width  [expr {2*$padx}]
        incr height [expr {2*$pady}]
        return [list $width $height]
    }
    method TextBox {x y text options {anchor {}}} {
        # Calculates text marker bounding box in physical widget pixels.
        #
        # x/y:
        #   Physical widget coordinates of the marker position.
        #
        # Result:
        #   {left top right bottom}
        lassign [my TextSize $text $options] width height
        if {$anchor eq {}} {
            set anchor [dict get $options -anchor]
        }
        switch -- $anchor {
            nw {
                set left $x
                set top $y
            }
            n {
                set left [expr {$x-$width/2}]
                set top $y
            }
            ne {
                set left [expr {$x-$width}]
                set top $y
            }
            w {
                set left $x
                set top [expr {$y-$height/2}]
            }
            center {
                set left [expr {$x-$width/2}]
                set top [expr {$y-$height/2}]
            }
            e {
                set left [expr {$x-$width}]
                set top [expr {$y-$height/2}]
            }
            sw {
                set left $x
                set top [expr {$y-$height}]
            }
            s {
                set left [expr {$x-$width/2}]
                set top [expr {$y-$height}]
            }
            se {
                set left [expr {$x-$width}]
                set top [expr {$y-$height}]
            }
            default {
                return -code error "unknown anchor '$anchor'"
            }
        }
        # RBC's text-marker extents are inclusive:
        # right = left + width - 1
        # bottom = top + height - 1
        set right [expr {$left+$width-1}]
        set bottom [expr {$top+$height-1}]
        return [list $left $top $right $bottom]
    }
    method TextOffset {x y anchor {offset 3}} {
        # Moves a text marker away from its reference point according
        # to the direction in which the selected anchor extends.
        #
        # x/y:
        #   Physical widget pixels of the reference point.
        #
        # Result:
        #   {x y} physical widget pixels of the shifted text position.
        switch -- $anchor {
            nw {
                incr x $offset
                incr y $offset
            }
            n {
                incr y $offset
            }
            ne {
                incr x -$offset
                incr y $offset
            }
            w {
                incr x $offset
            }
            center {
                # No natural direction.
            }
            e {
                incr x -$offset
            }
            sw {
                incr x $offset
                incr y -$offset
            }
            s {
                incr y -$offset
            }
            se {
                incr x -$offset
                incr y -$offset
            }
            default {
                return -code error "unknown anchor '$anchor'"
            }
        }
        return [list $x $y]
    }
    method TextAnchor {x y text options} {
        # Selects an anchor which keeps the complete shifted text marker
        # inside the plotting area.
        #
        # x/y are physical widget pixels of the reference point.
        set graph $Subwidgets(graph)
        lassign [$graph extents plotarea] plotLeft plotTop plotWidth plotHeight
        set plotRight [expr {$plotLeft+$plotWidth-1}]
        set plotBottom [expr {$plotTop+$plotHeight-1}]
        set preferred [dict get $options -anchor]
        set anchors [list $preferred nw ne sw se n s w e center]
        set tried [dict create]
        foreach anchor $anchors {
            if {[dict exists $tried $anchor]} {
                continue
            }
            dict set tried $anchor true
            # Test the actual shifted marker location.
            lassign [my TextOffset $x $y $anchor] textX textY
            lassign [my TextBox $textX $textY $text $options $anchor] left top right bottom
            if {($left>=$plotLeft) && ($top>=$plotTop) && ($right<=$plotRight) && ($bottom<=$plotBottom)} {
                return $anchor
            }
        }
        return $preferred
    }
    method DrawTextBackground {name x y text textOptions boxOptions mapx mapy} {
        set graph $Subwidgets(graph)
        if {([dict get $boxOptions -fill] eq {}) && ([dict get $boxOptions -outline] eq {})} {
            if {[$graph marker exists $name]} {
                $graph marker delete $name
            }
            return
        }
        lassign [my TextBox $x $y $text $textOptions] left top right bottom
        lassign [my WidgetToAxisValues $left $top $mapx $mapy] xValue1 yValue1
        lassign [my WidgetToAxisValues $right $bottom $mapx $mapy] xValue2 yValue2
        set coords [list $xValue1 $yValue1 $xValue2 $yValue1 $xValue2 $yValue2 $xValue1 $yValue2]
        if {[$graph marker exists $name]} {
            $graph marker configure $name -coords $coords -mapx $mapx -mapy $mapy {*}$boxOptions
        } else {
            $graph marker create polygon -name $name -coords $coords -mapx $mapx -mapy $mapy -bindtags {} -under no\
                    {*}$boxOptions
        }
    }
    method UsedAxes {dimension} {
        # Returns all axes actually used by the graph for the requested
        # data dimension:
        #
        #   - axes installed in the corresponding margins
        #   - axes mapped by elements
        #   - for a polar widget, the axis mapped by the Polar/Smith grid
        #
        # Completely unused axes are excluded.
        set graph $Subwidgets(graph)
        switch -- $dimension {
            x {
                set margins {xaxis x2axis}
                set mapOption -mapx
            }
            y {
                set margins {yaxis y2axis}
                set mapOption -mapy
            }
            default {
                return -code error "unknown axis dimension '$dimension': must be x or y"
            }
        }
        set axes [list]
        #
        # Axes explicitly installed in the graph margins.
        #
        foreach margin $margins {
            foreach axis [$graph $margin use] {
                if {($axis ne {}) && ($axis ni $axes)} {
                    lappend axes $axis
                }
            }
        }
        #
        # Axes used by elements, including axes that are not currently
        # displayed in any margin.
        #
        foreach elem [$graph element names] {
            set axis [$graph element cget $elem $mapOption]
            if {($axis ne {}) && ($axis ni $axes)} {
                lappend axes $axis
            }
        }
        #
        # A Polar/Smith grid can use an axis that is neither installed in
        # a margin nor mapped by an element.  It still participates in
        # the displayed coordinate system and in Polar automatic aspect
        # calculation, so it must participate in zoom/reset operations.
        #
        if {$GraphType eq {polar}} {
            set axis [$graph grid cget $mapOption]
            if {($axis ne {}) && ($axis ni $axes)} {
                lappend axes $axis
            }
        }
        return $axes
    }
    method PolarGridAxes {} {
        # Returns the axes which control the Polar/Smith grid itself.
        #
        # This is deliberately different from UsedAxes: auxiliary,
        # element-only, and margin-only axes do not determine whether
        # the Polar/Smith representation can be drawn.
        if {$GraphType ne {polar}} {
            return {}
        }
        set graph $Subwidgets(graph)
        set axes [list]
        foreach option {-mapx -mapy} {
            set axis [$graph grid cget $option]
            if {($axis ne {}) && ($axis ni $axes)} {
                lappend axes $axis
            }
        }

        return $axes
    }
    method VisibleAxes {dimension} {
        # Returns all visible named axes for the requested data dimension, preserving their margin/use order.
        set graph $Subwidgets(graph)
        set axes [list]
        foreach axis [my UsedAxes $dimension] {
            if {![$graph axis cget $axis -hide]} {
                lappend axes $axis
            }
        }
        return $axes
    }
    method InteractionAxes {dimension} {
        # Returns axes used by current-coordinate overlays.
        #
        # Ordinary graphs report all visible axes.
        #
        # A Polar/Smith widget additionally reports the axis mapped to the
        # specialized grid, even if that axis itself is hidden or is not
        # installed in a graph margin.
        set graph $Subwidgets(graph)
        set axes [my VisibleAxes $dimension]
        if {$GraphType ne {polar}} {
            return $axes
        }
        switch -- $dimension {
            x {
                set mapOption -mapx
            }
            y {
                set mapOption -mapy
            }
            default {
                return -code error "unknown axis dimension '$dimension': must be x or y"
            }
        }
        set gridAxis [$graph grid cget $mapOption]
        if {($gridAxis ne {}) && ($gridAxis ni $axes)} {
            lappend axes $gridAxis
        }
        return $axes
    }
    method WidgetToAxisPixels {x y} {
        # Converts physical widget pixels to pixels along the X/Y data dimensions.
        if {[$Subwidgets(graph) cget -invertxy]} {
            return [list $y $x]
        }
        return [list $x $y]
    }
    method AxisPixelsToWidget {xPixel yPixel} {
        # Converts pixels along the X/Y data dimensions to physical widget pixels.
        if {[$Subwidgets(graph) cget -invertxy]} {
            return [list $yPixel $xPixel]
        }
        return [list $xPixel $yPixel]
    }
    method WidgetToAxisValues {x y mapx mapy} {
        set graph $Subwidgets(graph)
        lassign [my WidgetToAxisPixels $x $y] xPixel yPixel
        # axis invtransform takes integer pixel coordinates.
        set xPixel [expr {round($xPixel)}]
        set yPixel [expr {round($yPixel)}]
        set xValue [$graph axis invtransform $mapx $xPixel]
        set yValue [$graph axis invtransform $mapy $yPixel]
        return [list $xValue $yValue]
    }
    method CoordinateOrientations {} {
        if {[$Subwidgets(graph) cget -invertxy]} {
            return {v h}
        }
        return {h v}
    }
    method FormatRealImagMarkerValue {value formatReal formatImag} {
        if {[llength $value] != 2} {
            return -code error {real/imag marker value must contain real and imaginary components}
        }
        lassign $value real imag
        # Suppress textual -0.
        if {$real == 0.0} {
            set real 0.0
        }
        if {$imag == 0.0} {
            set imag 0.0
        }
        lassign [my CoordinateOrientations] realOrientation imagOrientation
        set realText [format "%$formatReal" $real]
        set imagText [format "%$formatImag" $imag]
        return "real($realOrientation)=$realText\nimag($imagOrientation)=$imagText"
    }
    method AxisMarkerInfo {xPixel yPixel formatx formaty} {
        #
        # Returns formatted values of the axes used for current/zoom
        # coordinate display.
        #
        # Ordinary graphs:
        #   all visible axes.
        #
        # Polar/Smith:
        #   all visible axes plus the specialized grid axes.
        #
        # Grid X/Y axes are displayed semantically as real/imag rather
        # than by their underlying RBC axis names.
        #
        set graph $Subwidgets(graph)
        set xAxes [my InteractionAxes x]
        set yAxes [my InteractionAxes y]
        if {[llength $xAxes] == 0} {
            return -code error {graph has no X axis available for coordinate display}
        }
        if {[llength $yAxes] == 0} {
            return -code error {graph has no Y axis available for coordinate display}
        }
        set mapx [lindex $xAxes 0]
        set mapy [lindex $yAxes 0]
        set lines [list]
        lassign [my CoordinateOrientations] xOrientation yOrientation
        foreach axis $xAxes {
            set xValue [$graph axis invtransform $axis $xPixel]
            set name [my AxisMarkerName $axis x]
            lappend lines [format "%s($xOrientation)=%$formatx" $name $xValue]
            if {$axis eq $mapx} {
                set markerXValue $xValue
            }
        }
        foreach axis $yAxes {
            set yValue [$graph axis invtransform $axis $yPixel]
            set name [my AxisMarkerName $axis y]
            lappend lines [format "%s($yOrientation)=%$formaty" $name $yValue]
            if {$axis eq $mapy} {
                set markerYValue $yValue
            }
        }
        return [dict create text [join $lines \n] mapx $mapx mapy $mapy xValue $markerXValue yValue $markerYValue]
    }
    method AxisMarkerName {axis dimension} {
        set graph $Subwidgets(graph)
        if {$GraphType ne {polar}} {
            return $axis
        }
        switch -- $dimension {
            x {
                set gridAxis [$graph grid cget -mapx]
                if {$axis eq $gridAxis} {
                    return real
                }
            }
            y {
                set gridAxis [$graph grid cget -mapy]
                if {$axis eq $gridAxis} {
                    return imag
                }
            }
            default {
                return -code error "unknown axis dimension '$dimension': must be x or y"
            }
        }
        #
        # An auxiliary visible axis which is not part of the specialized
        # Polar/Smith grid retains its real axis name.
        #
        return $axis
    }

    method FormatComplexMarkerValue {name value formatReal formatImag {unit {}}} {
        if {[llength $value]!=2} {
            return -code error {complex marker value must contain real and imaginary components}
        }
        lassign $value real imag
        #
        # Suppress textual -0.
        #
        if {$real==0.0} {
            set real 0.0
        }
        if {$imag==0.0} {
            set imag 0.0
        }
        set realText [format "%$formatReal" $real]
        set imagText [format "%$formatImag" [expr {abs($imag)}]]
        if {$imag < 0.0} {
            set sign -
        } else {
            set sign +
        }
        set text "${name}=${realText}${sign}j${imagText}"
        if {$unit ne {}} {
            append text " $unit"
        }
        return $text
    }
    method CartesianPolarValues {xValue yValue} {
        set radius [expr {hypot($xValue,$yValue)}]
        if {$radius==0.0} {
            set angle 0.0
        } else {
            set angle [expr {atan2($yValue,$xValue)*180.0/acos(-1.0)}]
            if {$angle < 0.0} {
                set angle [expr {$angle+360.0}]
            }
            if {$angle==0.0} {
                set angle 0.0
            }
        }
        return [list $radius $angle]
    }
    method FormatPolarMarkerValue {radius angle formatRadius formatAngle} {
        set radiusText [format "%$formatRadius" $radius]
        set angleText [format "%$formatAngle" $angle]
        return "r=$radiusText\nangle=$angleText deg"
    }
    method SmithGammaToNormalized {gammaReal gammaImag mode} {
        #
        # Normalized impedance:
        #
        #               1 + Gamma
        #     z = -------------------
        #               1 - Gamma
        #
        # Normalized admittance:
        #
        #               1 - Gamma
        #     y = -------------------
        #               1 + Gamma
        #
        switch -- $mode {
            normalizedimpedance {
                set denom [expr {(1.0-$gammaReal)*(1.0-$gammaReal) + $gammaImag*$gammaImag}]
                if {$denom==0.0} {
                    #
                    # Gamma = +1: open circuit.
                    #
                    return [list Inf 0.0]
                }
                set real [expr {(1.0 - $gammaReal*$gammaReal - $gammaImag*$gammaImag) / $denom}]
                set imag [expr {2.0*$gammaImag / $denom}]
            }
            normalizedadmittance {
                set denom [expr { (1.0+$gammaReal)*(1.0+$gammaReal) + $gammaImag*$gammaImag }]
                if {$denom == 0.0} {
                    #
                    # Gamma = -1: infinite normalized admittance.
                    #
                    return [list Inf 0.0]
                }
                set real [expr {(1.0 - $gammaReal*$gammaReal - $gammaImag*$gammaImag) / $denom}]
                set imag [expr {-2.0*$gammaImag / $denom}]
            }
            default {
                return -code error "unknown Smith normalized coordinate mode '$mode'"
            }
        }
        if {$real == 0.0} {
            set real 0.0
        }
        if {$imag == 0.0} {
            set imag 0.0
        }
        return [list $real $imag]
    }
    method CoordinateMarkerInfo {xPixel yPixel formatx formaty mode} {
        set graph $Subwidgets(graph)
        #
        # Axis mode is the historical behavior:
        #
        #   current marker -> all interaction axes
        #   zoom marker    -> all interaction axes
        #
        if {$mode eq {auto}} {
            set mode [my EffectiveCoordinateMode]
        }
        if {$mode eq {axis}} {
            return [my AxisMarkerInfo $xPixel $yPixel $formatx $formaty]
        }
        #
        # Semantic Polar coordinates are based on the grid mapping itself.
        #
        if {$GraphType eq {polar}} {
            set mapx [$graph grid cget -mapx]
            set mapy [$graph grid cget -mapy]
        } else {
            set xAxes [my InteractionAxes x]
            set yAxes [my InteractionAxes y]
            if {![llength $xAxes] || ![llength $yAxes]} {
                return [my AxisMarkerInfo $xPixel $yPixel $formatx $formaty]
            }
            set mapx [lindex $xAxes 0]
            set mapy [lindex $yAxes 0]
        }
        if {($mapx eq {}) || ($mapy eq {})} {
            return [my AxisMarkerInfo $xPixel $yPixel $formatx $formaty]
        }
        set xPixel [expr {round($xPixel)}]
        set yPixel [expr {round($yPixel)}]
        set xValue [$graph axis invtransform $mapx $xPixel]
        set yValue [$graph axis invtransform $mapy $yPixel]
        switch -- $mode {
            complex {
                set text [my FormatComplexMarkerValue z [list $xValue $yValue] $formatx $formaty]
            }
            gamma {
                set text [my FormatComplexMarkerValue Gamma [list $xValue $yValue] $formatx $formaty]
            }
            polar {
                lassign [my CartesianPolarValues $xValue $yValue] radius angle
                set text [my FormatPolarMarkerValue $radius $angle $formatx $formaty]
            }
            normalizedimpedance -
            normalizedimpedanceri {
                set value [my SmithGammaToNormalized $xValue $yValue normalizedimpedance]
                if {$mode eq {normalizedimpedanceri}} {
                    set text [my FormatRealImagMarkerValue $value $formatx $formaty]
                } else {
                    set text [my FormatComplexMarkerValue z $value $formatx $formaty]
                }
            }
            normalizedadmittance -
            normalizedadmittanceri {
                set value [my SmithGammaToNormalized $xValue $yValue normalizedadmittance]
                if {$mode eq {normalizedadmittanceri}} {
                    set text [my FormatRealImagMarkerValue $value $formatx $formaty]
                } else {
                    set text [my FormatComplexMarkerValue y $value $formatx $formaty]
                }
            }
            default {
                return [my AxisMarkerInfo $xPixel $yPixel $formatx $formaty]
            }
        }
        return [dict create text $text mapx $mapx mapy $mapy xValue $xValue yValue $yValue]
    }

    #### axes toggle methods
    method ToggleAxisScale {graph} {
        set axis [$graph axis get current]
        if {$axis eq {}} {
            return
        }
        set logscale [$graph axis cget $axis -logscale]
        set newLogscale [expr {!$logscale}]
        #
        # Polar and Smith grids require linear Cartesian grid axes.
        #
        # Prevent the toolbar from switching a visible Polar/Smith grid
        # axis from linear to logarithmic.  Auxiliary axes remain free
        # to use logarithmic scaling.
        #
        # Always allow logarithmic -> linear so that a grid which was
        # made invalid by external configuration can be recovered.
        #
        if {$newLogscale && ($GraphType eq {polar}) && ![$graph grid cget -hide] && ($axis in [my PolarGridAxes])} {
            bell 
            return 
        }
        $graph axis configure $axis -logscale $newLogscale
    }
    method setAxisActiveScale {args} {
        # Enables/disables active linear-logarithmic scale axis toggling.
        #  axes - list of axes names
        #  -disabled - disables toggling for selected axes
        #  -event value - event action that toggles the axis, default is `ButtonPress-1`
        #  -background color - set axis background when hover pointer over it, default is `grey`
        argparse -pfirst {
            {axes -type list}
            {-disabled -boolean}
            {-event= -default {ButtonPress-1}}
            {-background= -default grey}
        }
        set graph $Subwidgets(graph)
        set tag active-axis-$graph
        if {$axes eq {all}} {
            set axes [$graph axis names]
        }
        if {$disabled} {
            # remove tag for provided axes
            foreach axis $axes {
                set tags [lindex [$graph axis configure $axis -bindtags] end]
                $graph axis configure $axis -bindtags [lsearch -all -inline -not -exact $tags $tag]
                # Remove a leftover highlight when disabling.
                $graph axis configure $axis -background {}
            }
            # Remove the shared bindings only when no axis still uses this tag.
            set stillUsed false
            foreach axis [$graph axis names] {
                set tags [lindex [$graph axis configure $axis -bindtags] end]
                if {$tag in $tags} {
                    set stillUsed true
                    break
                }
            }
            if {!$stillUsed} {
                foreach sequence [$graph axis bind $tag] {
                    $graph axis bind $tag $sequence {}
                }
                unset -nocomplain AxisScaleInfo(event)
            }
            return
        }
        # If the activation event changed, remove the old binding.
        if {[info exists AxisScaleInfo(event)] && ($AxisScaleInfo(event) ne $event)} {
            $graph axis bind $tag <$AxisScaleInfo(event)> {}
        }
        set AxisScaleInfo(event) $event
        # these bindings are shared by every axis carrying $tag.
        $graph axis bind $tag <Enter> [string map [list @background@ $background] {
            set axis [%W axis get current]
            set detail [%W axis get detail]
            if {($axis ne {}) && ($detail eq {line})} {
                %W axis configure $axis -background @background@
            }
        }]
        $graph axis bind $tag <Leave> {
            set axis [%W axis get current]
            if {$axis ne {}} {
                %W axis configure $axis -background {}
            }
        }
        $graph axis bind $tag <$event> [namespace code [list my ToggleAxisScale %W]]
        # update tags for provided axes
        foreach axis $axes {
            set tags [lindex [$graph axis configure $axis -bindtags] end]
            if {$tag ni $tags} {
                $graph axis configure $axis -bindtags [linsert $tags 0 $tag]
            }
        }
    }
    method getAxisActiveScale {} {
        # Returns list of axes names that are configured to be able to toggle linear/log scale.
        set graph $Subwidgets(graph)
        set tag active-axis-$graph
        set axes [$graph axis names]
        set activeAxes [list]
        foreach axis $axes {
            set tags [lindex [$graph axis configure $axis -bindtags] end]
            if {$tag in $tags} {
                lappend activeAxes $axis
            }
        }
        return $activeAxes
    }
    #### active legend methods
    method EnableActiveLegend {} {
        # Enables three-state legend interaction:
        #
        #   normal -> active -> plot-hidden -> normal
        #
        # Legend relief indicates the persistent state:
        #
        #   flat    normal
        #   raised  active
        #   sunken  plot-hidden
        set graph $Subwidgets(graph)
        $graph legend bind all <Enter> [namespace code {my ActivateLegend}]
        $graph legend bind all <Leave> [namespace code {my DeactivateLegend}]
        $graph legend bind all <ButtonPress-1> [namespace code {my ToggleLegendElement}]
    }
    method LegendInteractionSuppressed {} {
        return [expr {([info exists ZoomInfo(corner)] && ($ZoomInfo(corner) eq {B})) ||\
                              ([info exists PanInfo(active)] && $PanInfo(active))}]
    }
    method ActivateLegend {} {
        set graph $Subwidgets(graph)
        if {[my LegendInteractionSuppressed]} {
            return
        }
        set elem [$graph legend get current]
        if {$elem ne {}} {
            $graph legend activate $elem
        }
    }
    method DeactivateLegend {} {
        set graph $Subwidgets(graph)
        set elem [$graph legend get current]
        if {$elem ne {}} {
            $graph legend deactivate $elem
        }
    }
    method ToggleLegendElement {} {
        set graph $Subwidgets(graph)
        if {[my LegendInteractionSuppressed]} {
            return
        }
        set elem [$graph legend get current]
        if {$elem eq {}} {
            return
        }
        set hidePlot [$graph element cget $elem -hideplot]
        set relief [$graph element cget $elem -labelrelief]
        if {$hidePlot} {
            # plot-hidden -> normal
            # restore plotting and clear any persistent element activation.
            $graph element configure $elem -hideplot no -labelrelief flat
            $graph element deactivate $elem
        } elseif {$relief eq {raised}} {
            # active -> plot-hidden
            # remove the active drawing first, then suppress normal plotting.
            # -hideplot deliberately leaves the element in the display list,
            # therefore its legend entry remains available for the next click.
            $graph element deactivate $elem
            $graph element configure $elem -hideplot yes -labelrelief sunken
        } else {
            # normal -> active
            $graph element configure $elem -labelrelief raised
            $graph element activate $elem
        }
    }
    #### crosshairs methods
    ##### general crosshairs methods
    method CanRefreshCrosshairsMarker {x y} {
        set graph $Subwidgets(graph)
        if {![info exists CrosshairsMarkerInfo]} {
            return false
        }
        if {![my CheckBindTagExistence $graph crosshairs-marker-$graph]} {
            return false
        }
        # x/y are optional for zoom operations invoked without a graph
        # pointer event, e.g. ResetAllZoom from the toolbar.
        if {($x eq {}) || ($y eq {})} {
            return false
        }
        if {![$graph inside $x $y]} {
            return false
        }
        return true
    }
    method DeleteCrosshairsMarkers {} {
        set graph $Subwidgets(graph)
        set markerNames [$graph marker names crosshairs*]
        if {[llength $markerNames]} {
            $graph marker delete {*}$markerNames
        }
    }
    method RefreshCrosshairsMarker {x y} {
        set graph $Subwidgets(graph)
        if {![my CanRefreshCrosshairsMarker $x $y]} {
            return
        }
        my DeleteCrosshairsMarkers
        my CrosshairsMarkerMotion $graph $x $y {*}$CrosshairsMarkerInfo
    }
    method CrosshairsMarkerMotion {graph x y options mode interpolate halo single} {
        if {![$graph inside $x $y]} {
            return
        }
        if {$mode eq {current}} {
            lassign [my WidgetToAxisPixels $x $y] xPixel yPixel
            #
            # -coordmark is read at drawing time.  "axis" preserves the
            # normal current-marker behaviour: all visible axes plus, for
            # Polar/Smith, any additional axes mapped by the grid.
            #
            set info [my CoordinateMarkerInfo $xPixel $yPixel [dict get $options -formatx] [dict get $options -formaty]\
                              [my configure -coordmark]]
            set text [dict get $info text]
            set anchor [my TextAnchor $x $y $text $options]
            dict set options -anchor $anchor
            set mapx [dict get $info mapx]
            set mapy [dict get $info mapy]
            lassign [my TextOffset $x $y $anchor] textX textY
            set boxOptions [my configure -crosshairsmarkboxopts]
            my DrawTextBackground crosshairsTextBox $textX $textY $text $options $boxOptions $mapx $mapy
            lassign [my WidgetToAxisValues $textX $textY $mapx $mapy] textXValue textYValue
            $graph marker create text -name crosshairsText -text $text -coords [list $textXValue $textYValue] -mapx\
                    $mapx -mapy $mapy {*}[dict remove $options -formatx -formaty -formatparam]
            return
        }
        if {$mode ne {closest}} {
            return
        }
        set boxOptions [my configure -crosshairsmarkboxopts]
        if {$single} {
            #
            # pointVar may contain Polar/Smith-specific fields.  Always
            # clear it before a new search so stale fields can never
            # survive from a previous element.
            #
            unset -nocomplain pointVar
            if {[$graph element closest $x $y pointVar -along both -interpolate $interpolate -halo $halo]} {
                set element $pointVar(name)
                set closestInfo [array get pointVar]
                if {([$graph element type $element] eq {BarElement}) && [info exists pointVar(left)] &&\
                            [info exists pointVar(top)] && [info exists pointVar(right)] &&\
                            [info exists pointVar(bottom)]} {
                    my CreateClosestBarMarker crosshairsClosestText $element $pointVar(x) $pointVar(y) $pointVar(left)\
                            $pointVar(top) $pointVar(right) $pointVar(bottom) $options $boxOptions $closestInfo
                } else {
                    my CreateClosestMarker $graph crosshairsClosestText crosshairsClosestBitmap $element $pointVar(x)\
                            $pointVar(y) $options $closestInfo
                }
            }
            return
        }
        set i 0
        foreach elem [$graph element names] {
            #
            # -hideplot leaves the legend entry available but the element
            # must not participate in closest searches.
            #
            if {[$graph element cget $elem -hideplot]} {
                continue
            }
            #
            # Each element search starts with a completely fresh result
            # array.  This is important when ordinary and PolarElements
            # are mixed in the same graph.
            #
            unset -nocomplain pointVar
            if {![$graph element closest $x $y pointVar -along both -interpolate $interpolate -halo $halo $elem]} {
                continue
            }
            set element $pointVar(name)
            set closestInfo [array get pointVar]
            set marker crosshairsClosestText$i

            if {([$graph element type $element] eq {BarElement}) && [info exists pointVar(left)] &&\
                        [info exists pointVar(top)] && [info exists pointVar(right)] &&\
                        [info exists pointVar(bottom)]} {
                my CreateClosestBarMarker $marker $element $pointVar(x) $pointVar(y) $pointVar(left) $pointVar(top)\
                        $pointVar(right) $pointVar(bottom) $options $boxOptions $closestInfo
            } else {
                my CreateClosestMarker $graph crosshairsClosestText$i crosshairsClosestBitmap$i $element $pointVar(x)\
                        $pointVar(y) $options $closestInfo
            }
            incr i
        }
    }
    method CrosshairsMotion {graph x y {hide no}} {
        set markerNames [$graph marker names crosshairs*]
        $graph marker delete {*}$markerNames
        if {!$hide} {
            $graph crosshairs configure -position @${x},$y
        }
    }
    method CrosshairsModeLabel {mode} {
        classvariable CrosshairsModeLabels
        return [dict get $CrosshairsModeLabels $mode]
    }
    method CrosshairsModeFromLabel {label} {
        classvariable CrosshairsModeLabels
        dict for {mode text} $CrosshairsModeLabels {
            if {$text eq $label} {
                return $mode
            }
        }
        return -code error "unknown crosshairs mode label '$label'"
    }
    method CrosshairsModes {} {
        classvariable CrosshairsModeNames
        return $CrosshairsModeNames
    }
    method CrosshairsModeLabels {} {
        set labels [list]
        foreach mode [my CrosshairsModes] {
            lappend labels [my CrosshairsModeLabel $mode]
        }
        return $labels
    }
    method SelectCrosshairsMode {} {
        set mode [my CrosshairsModeFromLabel $CrosshairsSelector]
        my configure -crosshairsmode $mode
    }
    method UpdateCrosshairsModes {} {
        if {![info exists Subwidgets(crosshairsComBox)]} {
            return
        }
        $Subwidgets(crosshairsComBox) configure -values [my CrosshairsModeLabels]
        set CrosshairsSelector [my CrosshairsModeLabel [my configure -crosshairsmode]]
    }
    method ApplyCrosshairsMode {} {
        set graph $Subwidgets(graph)
        set tagCrosshairs crosshairs-$graph
        set tagCrosshairsMarker crosshairs-marker-$graph
        my RemoveBindTag $graph $tagCrosshairs
        my RemoveBindTag $graph $tagCrosshairsMarker
        set options [my configure -crosshairsmarkopts]
        $graph crosshairs configure {*}[my configure -crosshairsopts]
        #
        # This remains the canonical argument tuple used both by normal
        # pointer motion and by redraw after an axis-transform change.
        #
        # Coordinate-marker modes are intentionally not stored here.
        #
        unset -nocomplain CrosshairsMarkerInfo
        #
        # The closest-coordinate selector has meaning only in Closest
        # point mode.
        #
        if {[info exists Subwidgets(closestCoordComBox)]} {
            my UpdateClosestCoordinateModes
            $Subwidgets(closestCoordComBox) configure -state disabled
        }
        switch -- [my configure -crosshairsmode] {
            current {
                $graph crosshairs on
                bind $tagCrosshairs <Leave> {
                    %W crosshairs off
                }
                bind $tagCrosshairs <Enter> {
                    %W crosshairs on
                }
                bind $tagCrosshairs <Any-Motion> [namespace code [list my CrosshairsMotion %W %x %y]]
                set CrosshairsMarkerInfo [list $options current {} {} {}]
                bind $tagCrosshairsMarker <Any-Motion> [namespace code [list my CrosshairsMarkerMotion %W %x %y\
                                                                                {*}$CrosshairsMarkerInfo]]
                my AddBindTag $graph $tagCrosshairs
                my AddBindTag $graph $tagCrosshairsMarker $tagCrosshairs
            } 
            closest  {
                dict with crosshairsclosestopts {}
                if {[info exists Subwidgets(closestCoordComBox)]} {
                    $Subwidgets(closestCoordComBox) configure -state readonly
                }
                if {!$hide} {
                    $graph crosshairs on
                    bind $tagCrosshairs <Leave> {
                        %W crosshairs off
                    }
                    bind $tagCrosshairs <Enter> {
                        %W crosshairs on
                    }
                } else {
                    $graph crosshairs off
                }
                bind $tagCrosshairs <Any-Motion> [namespace code [list my CrosshairsMotion %W %x %y $hide]]
                set CrosshairsMarkerInfo [list $options closest $interpolate $halo $single]
                bind $tagCrosshairsMarker <Any-Motion> [namespace code [list my CrosshairsMarkerMotion %W %x %y\
                                                                                {*}$CrosshairsMarkerInfo]]
                my AddBindTag $graph $tagCrosshairs
                my AddBindTag $graph $tagCrosshairsMarker $tagCrosshairs
                unset -nocomplain {*}[dict keys $crosshairsclosestopts]
            }
            disabled  {
                $graph crosshairs off
            } 
            none {
                #
                # "No marker": ordinary RBC crosshairs only.
                #
                $graph crosshairs on
                bind $tagCrosshairs <Any-Motion> {
                    %W crosshairs configure -position @%x,%y
                }
                bind $tagCrosshairs <Leave> {
                    %W crosshairs off
                }
                bind $tagCrosshairs <Enter> {
                    %W crosshairs on
                }
                my AddBindTag $graph $tagCrosshairs
            }
        }
    }
    ##### current crosshairs methods
    method DefaultSmithCoordinateMode {} {
        set graph $Subwidgets(graph)
        switch -- [$graph cget -smithgrid] {
            admittance {
                return normalizedadmittanceri
            }
            impedance -
            both {
                return normalizedimpedanceri
            }
        }
        return normalizedimpedanceri
    }
    method EffectiveCoordinateMode {} {
        set graph $Subwidgets(graph)
        set mode [my configure -coordmark]
        if {$mode ne {auto}} {
            return $mode
        }
        if {$GraphType ne {polar}} {
            return axis
        }
        if {[$graph cget -representation] eq {smith}} {
            return [my DefaultSmithCoordinateMode]
        }
        return axis
    }
    ##### closest crosshairs methods
    method ClosestCoordinateLabel {mode} {
        classvariable CoordClosestMarkLabels
        return [dict get $CoordClosestMarkLabels $mode]
    }
    method ClosestCoordinateModeFromLabel {label} {
        classvariable CoordClosestMarkLabels
        dict for {mode text} $CoordClosestMarkLabels {
            if {$text eq $label} {
                return $mode
            }
        }
        return -code error "unknown closest coordinate label '$label'"
    }
    method ClosestCoordinateLabels {} {
        set labels [list]
        foreach mode [my ClosestCoordinateModes] {
            lappend labels [my ClosestCoordinateLabel $mode]
        }
        return $labels
    }
    method SelectClosestCoordinateMode {} {
        set mode [my ClosestCoordinateModeFromLabel $ClosestCoordSelector]
        my configure -coordclosestmark $mode
    }
    method ClosestCoordinateModes {} {
        set graph $Subwidgets(graph)
        if {$GraphType ne {polar}} {
            return {axis}
        }
        switch -- [$graph cget -representation] {
            polar {
                return {axis complex polar}
            }
            smith {
                return {axis gamma normalizedimpedance impedance normalizedadmittance admittance}
            }
        }
        return {axis}
    }
    method DefaultClosestCoordinateMode {} {
        set graph $Subwidgets(graph)
        if {$GraphType ne {polar}} {
            return axis
        }
        if {[$graph cget -representation] ne {smith}} {
            return axis
        }
        switch -- [$graph cget -smithgrid] {
            admittance {
                return normalizedadmittance
            }
            impedance -
            both {
                return normalizedimpedance
            }
        }
        return normalizedimpedance
    }
    method UpdateClosestCoordinateModes {} {
        if {![info exists Subwidgets(closestCoordComBox)]} {
            return
        }
        set modes [my ClosestCoordinateModes]
        if {$coordclosestmark ni $modes} {
            my configure -coordclosestmark [my DefaultClosestCoordinateMode]
        }
        $Subwidgets(closestCoordComBox) configure -values [my ClosestCoordinateLabels]
        set ClosestCoordSelector [my ClosestCoordinateLabel $coordclosestmark]
    }
    method ClosestAxisFormattedValue {axis value formatSpec} {
        set graph $Subwidgets(graph)
        #
        # Existing graphtoolbar representation is always the fallback.
        #
        set fallback [format "%$formatSpec" $value]
        set command [$graph axis cget $axis -command]
        if {$command eq {}} {
            return $fallback
        }
        #
        # Reproduce the default label that RBC passes to an axis
        # -command callback.
        #
        if {[$graph axis cget $axis -logscale]} {
            #
            # Rbc formats logarithmic tick coordinates as 1E<n>.
            # The value here is the actual graph value, so convert it
            # back to logarithmic coordinate space first.
            #
            if {![string is double -strict $value] ||
                !isfinite($value) ||
                ($value <= 0.0)} {
                return $fallback
            }
            set exponent [expr {round(log($value)/log(10.0))}]
            set defaultLabel [format "1E%d" $exponent]
        } else {
            #
            # Keep this consistent with Rbc's NUMDIGITS == 15
            # standard linear-axis label.
            #
            set defaultLabel [format %.15g $value]
        }
        #
        # -command is a command prefix.  Append the same two arguments
        # as the axis formatter:
        #
        #     widgetPath defaultLabel
        #
        # A formatter failure must never break crosshair motion.
        #
        if {[catch {uplevel #0 [list {*}$command $graph $defaultLabel]} result]} {
            return $fallback
        }
        #
        # An empty successful result is intentional: axis -command also
        # permits an empty label.
        #
        return $result
    }
    method ClosestAxisMarkerText {element xValue yValue options} {
        set graph $Subwidgets(graph)
        set mapx [$graph element cget $element -mapx]
        set mapy [$graph element cget $element -mapy]
        lassign [my CoordinateOrientations] xOrientation yOrientation
        #
        # PolarElement Cartesian coordinates have complex-plane semantics.
        #
        # In Polar representation:
        #     x/y = real/imaginary Cartesian components.
        #
        # In Smith representation:
        #     x/y = real/imaginary components of Gamma.
        #
        # Other element types retain their actual RBC axis names.
        #
        if {($GraphType eq {polar}) && ([$graph element type $element] eq {PolarElement})} {
            set xName real
            set yName imag
        } else {
            set xName $mapx
            set yName $mapy
        }
        set xText [my ClosestAxisFormattedValue $mapx $xValue [dict get $options -formatx]]
        set yText [my ClosestAxisFormattedValue $mapy $yValue [dict get $options -formaty]]
        return [format "%s\n%s(%s)=%s\n%s(%s)=%s" $element $xName $xOrientation $xText $yName $yOrientation $yText]
    }
    method ClosestMarkerText {element xValue yValue options closestInfo} {
        set mode [my configure -coordclosestmark]
        # Never silently reinterpret a requested marker mode.
        #
        # If the current graph representation does not support the
        # configured mode, no marker text is produced.
        if {$mode ni [my ClosestCoordinateModes]} {
            return
        }
        if {$mode eq {axis}} {
            set text [my ClosestAxisMarkerText $element $xValue $yValue $options]
        } else {
            set formatReal [dict get $options -formatx]
            set formatImag [dict get $options -formaty]
            switch -- $mode {
                complex {
                    # Complex Cartesian value of a Polar point.
                    set valueText [my FormatComplexMarkerValue z [list $xValue $yValue] $formatReal $formatImag]
                }
                polar {
                    if {![dict exists $closestInfo radius] || ![dict exists $closestInfo angle]} {
                        return
                    }
                    set valueText [my FormatPolarMarkerValue [dict get $closestInfo radius]\
                                           [dict get $closestInfo angle] $formatReal $formatImag]
                }
                gamma {
                    if {![dict exists $closestInfo gamma]} {
                        return
                    }
                    set valueText [my FormatComplexMarkerValue Gamma [dict get $closestInfo gamma] $formatReal\
                                           $formatImag]
                }
                normalizedimpedance {
                    if {![dict exists $closestInfo normalizedImpedance]} {
                        return
                    }
                    set valueText [my FormatComplexMarkerValue z [dict get $closestInfo normalizedImpedance]\
                                           $formatReal $formatImag]
                }
                impedance {
                    if {![dict exists $closestInfo impedance]} {
                        return
                    }
                    set valueText [my FormatComplexMarkerValue Z [dict get $closestInfo impedance] $formatReal\
                                           $formatImag Ohm]
                }
                normalizedadmittance {
                    if {![dict exists $closestInfo normalizedAdmittance]} {
                        return
                    }
                    set valueText [my FormatComplexMarkerValue y [dict get $closestInfo normalizedAdmittance]\
                                           $formatReal $formatImag]
                }
                admittance {
                    if {![dict exists $closestInfo admittance]} {
                        return
                    }
                    set valueText [my FormatComplexMarkerValue Y [dict get $closestInfo admittance] $formatReal\
                                           $formatImag S]
                }
                default {
                    return
                }
            }
            set text "$element\n$valueText"
        }
        # -param is optional metadata supplied by line and Polar elements.
        # RBC omits it from the closest result whenever no valid parameter
        # mapping exists, so its presence in closestInfo is sufficient.
        if {[dict exists $closestInfo param]} {
            set param [dict get $closestInfo param]
            set formatParam [dict get $options -formatparam]
            append text [format "\nparam=%$formatParam" $param]
        }
        return $text
    }
    method CreateClosestMarker {graph textMarker bitmapMarker element xValue yValue options closestInfo} {
        set mapx [$graph element cget $element -mapx]
        set mapy [$graph element cget $element -mapy]
        set xPixel [$graph axis transform $mapx $xValue]
        set yPixel [$graph axis transform $mapy $yValue]
        set text [my ClosestMarkerText $element $xValue $yValue $options $closestInfo]
        if {$text eq {}} {
            return
        }
        #
        # Physical widget position of the actual closest point.
        #
        lassign [my AxisPixelsToWidget $xPixel $yPixel] x y
        set anchor [my TextAnchor $x $y $text $options]
        dict set options -anchor $anchor
        lassign [my TextOffset $x $y $anchor] textX textY
        set boxOptions [my configure -crosshairsmarkboxopts]
        my DrawTextBackground ${textMarker}Box $textX $textY $text $options $boxOptions $mapx $mapy
        lassign [my WidgetToAxisValues $textX $textY $mapx $mapy] textXValue textYValue

        $graph marker create text -name $textMarker -text $text -coords [list $textXValue $textYValue] -mapx $mapx\
                -mapy $mapy {*}[dict remove $options -formatx -formaty -formatparam]
        #
        # Bitmap remains exactly at the real selected element point.
        #
        my AddBitmapPoint $bitmapMarker $xValue $yValue $mapx $mapy
    }
    method ClosestBarLayout {left top right bottom text textOptions} {
        set graph $Subwidgets(graph)
        lassign [$graph extents plotarea] plotLeft plotTop plotWidth plotHeight
        set plotRight [expr {$plotLeft+$plotWidth-1}]
        set plotBottom [expr {$plotTop+$plotHeight-1}]
        lassign [my TextSize $text $textOptions] textWidth textHeight
        # Distance from the bar to the dimension line, and from the
        # dimension line to the text box.
        set lineGap 4
        set textGap 4
        if {![$graph cget -invertxy]} {
            #
            # Normal graph: bars extend physically in Y.
            #
            # Prefer:
            #
            #      text  <->   bar
            #             |
            #             |
            #
            # If there is insufficient room on the left, put the
            # annotation on the right.
            #
            set centerY [expr {($top+$bottom)/2.0}]
            # Keep vertically centred text inside the plot area.
            set halfHeight [expr {($textHeight-1)/2.0}]
            if {($plotBottom-$plotTop+1)>=$textHeight} {
                set textY [expr {max($plotTop+$halfHeight, min($centerY, $plotBottom-$halfHeight))}]
            } else {
                set textY $centerY
            }
            set leftLineX [expr {$left-$lineGap}]
            set leftTextX [expr {$leftLineX-$textGap}]
            set rightLineX [expr {$right+$lineGap}]
            set rightTextX [expr {$rightLineX+$textGap}]
            set leftFits [expr {($leftLineX>=$plotLeft) && (($leftTextX-$textWidth+1) >= $plotLeft)}]
            set rightFits [expr {($rightLineX<=$plotRight) && (($rightTextX+$textWidth-1) <= $plotRight)}]
            if {$leftFits} {
                set lineX $leftLineX
                set textX $leftTextX
                set anchor e
            } elseif {$rightFits} {
                set lineX $rightLineX
                set textX $rightTextX
                set anchor w
            } elseif {($left-$plotLeft) >= ($plotRight-$right)} {
                # Neither complete annotation fits. Prefer the side
                # with more available space.
                set lineX [expr {max($plotLeft,$leftLineX)}]
                set textX [expr {$lineX-$textGap}]
                set anchor e
            } else {
                set lineX [expr {min($plotRight,$rightLineX)}]
                set textX [expr {$lineX+$textGap}]
                set anchor w
            }
            return [list $lineX $top $lineX $bottom $textX $textY $anchor]
        }
        #
        # Inverted graph: bars extend physically in X.
        #
        # Prefer the annotation above the bar; if there is insufficient
        # room there, put it below.
        #
        set centerX [expr {($left+$right)/2.0}]
        set halfWidth [expr {($textWidth-1)/2.0}]
        if {($plotRight-$plotLeft+1) >= $textWidth} {
            set textX [expr {max($plotLeft+$halfWidth,min($centerX,$plotRight-$halfWidth))}]
        } else {
            set textX $centerX
        }
        set topLineY [expr {$top-$lineGap}]
        set topTextY [expr {$topLineY-$textGap}]
        set bottomLineY [expr {$bottom+$lineGap}]
        set bottomTextY [expr {$bottomLineY+$textGap}]
        set topFits [expr {($topLineY>=$plotTop) && (($topTextY-$textHeight+1) >= $plotTop)}]
        set bottomFits [expr {($bottomLineY<=$plotBottom) && (($bottomTextY+$textHeight-1) <= $plotBottom)}]
        if {$topFits} {
            set lineY $topLineY
            set textY $topTextY
            set anchor s
        } elseif {$bottomFits} {
            set lineY $bottomLineY
            set textY $bottomTextY
            set anchor n
        } elseif {($top-$plotTop) >= ($plotBottom-$bottom)} {
            set lineY [expr {max($plotTop,$topLineY)}]
            set textY [expr {$lineY-$textGap}]
            set anchor s
        } else {
            set lineY [expr {min($plotBottom,$bottomLineY)}]
            set textY [expr {$lineY+$textGap}]
            set anchor n
        }
        return [list $left  $lineY $right $lineY $textX $textY $anchor]
    }
    method ClosestBarArrowDepth {lineOptions} {
        set graph $Subwidgets(graph)
        set shape [dict get $lineOptions -arrowshape]
        if {[llength $shape] != 3} {
            return -code error "bad bar arrow shape '$shape': must contain three screen distances"
        }
        lassign $shape a b c
        #
        # Match the screen-distance interpretation used by the RBC line
        # marker.  c controls the transverse width of the arrowhead;
        # a and b determine how far it extends along the dimension line.
        #
        set a [winfo pixels $graph $a]
        set b [winfo pixels $graph $b]
        return [expr {double(max($a,$b))}]
    }

    method SetClosestBarLineMarker {marker coords mapx mapy lineOptions arrow} {
        set graph $Subwidgets(graph)
        if {[$graph marker exists $marker]} {
            $graph marker configure $marker -coords $coords -mapx $mapx -mapy $mapy {*}$lineOptions -arrow $arrow
        } else {
            $graph marker create line -name $marker -coords $coords -mapx $mapx -mapy $mapy -bindtags {} -under no\
                    {*}$lineOptions -arrow $arrow
        }
    }
    method CreateClosestBarMarker {marker element xValue yValue left top right bottom options boxOptions closestInfo} {
        set graph $Subwidgets(graph)
        set mapx [$graph element cget $element -mapx]
        set mapy [$graph element cget $element -mapy]
        set text [my ClosestMarkerText $element $xValue $yValue $options $closestInfo]
        if {$text eq {}} {
            return
        }
        # -formatx/-formaty are graphtoolbar formatting options, not RBC text-marker options.
        set textOptions [dict remove $options -formatx -formaty -formatparam]
        lassign [my ClosestBarLayout $left $top $right $bottom $text $textOptions] lineX1 lineY1 lineX2 lineY2 textX\
                textY anchor
        dict set textOptions -anchor $anchor
        # convert the physical dimension-line endpoints back into the element's mapped axis coordinates.
        lassign [my WidgetToAxisValues $lineX1 $lineY1 $mapx $mapy] lineXValue1 lineYValue1
        lassign [my WidgetToAxisValues $lineX2 $lineY2 $mapx $mapy] lineXValue2 lineYValue2
        set lineCoords [list $lineXValue1 $lineYValue1 $lineXValue2 $lineYValue2]
        #
        # cursorText      -> cursorBarLine
        # cursorText0     -> cursorBarLine0
        #
        set lineMarker [string map {crosshairsClosestText crosshairsClosestBarLine} $marker]
        set lineOptions [my configure -crosshairsbarlineopts]
        #
        # The normal dimension marker uses arrowheads at both ends of
        # the bar.  Each arrowhead extends inward from its endpoint.
        #
        # For a short displayed bar those two heads would overlap.
        # In that case leave the central dimension line unarrowed and
        # draw two additional arrow markers outside the measured span,
        # with their tips at the bar boundaries and pointing inward.
        #
        set dx [expr {$lineX2-$lineX1}]
        set dy [expr {$lineY2-$lineY1}]
        set lineLength [expr {hypot($dx,$dy)}]
        set arrowDepth [my ClosestBarArrowDepth $lineOptions]
        #
        # Leave a small visible gap between opposing internal heads.
        #
        set arrowGap 2.0
        set minimumInsideLength [expr {2.0*$arrowDepth+$arrowGap}]
        set firstArrowMarker ${lineMarker}FirstArrow
        set lastArrowMarker  ${lineMarker}LastArrow
        if {($arrowDepth <= 0.0) ||
            ($lineLength >= $minimumInsideLength)} {
            # Enough room: retain the existing representation.
            my SetClosestBarLineMarker $lineMarker $lineCoords $mapx $mapy $lineOptions both
            # Remove auxiliary markers if the bar was previously short.
            set deleteMarkers [list]
            foreach arrowMarker [list $firstArrowMarker $lastArrowMarker] {
                if {[$graph marker exists $arrowMarker]} {
                    lappend deleteMarkers $arrowMarker
                }
            }
            if {[llength $deleteMarkers]} {
                $graph marker delete {*}$deleteMarkers
            }
        } else {
            # Not enough room for both arrowheads inside the bar span.
            # Keep the measured dimension line itself.
            my SetClosestBarLineMarker $lineMarker $lineCoords $mapx $mapy $lineOptions none
            # Unit vector from the first dimension endpoint to the second one.
            if {$lineLength > 0.0} {
                set ux [expr {$dx/$lineLength}]
                set uy [expr {$dy/$lineLength}]
            } elseif {[$graph cget -invertxy]} {
                # Degenerate horizontal bar.
                set ux 1.0
                set uy 0.0
            } else {
                # Degenerate vertical bar.
                set ux 0.0
                set uy 1.0
            }
            # Put one complete arrowhead outside each endpoint.
            set outsideLength $arrowDepth
            set outerX1 [expr {$lineX1-$ux*$outsideLength}]
            set outerY1 [expr {$lineY1-$uy*$outsideLength}]
            set outerX2 [expr {$lineX2+$ux*$outsideLength}]
            set outerY2 [expr {$lineY2+$uy*$outsideLength}]
            lassign [my WidgetToAxisValues $outerX1 $outerY1 $mapx $mapy] outerXValue1 outerYValue1
            lassign [my WidgetToAxisValues $outerX2 $outerY2 $mapx $mapy] outerXValue2 outerYValue2
            #
            # Both auxiliary markers put the arrow at their FIRST
            # coordinate.
            #
            # First endpoint:
            #
            #     outer ----> tip | measured span
            #
            # Last endpoint:
            #
            #     measured span | tip <---- outer
            #
            # Therefore both heads point inward toward the bar.
            #
            my SetClosestBarLineMarker $firstArrowMarker [list $lineXValue1 $lineYValue1 $outerXValue1 $outerYValue1]\
                    $mapx $mapy $lineOptions first

            my SetClosestBarLineMarker $lastArrowMarker [list $lineXValue2 $lineYValue2 $outerXValue2 $outerYValue2] \
                    $mapx $mapy $lineOptions first
        }
        #
        # a bar annotation does not use the ordinary closest-point
        # bitmap.
        #
        set bitmapMarker [string map {crosshairsClosestText crosshairsClosestBitmap} $marker]
        if {[$graph marker exists $bitmapMarker]} {
            $graph marker delete $bitmapMarker
        }
        #
        # convert the selected physical text position back through the
        # element's mapped axes.
        #
        lassign [my WidgetToAxisValues $textX $textY $mapx $mapy] textXValue textYValue
        #
        # draw/update the separate text background before the text
        # itself.
        #
        my DrawTextBackground ${marker}Box $textX $textY $text $textOptions $boxOptions $mapx $mapy
        if {[$graph marker exists $marker]} {
            $graph marker configure $marker -coords [list $textXValue $textYValue] -text $text -mapx $mapx -mapy $mapy\
                    {*}$textOptions
        } else {
            $graph marker create text -name $marker -coords [list $textXValue $textYValue] -text $text -mapx $mapx\
                    -mapy $mapy -bindtags {} -under no {*}$textOptions
        }
    }
    #### zoom methods
    method InitZoomStack {} {
        # Initializes zoom stack as an array variable.
        #
        # A,x/A,y - physical widget pixels of the first zoom-box point.
        # A,xPixel/A,yPixel - pixels along the X/Y data dimensions of the first point.
        # B,x/B,y - physical widget pixels of the second zoom-box point.
        # B,xPixel/B,yPixel - pixels along the X/Y data dimensions of the second point.
        # stack - saved scale commands.
        # corner - current zoom-box point, A or B.
        array set ZoomInfo {
            A,x {}
            A,y {}
            A,xPixel {}
            A,yPixel {}
            B,x {}
            B,y {}
            B,xPixel {}
            B,yPixel {}
            stack {}
            corner A
        }
    }
    method EnableZoom {start end reset} {
        # Enables drag-selection zoom for graph subwidget.
        #
        # start - event that starts the zoom box selection
        # end   - event that confirms the zoom box selection
        # reset - event that restores the previous scale, or aborts
        #         the current selection
        set graph $Subwidgets(graph)
        my InitZoomStack
        set modifier $ZoomMod

        bind zoom-$graph <${modifier}${start}> [namespace code {my StartZoom %x %y}]
        bind zoom-$graph <${end}> [namespace code {my FinishZoom %x %y}]
        bind zoom-$graph <${modifier}${reset}> [namespace code {
            if {[%W inside %x %y]} {
                my ResetZoom %x %y
            }
        }]
        my AddBindTag $graph zoom-$graph
    }
    method EnableWheelZoom {modifier scale} {
        set graph $Subwidgets(graph)
        if {![string is double -strict $scale] || !isfinite($scale) || ($scale <= 1.0)} {
            return -code error "wheel zoom scale must be a finite number greater than 1.0"
        }
        bind zoom-$graph <${modifier}MouseWheel> [namespace code [list my WheelZoom %W %D %x %y %s $scale]]
    }
    method EnablePan {start end modifier} {
        # Enables drag panning of the visible plot area.
        #
        # The modifier is required only to start the operation.  Once a
        # pan has begun, Motion and ButtonRelease continue the same pan
        # even if the user releases the modifier first.
        set graph $Subwidgets(graph)
        bind pan-$graph <${modifier}${start}> [namespace code {
            if {[my StartPan %x %y]} {
                break
            }
        }]
        # Deliberately do not include $modifier here.  Otherwise releasing
        # Shift before Button-1 could leave the pan operation active.
        bind pan-$graph <$end> [namespace code {
            if {[my FinishPan %x %y]} {
                break
            }
        }]
        my AddBindTag $graph pan-$graph
    }
    method SaveZoomState {} {
        # Saves the current explicit axis limits and scale type as one
        # reversible zoom operation.
        #
        # cget is intentionally used instead of "axis limits":
        # an auto-scaled {} limit must be restored as {}, not as the
        # numeric limit calculated from the current data.
        set graph $Subwidgets(graph)
        set cmds [list]
        foreach dimension {x y} {
            foreach axis [my UsedAxes $dimension] {
                set min [$graph axis cget $axis -min]
                set max [$graph axis cget $axis -max]
                set logscale [$graph axis cget $axis -logscale]
                lappend cmds [list $graph axis configure $axis -min $min -max $max -logscale $logscale]
            }
        }
        set ZoomInfo(stack) [linsert $ZoomInfo(stack) 0 $cmds]
    }
    method SaveZoomPoint {x y index} {
        # x/y are physical widget pixels.
        set ZoomInfo($index,x) $x
        set ZoomInfo($index,y) $y
        lassign [my WidgetToAxisPixels $x $y] xPixel yPixel
        set ZoomInfo($index,xPixel) $xPixel
        set ZoomInfo($index,yPixel) $yPixel
    }
    method MarkZoomPoint {index} {
        set graph $Subwidgets(graph)
        set options [my configure -zoommarkopts]
        set boxOptions [my configure -zoommarkboxopts]
        set info [my CoordinateMarkerInfo $ZoomInfo($index,xPixel) $ZoomInfo($index,yPixel)\
                          [dict get $options -formatx] [dict get $options -formaty] [my configure -coordmark]]
        set marker gtbZoomText_$index
        set text [dict get $info text]
        set x $ZoomInfo($index,x)
        set y $ZoomInfo($index,y)
        set anchor [my TextAnchor $x $y $text $options]
        dict set options -anchor $anchor
        set mapx [dict get $info mapx]
        set mapy [dict get $info mapy]
        lassign [my TextOffset $x $y $anchor] textX textY
        my DrawTextBackground ${marker}Box $textX $textY $text $options $boxOptions $mapx $mapy
        lassign [my WidgetToAxisValues $textX $textY $mapx $mapy] textXValue textYValue
        if {[$graph marker exists $marker]} {
            $graph marker configure $marker -coords [list $textXValue $textYValue] -mapx $mapx -mapy $mapy -text $text\
                    -anchor $anchor
        } else {
            $graph marker create text -name $marker -coords [list $textXValue $textYValue] -mapx $mapx -mapy $mapy\
                    -text $text {*}[dict remove $options -formatx -formaty -formatparam]
            
        }
    }
    method DestroyZoomTitle {} {
        unset -nocomplain ZoomInfo(titleTimer)
        if {$ZoomInfo(corner) eq {A}} {
            catch {$Subwidgets(graph) marker delete gtbZoomTitle} errorStr
        }
    }
    method WheelZoom {graph delta x y state step} {
        # Do not alter the graph underneath an unfinished rectangle selection.
        if {[info exists ZoomInfo(corner)] && ($ZoomInfo(corner) ne {A})} {
            return -code break
        }
        if {[info exists PanInfo(active)] && $PanInfo(active)} {
            return -code break
        }
        if {$delta > 0} {
            # Wheel up: zoom in.
            set factor [expr {1.0/$step}]
        } elseif {$delta < 0} {
            # Wheel down: zoom out.
            set factor $step
        } else {
            return
        }
        set changes [dict create]
        if {[$graph inside $x $y]} {
            #
            # Plot area:
            #
            # Scale all X and Y axes around the value underneath the
            # physical mouse location.
            #
            lassign [my WidgetToAxisPixels $x $y] xPixel yPixel
            # axis invtransform requires integer pixels.
            set xPixel [expr {round($xPixel)}]
            set yPixel [expr {round($yPixel)}]
            foreach axis [my UsedAxes x] {
                set center [$graph axis invtransform $axis $xPixel]
                dict set changes $axis [my ScaledAxisLimits $axis $factor $center]
            }
            foreach axis [my UsedAxes y] {
                set center [$graph axis invtransform $axis $yPixel]
                dict set changes $axis [my ScaledAxisLimits $axis $factor $center]
            }
        } else {
            #
            # Axis area:
            #
            # Refresh RBC's "current" axis using the actual wheel-event
            # position.  This avoids relying on a possibly stale current
            # axis from the last Motion event.
            #
            event generate $graph <Motion> -x $x -y $y -state $state
            set axis [$graph axis get current]
            if {$axis eq {}} {
                # Outside both the plot area and an axis: don't consume
                # the wheel event.
                return
            }
            #
            # An individual axis is scaled around its own center.
            #
            if {($GraphType eq {polar}) && ($axis in [my PolarGridAxes])} {
                #
                # A Polar grid is an equal-scale two-dimensional coordinate
                # system.  Scaling only one of its grid axes would force RBC to
                # resize the physical plot area.
                #
                # Scale both grid axes by the same factor instead.  The selected
                # axis and its perpendicular partner each scale about their own
                # numerical center.
                #
                foreach gridAxis [my PolarGridAxes] {
                    dict set changes $gridAxis [my ScaledAxisLimits $gridAxis $factor]
                }
            } else {
                dict set changes $axis [my ScaledAxisLimits $axis $factor]
            }
        }
        if {[dict size $changes] == 0} {
            return
        }
        #
        # Every wheel step is a normal reversible zoom operation.
        #
        set refreshCrosshairs [my CanRefreshCrosshairsMarker $x $y]
        #
        # The crosshairs text boxes are pixel-sized objects represented
        # internally by graph coordinates.  Remove them before changing the
        # axis transform so they cannot be drawn using the new scale.
        #
        if {$refreshCrosshairs} {
            my DeleteCrosshairsMarkers
        }
        my SaveZoomState
        dict for {axis limits} $changes {
            lassign $limits min max
            $graph axis configure $axis -min $min -max $max
        }
        if {$refreshCrosshairs} {
            #
            # Make the new axis mapping current before converting widget
            # pixels back into graph coordinates.
            #
            update idletasks
            my RefreshCrosshairsMarker $x $y
        }
        #
        # Keep -zoomtitle meaningful for wheel zoom as well.
        #
        if {[my configure -zoomtitle]} {
            my FinishZoomTitle
        }
        return -code break
    }
    method PopZoom {{single yes} {x {}} {y {}}} {
        # Removes last zoom box scale commands for all visible axes from the stack and restores graph scale to the
        # previous state on the stack.
        set graph $Subwidgets(graph)
        set zoomStack $ZoomInfo(stack)
        if {[llength $zoomStack] > 0} {
            if {$single} {
                set cmds [lindex $zoomStack 0]
                set ZoomInfo(stack) [lrange $zoomStack 1 end]
                # A pop happens while the pointer is still in the
                # plot area.  Remove the old graph-coordinate representation of
                # the pixel-sized crosshair marker before changing the transform.
                set refreshCrosshairs [my CanRefreshCrosshairsMarker $x $y]
                if {$refreshCrosshairs} {
                    my DeleteCrosshairsMarkers
                }
                foreach cmd $cmds {
                    {*}$cmd
                }
                if {[my configure -zoomtitle]} {
                    my ZoomTitleLast
                }
                tk busy hold $graph
                event generate $graph <Configure>
                update idletasks
                tk busy forget $graph
                if {$refreshCrosshairs} {
                    my RefreshCrosshairsMarker $x $y
                }
                if {[my configure -zoomtitle]} {
                    my FinishZoomTitle
                }
            } else {
                set cmds [lindex $zoomStack end]
                set ZoomInfo(stack) {}
                # A pop happens while the pointer is still in the
                # plot area.  Remove the old graph-coordinate representation of
                # the pixel-sized crosshair marker before changing the transform.
                set refreshCrosshairs [my CanRefreshCrosshairsMarker $x $y]
                if {$refreshCrosshairs} {
                    my DeleteCrosshairsMarkers
                }
                foreach cmd $cmds {
                    {*}$cmd
                }
                if {[my configure -zoomtitle]} {
                    my ZoomTitleLast
                }
                tk busy hold $graph
                event generate $graph <Configure>
                update idletasks
                tk busy forget $graph
                if {$refreshCrosshairs} {
                    my RefreshCrosshairsMarker $x $y
                }
                if {[my configure -zoomtitle]} {
                    my FinishZoomTitle
                }
            }
        } else {
            if {[my configure -zoomtitle]} {
                catch {$graph marker delete gtbZoomTitle}
            }
        }
    }
    method PushZoom {} {
        # Push selected zoom box scale commands for all visible axes onto the zoom stack, and changes graph scale
        # accordingly.
        set graph $Subwidgets(graph)
        $graph marker delete {*}[$graph marker names gtbZoom*]
        set xPixel1 $ZoomInfo(A,xPixel)
        set yPixel1 $ZoomInfo(A,yPixel)
        set xPixel2 $ZoomInfo(B,xPixel)
        set yPixel2 $ZoomInfo(B,yPixel)
        if {($xPixel1==$xPixel2) || ($yPixel1==$yPixel2)} {
            return
        }
        my SaveZoomState
        foreach axis [my UsedAxes x] {
            set min [$graph axis invtransform $axis $xPixel1]
            set max [$graph axis invtransform $axis $xPixel2]
            if {$min>$max} {
                $graph axis configure $axis -min $max -max $min
            } else {
                $graph axis configure $axis -min $min -max $max
            }
        }
        foreach axis [my UsedAxes y] {
            set min [$graph axis invtransform $axis $yPixel1]
            set max [$graph axis invtransform $axis $yPixel2]
            if {$min>$max} {
                $graph axis configure $axis -min $max -max $min
            } else {
                $graph axis configure $axis -min $min -max $max
            }
        }
        tk busy hold $graph
        event generate $graph <Configure>
        update idletasks
        tk busy forget $graph
        my RefreshCrosshairsMarker $ZoomInfo(B,x) $ZoomInfo(B,y)
    }
    method ScaledAxisLimits {axis factor {center {}}} {
        # Returns new numeric limits for an axis.
        #
        # factor < 1.0 -> zoom in
        # factor > 1.0 -> zoom out
        #
        # center {} uses the center of the current axis range.
        #              center
        # min ───────────┼─────────── max
        #                ↑
        #        zoom around axis center
        #
        #                mouse
        #                  ↓
        # min ─────────────●──────────── max
        #                  ● remains at the same screen position
        #
        # Linear axes are scaled arithmetically.
        # Logarithmic axes are scaled in logarithmic coordinate space.
        set graph $Subwidgets(graph)
        if {![string is double -strict $factor] || !isfinite($factor) || ($factor <= 0.0)} {
            return -code error "axis scaling factor must be a finite number greater than zero"
        }
        lassign [$graph axis limits $axis] min max
        if {!isfinite($min) || !isfinite($max) || !($min < $max)} {
            return -code error "axis '$axis' has invalid limits"
        }
        if {[$graph axis cget $axis -logscale]} {
            if {($min <= 0.0) || ($max <= 0.0)} {
                return -code error "logarithmic axis limits must be greater than zero"
            }
            set logMin [expr {log($min)}]
            set logMax [expr {log($max)}]
            if {$center eq {}} {
                set logCenter [expr {($logMin+$logMax)/2.0}]
            } else {
                if {![string is double -strict $center] || !isfinite($center) || ($center <= 0.0)} {
                    return -code error "logarithmic axis center must be a finite number greater than zero"
                }
                set logCenter [expr {log($center)}]
            }
            set newLogMin [expr {$logCenter+($logMin-$logCenter)*$factor}]
            set newLogMax [expr {$logCenter+($logMax-$logCenter)*$factor}]
            if {!isfinite($newLogMin) || !isfinite($newLogMax) || !($newLogMin < $newLogMax)} {
                return -code error "logarithmic axis scaling produced invalid limits"
            }
            #
            # exp() can overflow even though its input is itself finite.
            #
            if {[catch {expr {exp($newLogMin)}} newMin] || [catch {expr {exp($newLogMax)}} newMax]} {
                return -code error "logarithmic axis scaling exceeds the numeric range"
            }
            #
            # Very large zoom operations can also underflow the lower
            # limit to zero.
            #
            if {!isfinite($newMin) || !isfinite($newMax) || ($newMin <= 0.0) || !($newMin < $newMax)} {
                return -code error "logarithmic axis scaling produced invalid limits"
            }
            return [list $newMin $newMax]
        }
        if {$center eq {}} {
            set center [expr {($min+$max)/2.0}]
        } else {
            if {![string is double -strict $center] || !isfinite($center)} {
                return -code error "axis scaling center must be finite"
            }
        }
        set newMin [expr {$center+($min-$center)*$factor}]
        set newMax [expr {$center+($max-$center)*$factor}]
        if {!isfinite($newMin) || !isfinite($newMax) || !($newMin < $newMax)} {
            return -code error "axis scaling produced invalid limits"
        }
        return [list $newMin $newMax]
    }
    method ResetZoom {{x {}} {y {}}} {
        # Restores graph scale to the previous state, or abort the current selection operation.
        set graph $Subwidgets(graph)
        if {[info exists PanInfo(active)] && $PanInfo(active)} {
            my CancelPan $x $y
            return
        }
        if {![info exists ZoomInfo(corner)]} {
            my InitZoomStack
        }
        # restore active axes behaviour
        if {[info exists ZoomTransientChecks(activeAxes)]} {
            my setAxisActiveScale $ZoomTransientChecks(activeAxes)
            unset ZoomTransientChecks(activeAxes)
        }
        if {[info exists ZoomTransientChecks(crosshairsMarker)]} {
            if {$ZoomTransientChecks(crosshairsMarker)} {
                # restore crosshair marker behaviour
                my AddBindTag $graph crosshairs-marker-$graph crosshairs-$graph
            }
            unset ZoomTransientChecks(crosshairsMarker)
        }
        my ChangeToolbarState restore
        $graph marker delete {*}[$graph marker names gtbZoom*]
        if {$ZoomInfo(corner) eq {A}} {
            # Reset the whole axis
            my PopZoom yes $x $y
        } else {
            set modifier $ZoomMod
            set ZoomInfo(corner) A
            my RemoveBindTag $graph select-region-$graph
        }
    }
    method FinishZoomTitle {} {
        if {![my configure -zoomtitle]} {
            return
        }
        my ZoomTitleLast
        if {[info exists ZoomInfo(titleTimer)]} {
            after cancel $ZoomInfo(titleTimer)
        }
        set ZoomInfo(titleTimer) [after 2000 [namespace code {my DestroyZoomTitle}]]
    }
    method ZoomTitleNext {} {
        set graph $Subwidgets(graph)
        set level [expr {[llength $ZoomInfo(stack)]+1}]
        if {[$graph cget -invertxy]} {
            set coords {-Inf -Inf}
        } else {
            set coords {-Inf Inf}
        }
        $graph marker create text -name gtbZoomTitle -text "Zoom #$level" -coords $coords -bindtags {}\
                {*}[my configure -zoomtitleopts]
    }
    method ZoomTitleLast {} {
        set level [llength $ZoomInfo(stack)]
        if {$level > 0} {
            $Subwidgets(graph) marker create text -name gtbZoomTitle -text "Zoom #$level"\
                    {*}[my configure -zoomtitleopts]
        }
    }
    method ClampToPlot {x y} {
        # Clamps provided pointer coordinates to the visible plot area.
        #  x - horizontal coordinate of the pointer
        #  y - vertical coordinate of the pointer
        lassign [$Subwidgets(graph) extents plotarea] left top width height
        set right  [expr {$left+$width-1}]
        set bottom [expr {$top+$height-1}]
        set x [expr {max($left,min($right,$x))}]
        set y [expr {max($top,min($bottom,$y))}]
        return [list $x $y]
    }
    method ZoomPointInLegend {x y} {
        set graph $Subwidgets(graph)
        #
        # Only an internally drawn legend in the plotting area can
        # conflict with region selection.
        #
        if {[$graph legend cget -hide]} {
            return false
        }
        if {[$graph legend cget -position] ne {plotarea}} {
            return false
        }
        #
        # RBC reports the final laid-out legend rectangle in physical
        # graph-window coordinates.
        #
        lassign [$graph extents legend] left top width height
        #
        # An empty legend occupies no forbidden region.
        #
        if {($width <= 0) || ($height <= 0)} {
            return false
        }
        set right  [expr {$left+$width-1}]
        set bottom [expr {$top+$height-1}]
        return [expr {($x >= $left) && ($x <= $right) && ($y >= $top) && ($y <= $bottom)}]
    }
    method ConstrainPolarZoomPoint {x y} {
        # Polar/Smith automatic aspect keeps one X data unit and one Y
        # data unit physically equal.
        #
        # An arbitrary rectangular zoom box changes the X/Y range ratio,
        # which makes the Polar graph resize its physical plot area.
        #
        # Constrain the second zoom corner so that the zoom box has the
        # same physical aspect ratio as the current plot area.  The new
        # numerical X/Y ranges therefore preserve the existing equal-unit
        # geometry without changing the plot-area size.
        if {$GraphType ne {polar}} {
            return [list $x $y]
        }
        set graph $Subwidgets(graph)
        lassign [$graph extents plotarea] left top width height
        if {($width <= 1) || ($height <= 1)} {
            return [list $x $y]
        }
        set right  [expr {$left+$width-1}]
        set bottom [expr {$top+$height-1}]
        set ax $ZoomInfo(A,x)
        set ay $ZoomInfo(A,y)
        set dx [expr {$x-$ax}]
        set dy [expr {$y-$ay}]
        if {($dx == 0) || ($dy == 0)} {
            return [list $x $y]
        }
        set signX [expr {$dx < 0 ? -1 : 1}]
        set signY [expr {$dy < 0 ? -1 : 1}]
        set absDx [expr {abs($dx)}]
        set absDy [expr {abs($dy)}]
        set aspect [expr {double($width-1)/double($height-1)}]
        set boxAspect [expr {$absDx/$absDy}]
        if {$boxAspect > $aspect} {
            #
            # The dragged box is too wide.  Prefer extending its height
            # while retaining the user's horizontal selection.
            #
            set wantedDy [expr {$absDx/$aspect}]
            if {$signY > 0} {
                set availableDy [expr {$bottom-$ay}]
            } else {
                set availableDy [expr {$ay-$top}]
            }
            if {$wantedDy <= $availableDy} {
                set absDy $wantedDy
            } else {
                #
                # The required height does not fit.  Use all available
                # height and reduce the width instead.
                #
                set absDy $availableDy
                set absDx [expr {$absDy*$aspect}]
            }
        } else {
            #
            # The dragged box is too tall.  Prefer extending its width
            # while retaining the user's vertical selection.
            #
            set wantedDx [expr {$absDy*$aspect}]
            if {$signX > 0} {
                set availableDx [expr {$right-$ax}]
            } else {
                set availableDx [expr {$ax-$left}]
            }
            if {$wantedDx <= $availableDx} {
                set absDx $wantedDx
            } else {
                set absDx $availableDx
                set absDy [expr {$absDx/$aspect}]
            }
        }
        set x [expr {round($ax+$signX*$absDx)}]
        set y [expr {round($ay+$signY*$absDy)}]
        return [list $x $y]
    }
    method StartZoom {x y} {
        # Starts a drag-selection zoom.
        #
        # x/y are physical widget coordinates of corner A.
        set graph $Subwidgets(graph)
        if {[info exists PanInfo(active)] && $PanInfo(active)} {
            return
        }
        if {![info exists ZoomInfo(corner)]} {
            my InitZoomStack
        }
        # Ignore another start event while a selection is already active.
        if {$ZoomInfo(corner) ne {A}} {
            return
        }
        # The first point must actually be inside the plotting area.
        if {![$graph inside $x $y]} {
            return
        }
        # Do not start zoom on an internally drawn legend.  The press is
        # left available to the normal legend interaction machinery.
        if {[my ZoomPointInLegend $x $y]} {
            return
        }
        my SaveZoomPoint $x $y A
        # During the drag, ordinary Motion events update corner B.  The
        # select-region tag exists only until ButtonRelease.
        set modifier $ZoomMod
        bind select-region-$graph <${modifier}Motion> [namespace code {my DragZoom %x %y}]
        # Disable active-axis interaction while selecting.
        set ZoomTransientChecks(activeAxes) [my getAxisActiveScale]
        my setAxisActiveScale $ZoomTransientChecks(activeAxes) -disabled
        # Disable closest/current crosshair marker interaction while
        # selecting.
        set ZoomTransientChecks(crosshairsMarker) false
        if {[my CheckBindTagExistence $graph crosshairs-marker-$graph]} {
            set ZoomTransientChecks(crosshairsMarker) true
            my RemoveBindTag $graph crosshairs-marker-$graph
        }
        if {$ZoomMark} {
            my MarkZoomPoint A
        }
        if {[my configure -zoomtitle]} {
            my ZoomTitleNext
        }
        my AddBindTag $graph select-region-$graph
        my ChangeToolbarState disable
        # corner B also acts as the "drag selection active" flag.
        set ZoomInfo(corner) B
    }
    method DragZoom {x y} {
        # Updates corner B while the selection button is held.
        set graph $Subwidgets(graph)
        if {![info exists ZoomInfo(corner)] ||
            ($ZoomInfo(corner) ne {B})} {
            return
        }
        # The pointer may leave the plotting area while dragging.
        lassign [my ClampToPlot $x $y] x y
        if {$GraphType eq {polar}} {
            lassign [my ConstrainPolarZoomPoint $x $y] x y
        }
        my SaveZoomPoint $x $y B
        if {$ZoomMark} {
            my MarkZoomPoint B
        }
        my Box
    }
    method FinishZoom {x y} {
        # Finishes the active drag-selection and applies the selected range.
        set graph $Subwidgets(graph)
        if {![info exists ZoomInfo(corner)] ||
            ($ZoomInfo(corner) ne {B})} {
            return
        }
        # Release may occur outside the plot area.
        lassign [my ClampToPlot $x $y] x y
        if {$GraphType eq {polar}} {
            lassign [my ConstrainPolarZoomPoint $x $y] x y
        }
        # Always use the actual release position as the final B point.
        # This also makes press/release without an intervening Motion event
        # well-defined.
        my SaveZoomPoint $x $y B
        # Stop region tracking before changing the graph transform.
        my RemoveBindTag $graph select-region-$graph
        # Restore active-axis behaviour.
        if {[info exists ZoomTransientChecks(activeAxes)]} {
            my setAxisActiveScale $ZoomTransientChecks(activeAxes)
            unset ZoomTransientChecks(activeAxes)
        }
        # Restore crosshair-marker behaviour.
        if {[info exists ZoomTransientChecks(crosshairsMarker)]} {
            if {$ZoomTransientChecks(crosshairsMarker)} {
                my AddBindTag $graph crosshairs-marker-$graph crosshairs-$graph
            }
            unset ZoomTransientChecks(crosshairsMarker)
        }
        my PushZoom
        my ChangeToolbarState restore
        set ZoomInfo(corner) A
    }
    method Box {} {
        # Creates zoom-box outline from the saved A/B points.
        #
        # The marker only needs one valid X/Y mapping pair.  Normally use
        # visible axes; a Polar/Smith widget with hidden Cartesian axes
        # falls back to the grid-mapped axes.
        set graph $Subwidgets(graph)
        set xAxes [my InteractionAxes x]
        set yAxes [my InteractionAxes y]
        if {![llength $xAxes] || ![llength $yAxes]} {
            return
        }
        set mapx [lindex $xAxes 0]
        set mapy [lindex $yAxes 0]
        set xValue1 [$graph axis invtransform $mapx $ZoomInfo(A,xPixel)]
        set yValue1 [$graph axis invtransform $mapy $ZoomInfo(A,yPixel)]
        set xValue2 [$graph axis invtransform $mapx $ZoomInfo(B,xPixel)]
        set yValue2 [$graph axis invtransform $mapy $ZoomInfo(B,yPixel)]
        set coords [list $xValue1 $yValue1 $xValue2 $yValue1 $xValue2 $yValue2 $xValue1 $yValue2 $xValue1 $yValue1]
        my AddBitmapPoint gtbZoomBitmap $xValue1 $yValue1 $mapx $mapy
        if {[$graph marker exists gtbZoomOutline]} {
            $graph marker configure gtbZoomOutline -coords $coords -mapx $mapx -mapy $mapy
        } else {
            $graph marker create line -name gtbZoomOutline -coords $coords -mapx $mapx -mapy $mapy \
                    {*}[my configure -zoomboxopts]
        }
    }
    method StartPan {x y} {
        set graph $Subwidgets(graph)
        if {[info exists PanInfo(active)] && $PanInfo(active)} {
            return true
        }
        # Do not start panning while a rectangle zoom is already active.
        if {[info exists ZoomInfo(corner)] &&
            ($ZoomInfo(corner) ne {A})} {
            return false
        }
        # Panning must start inside the plotting area.
        if {![$graph inside $x $y]} {
            return false
        }
        # Leave an internally drawn plot-area legend available to its normal interaction bindings.
        if {[my ZoomPointInLegend $x $y]} {
            return false
        }
        lassign [my WidgetToAxisPixels $x $y] xPixel yPixel
        set xPixel [expr {round($xPixel)}]
        set yPixel [expr {round($yPixel)}]
        # Capture the ORIGINAL mapping for every axis.  DragPan always
        # calculates from this state rather than incrementally from the
        # previous Motion event.
        #
        # This is important because graph redraw/remapping is idle-driven:
        # several Motion events may arrive before the display catches up.
        set axes [dict create]
        foreach dimension {x y} {
            foreach axis [my UsedAxes $dimension] {
                lassign [$graph axis limits $axis] min max
                set minPixel [$graph axis transform $axis $min]
                set maxPixel [$graph axis transform $axis $max]
                set pixelSpan [expr {$maxPixel-$minPixel}]
                if {$pixelSpan == 0} {
                    continue
                }
                set logscale [$graph axis cget $axis -logscale]
                if {$logscale} {
                    if {($min <= 0.0) || ($max <= 0.0)} {
                        continue
                    }
                    set unitsPerPixel [expr {(log($max)-log($min))/double($pixelSpan)}]
                } else {
                    set unitsPerPixel [expr {($max-$min)/double($pixelSpan)}]
                }
                if {!isfinite($unitsPerPixel)} {
                    continue
                }
                dict set axes $axis [list $dimension $min $max $logscale $unitsPerPixel]
            }
        }
        if {![dict size $axes]} {
            return false
        }
        array unset PanInfo
        array set PanInfo [list active true historySaved false changed false start,xPixel $xPixel start,yPixel $yPixel\
                                   dx 0 dy 0 axes $axes]
        # Disable the same transient interactions which rectangle zoom disables.
        array unset PanTransientChecks
        set PanTransientChecks(activeAxes) [my getAxisActiveScale]
        my setAxisActiveScale $PanTransientChecks(activeAxes) -disabled
        # Disable all crosshair interaction while panning.
        #
        # Save both bind tags independently because the current crosshair mode
        # determines which of them is installed.
        set PanTransientChecks(crosshairs) [my CheckBindTagExistence $graph crosshairs-$graph]
        set PanTransientChecks(crosshairsMarker) [my CheckBindTagExistence $graph crosshairs-marker-$graph]
        # Preserve whether the RBC crosshairs themselves were displayed.
        set PanTransientChecks(crosshairsHidden) [$graph crosshairs cget -hide]
        my RemoveBindTag $graph crosshairs-marker-$graph
        my RemoveBindTag $graph crosshairs-$graph
        # Remove marker text/background before the axes begin moving.
        if {$PanTransientChecks(crosshairsMarker)} {
            my DeleteCrosshairsMarkers
        }
        # Erase the crosshair lines themselves.
        $graph crosshairs off
        bind pan-region-$graph <Motion> [namespace code {
            my DragPan %x %y
            break
        }]
        my AddBindTag $graph pan-region-$graph
        my ChangeToolbarState disable
        return true
    }
    method DragPan {x y} {
        if {![info exists PanInfo(active)] ||
            !$PanInfo(active)} {
            return
        }
        set graph $Subwidgets(graph)
        lassign [my WidgetToAxisPixels $x $y] xPixel yPixel
        set xPixel [expr {round($xPixel)}]
        set yPixel [expr {round($yPixel)}]
        set dx [expr {$xPixel-$PanInfo(start,xPixel)}]
        set dy [expr {$yPixel-$PanInfo(start,yPixel)}]
        if {($dx == $PanInfo(dx)) && ($dy == $PanInfo(dy))} {
            return
        }
        # Prepare every new limit pair first.  Do not leave some axes
        # modified if another axis overflows.
        set cmds [list]
        set changed false
        dict for {axis state} $PanInfo(axes) {
            lassign $state dimension min max logscale unitsPerPixel
            if {$dimension eq {x}} {
                set deltaPixel $dx
            } else {
                set deltaPixel $dy
            }
            if {$logscale} {
                # Translate in log space.  Multiplying both limits by
                # the same factor preserves max/min exactly.
                set logShift [expr {-$unitsPerPixel*$deltaPixel}]
                if {[catch {expr {exp($logShift)}} factor] || ![string is double -strict $factor]\
                            || !isfinite($factor) || ($factor <= 0.0)} {
                    return
                }
                set newMin [expr {$min*$factor}]
                set newMax [expr {$max*$factor}]
            } else {
                # Ordinary linear translation.  Adding the same shift
                # to both limits preserves max-min exactly.
                set shift [expr {-$unitsPerPixel*$deltaPixel}]
                set newMin [expr {$min+$shift}]
                set newMax [expr {$max+$shift}]
            }
            if {!isfinite($newMin) || !isfinite($newMax)} {
                return
            }
            if {($newMin != $min) || ($newMax != $max)} {
                set changed true
            }
            lappend cmds [list $graph axis configure $axis -min $newMin -max $newMax]
        }
        # Create the undo entry only when the view actually moves.
        # Therefore press/release without movement does not pollute the
        # zoom history.
        if {$changed && !$PanInfo(historySaved)} {
            if {[my configure -zoomtitle]} {
                my ZoomTitleNext
            }
            my SaveZoomState
            set PanInfo(historySaved) true
        }
        # If the pointer was previously moved and then returns to the
        # starting location, these commands also restore the original
        # limits.
        if {$changed || $PanInfo(historySaved)} {
            foreach cmd $cmds {
                {*}$cmd
            }
        }
        set PanInfo(dx) $dx
        set PanInfo(dy) $dy
        set PanInfo(changed) $changed
    }
    method RestorePanInteractions {x y} {
        set graph $Subwidgets(graph)
        my RemoveBindTag $graph pan-region-$graph
        if {[info exists PanTransientChecks(activeAxes)]} {
            my setAxisActiveScale $PanTransientChecks(activeAxes)
            unset PanTransientChecks(activeAxes)
        }
        # Restore the ordinary crosshair Motion/Enter/Leave tag first.
        if {[info exists PanTransientChecks(crosshairs)] && $PanTransientChecks(crosshairs)} {
            my AddBindTag $graph crosshairs-$graph
        }
        # Restore the marker tag in its normal position after the
        # crosshair tag.
        set refreshCrosshairs false
        if {[info exists PanTransientChecks(crosshairsMarker)] && $PanTransientChecks(crosshairsMarker)} {
            my AddBindTag $graph crosshairs-marker-$graph crosshairs-$graph
            set refreshCrosshairs true
        }
        my ChangeToolbarState restore
        # The graph transform is now stable.  Recreate any fixed-pixel
        # marker geometry at the release/cancel pointer position.
        if {$refreshCrosshairs} {
            update idletasks
            my RefreshCrosshairsMarker $x $y
        }
        # Restore the RBC crosshair display state which existed before
        # panning.  Do this last so the hairs are drawn using the final
        # plot geometry.
        if {[info exists PanTransientChecks(crosshairsHidden)] && !$PanTransientChecks(crosshairsHidden)} {
            $graph crosshairs on
        }
    }
    method FinishPan {x y} {
        if {![info exists PanInfo(active)] ||
            !$PanInfo(active)} {
            return false
        }
        # Use the real release position even when there was no final Motion event.
        my DragPan $x $y
        set historySaved $PanInfo(historySaved)
        set changed $PanInfo(changed)
        set PanInfo(active) false
        # If the user moved away and then returned exactly to the starting
        # point, the resulting operation is a no-op.  Remove the temporary
        # history entry again.
        if {$historySaved && !$changed} {
            set ZoomInfo(stack) [lrange $ZoomInfo(stack) 1 end]
            if {[my configure -zoomtitle]} {
                if {[info exists ZoomInfo(titleTimer)]} {
                    after cancel $ZoomInfo(titleTimer)
                }
                my DestroyZoomTitle
            }
        }
        if {$historySaved && $changed && [my configure -zoomtitle]} {
            my FinishZoomTitle
        }
        my RestorePanInteractions $x $y
        array unset PanInfo
        array unset PanTransientChecks
        return true
    }
    method CancelPan {{x {}} {y {}}} {
        if {![info exists PanInfo(active)] || !$PanInfo(active)} {
            return false
        }
        set graph $Subwidgets(graph)
        set historySaved $PanInfo(historySaved)
        set PanInfo(active) false
        my RemoveBindTag $graph pan-region-$graph
        if {$historySaved} {
            my PopZoom yes $x $y
        }
        my RestorePanInteractions $x $y
        array unset PanInfo
        array unset PanTransientChecks
        return true
    }
    method ResetAllZoom {} {
        set graph $Subwidgets(graph)
        my PopZoom no
        my RemoveBindTag $graph select-region-$graph
        my InitZoomStack
        $graph marker delete {*}[$graph marker names gtbZoom*]
    }
    ### snapshot methods
    method MakeSnapshot {} {
        if {![catch {set savePath [tk_getSaveFile -initialfile snapshot.png]} errorStr] && ($savePath ne {})} {
            set [namespace current]::baseImg [image create photo]
            $Subwidgets(graph) snap [set [namespace current]::baseImg]
            [set [namespace current]::baseImg] write [list $savePath] -format png
            unset -- [namespace current]::baseImg
        } else {
            return
        }
    }
    method PostScriptDialog {} {
        set graph $Subwidgets(graph)
        variable PsData
        set top $Subwidgets(toolbarFrame).top
        toplevel $top
        wm title $top {Postscript dialog}
        foreach var {center landscape maxpect preview decorations padx pady paperwidth paperheight width height\
                             colormode} {
            set PsData($graph.$var) [$graph postscript cget -$var]
        }
        set var [namespace current]::PsData
        set row 1
        set col 0
        ttk::label $top.title -text {PostScript Options}
        grid $top.title -columnspan 7
        foreach bool {center landscape maxpect preview decorations} {
            set w $top.$bool-label
            ttk::label $w -text -$bool -font {Courier 12}
            grid $w -row $row -column $col -sticky e -pady {2 0} -padx {0 4}
            set w $top.$bool-yes
            ttk::radiobutton $w -text yes -variable ${var}($graph.$bool) -value 1
            grid $w -row $row -column [expr {$col+1}] -sticky w
            set w $top.$bool-no
            ttk::radiobutton $w -text no -variable ${var}($graph.$bool) -value 0
            grid $w -row $row -column [expr {$col+2}] -sticky w
            incr row
        }
        ttk::label $top.modes -text -colormode -font {Courier 12}
        grid $top.modes -row $row -column 0 -sticky e -pady {2 0} -padx {0 4}
        set col 1
        foreach m {color greyscale} {
            set w $top.$m
            ttk::radiobutton $w -text $m -variable ${var}($graph.colormode) -value $m
            grid $w -row $row -column $col -sticky w
            incr col
        }
        set row 1
        ttk::frame $top.sep -width 2
        grid $top.sep -row $row -column 3 -sticky ns -rowspan 6
        set col 4
        foreach value {padx pady paperwidth paperheight width height} {
            set w $top.$value-label
            ttk::label $w -text -$value -font {Courier 12}
            grid $w -row $row -column $col -sticky e -pady {2 0} -padx {0 4}
            set w $top.$value-entry
            #global $graph.$value
            ttk::entry $w -textvariable ${var}($graph.$value) -width 8
            grid $w -row $row -column [expr {$col+1}] -columnspan 2 -sticky w -padx 8
            incr row
        }
        grid columnconfigure $top 3 -minsize .125i
        ttk::button $top.cancel -text Cancel -command [list destroy $top]
        grid $top.cancel -row $row -column 0 -pady 2 -columnspan 3
        ttk::button $top.reset -text Reset -command [list destroy $top]
        ttk::button $top.print -text Print -command [namespace code {my ResetPostScript}]
        grid $top.print -row $row -column 4 -pady 2 -columnspan 2
        bind $top.print <Destroy> [list array unset $var $graph*]
    }
    method ResetPostScript {} {
        set graph $Subwidgets(graph)
        foreach var {center landscape maxpect preview decorations padx pady paperwidth paperheight width height\
                             colormode} {
            set old [$graph postscript cget -$var]
            if {[catch {$graph postscript configure -$var [set PsData($graph.$var)]} errorStr]} {
                $graph postscript configure -$var $old
                set PsData($graph.$var) $old
            }
        }
        if {![catch {set savePath [tk_getSaveFile -initialfile snapshot.ps]} errorStr] && ($savePath ne {})} {
            $graph postscript output $savePath
        } else {
            return
        }
    }
}

namespace eval ::rbc {
    namespace eval graphtoolbar {
        namespace export graphtoolbar
    }
    namespace import ::rbc::graphtoolbar::graphtoolbar
    namespace export graphtoolbar
}
