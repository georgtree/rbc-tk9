package require argparse
package require Tk
package require rbc

namespace eval ::graphtoolbar {
    variable libDir [file dirname [file normalize [info script]]]
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

    # crosshairs markers box default options
    option add *gtbCrosshairsTextBoxFill #FFEB3B widgetDefault
    option add *gtbCrosshairsTextBoxOutline grey widgetDefault
    option add *gtbCrosshairsTextBoxLineWidth 1 widgetDefault
}

namespace eval ::graphtoolbar::icons {
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

oo::configurable create ::graphtoolbar::graphtoolbar {
    variable Subwidgets
    initialize {
        variable CrosshairsModes
        const CrosshairsModes [dict create current {Current point} closest {Closest point} none {No marker}\
                                         disabled Disabled]
    }
    property zoomtitle
    property zoomtitleopts -set {
        set zoomtitleopts\
                [argparse -inline\
                         [list\
                                  [list -font= -key -font -default\
                                           [option get $Subwidgets(graph) gtbZoomTitleFont GtbZoomTitleFont]]\
                                  [list -foreground= -key -foreground -default\
                                           [option get $Subwidgets(graph) gtbZoomTitleForeground GtbZoomTitleForeground]]\
                                  [list -shadow= -key -shadow -default\
                                           [option get $Subwidgets(graph) gtbZoomTitleShadow GtbZoomTitleShadow]]\
                                  [list -anchor= -key -anchor -default\
                                           [option get $Subwidgets(graph) gtbZoomTitleAnchor GtbZoomTitleAnchor]]\
                                  [list -coords= -key -coords -default\
                                           [option get $Subwidgets(graph) gtbZoomTitleCoords GtbZoomTitleCoords]]]\
                         $value]
    }
    property zoomboxopts  -set {
        set zoomboxopts\
                [argparse -inline\
                         [list\
                                  [list -dashes= -key -dashes -default\
                                           [option get $Subwidgets(graph) gtbZoomOutlineDashes GtbZoomOutlineDashes]]\
                                  [list -linewidth= -key -linewidth -default\
                                           [option get $Subwidgets(graph) gtbZoomOutlineLineWidth GtbZoomOutlineLineWidth]]\
                                  [list -xor= -key -xor -default\
                                           [option get $Subwidgets(graph) gtbZoomOutlineXor GtbZoomOutlineXor]]]\
                         $value]
    }
    property zoommarkopts -set {
        set zoommarkopts\
                [argparse -inline\
                         [list\
                                  [list -font= -key -font -default\
                                           [option get $Subwidgets(graph) gtbZoomTextFont GtbZoomTextFont]]\
                                  [list -anchor= -key -anchor -default\
                                           [option get $Subwidgets(graph) gtbZoomTextAnchor GtbZoomTextAnchor]]\
                                  [list -justify=  -key -justify -default\
                                           [option get $Subwidgets(graph) gtbZoomTextJustify GtbZoomTextJustify]]\
                                  [list -foreground= -key -foreground -default\
                                           [option get $Subwidgets(graph) gtbZoomTextForeground GtbZoomTextForeground]]\
                                  [list -padx=  -key -padx -default\
                                           [option get $Subwidgets(graph) gtbZoomTextPadX GtbZoomTextPadX]]\
                                  [list -formatx= -key -formatx -default\
                                           [option get $Subwidgets(graph) gtbZoomTextXFormat GtbZoomTextXFormat]]\
                                  [list -formaty= -key -formaty -default\
                                           [option get $Subwidgets(graph) gtbZoomTextYFormat GtbZoomTextYFormat]]\
                                  [list -pady=  -key -pady -default\
                                            [option get $Subwidgets(graph) gtbZoomTextPadY GtbZoomTextPadY]]]\
                         $value]
    }
    property zoommarkboxopts -set {
        set zoommarkboxopts\
            [argparse -inline\
                 [list\
                      [list -fill= -key -fill -default\
                           [option get $Subwidgets(graph) gtbZoomTextBoxFill GtbZoomTextBoxFill]] \
                      [list -outline= -key -outline -default\
                           [option get $Subwidgets(graph) gtbZoomTextBoxOutline GtbZoomTextBoxOutline]] \
                      [list -linewidth= -key -linewidth -default\
                           [option get $Subwidgets(graph) gtbZoomTextBoxLineWidth GtbZoomTextBoxLineWidth]]] \
                 $value]
    }
    property crosshairsopts  -set {
        set crosshairsopts\
                [argparse -inline\
                         [list\
                                  [list -linewidth= -key -linewidth -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsLineWidth GtbCrosshairsLineWidth]]\
                                  [list -color= -key -color -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsColor GtbCrosshairsColor]]\
                                  [list -dashes= -key -dashes -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsDashes GtbCrosshairsDashes]]]\
                         $value]
    }
    property crosshairsclosestopts -set {
        set crosshairsclosestopts [argparse -inline {
            {-interpolate= -default yes}
            {-halo= -default 10}
            {-single= -default yes}
            {-hide= -default yes}} $value]
    }
    property crosshairsmarkopts -set {
        set crosshairsmarkopts\
                [argparse -inline\
                         [list\
                                  [list -font= -key -font -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsTextFont GtbCrosshairsTextFont]]\
                                  [list -anchor= -key -anchor -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsTextAnchor\
                                                    GtbCrosshairsTextAnchor]]\
                                  [list -justify=  -key -justify -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsTextJustify\
                                                    GtbCrosshairsTextJustify]]\
                                  [list -foreground= -key -foreground -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsTextForeground GtbCrosshairsTextForeground]]\
                                  [list -formatx= -key -formatx -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsTextXFormat GtbCrosshairsTextXFormat]]\
                                  [list -formaty= -key -formaty -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsTextYFormat GtbCrosshairsTextYFormat]]\
                                  [list -padx=  -key -padx -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsTextPadX GtbCrosshairsTextPadX]]\
                                  [list -pady=  -key -pady -default\
                                            [option get $Subwidgets(graph) gtbCrosshairsTextPadY GtbCrosshairsTextPadY]]]\
                         $value]
    }
    property crosshairsmarkboxopts -set {
        set crosshairsmarkboxopts\
            [argparse -inline\
                 [list\
                      [list -fill= -key -fill -default\
                           [option get $Subwidgets(graph) gtbCrosshairsTextBoxFill GtbCrosshairsTextBoxFill]]\
                      [list -outline= -key -outline -default\
                           [option get $Subwidgets(graph) gtbCrosshairsTextBoxOutline GtbCrosshairsTextBoxOutline]]\
                      [list -linewidth= -key -linewidth -default\
                           [option get $Subwidgets(graph) gtbCrosshairsTextBoxLineWidth GtbCrosshairsTextBoxLineWidth]]]\
                 $value]
    }
    variable PsData ZoomInfo ZoomMod zoomtitle ZoomMark zoomtitleopts zoomboxopts zoommarkopts ZoomTransientChecks\
            zoommarkboxopts
    variable CrosshairsSelector crosshairsmarkopts crosshairsclosestopts crosshairsopts crosshairsmarkboxopts
    variable AxisScaleInfo SavedToolbarStates
    classmethod unknown {w args} {
        if {[string match .* $w]} {
            [self] new $w {*}$args
            return $w
        }
        next $w {*}$args
    }
    constructor {args} {
        classvariable CrosshairsModes
        set arguments [argparse -inline -pfirst {
            path
            {-width=}
            {-height=}
            {-toolbarside= -default bottom}
            -zoom
            {-zoomstartbut -default {ButtonPress-1}}
            {-zoombackbut -default {ButtonPress-3}}
            {-zoommod= -default {Any-}}
            {-zoomtitle -require zoom}
            {-zoomtitleopts= -require zoom -type dict -default {}}
            {-zoomboxopts= -require zoom -type dict -default {}}
            {-zoommark -require zoom}
            {-zoommarkopts= -require zoom -type dict -default {}}
            {-zoommarkboxopts= -require zoom -type dict -default {}}
            -crosshairs
            {-crosshairsmode= -require crosshairs -default closest}
            {-crosshairsopts= -require crosshairs -type dict -default {}}
            {-crosshairsmarkopts= -require crosshairs -type dict -default {}}
            {-crosshairsmarkboxopts= -require crosshairs -type dict -default {}}
            {-crosshairsclosestopts= -require crosshairs -type dict -default {}}
            {-scaletoggle= -type list}
            -activelegend
        }]
        my configure -crosshairsclosestopts [dict get $arguments crosshairsclosestopts]
        set ZoomMod [dict get $arguments zoommod]
        set currentNamespace [namespace current]
        set frameName [dict get $arguments path]
        ttk::frame $frameName -width [dict get $arguments width] -height [dict get $arguments height]
        set Subwidgets(toolbarFrame) [ttk::frame $frameName.toolbarFr]
        set Subwidgets(graph) [::rbc::graph $frameName.graph]
        grid $Subwidgets(graph) -row [expr {[dict get $arguments toolbarside] eq {bottom} ? 0 : 1}] -column 0\
                -sticky nsew
        grid $Subwidgets(toolbarFrame) -row [expr {[dict get $arguments toolbarside] eq {bottom} ? 1 : 0}] -column 0\
                -sticky ew


        set butCount -1
        set Subwidgets(removeAllMarkersBut) [ttk::button $Subwidgets(toolbarFrame).removeAllMarkersBut -width 14\
                                                     -image ::graphtoolbar::icons::removeAllMarkersIcon]
        grid $Subwidgets(removeAllMarkersBut) -row 0 -column [incr butCount] -sticky ns
        set Subwidgets(makeSnapshotBut) [ttk::button $Subwidgets(toolbarFrame).makeSnapshotBut -width 14\
                                                 -image ::graphtoolbar::icons::makeSnapshotIcon\
                                                 -command [namespace code {my MakeSnapshot}]]
        grid $Subwidgets(makeSnapshotBut) -row 0 -column [incr butCount] -sticky ns
        set Subwidgets(postScriptDialogBut) [ttk::button $Subwidgets(toolbarFrame).postScriptDialogBut -width 14\
                                                     -image ::graphtoolbar::icons::postScriptDialogIcon\
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
            my EnableZoom [dict get $arguments zoomstartbut] [dict get $arguments zoombackbut]
            set Subwidgets(resetZoomBut) [ttk::button $Subwidgets(toolbarFrame).resetZoomBut -width 14\
                                                  -image ::graphtoolbar::icons::resetZoomIcon\
                                                  -command [namespace code {my ResetAllZoom}]]
            grid $Subwidgets(resetZoomBut) -row 0 -column [incr butCount] -sticky ns
            set Subwidgets(revertZoomBut) [ttk::button $Subwidgets(toolbarFrame).revertZoomBut -width 14\
                                                   -image ::graphtoolbar::icons::revertZoomIcon\
                                                   -command [namespace code {my ResetZoom}]]
            grid $Subwidgets(revertZoomBut) -row 0 -column [incr butCount] -sticky ns
        }

        ##### crosshairs activation
        if {[dict exists $arguments crosshairs]} {
            my configure -crosshairsopts [dict get $arguments crosshairsopts]
            my configure -crosshairsmarkopts [dict get $arguments crosshairsmarkopts]
            my configure -crosshairsmarkboxopts [dict get $arguments crosshairsmarkboxopts]
            set Subwidgets(crosshairsComBox) [ttk::combobox $Subwidgets(toolbarFrame).crosshairsComBox -width 10\
                                                      -values [dict values $CrosshairsModes]\
                                                      -textvariable [self namespace]::CrosshairsSelector]
            if {[dict get $arguments crosshairsmode] ni [dict keys $CrosshairsModes]} {
                return -code error "Crosshair mode '[dict get $arguments crosshairsmode]' is not in the list of\
                        availible modes '[dict keys $CrosshairsModes]'"
            }
            set CrosshairsSelector [dict get $CrosshairsModes [dict get $arguments crosshairsmode]]
            grid $Subwidgets(crosshairsComBox) -row 0 -column [incr butCount] -sticky ns
            bind $Subwidgets(crosshairsComBox) <<ComboboxSelected>> [namespace code {my SelectCrosshairsMode %W}]
            my SelectCrosshairsMode $Subwidgets(crosshairsComBox)
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
        if {[info exists ZoomInfo(titleTimer)]} {
            after cancel $ZoomInfo(titleTimer)
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
                -bitmap "@[file join $::graphtoolbar::libDir pointer.xbm]"\
                -mask "@[file join $::graphtoolbar::libDir pointer_mask.xbm]" -under no {*}$mapopts
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
            if {$lineWidth > $width} {
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
            if {($left >= $plotLeft) && ($top >= $plotTop) && ($right <= $plotRight) && ($bottom <= $plotBottom)} {
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
        lassign [my WidgetToAxisPixels $left $top] xPixel1 yPixel1
        lassign [my WidgetToAxisPixels $right $bottom] xPixel2 yPixel2
        set xValue1 [$graph axis invtransform $mapx $xPixel1]
        set xValue2 [$graph axis invtransform $mapx $xPixel2]
        set yValue1 [$graph axis invtransform $mapy $yPixel1]
        set yValue2 [$graph axis invtransform $mapy $yPixel2]
        set coords [list $xValue1 $yValue1 $xValue2 $yValue1 $xValue2 $yValue2 $xValue1 $yValue2]
        if {[$graph marker exists $name]} {
            $graph marker configure $name -coords $coords -mapx $mapx -mapy $mapy {*}$boxOptions
        } else {
            $graph marker create polygon -name $name -coords $coords -mapx $mapx -mapy $mapy -bindtags {} -under no\
                    {*}$boxOptions
        }
    }
    method VisibleAxes {dimension} {
        # Returns all visible named axes for the requested data dimension, preserving their margin/use order.
        #  dimension - `x` or `y`
        set graph $Subwidgets(graph)
        switch -- $dimension {
            x {
                set margins {xaxis x2axis}
            }
            y {
                set margins {yaxis y2axis}
            }
            default {
                return -code error "unknown axis dimension '$dimension': must be x or y"
            }
        }
        set axes [list]
        foreach margin $margins {
            foreach axis [$graph $margin use] {
                if {![$graph axis cget $axis -hide]} {
                    lappend axes $axis
                }
            }
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
        # Converts physical widget pixels to values on the specified
        # X and Y axes.
        set graph $Subwidgets(graph)
        lassign [my WidgetToAxisPixels $x $y] xPixel yPixel
        set xValue [$graph axis invtransform $mapx $xPixel]
        set yValue [$graph axis invtransform $mapy $yPixel]
        return [list $xValue $yValue]
    }
    method AxisMarkerInfo {xPixel yPixel formatx formaty} {
        # Returns formatted values of all visible axes at xPixel/yPixel. The first visible X/Y axes are also used to
        #  place the marker.
        set graph $Subwidgets(graph)
        set xAxes [my VisibleAxes x]
        set yAxes [my VisibleAxes y]
        if {[llength $xAxes] == 0} {
            return -code error {graph has no visible X axis}
        }
        if {[llength $yAxes] == 0} {
            return -code error {graph has no visible Y axis}
        }
        set mapx [lindex $xAxes 0]
        set mapy [lindex $yAxes 0]
        set lines [list]
        if {[$Subwidgets(graph) cget -invertxy]} {
            set xOrientation v
            set yOrientation h
        } else {
            set xOrientation h
            set yOrientation v
        }
        foreach axis $xAxes {
            set xValue [$graph axis invtransform $axis $xPixel]
            lappend lines [format "%s($xOrientation)=%$formatx" $axis $xValue]
            if {$axis eq $mapx} {
                set markerXValue $xValue
            }
        }
        foreach axis $yAxes {
            set yValue [$graph axis invtransform $axis $yPixel]
            lappend lines [format "%s($yOrientation)=%$formaty" $axis $yValue]
            if {$axis eq $mapy} {
                set markerYValue $yValue
            }
        }
        return [dict create text [join $lines \n] mapx $mapx mapy $mapy xValue $markerXValue yValue $markerYValue]
    }

    #### axes toggle methods
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
            if {$axis ne ""} {
                %W axis configure $axis -background {}
            }
        }
        $graph axis bind $tag <$event> {
            set axis [%W axis get current]
            if {$axis ne {}} {
                set logscale [lindex [%W axis configure $axis -logscale] end]
                %W axis configure $axis -logscale [expr {!$logscale}]
            }
        }
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
    method ActivateLegend {} {
        set graph $Subwidgets(graph)
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
    method CreateClosestMarker {graph textMarker bitmapMarker element xValue yValue options} {
        set mapx [$graph element cget $element -mapx]
        set mapy [$graph element cget $element -mapy]
        set xPixel [$graph axis transform $mapx $xValue]
        set yPixel [$graph axis transform $mapy $yValue]
        # a closest marker belongs to a specific element, therefore show only the axes actually used by that element.
        if {[$Subwidgets(graph) cget -invertxy]} {
            set xOrientation v
            set yOrientation h
        } else {
            set xOrientation h
            set yOrientation v
        }
        set formatx [dict get $options -formatx]
        set formaty [dict get $options -formaty]
        set text [format "%s\n%s($xOrientation)=%$formatx\n%s($yOrientation)=%$formaty" $element $mapx\
                          $xValue $mapy $yValue]
        # physical widget position of the actual closest point.
        lassign [my AxisPixelsToWidget $xPixel $yPixel] x y
        set anchor [my TextAnchor $x $y $text $options]
        dict set options -anchor $anchor
        # shift the label away from the actual point/crosshairs.
        lassign [my TextOffset $x $y $anchor] textX textY
        set boxOptions [my configure -crosshairsmarkboxopts]
        my DrawTextBackground ${textMarker}Box $textX $textY $text $options $boxOptions $mapx $mapy
        # the text marker itself must be shifted too.
        lassign [my WidgetToAxisValues $textX $textY $mapx $mapy] textXValue textYValue
        $graph marker create text -name $textMarker -text $text -coords [list $textXValue $textYValue] -mapx $mapx\
                -mapy $mapy {*}[dict remove $options -formatx -formaty]
        # bitmap remains exactly at the real element point.
        my AddBitmapPoint $bitmapMarker $xValue $yValue $mapx $mapy
    }
    method CrosshairsMarkerMotion {graph x y options mode interpolate halo single} {
        if {![$graph inside $x $y]} {
            return
        }
        if {$mode eq {current}} {
            lassign [my WidgetToAxisPixels $x $y] xPixel yPixel
            set info [my AxisMarkerInfo $xPixel $yPixel [dict get $options -formatx] [dict get $options -formaty]]
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
                    $mapx -mapy $mapy {*}[dict remove $options -formatx -formaty]
            return
         } elseif {$mode eq {closest}} {
            if {$single} {
                if {[$graph element closest $x $y pointVar -along both -interpolate $interpolate -halo $halo]} {
                    my CreateClosestMarker $graph crosshairsText crosshairsBitmap $pointVar(name) $pointVar(x)\
                            $pointVar(y) $options
                }
            } else {
                set i 0
                foreach elem [$graph element names] {
                    if {![$graph element closest $x $y pointVar -along both -interpolate $interpolate -halo $halo\
                                  $elem]} {
                        continue
                    }
                    my CreateClosestMarker $graph crosshairsText$i crosshairsBitmap$i $pointVar(name) $pointVar(x)\
                            $pointVar(y) $options
                    incr i
                }
            }
        }

    }
    method CrosshairsMotion {graph x y {hide no}} {
        set markerNames [$graph marker names crosshairs*]
        $graph marker delete {*}$markerNames
        if {!$hide} {
            $graph crosshairs configure -position @${x},$y
        }
    }
    method SelectCrosshairsMode {widget} {
        set graph $Subwidgets(graph)
        set tagCrosshairs crosshairs-$graph
        set tagCrosshairsMarker crosshairs-marker-$graph
        my RemoveBindTag $graph $tagCrosshairs
        my RemoveBindTag $graph $tagCrosshairsMarker
        set options [my configure -crosshairsmarkopts]
        $graph crosshairs configure {*}[my configure -crosshairsopts]
        if {$CrosshairsSelector eq {Current point}} {
            $graph crosshairs on
            bind $tagCrosshairs <Leave> {
                %W crosshairs off
            }
            bind $tagCrosshairs <Enter> {
                %W crosshairs on
            }
            bind $tagCrosshairs <Any-Motion> [namespace code [list my CrosshairsMotion %W %x %y]]
            bind $tagCrosshairsMarker <Any-Motion>\
                    [namespace code [list my CrosshairsMarkerMotion %W %x %y $options current {} {} {}]]
            my AddBindTag $graph $tagCrosshairs
            my AddBindTag $graph $tagCrosshairsMarker $tagCrosshairs
        } elseif {$CrosshairsSelector eq {Closest point}} {
            dict with crosshairsclosestopts {}
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
            bind $tagCrosshairsMarker <Any-Motion>\
                    [namespace code [list my CrosshairsMarkerMotion %W %x %y $options closest $interpolate $halo\
                                             $single]]
            my AddBindTag $graph $tagCrosshairs
            my AddBindTag $graph $tagCrosshairsMarker $tagCrosshairs
            unset -nocomplain {*}[dict keys $crosshairsclosestopts]
        } elseif {$CrosshairsSelector eq {Disabled}} {
            $graph crosshairs off
        } else {
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
    method EnableZoom {start reset} {
        # Enables zoom for graph subwidget.
        #  start - event that starts and ends the zoom box selection
        #  reset - event that restore the previous scale of the graph, or abort selection of second point
        set graph $Subwidgets(graph)
        my InitZoomStack
        set modifier $ZoomMod
        bind zoom-$graph <${modifier}${start}> [namespace code {my SetZoomPoint %x %y}]
        bind zoom-$graph <${modifier}${reset}> [namespace code {
            if {[%W inside %x %y]} {
                my ResetZoom
            }
        }]
        my AddBindTag $graph zoom-$graph
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
        set info [my AxisMarkerInfo $ZoomInfo($index,xPixel) $ZoomInfo($index,yPixel) [dict get $options -formatx]\
                          [dict get $options -formaty]]
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
        if {$index eq {A}} {
            my AddBitmapPoint ${marker}Bitmap\
                    {*}[my WidgetToAxisValues $ZoomInfo($index,x) $ZoomInfo($index,y) $mapx $mapy] $mapx $mapy
        }
        if {[$graph marker exists $marker]} {
            $graph marker configure $marker -coords [list $textXValue $textYValue] -mapx $mapx -mapy $mapy -text $text\
                    -anchor $anchor
        } else {
            $graph marker create text -name $marker -coords [list $textXValue $textYValue] -mapx $mapx -mapy $mapy\
                    -text $text {*}[dict remove $options -formatx -formaty]
            
        }
    }
    method DestroyZoomTitle {} {
        unset -nocomplain ZoomInfo(titleTimer)
        if {$ZoomInfo(corner) eq {A}} {
            catch {$Subwidgets(graph) marker delete gtbZoomTitle} errorStr
        }
    }
    method PopZoom {} {
        # Removes last zoom box scale commands for all visible axes from the stack and restores graph scale to the
        # previous state on the stack.
        set graph $Subwidgets(graph)
        set zoomStack $ZoomInfo(stack)
        if {[llength $zoomStack] > 0} {
            set cmds [lindex $zoomStack 0]
            set ZoomInfo(stack) [lrange $zoomStack 1 end]
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
            if {[my configure -zoomtitle]} {
                if {[info exists ZoomInfo(titleTimer)]} {
                    after cancel $ZoomInfo(titleTimer)
                }
                set ZoomInfo(titleTimer) [after 2000 [namespace code {my DestroyZoomTitle}]]
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
        if {($xPixel1 == $xPixel2) || ($yPixel1 == $yPixel2)} {
            return
        }
        set cmds [list]
        foreach margin {xaxis yaxis x2axis y2axis} {
            foreach axis [$graph $margin use] {
                set min [$graph axis cget $axis -min]
                set max [$graph axis cget $axis -max]
                set cmd [list $graph axis configure $axis -min $min -max $max]
                lappend cmds $cmd
            }
        }
        set ZoomInfo(stack) [linsert $ZoomInfo(stack) 0 $cmds]
        foreach margin {xaxis x2axis} {
            foreach axis [$graph $margin use] {
                set min [$graph axis invtransform $axis $xPixel1]
                set max [$graph axis invtransform $axis $xPixel2]
                if {$min > $max} {
                    $graph axis configure $axis -min $max -max $min
                } else {
                    $graph axis configure $axis -min $min -max $max
                }
            }
        }
        foreach margin {yaxis y2axis} {
            foreach axis [$graph $margin use] {
                set min [$graph axis invtransform $axis $yPixel1]
                set max [$graph axis invtransform $axis $yPixel2]
                if {$min > $max} {
                    $graph axis configure $axis -min $max -max $min
                } else {
                    $graph axis configure $axis -min $min -max $max
                }
            }
        }
        tk busy hold $graph
        event generate $graph <Configure>
        update idletasks
        tk busy forget $graph
    }
    method ResetZoom {} {
        # Restores graph scale to the previous state, or abort the current selection operation.
        set graph $Subwidgets(graph)
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
            my PopZoom
        } else {
            set modifier $ZoomMod
            set ZoomInfo(corner) A
            my RemoveBindTag $graph select-region-$graph
        }
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
    method SetZoomPoint {x y} {
        # Sets the first (A) or second (B) zoom box point
        #  x - horizontal coordinate of the pointer
        #  y - vertical coordinate of the pointer
        set graph $Subwidgets(graph)
        if {![info exists ZoomInfo(corner)]} {
            my InitZoomStack
        }
        if {$ZoomInfo(corner) eq {A}} {
            # First point must actually be inside the plotting area.
            if {![$graph inside $x $y]} {
                return
            }
        } else {
            # Second point may be outside: clamp it to the plot edge.
            lassign [my ClampToPlot $x $y] x y
        }
        my SaveZoomPoint $x $y $ZoomInfo(corner)
        set modifier $ZoomMod
        bind select-region-$graph <${modifier}Motion> [namespace code {
            lassign [my ClampToPlot %x %y] x y
            my SaveZoomPoint $x $y B
            if {$ZoomMark} {
                my MarkZoomPoint B
            }
            my Box
        }]
        if {$ZoomInfo(corner) eq {A}} {
            # disable active axes
            set ZoomTransientChecks(activeAxes) [my getAxisActiveScale]
            my setAxisActiveScale $ZoomTransientChecks(activeAxes) -disabled
            # disable crosshairs marker
            set ZoomTransientChecks(crosshairsMarker) false
            if {[my CheckBindTagExistence $graph crosshairs-marker-$graph]} {
                set ZoomTransientChecks(crosshairsMarker) true
                my RemoveBindTag $graph crosshairs-marker-$graph
            }
            # First corner selected, start watching motion events
            if {$ZoomMark} {
                my MarkZoomPoint A
            }
            if {[my configure -zoomtitle]} {
                my ZoomTitleNext
            }
            my AddBindTag $graph select-region-$graph
            my ChangeToolbarState disable
            set ZoomInfo(corner) B
        } else {
            # Delete the modal binding
            my RemoveBindTag $graph select-region-$graph
            # restore active axes behaviour
            my setAxisActiveScale $ZoomTransientChecks(activeAxes)
            unset ZoomTransientChecks(activeAxes)
            if {$ZoomTransientChecks(crosshairsMarker)} {
                # restore crosshair marker behaviour
                my AddBindTag $graph crosshairs-marker-$graph crosshairs-$graph
            }
            unset ZoomTransientChecks(crosshairsMarker)
            my PushZoom
            my ChangeToolbarState restore
            set ZoomInfo(corner) A
        }
    }
    method Box {} {
        # Creates zoom-box outline from the saved A/B points.
        set graph $Subwidgets(graph)
        set xAxes [my VisibleAxes x]
        set yAxes [my VisibleAxes y]
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
        if {[$graph marker exists gtbZoomOutline]} {
            $graph marker configure gtbZoomOutline -coords $coords
        } else {
            $graph marker create line -name gtbZoomOutline -coords $coords -mapx $mapx -mapy $mapy\
                    {*}[my configure -zoomboxopts]
        }
    }
    method ResetAllZoom {} {
        set graph $Subwidgets(graph)
        # variable AxesScaling
        # if {[dict exists $AxesScaling $graph]} {
        #     dict for {axis limits} [dict get $AxesScaling $graph] {
        #         lassign $limits min max
        #         $graph axis configure $axis -min $min -max $max
        #     }
        # }
        foreach state [set ZoomInfo(stack)] {
            my PopZoom
        }
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
