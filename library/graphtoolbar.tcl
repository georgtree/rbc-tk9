package require argparse
package require Tk
package require rbc

namespace eval ::graphtoolbar {
    variable libDir [file dirname [file normalize [info script]]]
    # zoom title default options
    option add *gtbZoomTitleFont {Helvetica 18} widgetDefault
    option add *gtbZoomTitleShadow yellow4 widgetDefault
    option add *gtbZoomTitleForeground black widgetDefault
    option add *gtbZoomTitleCoords {-Inf Inf} widgetDefault
    option add *gtbZoomTitleAnchor nw widgetDefault

    # zoom box default options
    option add *gtbZoomOutlineDashes 4 widgetDefault
    option add *gtbZoomOutlineLineWidth 1 widgetDefault
    option add *gtbZoomOutlineXor yes widgetDefault

    # zoom box corner markers default options
    option add *gtbZoomTextFont {Helvetica 10} widgetDefault
    option add *gtbZoomTextAnchor ne widgetDefault
    option add *gtbZoomTextBackground {} widgetDefault
    option add *gtbZoomTextJustify left widgetDefault
    option add *gtbZoomTextPadX 4 widgetDefault
    option add *gtbZoomTextPadY 4 widgetDefault

    # crosshairs style default options
    option add *gtbCrosshairsLineWidth 1 widgetDefault
    option add *gtbCrosshairsColor grey widgetDefault
    option add *gtbCrosshairsDashes {} widgetDefault

    # crosshairs markers default options
    option add *gtbCrosshairsTextFont {Helvetica 10} widgetDefault
    option add *gtbCrosshairsTextAnchor nw widgetDefault
    option add *gtbCrosshairsTextBackground {} widgetDefault
    option add *gtbCrosshairsTextJustify left widgetDefault
    option add *gtbCrosshairsTextPadX 4 widgetDefault
    option add *gtbCrosshairsTextPadY 4 widgetDefault
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
                                  [list -bg= -key -bg -default\
                                           [option get $Subwidgets(graph) gtbZoomTextBackground GtbZoomTextBackground]]\
                                  [list -justify=  -key -justify -default\
                                           [option get $Subwidgets(graph) gtbZoomTextJustify GtbZoomTextJustify]]\
                                  [list -padx=  -key -padx -default\
                                           [option get $Subwidgets(graph) gtbZoomTextPadX GtbZoomTextPadX]]\
                                  [list -pady=  -key -pady -default\
                                            [option get $Subwidgets(graph) gtbZoomTextPadY GtbZoomTextPadY]]]\
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
            {-single= -default yes}} $value]
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
                                  [list -bg= -key -bg -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsTextBackground\
                                                    GtbCrosshairsTextBackground]]\
                                  [list -justify=  -key -justify -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsTextJustify\
                                                    GtbCrosshairsTextJustify]]\
                                  [list -padx=  -key -padx -default\
                                           [option get $Subwidgets(graph) gtbCrosshairsTextPadX GtbCrosshairsTextPadX]]\
                                  [list -pady=  -key -pady -default\
                                            [option get $Subwidgets(graph) gtbCrosshairsTextPadY GtbCrosshairsTextPadY]]]\
                         $value]
    }
    variable PsData ZoomInfo ZoomMod zoomtitle ZoomMark zoomtitleopts zoomboxopts zoommarkopts ZoomTransientChecks
    variable CrosshairsSelector crosshairsmarkopts crosshairsclosestopts crosshairsopts
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
            -crosshairs
            {-crosshairsmode= -require crosshairs -default closest}
            {-crosshairsopts= -require crosshairs -type dict -default {}}
            {-crosshairsmarkopts= -require crosshairs -type dict -default {}}
            {-crosshairsclosestopts= -require crosshairs -type dict -default {}}
            {-scaletoggle= -type list}
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
    method AddBitmapPoint {name coords {anchor w}} {
        $Subwidgets(graph) marker create bitmap -name $name -coords $coords\
                -bitmap "@[file join $::graphtoolbar::libDir circle.xbm]"\
                -mask "@[file join $::graphtoolbar::libDir circle_mask.xbm]" -under no
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
    method TextSize {text font padx pady} {
        # Calculates size of bounding box for text marker. 
        #  text - marker text string
        #  font - selected font
        #  padx - horizontal padding of text marker
        #  pady - vertical padding of text marker
        set graph $Subwidgets(graph)
        set width 0
        set lines [split $text \n]
        foreach line $lines {
            set w [font measure $font -displayof $graph $line]
            if {$w > $width} {
                set width $w
            }
        }
        set height [expr {[llength $lines]*[font metrics $font -displayof $graph -linespace]}]
        # padding adjustement
        set padx [winfo pixels $graph $padx]
        set pady [winfo pixels $graph $pady]
        incr width  [expr {2*$padx}]
        incr height [expr {2*$pady}]
        return [list $width $height]
    }
    method TextAnchor {x y text options} {
        # Selects text marker anchor depending on the availible space for the marker to be fully visible.
        #  x - horizontal coordinate of marker location
        #  y - vertical coordinate of marker location
        #  text - string displayed by the marker
        #  options - dictionary containing information about text font and text marker padding
        set graph $Subwidgets(graph)
        lassign [$graph extents plotarea] left top width height
        set right  [expr {$left+$width-1}]
        set bottom [expr {$top+$height-1}]
        set font [dict get $options -font]
        lassign [my TextSize $text $font [dict get $options -padx] [dict get $options -pady]] w h
        set preferred [dict get $options -anchor]
        foreach anchor [linsert {nw ne sw se} 0 $preferred] {
            switch -- $anchor {
                nw {
                    set x0 $x
                    set y0 $y
                }
                ne {
                    set x0 [expr {$x - $w}]
                    set y0 $y
                }
                sw {
                    set x0 $x
                    set y0 [expr {$y - $h}]
                }
                se {
                    set x0 [expr {$x - $w}]
                    set y0 [expr {$y - $h}]
                }
                default {
                    continue
                }
            }
            set x1 [expr {$x0 + $w}]
            set y1 [expr {$y0 + $h}]

            if {($x0>=$left) && ($y0>=$top) && ($x1<=$right) && ($y1<=$bottom)} {
                return $anchor
            }
        }
        # very small plot/very large text: pick the direction with the greatest available space.
        set horizontal [expr {($x-$left) > ($right-$x) ? {e} : {w}}]
        set vertical [expr {($y-$top) > ($bottom-$y) ? {s} : {n}}]
        return ${vertical}${horizontal}
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
    #### crosshairs methods
    method CrosshairsMarkerMotion {graph xScreen yScreen options mode interpolate halo single} {
        if {$mode eq {current}} {
            if {![$graph inside $xScreen $yScreen]} {
                return
            }
            if {[$graph cget -invertxy]} {
                set x [$graph xaxis invtransform $yScreen]
                set y [$graph yaxis invtransform $xScreen]
            } else {
                set x [$graph xaxis invtransform $xScreen]
                set y [$graph yaxis invtransform $yScreen]
            }
            set text "x=[format %.4g $x]\ny=[format %.4g $y]"
            set anchor [my TextAnchor $xScreen $yScreen $text $options]
            dict set options -anchor $anchor
            $graph marker create text -name crosshairsText -text $text -coords [list $x $y] {*}$options
        } else {
            if {$single} {
                if {![$graph inside $xScreen $yScreen]} {
                    return
                }
                if {[$graph cget -invertxy]} {
                    set result [$graph element closest $yScreen $xScreen pointVar -along both -interpolate $interpolate\
                                        -halo $halo]
                } else {
                    set result [$graph element closest $xScreen $yScreen pointVar -along both -interpolate $interpolate\
                                        -halo $halo]
                }
                if {$result} {
                    set text "$pointVar(name): x=[format %.4g $pointVar(x)]\ny=[format %.4g $pointVar(y)]"
                    set anchor [my TextAnchor $xScreen $yScreen $text $options]
                    dict set options -anchor $anchor
                    $graph marker create text -name crosshairsText -text $text -coords [list $pointVar(x) $pointVar(y)]\
                            {*}$options
                    my AddBitmapPoint crosshairsBitmap [list $pointVar(x) $pointVar(y)]
                }
            } else {
                set i 0
                foreach elem [$graph element names] {
                    if {![$graph inside $xScreen $yScreen]} {
                        return
                    }
                    if {[$graph cget -invertxy]} {
                        set result [$graph element closest $yScreen $xScreen pointVar -along both\
                                            -interpolate $interpolate -halo $halo $elem]
                    } else {
                        set result [$graph element closest $xScreen $yScreen pointVar -along both\
                                            -interpolate $interpolate -halo $halo $elem]
                    }
                    if {!$result} {
                        continue
                    }
                    set text "$pointVar(name): x=[format %.4g $pointVar(x)]\ny=[format %.4g $pointVar(y)]"
                    set anchor [my TextAnchor $xScreen $yScreen $text $options]
                    dict set options -anchor $anchor
                    $graph marker create text -name crosshairsText$i -text $text\
                            -coords [list $pointVar(x) $pointVar(y)] {*}$options
                    my AddBitmapPoint crosshairsBitmap$i [list $pointVar(x) $pointVar(y)]
                    incr i
                }
            }
        }
    }
    method CrosshairsMotion {graph xScreen yScreen} {
        set markersNames [$graph marker names crosshairs*]
        $graph marker delete {*}$markersNames
        $graph crosshairs configure -position @${xScreen},$yScreen
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
            $graph crosshairs on
            bind $tagCrosshairs <Leave> {
                %W crosshairs off
            }
            bind $tagCrosshairs <Enter> {
                %W crosshairs on
            }
            dict with crosshairsclosestopts {}
            bind $tagCrosshairs <Any-Motion> [namespace code [list my CrosshairsMotion %W %x %y]]
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
        # ZoomInfo array contains next variables:
        # A,x - horizontal coordinate of the current first point (A) of the zoom box
        # A,y - vertical coordinate of the current first point (A) of the zoom box
        # B,x - horizontal coordinate of the current second point (B) of the zoom box
        # B,y - vertical coordinate of the current second point (B) of the zoom box
        # stack - list of lists containing all scale commands for all visible axes applied to the graph
        # corner - current selection point during the zoom box selection operation, either A or B
        array set ZoomInfo {
            A,x {}
            A,y {}
            B,x {}
            B,y {}
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
    method GetCoords {x y index} {
        # Saves current pointer coordinates into ZoomInfo
        set ZoomInfo($index,screenX) $x
        set ZoomInfo($index,screenY) $y
        if {[$Subwidgets(graph) cget -invertxy]} {
            set ZoomInfo($index,x) $y
            set ZoomInfo($index,y) $x
        } else {
            set ZoomInfo($index,x) $x
            set ZoomInfo($index,y) $y
        }
    }
    method MarkZoomPoint {index} {
        # Creates marker at the start of zoom box selection, or at it's end
        #  index - point A or point B selection
        set graph $Subwidgets(graph)
        set x [$graph xaxis invtransform $ZoomInfo($index,x)]
        set y [$graph yaxis invtransform $ZoomInfo($index,y)]
        set marker gtbZoomText_$index
        set options [my configure -zoommarkopts]
        set text [format "x=%.4g\ny=%.4g" $x $y]
        set anchor [my TextAnchor $ZoomInfo($index,screenX) $ZoomInfo($index,screenY) $text $options]
        # dynamic anchor overrides the configured preferred anchor.
        dict set options -anchor $anchor
        if {[$graph marker exists $marker]} {
            $graph marker configure $marker -coords [list $x $y] -text $text -anchor $anchor
        } else {
            $graph marker create text -coords [list $x $y] -name $marker -text $text {*}$options
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
        set x1 $ZoomInfo(A,x)
        set y1 $ZoomInfo(A,y)
        set x2 $ZoomInfo(B,x)
        set y2 $ZoomInfo(B,y)
        if {($x1 == $x2) || ($y1 == $y2)} {
            # No delta, revert to start
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
                set min [$graph axis invtransform $axis $x1]
                set max [$graph axis invtransform $axis $x2]
                if {$min > $max} {
                    $graph axis configure $axis -min $max -max $min
                } else {
                    $graph axis configure $axis -min $min -max $max
                }
            }
        }
        foreach margin {yaxis y2axis} {
            foreach axis [$graph $margin use] {
                set min [$graph axis invtransform $axis $y1]
                set max [$graph axis invtransform $axis $y2]
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
        my GetCoords $x $y $ZoomInfo(corner)
        set modifier $ZoomMod
        bind select-region-$graph <${modifier}Motion> [namespace code {
            lassign [my ClampToPlot %x %y] x y
            my GetCoords $x $y B
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
        # Creates zoom box outline from A and B points coordinate saved in zoom stack.
        set graph $Subwidgets(graph)
        if {$ZoomInfo(A,x)>$ZoomInfo(B,x)} {
            set x1 [$graph xaxis invtransform $ZoomInfo(B,x)]
            set y1 [$graph yaxis invtransform $ZoomInfo(B,y)]
            set x2 [$graph xaxis invtransform $ZoomInfo(A,x)]
            set y2 [$graph yaxis invtransform $ZoomInfo(A,y)]
        } else {
            set x1 [$graph xaxis invtransform $ZoomInfo(A,x)]
            set y1 [$graph yaxis invtransform $ZoomInfo(A,y)]
            set x2 [$graph xaxis invtransform $ZoomInfo(B,x)]
            set y2 [$graph yaxis invtransform $ZoomInfo(B,y)]
        }
        set coords [list $x1 $y1 $x2 $y1 $x2 $y2 $x1 $y2 $x1 $y1]
        if {[$graph marker exists gtbZoomOutline]} {
            $graph marker configure gtbZoomOutline -coords $coords
        } else {
            set X [lindex [$graph xaxis use] 0]
            set Y [lindex [$graph yaxis use] 0]
            $graph marker create line -coords $coords -name gtbZoomOutline -mapx $X -mapy $Y\
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
