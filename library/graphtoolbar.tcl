package require argparse
package require Tk
package require rbc

namespace eval ::rbc::graphtoolbar {
    variable libDir [file dirname [file normalize [info script]]]

    variable _ruff_ns_opts {
        -heading {graphtoolbar}
        -includeprivate false
    }

    variable _ruff_preamble {
        The `graphtoolbar` command creates an Rbc graph megawidget with an optional toolbar or context-menu control
        surface and a set of coordinated interactive graph-navigation facilities.

        The graph itself is a normal Rbc `graph`, `barchart`, `stripchart`, or `polar` widget.  The megawidget adds:

        - Drag-selection zoom.
        - Mouse-wheel zoom.
        - Plot-area panning.
        - Reversible navigation history shared by zoom and pan operations.
        - Current-position and closest-element crosshair annotations.
        - Polar and Smith-chart coordinate displays.
        - Interactive linear/logarithmic axis toggling.
        - Interactive three-state legend entries.
        - PNG snapshots and PostScript output.
        - Either a permanently visible toolbar or a right-click context menu.

        The command is exported from the `::rbc` namespace and may normally be used as:
        ```tcl
        ::rbc::graphtoolbar .gtb ?option ...?
        ```

        or, after importing the Rbc commands:
        ```tcl
        namespace import ::rbc::*
        graphtoolbar .gtb ?option ...?
        ```

        ## Basic example
        The following creates a normal graph with rectangle zoom, wheel zoom, panning, crosshair annotations, active
        legend entries, and interactive axis scale selection:

        ```tcl
        package require Tk
        package require rbc

        ::rbc::graphtoolbar .gtb -width 800 -height 500 -zoom -zoomwheel -pan -crosshairs -crosshairsmode closest\
                -scaletoggle all -activelegend

        pack .gtb -fill both -expand yes
        .gtb graph configure -title {Interactive graph}
        .gtb graph element create trace -data {0 0 1 1 2 4 3 9 4 16}
        ```

        `graph` forwards the remaining arguments directly to the embedded Rbc graph widget. The graph pathname can
        alternatively be obtained with:
        ```tcl
        set graph [.gtb subwidget graph]
        ```

        ## Control surfaces
        `-controlmode toolbar`, the default, creates a visible toolbar.  Snapshot and PostScript buttons are always
        present.  Enabling `-zoom` adds **Reset view** and **Previous view** controls.  Enabling `-crosshairs` adds
        selectors for crosshair mode and closest-coordinate format.

        `-controlmode contextmenu` leaves the graph occupying the entire megawidget.  Right-clicking the graph displays\
        the corresponding controls in a popup menu:
        - **Make snapshot...**
        - **PostScript...**
        - **Reset view**, when `-zoom` is enabled.
        - **Previous view**, when `-zoom` is enabled.
        - **Crosshairs mode**, when `-crosshairs` is enabled.
        - **Closest crosshairs format**, when `-crosshairs` is enabled.

        The toolbar frame still exists in context-menu mode but is neither populated nor managed.

        ## Zooming and navigation
        With `-zoom`, rectangle zoom is performed by pressing the event selected by `-zoomstartbut`, together with
        `-zoommod`, dragging over the plot, and releasing the event selected by `-zoomendbut`.

        The defaults are:

        ```text
        start       <Any-ButtonPress-1>
        finish      <ButtonRelease-1>
        previous    <Any-ButtonPress-2>
        ```

        The finish binding intentionally does not require `-zoommod`. A selection therefore still finishes normally if
        the modifier is released before the mouse button.

        The final release position is always used as the second zoom corner, even if no final Motion event occurred.

        Dragging outside the plot area clamps the selected corner to the plot boundary.

        Each completed rectangle zoom saves the preceding limits and scale type of every axis participating in the
        displayed coordinate system.  Previous view restores the latest saved state. Reset view restores the oldest
        state and clears the navigation history.

        Automatic axis limits are saved as their actual configured value `{}`, rather than as the temporary numerical
        limits calculated from the data, so undoing a navigation operation correctly restores autoscaling.

        `-zoomtitle` briefly displays `Zoom #n` after a navigation change. `-zoommark` annotates the corners of a
        rectangle selection.

        ## Wheel zoom
        `-zoomwheel` requires `-zoom`. By default, **Control-MouseWheel** scales the graph by a factor of `1.1`.

        When the pointer is inside the plot area, every used X and Y axis is scaled around the value beneath the
        pointer. The value underneath the pointer therefore remains at the same physical screen position.

        When the pointer is over an axis instead, only that axis is scaled, around the centre of its numerical range.

        Linear axes are scaled arithmetically. Logarithmic axes are scaled in logarithmic space.

        Every wheel step is added to the same navigation history used by rectangle zoom and panning.

        ## Panning
        `-pan` requires `-zoom`.

        With the defaults, holding **Shift** and dragging **Button-1** inside the plotting area translates the visible
        coordinate system:
        ```text
        start       <Shift-ButtonPress-1>
        drag        <Motion>
        finish      <ButtonRelease-1>
        ```

        The **Shift** modifier is required only when the operation starts. Releasing **Shift** while **Button-1**
        remains held does not terminate or strand the pan.

        Panning changes axis limits without changing their scale:
        - Linear axes preserve `max-min`.
        - Logarithmic axes preserve `max/min`.

        Axis scrolling limits are also honored during panning. If an axis has `-scrollmin` or `-scrollmax` configured,
        or uses `-scrollcommand`, the effective `axis view` scrolling region bounds the pan. When the viewport reaches
        a scrolling boundary it simply stops moving; its width or logarithmic ratio is never reduced.

        If several used axes belong to the same data dimension, their scrolling constraints are combined. The most
        restrictive axis limits the common physical drag displacement, so all mapped axes remain synchronized.

        When `-scrollcommand` is configured without explicit `-scrollmin` or `-scrollmax`, Rbc uses the complete data
        range of the axis as the scrolling region.

        If the current viewport already covers that complete range, `axis view` reports the full interval `0.0 1.0`.
        The associated scrollbar thumb therefore fills the entire trough, and graphtoolbar panning is correspondingly
        unable to move the viewport because there is no remaining scrollable region.

        This normally occurs for an automatically scaled axis before any zoom has been performed. After zooming into a
        smaller portion of the data range, the scrollbar represents that smaller viewport and panning can move it
        within the complete data range.

        Applications that want to permit panning beyond the data range should define a larger scrolling world
        explicitly with `-scrollmin` and/or `-scrollmax`. For example:

        ```tcl
        $graph axis configure x -scrollmin 0 -scrollmax 12 -scrollcommand {.xbar set}
        ```

        With data occupying only part of that range, the viewport may then be panned through the additional empty
        region while preserving its scale.

        All axes actually participating in the displayed coordinate system are moved together. This includes axes
        installed in margins, hidden axes mapped by elements, and the axes mapped by a Polar or Smith grid.  Completely
        unused axes are not changed.

        Panning and zooming therefore operate correctly with multiple axes and with graph `-invertxy`.

        A completed pan creates one normal navigation-history entry. Pressing and releasing without movement creates
        no history entry.  Moving away and returning exactly to the starting position is also treated as a no-op.

        ## Cross-interactions
        The navigation facilities deliberately suspend conflicting interactions while a transient operation is active.

        During rectangle zoom:

        - active axis scale toggling is temporarily disabled;
        - current/closest crosshair marker interaction is temporarily disabled;
        - toolbar controls are temporarily disabled;
        - wheel zoom is ignored;
        - panning cannot start.

        During panning:

        - active axis scale toggling is temporarily disabled;
        - both raw crosshair lines and crosshair marker interaction are suspended;
        - existing crosshair marker graphics are removed before axis limits move;
        - toolbar controls are temporarily disabled;
        - wheel zoom is ignored;
        - rectangle zoom cannot start.

        At the end of the operation the previous interaction state is restored. Fixed-pixel crosshair annotation boxes
        are rebuilt only after the new graph transform is current, preventing their geometry from being stretched by
        the changed axis scale.

        **Button-3** has a special role in both control modes. If a rectangle zoom or pan is currently active,
        **Button-3** cancels that operation and consumes the event. In context-menu mode, **Button-3** posts the menu
        only when there is no transient navigation operation to cancel.

        Rectangle zoom and panning do not start when the initial press is on a legend drawn inside the plot area. The
        press is left for normal legend interaction instead.

        While an already-started zoom or pan passes through an internal plot-area legend, active-legend hover and click
        changes are suppressed. Normal legend interaction resumes when the transient operation finishes.

        ## Crosshairs
        `-crosshairs` enables the enhanced crosshair system. The `-crosshairsmode` property selects one of four modes:
        - `current` - show Rbc crosshair lines and annotate the current pointer coordinates.
        - `closest` - find plotted data near the pointer and annotate the selected point or points.
        - `none` - show ordinary Rbc crosshair lines without an annotation marker.
        - `disabled` - disable both the lines and the annotation marker.

        The default is `closest`.

        In closest mode, `-crosshairsclosestopts` controls the search:
        - `-interpolate yes` allows a closest point on an interpolated trace.
        - `-halo 10` sets the search halo in pixels.
        - `-single yes` displays the globally closest result. With `-single no`, each visible element is searched
          independently.
        - `-hide yes` hides the ordinary Rbc crosshair lines while closest annotations are displayed.

        Elements configured with `-hideplot yes` do not participate in closest searches.

        A closest line, strip, or Polar point is marked by a bitmap together with its text annotation. A closest bar
        uses a dimension line with arrowheads spanning the displayed bar. Very short bars use external arrowheads when
        two internal heads would overlap.

        When an element has a valid `-param` mapping, the parameter value reported by `element closest` is appended to
        the closest annotation.  Its format is controlled by `-formatparam`.

        In axis-format closest mode, an axis `-command` formatter is honoured.  If the formatter fails, the
        graphtoolbar numeric format is used as a fallback.

        Posting the context menu temporarily removes crosshair graphics.  After the menu is dismissed, the selected
        crosshair mode is restored at the pointer's current location rather than at the old popup location.

        ## Coordinate marker formats
        `-coordmark` controls current-position and rectangle-corner annotations. It accepts:
        - `auto`
        - `axis`
        - `complex`
        - `polar`
        - `gamma`
        - `normalizedimpedance`
        - `normalizedimpedanceri`
        - `normalizedadmittance`
        - `normalizedadmittanceri`

        `auto` uses normal axis coordinates for ordinary and Polar graphs. For a Smith representation it selects
        real/imaginary normalized impedance or admittance according to `-smithgrid`.

        `-coordclosestmark` controls closest-point annotation. The permitted formats depend on the graph
        representation:
        - ordinary graph, barchart, or stripchart: `axis`
        - Polar representation: `axis`, `complex`, `polar`
        - Smith representation: `axis`, `gamma`, `normalizedimpedance`, `impedance`, `normalizedadmittance`,
          `admittance`

        The toolbar combobox and context-menu submenu expose only the modes valid for the current representation.

        ## Polar and Smith interaction
        A Polar/Smith graph uses its grid-mapped axes as part of the displayed coordinate system even when those axes
        are hidden or are not installed in graph margins. They therefore participate in zooming, panning, coordinate
        display, and navigation history.

        Rectangle zoom preserves the physical plot-area aspect ratio. This keeps equal X and Y data units at equal
        physical scales and prevents Polar or Smith circles from becoming elliptical.

        When wheel zoom is performed over one of the Polar/Smith grid axes, both grid axes are scaled by the same
        factor.  Scaling only one grid axis would otherwise force the automatic-aspect layout to resize the plotting
        area.

        A visible Polar/Smith grid requires linear Cartesian grid axes. Interactive scale toggling therefore refuses
        to change a visible grid axis from linear to logarithmic. Auxiliary axes may still be logarithmic.

        Example Smith chart:

        ```tcl
        ::rbc::graphtoolbar .smith -type polar -representation smith -smithgrid impedance -controlmode contextmenu\
                -zoom -zoomwheel -pan -crosshairs -crosshairsmode closest -activelegend

        pack .smith -fill both -expand yes
        set g [.smith subwidget graph]
        ::rbc::vector create gamma -type complex
        ::rbc::vector create frequency
        gamma set {
            {0.10  0.05}
            {0.20  0.15}
            {0.30  0.10}
            {0.25 -0.05}
        }
        frequency set {1.0e9 2.0e9 3.0e9 4.0e9}
        $g element create S11 -cdata gamma -param frequency
        ```

        ## Active legend
        `-activelegend` adds hover activation and a three-state **Button-1** cycle to every legend entry:
        ```text
        normal -> active -> plot-hidden -> normal
        ```

        The persistent state is also represented by the element's legend relief:
        ```text
        flat       normal
        raised     active
        sunken     plot-hidden
        ```

        Plot-hidden elements use element `-hideplot yes`; the plotted element disappears while its legend entry remains
        available for the next click.

        ## Public megawidget access
        The most common access forms are:

        ```tcl
        # Forward directly to the Rbc graph.
        .gtb graph axis configure x -min 0 -max 10
        .gtb graph element create e1 -data {0 0 1 1}

        # Obtain a real subwidget pathname.
        set graph [.gtb subwidget graph]

        # Send a command to any named subwidget.
        .gtb widgetcommand graph configure -title Example

        # Inspect the names which exist for this particular configuration.
        puts [.gtb names]
        ```

        The exact subwidget set depends on `-controlmode` and on which optional facilities were enabled.

        ## Reserved internal names
        `graphtoolbar` installs private binding tags and graph markers in the embedded Rbc graph.

        Binding tags whose names begin with `gtb-` are reserved for `graphtoolbar`. Applications should not create,
        modify, remove, or attach their own bindings to tags using this prefix.

        Rbc graph marker names beginning with `gtb` are likewise reserved for `graphtoolbar`. Applications using the
        embedded graph should choose marker names that do not begin with `gtb`.

        The exact private names are implementation details and may change. Applications should rely only on the
        reserved prefixes, and should use the public graphtoolbar interface rather than manipulating its internal
        bindings or markers.

        Graphtoolbar-created markers are visual overlays only. They are created with `-state disabled`, which keeps
        them visible but excludes them from Rbc graph hit testing and binding dispatch. As a result, internal markers
        such as closest-point symbols, annotation text, annotation backgrounds, zoom outlines, and zoom titles do not
        become the graph's current marker and do not interfere with application element bindings.

        This is particularly important for element bindings such as:
        ```tcl
        $graph element bind all <Enter> {
            %W legend activate [%W element get current]
        }

        $graph element bind all <Leave> {
            %W legend deactivate [%W element get current]
        }
        ```

        Such bindings continue to track the underlying graph element even when a graphtoolbar marker is drawn above
        that element.

        Applications should not depend on the individual names or configuration of `gtb*` markers. Their names,
        types, number, geometry, and other options are private implementation details. The only guaranteed convention
        is that marker names beginning with `gtb` are reserved for graphtoolbar.

        ## Tk option database
        Most marker styling dictionaries take their initial defaults from the Tk option database. Applications can
        therefore customize the graphtoolbar before creating a widget:

        ```tcl
        option add *gtbCrosshairsColor navy userDefault
        option add *gtbCrosshairsTextFont {TkDefaultFont 9} userDefault
        option add *gtbZoomTextBoxFill lightyellow userDefault

        ::rbc::graphtoolbar .gtb -zoom -zoommark -crosshairs
        ```

        The package itself also installs:
        ```tcl
        option add *Element.ScaleSymbols no widgetDefault
        ```

        so graph elements default to fixed-size symbols rather than scaling their symbols with the graph.

        ### Zoom option-database resources

        #ruffopt excludedformats nroff
        | Property             | Key           | Database name               | Database class               | Default          |
        |----------------------|---------------|-----------------------------|------------------------------|------------------|
        | `zoomtitleopts`      | `-font`       | `gtbZoomTitleFont`          | `GtbZoomTitleFont`           | `{Arial 18}`     |
        | `zoomtitleopts`      | `-foreground` | `gtbZoomTitleForeground`    | `GtbZoomTitleForeground`     | `black`          |
        | `zoomtitleopts`      | `-shadow`     | `gtbZoomTitleShadow`        | `GtbZoomTitleShadow`         | `yellow4`        |
        | `zoomtitleopts`      | `-anchor`     | `gtbZoomTitleAnchor`        | `GtbZoomTitleAnchor`         | `nw`             |
        | `zoomtitleopts`      | `-coords`     | `gtbZoomTitleCoords`        | `GtbZoomTitleCoords`         | `{-Inf Inf}`     |
        | `zoomboxopts`        | `-dashes`     | `gtbZoomOutlineDashes`      | `GtbZoomOutlineDashes`       | `4`              |
        | `zoomboxopts`        | `-linewidth`  | `gtbZoomOutlineLineWidth`   | `GtbZoomOutlineLineWidth`    | `1`              |
        | `zoomboxopts`        | `-outline`    | `gtbZoomOutlineColor`       | `GtbZoomOutlineColor`        | `grey`           |
        | `zoomboxopts`        | `-xor`        | `gtbZoomOutlineXor`         | `GtbZoomOutlineXor`          | `no`             |
        | `zoommarkopts`       | `-font`       | `gtbZoomTextFont`           | `GtbZoomTextFont`            | `{ArialNarrow 8}`|
        | `zoommarkopts`       | `-anchor`     | `gtbZoomTextAnchor`         | `GtbZoomTextAnchor`          | `ne`             |
        | `zoommarkopts`       | `-foreground` | `gtbZoomTextForeground`     | `GtbZoomTextForeground`      | `black`          |
        | `zoommarkopts`       | `-justify`    | `gtbZoomTextJustify`        | `GtbZoomTextJustify`         | `left`           |
        | `zoommarkopts`       | `-padx`       | `gtbZoomTextPadX`           | `GtbZoomTextPadX`            | `4`              |
        | `zoommarkopts`       | `-pady`       | `gtbZoomTextPadY`           | `GtbZoomTextPadY`            | `4`              |
        | `zoommarkopts`       | `-formatx`    | `gtbZoomTextXFormat`        | `GtbZoomTextXFormat`         | `.4g`            |
        | `zoommarkopts`       | `-formaty`    | `gtbZoomTextYFormat`        | `GtbZoomTextYFormat`         | `.4g`            |
        | `zoommarkboxopts`    | `-fill`       | `gtbZoomTextBoxFill`        | `GtbZoomTextBoxFill`         | `#FFEB3B`        |
        | `zoommarkboxopts`    | `-outline`    | `gtbZoomTextBoxOutline`     | `GtbZoomTextBoxOutline`      | `grey`           |
        | `zoommarkboxopts`    | `-linewidth`  | `gtbZoomTextBoxLineWidth`   | `GtbZoomTextBoxLineWidth`    | `1`              |

        #ruffopt includedformats nroff
        ```text
        ┌───────────────────┬─────────────┬───────────────────────────┬────────────────────────────┬────────────────┐
        │ Property          │ Key         │ Database name             │ Database class             │ Default        │
        ├───────────────────┼─────────────┼───────────────────────────┼────────────────────────────┼────────────────┤
        │ zoomtitleopts     │ -font       │ gtbZoomTitleFont          │ GtbZoomTitleFont           │ {Arial 18}     │
        │ zoomtitleopts     │ -foreground │ gtbZoomTitleForeground    │ GtbZoomTitleForeground     │ black          │
        │ zoomtitleopts     │ -shadow     │ gtbZoomTitleShadow        │ GtbZoomTitleShadow         │ yellow4        │
        │ zoomtitleopts     │ -anchor     │ gtbZoomTitleAnchor        │ GtbZoomTitleAnchor         │ nw             │
        │ zoomtitleopts     │ -coords     │ gtbZoomTitleCoords        │ GtbZoomTitleCoords         │ {-Inf Inf}     │
        │ zoomboxopts       │ -dashes     │ gtbZoomOutlineDashes      │ GtbZoomOutlineDashes       │ 4              │
        │ zoomboxopts       │ -linewidth  │ gtbZoomOutlineLineWidth   │ GtbZoomOutlineLineWidth    │ 1              │
        │ zoomboxopts       │ -outline    │ gtbZoomOutlineColor       │ GtbZoomOutlineColor        │ grey           │
        │ zoomboxopts       │ -xor        │ gtbZoomOutlineXor         │ GtbZoomOutlineXor          │ no             │
        │ zoommarkopts      │ -font       │ gtbZoomTextFont           │ GtbZoomTextFont            │ {ArialNarrow 8}│
        │ zoommarkopts      │ -anchor     │ gtbZoomTextAnchor         │ GtbZoomTextAnchor          │ ne             │
        │ zoommarkopts      │ -foreground │ gtbZoomTextForeground     │ GtbZoomTextForeground      │ black          │
        │ zoommarkopts      │ -justify    │ gtbZoomTextJustify        │ GtbZoomTextJustify         │ left           │
        │ zoommarkopts      │ -padx       │ gtbZoomTextPadX           │ GtbZoomTextPadX            │ 4              │
        │ zoommarkopts      │ -pady       │ gtbZoomTextPadY           │ GtbZoomTextPadY            │ 4              │
        │ zoommarkopts      │ -formatx    │ gtbZoomTextXFormat        │ GtbZoomTextXFormat         │ .4g            │
        │ zoommarkopts      │ -formaty    │ gtbZoomTextYFormat        │ GtbZoomTextYFormat         │ .4g            │
        │ zoommarkboxopts   │ -fill       │ gtbZoomTextBoxFill        │ GtbZoomTextBoxFill         │ #FFEB3B        │
        │ zoommarkboxopts   │ -outline    │ gtbZoomTextBoxOutline     │ GtbZoomTextBoxOutline      │ grey           │
        │ zoommarkboxopts   │ -linewidth  │ gtbZoomTextBoxLineWidth   │ GtbZoomTextBoxLineWidth    │ 1              │
        └───────────────────┴─────────────┴───────────────────────────┴────────────────────────────┴────────────────┘
        ```
        #ruffopt excludedformats {}

        ### Crosshair option-database resources

        #ruffopt excludedformats nroff
        | Property                  | Key            | Database name                      | Database class                      | Default           |
        |---------------------------|----------------|------------------------------------|-------------------------------------|-------------------|
        | `crosshairsopts`          | `-linewidth`   | `gtbCrosshairsLineWidth`           | `GtbCrosshairsLineWidth`            | `1`               |
        | `crosshairsopts`          | `-color`       | `gtbCrosshairsColor`               | `GtbCrosshairsColor`                | `grey`            |
        | `crosshairsopts`          | `-dashes`      | `gtbCrosshairsDashes`              | `GtbCrosshairsDashes`               | `{}`              |
        | `crosshairsmarkopts`      | `-font`        | `gtbCrosshairsTextFont`            | `GtbCrosshairsTextFont`             | `{ArialNarrow 8}` |
        | `crosshairsmarkopts`      | `-anchor`      | `gtbCrosshairsTextAnchor`          | `GtbCrosshairsTextAnchor`           | `nw`              |
        | `crosshairsmarkopts`      | `-foreground`  | `gtbCrosshairsTextForeground`      | `GtbCrosshairsTextForeground`       | `black`           |
        | `crosshairsmarkopts`      | `-justify`     | `gtbCrosshairsTextJustify`         | `GtbCrosshairsTextJustify`          | `left`            |
        | `crosshairsmarkopts`      | `-padx`        | `gtbCrosshairsTextPadX`            | `GtbCrosshairsTextPadX`             | `4`               |
        | `crosshairsmarkopts`      | `-pady`        | `gtbCrosshairsTextPadY`            | `GtbCrosshairsTextPadY`             | `4`               |
        | `crosshairsmarkopts`      | `-formatx`     | `gtbCrosshairsTextXFormat`         | `GtbCrosshairsTextXFormat`          | `.4g`             |
        | `crosshairsmarkopts`      | `-formaty`     | `gtbCrosshairsTextYFormat`         | `GtbCrosshairsTextYFormat`          | `.4g`             |
        | `crosshairsmarkopts`      | `-formatparam` | `gtbCrosshairsTextParamFormat`     | `GtbCrosshairsTextParamFormat`      | `.4g`             |
        | `crosshairsmarkboxopts`   | `-fill`        | `gtbCrosshairsTextBoxFill`         | `GtbCrosshairsTextBoxFill`          | `#FFEB3B`         |
        | `crosshairsmarkboxopts`   | `-outline`     | `gtbCrosshairsTextBoxOutline`      | `GtbCrosshairsTextBoxOutline`       | `grey`            |
        | `crosshairsmarkboxopts`   | `-linewidth`   | `gtbCrosshairsTextBoxLineWidth`    | `GtbCrosshairsTextBoxLineWidth`     | `1`               |
        | `crosshairsbarlineopts`   | `-outline`     | `gtbCrosshairsBarLineOutline`      | `GtbCrosshairsBarLineOutline`       | `black`           |
        | `crosshairsbarlineopts`   | `-linewidth`   | `gtbCrosshairsBarLineWidth`        | `GtbCrosshairsBarLineWidth`         | `1`               |
        | `crosshairsbarlineopts`   | `-arrowshape`  | `gtbCrosshairsBarLineArrowShape`   | `GtbCrosshairsBarLineArrowShape`    | `{8 10 3}`        |

        #ruffopt includedformats nroff
        ```text
        ┌──────────────────────┬─────────────┬───────────────────────────────┬───────────────────────────────┬───────────────┐
        │ Property             │ Key         │ Database name                 │ Database class                │ Default       │
        ├──────────────────────┼─────────────┼───────────────────────────────┼───────────────────────────────┼───────────────┤
        │ crosshairsopts       │ -linewidth  │ gtbCrosshairsLineWidth        │ GtbCrosshairsLineWidth        │ 1             │
        │ crosshairsopts       │ -color      │ gtbCrosshairsColor            │ GtbCrosshairsColor            │ grey          │
        │ crosshairsopts       │ -dashes     │ gtbCrosshairsDashes           │ GtbCrosshairsDashes           │ {}            │
        │ crosshairsmarkopts   │ -font       │ gtbCrosshairsTextFont         │ GtbCrosshairsTextFont         │ ArialNarrow 8 │
        │ crosshairsmarkopts   │ -anchor     │ gtbCrosshairsTextAnchor       │ GtbCrosshairsTextAnchor       │ nw            │
        │ crosshairsmarkopts   │ -foreground │ gtbCrosshairsTextForeground   │ GtbCrosshairsTextForeground   │ black         │
        │ crosshairsmarkopts   │ -justify    │ gtbCrosshairsTextJustify      │ GtbCrosshairsTextJustify      │ left          │
        │ crosshairsmarkopts   │ -padx       │ gtbCrosshairsTextPadX         │ GtbCrosshairsTextPadX         │ 4             │
        │ crosshairsmarkopts   │ -pady       │ gtbCrosshairsTextPadY         │ GtbCrosshairsTextPadY         │ 4             │
        │ crosshairsmarkopts   │ -formatx    │ gtbCrosshairsTextXFormat      │ GtbCrosshairsTextXFormat      │ .4g           │
        │ crosshairsmarkopts   │ -formaty    │ gtbCrosshairsTextYFormat      │ GtbCrosshairsTextYFormat      │ .4g           │
        │ crosshairsmarkopts   │ -formatparam│ gtbCrosshairsTextParamFormat  │ GtbCrosshairsTextParamFormat  │ .4g           │
        │ crosshairsmarkboxopts│ -fill       │ gtbCrosshairsTextBoxFill      │ GtbCrosshairsTextBoxFill      │ #FFEB3B       │
        │ crosshairsmarkboxopts│ -outline    │ gtbCrosshairsTextBoxOutline   │ GtbCrosshairsTextBoxOutline   │ grey          │
        │ crosshairsmarkboxopts│ -linewidth  │ gtbCrosshairsTextBoxLineWidth │ GtbCrosshairsTextBoxLineWidth │ 1             │
        │ crosshairsbarlineopts│ -outline    │ gtbCrosshairsBarLineOutline   │ GtbCrosshairsBarLineOutline   │ black         │
        │ crosshairsbarlineopts│ -linewidth  │ gtbCrosshairsBarLineWidth     │ GtbCrosshairsBarLineWidth     │ 1             │
        │ crosshairsbarlineopts│ -arrowshape │ gtbCrosshairsBarLineArrowShape│ GtbCrosshairsBarLineArrowShape│ {8 10 3}      │
        └──────────────────────┴─────────────┴───────────────────────────────┴───────────────────────────────┴───────────────┘
        ```
        #ruffopt excludedformats {}

        `crosshairsclosestopts` has no Tk option-database resources. Its built-in defaults are:
        ```tcl
        -interpolate yes
        -halo        10
        -single      yes
        -hide        yes
        ```

        ## Styling example
        Dictionary properties may be changed after creation through the `oo::configurable` `configure` method:

        ```tcl
        .gtb configure -zoomtitle yes\
                -zoomtitleopts {-foreground navy -shadow {}}\
                -crosshairsmarkopts {-font {TkDefaultFont 9} -formatx .6g -formaty .6g -formatparam .6g}\
                -crosshairsmarkboxopts {-fill lightyellow -outline grey40}
        ```

        For the dictionary properties a later configuration containing only some recognized keys updates those keys
        while retaining the other current values. Unknown dictionary keys are rejected.
    }

    option add *Element.ScaleSymbols no widgetDefault

    # bitmap pointer default options
    option add *gtbPointerOutline black widgetDefault
    option add *gtbPointerRotate 0 widgetDefault

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
    option add *gtbZoomOutlineColor grey widgetDefault

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
    classmethod _ruffClassHook {} {
        return {
            preamble {
                Configurable TclOO class implementing the `graphtoolbar` megawidget.

                A widget is normally created through the exported class command:
                ```tcl
                ::rbc::graphtoolbar .gtb ?option ...?
                ```

                The Tk pathname becomes the megawidget command.  Properties documented below may subsequently be read
                or changed through the `oo::configurable` interface:
                ```tcl
                .gtb configure -crosshairsmode
                .gtb configure -crosshairsmode current
                .gtb configure -zoomtitle yes
                ```

                Options such as `-type`, `-controlmode`, `-zoom`, `-pan`, and `-crosshairs` select structural features
                when the megawidget is created and are not configurable properties afterward.

                Private binding tags created by the class use the reserved `gtb-` prefix. Private Rbc graph markers
                use the reserved `gtb` prefix. Applications should not use either prefix for objects they create in
                the embedded graph.
            }

            options {
                -width pixels - Initial requested width of the embedded Rbc graph. The default is `800`.
                -height pixels - Initial requested height of the embedded Rbc graph. The default is `600`.
                -controlmode mode - Selects the user-interface control surface. Must be `toolbar` or `contextmenu`. The
                 default is `toolbar`.
                -type type - Selects the embedded Rbc widget. Must be `graph`, `barchart`, `stripchart`, or
                 `polar`. The default is `graph`.
                -representation mode - For `-type polar`, selects `polar` or `smith` representation. The default is
                 `polar`. It is ignored by the other graph types.
                -smithgrid mode - For a Smith representation, selects `impedance`, `admittance`, or `both`. The default
                 is `impedance`.
                -coordmark mode - Initial value of the `-coordmark` configurable property. The default is `auto`.
                -coordclosestmark mode - Initial closest-point coordinate format. The default is `axis`. For a Smith
                 widget with crosshairs enabled, initialization selects the normalized impedance or admittance format
                 appropriate to `-smithgrid`.
                -toolbarside side - Places the visible toolbar at `bottom` or `top`. The default is `bottom`. This has
                 no visible effect in context-menu mode.
                -zoom - Enables rectangle zoom and navigation history.
                -zoomstartbut event - Event component which starts rectangle zoom. The default is `ButtonPress-1`.
                -zoomendbut event - Event component which finishes rectangle zoom. The default is
                 `ButtonRelease-1`. The finish event is intentionally not combined with `-zoommod`.
                -zoombackbut event - Event component which restores the previous view. The default is `ButtonPress-2`.
                -zoommod modifier - Tk event modifier prefix used for the zoom start and previous-view bindings. The
                 default is `Any-`.
                -zoomwheel - Enables mouse-wheel zoom. Requires `-zoom`.
                -zoomwheelscale factor - Multiplicative wheel-zoom step.  Must be a finite number greater than
                 `1.0`. The default is `1.1`.
                -zoomwheelmod modifier - Modifier prefix for MouseWheel zoom.  The default is `Control-`.
                -zoomtitle - Enables the transient `Zoom #n` marker. Requires `-zoom`.
                -zoomtitleopts dictionary - Initial `-zoomtitleopts` property. Requires `-zoom`.
                -zoomboxopts dictionary - Initial `-zoomboxopts` property. Requires `-zoom`.
                -zoommark - Displays coordinate annotations at rectangle-zoom corners. Requires `-zoom`.
                -zoommarkopts dictionary - Initial `-zoommarkopts` property. Requires `-zoom`.
                -zoommarkboxopts dictionary - Initial `-zoommarkboxopts` property. Requires `-zoom`.
                -pan - Enables plot-area panning. Requires `-zoom` because panning shares the zoom navigation history.
                -panstartbut event - Event component which starts a pan. The default is `ButtonPress-1`.
                -panendbut event - Event component which finishes a pan. The default is `ButtonRelease-1`. It is
                 intentionally not combined with `-panmod`.
                -panmod modifier - Modifier prefix required to start panning.  The default is `Shift-`.
                -crosshairs - Enables enhanced crosshair interaction.
                -crosshairsmode mode - Initial crosshair mode. Must be `current`, `closest`, `none`, or `disabled`. The
                 default is `closest`. Requires `-crosshairs`.
                -crosshairsopts dictionary - Initial Rbc crosshair line options. Requires `-crosshairs`.
                -crosshairsmarkopts dictionary - Initial text-marker and numeric-format options used by current and
                 closest annotations. Requires `-crosshairs`.
                -crosshairsmarkboxopts dictionary - Initial background-box options for crosshair annotations. Requires
                `-crosshairs`.
                -crosshairsclosestopts dictionary - Initial closest-search options. Recognized keys are `-interpolate`,
                `-halo`, `-single`, and `-hide`. Requires `-crosshairs`.
                -crosshairsbarlineopts dictionary - Initial line-marker options for closest-bar dimension
                 annotations. Requires `-crosshairs`.
                -scaletoggle axes - Enables interactive linear/logarithmic scale toggling on the specified axis
                list. `all` selects all axes existing at construction time.
                -activelegend - Enables hover activation and three-state Button-1 interaction on legend entries.
            }
            propertydescriptions {
                -crosshairsmode {
                    Selects enhanced crosshair behaviour.

                    Accepted values are `current`, `closest`, `none`, and `disabled`.

                    `current` annotates the current pointer coordinates. `closest` annotates nearby plotted\
                    data. `none` leaves only ordinary Rbc crosshair lines. `disabled` suppresses both lines and\
                    annotation markers.

                    This property is intended for an instance created with `-crosshairs`.
                }
                -coordmark {
                    Selects the coordinate representation used by current-position crosshair markers and rectangle-zoom
                    corner markers.

                    Accepted values are `auto`, `axis`, `complex`, `polar`, `gamma`, `normalizedimpedance`,
                    `normalizedimpedanceri`, `normalizedadmittance`, and `normalizedadmittanceri`.

                    `auto` uses ordinary axis values except on a Smith chart, where normalized impedance/admittance
                    real and imaginary components are selected according to the Smith grid.
                }

                -coordclosestmark {
                    Selects the value representation used for closest-point annotations.

                    The valid values depend on the current graph representation:
                    - ordinary graph/barchart/stripchart: `axis`
                    - Polar: `axis`, `complex`, `polar`
                    - Smith: `axis`, `gamma`, `normalizedimpedance`, `impedance`, `normalizedadmittance`, `admittance`

                    The toolbar and context-menu selectors are synchronized with this property.
                }
                -zoomtitle {
                    Boolean controlling the transient `Zoom #n` annotation produced after a completed navigation
                    operation.

                    This property is meaningful for instances created with `-zoom`.
                }

                -zoomtitleopts {
                    Dictionary of Rbc text-marker options used for the transient zoom-level title.

                    Recognized keys are `-font`, `-foreground`, `-shadow`, `-anchor`, and `-coords`.

                    Initial values are obtained from the corresponding `gtbZoomTitle*` Tk option-database resources.
                }

                -zoomboxopts {
                    Dictionary configuring the rectangle-zoom outline.

                    Recognized keys are `-dashes`, `-linewidth`, `color`, and `-xor`.

                    Initial values are obtained from the `gtbZoomOutline*` Tk option-database resources.
                }

                -zoommarkopts {
                    Dictionary configuring rectangle-corner text annotations.

                    Recognized keys are `-font`, `-anchor`, `-justify`, `-foreground`, `-padx`, `-pady`, `-formatx`,
                    and `-formaty`.

                    `-formatx` and `-formaty` are `format` conversion bodies such as `.4g`; the leading `%` is supplied
                    internally.

                    Initial values are obtained from the `gtbZoomText*` option-database resources.
                }

                -zoommarkboxopts {
                    Dictionary configuring the polygon drawn behind each rectangle-corner text marker.

                    Recognized keys are `-fill`, `-outline`, and `-linewidth`.

                    If both fill and outline are empty, no background polygon is drawn.

                    Initial values are obtained from the `gtbZoomTextBox*` option-database resources.
                }

                -crosshairsopts {
                    Dictionary configuring the underlying Rbc crosshair lines.

                    Recognized keys are `-linewidth`, `-color`, and `-dashes`.

                    Initial values come from `gtbCrosshairsLineWidth`, `gtbCrosshairsColor`, and `gtbCrosshairsDashes`.
                }

                -crosshairsclosestopts {
                    Dictionary controlling closest-element searches.

                    Recognized keys and defaults are:
                    ```tcl
                    -interpolate yes
                    -halo        10
                    -single      yes
                    -hide        yes
                    ```

                    `-interpolate` is forwarded to `element closest`.
                    `-halo` is the search distance in pixels.
                    `-single yes` selects one globally closest result;
                    `-single no` searches every visible element separately.
                    `-hide` controls visibility of the ordinary Rbc crosshair lines while closest annotations are
                     displayed.
                }

                -crosshairsmarkopts {
                    Dictionary configuring current/closest text annotations.

                    Recognized keys are `-font`, `-anchor`, `-justify`, `-foreground`, `-padx`, `-pady`, `-formatx`,
                    `-formaty`, and `-formatparam`.

                    The three format keys are `format` conversion bodies, for example `.4g`. `-formatparam` is used
                    when a closest element supplies `info(param)`.

                    Initial values are obtained from the `gtbCrosshairsText*` Tk option-database resources.
                }

                -crosshairsmarkboxopts {
                    Dictionary configuring the polygon background behind current and closest text annotations.

                    Recognized keys are `-fill`, `-outline`, and `-linewidth`.

                    If fill and outline are both empty, no background is drawn.

                    Initial values come from the `gtbCrosshairsTextBox*` Tk option-database resources.
                }

                -crosshairsbarlineopts {
                    Dictionary configuring the dimension line used when the closest result is a bar.

                    Recognized keys are `-outline`, `-linewidth`, and `-arrowshape`.

                    `-arrowshape` contains the three screen distances used by an Rbc line-marker arrowhead.

                    Initial values come from the `gtbCrosshairsBarLine*` Tk option-database resources.
                }
            }
        }
    }
    property crosshairsmode -set {
        classvariable CrosshairsModeNames
        if {$value ni $CrosshairsModeNames} {
            return -code error "bad crosshairs mode '$value': must be [join $CrosshairsModeNames {, }]"
        }
        set crosshairsmode $value
        set CrosshairsSelector [my CrosshairsModeLabel $value]
        if {[info exists Subwidgets(graph)] && (![info exists ContextMenuPosted] || !$ContextMenuPosted)} {
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
                                               [list -outline= -key -outline -default\
                                                        [option get $Subwidgets(graph) gtbZoomOutlineColor\
                                                                 GtbZoomOutlineColor]]\
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
        if {[info exists crosshairsopts]} {
            dict for {option optionValue} $value {
                if {[dict exists $crosshairsopts $option]} {
                    dict set crosshairsopts $option $optionValue
                } else {
                    return -code error "option with name '$option' does not exists in crosshairsopts"
                }
            }
        } else {
            set crosshairsopts\
                    [argparse -inline [list [list -linewidth= -key -linewidth -default\
                                                     [option get $Subwidgets(graph) gtbCrosshairsLineWidth\
                                                              GtbCrosshairsLineWidth]]\
                                               [list -color= -key -color -default\
                                                        [option get $Subwidgets(graph) gtbCrosshairsColor\
                                                                 GtbCrosshairsColor]]\
                                               [list -dashes= -key -dashes -default\
                                                        [option get $Subwidgets(graph) gtbCrosshairsDashes\
                                                                 GtbCrosshairsDashes]]]\
                             $value]
        }
    }
    property pointeropts -set {
        if {[info exists pointeropts]} {
            dict for {option optionValue} $value {
                if {[dict exists $pointeropts $option]} {
                    dict set pointeropts $option $optionValue
                } else {
                    return -code error "option with name '$option' does not exists in pointeropts"
                }
            }
        } else {
            set pointeropts\
                    [argparse -inline [list [list -outline= -key -outline -default\
                                                     [option get $Subwidgets(graph) gtbPointerOutline\
                                                                 GtbPointerOutline]]\
                                               [list -rotate= -key -rotate -default\
                                                        [option get $Subwidgets(graph) gtbPointerRotate\
                                                                 GtbPointerRotate]]]\
                             $value]
        }
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
            zoommarkboxopts GraphType coordmark coordclosestmark crosshairsmode PanInfo PanTransientChecks ControlMode
    variable CrosshairsSelector crosshairsmarkopts crosshairsmarkboxopts crosshairsclosestopts crosshairsopts
    variable crosshairsbarlineopts CrosshairsMarkerInfo pointeropts
    variable CrosshairsSelector ClosestCoordSelector
    variable AxisScaleInfo SavedToolbarStates
    variable ContextMenuPosted
    classmethod unknown {w args} {
        # Implements Tk-style pathname construction for the graphtoolbar class.
        #  w - requested object/widget pathname.
        #  args - graphtoolbar creation options.
        #
        # A class invocation whose first argument begins with `.` is interpreted # as creation of a megawidget with
        # that Tk pathname. Other unknown class # methods are delegated to the superclass implementation.
        #
        # Returns: Widget pathname for Tk-style construction, or the result from # the superclass unknown handler.
        if {[string match .* $w]} {
            [self] new $w {*}$args
            return $w
        }
        next $w {*}$args
    }
    constructor {args} {
        # Creates a graphtoolbar megawidget.
        #  args - widget pathname followed by creation options documented in the class Options section.
        #
        # The first argument is the Tk pathname of the new megawidget. Structural facilities such as zoom, pan,
        # crosshairs, control mode, graph type, and active legend interaction are selected here.
        #
        # Returns: The newly created megawidget object. When creation is  invoked through the exported class command
        # with a pathname, the command result is the widget pathname.
        #
        # Synopsis: path ?option ...?
        classvariable CrosshairsModes CoordClosestMarkModes
        set arguments [argparse -inline -pfirst {
            path
            {-width= -default 800}
            {-height= -default 600}
            {-controlmode= -default toolbar -enum {toolbar contextmenu}}
            {-type= -default graph -enum {graph barchart stripchart polar}}
            {-representation= -default polar -enum {polar smith}}
            {-smithgrid= -default impedance -enum {impedance admittance both}}
            {-coordmark= -default auto}
            {-coordclosestmark= -default axis}
            {-toolbarside= -default bottom -enum {bottom top}}
            {-pointeropts= -type dict -default {}}
            -zoom
            {-zoomstartbut= -default {ButtonPress-1}}
            {-zoomendbut= -default {ButtonRelease-1}}
            {-zoombackbut= -default {ButtonPress-2}}
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
        set ControlMode [dict get $arguments controlmode]
        set currentNamespace [namespace current]
        set frameName [dict get $arguments path]
        ttk::frame $frameName
        # The toolbar frame exists in both control modes.  In context-menu
        # mode it is simply not populated or managed.  Keeping the frame
        # available means general toolbar-state code does not need special
        # cases for the selected control surface.
        set Subwidgets(toolbarFrame) [ttk::frame $frameName.toolbarFr]
        set Subwidgets(graph) [::rbc::$GraphType $frameName.graph -width [dict get $arguments width]\
                                       -height [dict get $arguments height]]
        if {$GraphType eq {polar}} {
            $Subwidgets(graph) configure -representation [dict get $arguments representation]\
                    -smithgrid [dict get $arguments smithgrid]
        }
        my configure -coordmark [dict get $arguments coordmark] -coordclosestmark [dict get $arguments coordclosestmark]
        my configure -pointeropts [dict get $arguments pointeropts]
        # Layout of the graph/control surface.
        grid columnconfigure $frameName 0 -weight 1
        if {$ControlMode eq {toolbar}} {
            if {[dict get $arguments toolbarside] eq {bottom}} {
                set graphRow 0
                set toolbarRow 1
            } else {
                set graphRow 1
                set toolbarRow 0
            }
            grid $Subwidgets(graph) -row $graphRow -column 0 -sticky nsew
            grid $Subwidgets(toolbarFrame) -row $toolbarRow -column 0 -sticky ew
            grid rowconfigure $frameName $graphRow -weight 1
        } else {
            # No visible toolbar: the graph occupies the entire megawidget.
            grid $Subwidgets(graph) -row 0 -column 0 -sticky nsew
            grid rowconfigure $frameName 0 -weight 1
        }
        # Create visible toolbar controls only in toolbar mode.
        if {$ControlMode eq {toolbar}} {
            set butCount -1
            set Subwidgets(makeSnapshotBut) [ttk::button $Subwidgets(toolbarFrame).makeSnapshotBut -width 14 -image\
                                                     ::rbc::graphtoolbar::icons::makeSnapshotIcon -style Toolbutton\
                                                     -command [namespace code {my MakeSnapshot}]]
            grid $Subwidgets(makeSnapshotBut) -row 0 -column [incr butCount] -sticky ns
            set Subwidgets(postScriptDialogBut) [ttk::button $Subwidgets(toolbarFrame).postScriptDialogBut -width 14\
                                                         -image ::rbc::graphtoolbar::icons::postScriptDialogIcon\
                                                         -style Toolbutton\
                                                         -command [namespace code {my PostScriptDialog}]]
            grid $Subwidgets(postScriptDialogBut) -row 0 -column [incr butCount] -sticky ns
        }
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

            # Previous-view/reset is middle-button by default.
            my EnableZoom [dict get $arguments zoomstartbut] [dict get $arguments zoomendbut]\
                    [dict get $arguments zoombackbut]
            if {[dict exists $arguments pan]} {
                my EnablePan [dict get $arguments panstartbut] [dict get $arguments panendbut]\
                        [dict get $arguments panmod]
            }
            if {[dict exists $arguments zoomwheel]} {
                my EnableWheelZoom [dict get $arguments zoomwheelmod] [dict get $arguments zoomwheelscale]
            }
            if {$ControlMode eq {toolbar}} {
                set Subwidgets(resetZoomBut) [ttk::button $Subwidgets(toolbarFrame).resetZoomBut -width 14\
                                                      -image ::rbc::graphtoolbar::icons::resetZoomIcon\
                                                      -command [namespace code {my ResetAllZoom}] -style Toolbutton]
                grid $Subwidgets(resetZoomBut) -row 0 -column [incr butCount] -sticky ns
                set Subwidgets(revertZoomBut) [ttk::button $Subwidgets(toolbarFrame).revertZoomBut -width 14\
                                                       -image ::rbc::graphtoolbar::icons::revertZoomIcon\
                                                       -command [namespace code {my ResetZoom}] -style Toolbutton]
                grid $Subwidgets(revertZoomBut) -row 0 -column [incr butCount] -sticky ns
            }
        }

        ##### crosshairs activation
        if {[dict exists $arguments crosshairs]} {
            # Smith representation has a semantic default for the closest coordinate display.
            if {$GraphType eq {polar}} {
                if {[dict get $arguments representation] eq {smith}} {
                    if {[dict get $arguments smithgrid] eq {impedance}} {
                        my configure -coordclosestmark normalizedimpedance
                    } elseif {
                        [dict get $arguments smithgrid] eq {admittance}
                    } {
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
            # The actual crosshair behaviour exists independently of its
            # control surface. Only create the label/combobox widgets when
            # the toolbar is visible.
            if {$ControlMode eq {toolbar}} {
                set crosshairsModeWidths [list]
                foreach label [my CrosshairsModeLabels] {
                    lappend crosshairsModeWidths [string length $label]
                }
                set Subwidgets(crosshairsModeLabel)\
                    [ttk::label $Subwidgets(toolbarFrame).crosshairsModeLabel -text {Crosshairs mode}]
                grid $Subwidgets(crosshairsModeLabel) -row 0 -column [incr butCount] -sticky e -padx {6 2}
                set Subwidgets(crosshairsComBox)\
                        [ttk::combobox $Subwidgets(toolbarFrame).crosshairsComBox -values [my CrosshairsModeLabels]\
                                 -width [expr {[::tcl::mathfunc::max {*}$crosshairsModeWidths]}]\
                                 -textvariable [self namespace]::CrosshairsSelector -state readonly\
                                 -postcommand [namespace code {my UpdateCrosshairsModes}]]
                bind $Subwidgets(crosshairsComBox) <<ComboboxSelected>> [namespace code {my SelectCrosshairsMode}]
                grid $Subwidgets(crosshairsComBox) -row 0 -column [incr butCount] -sticky ns
                set closestCoordWidths [list]
                foreach mode $CoordClosestMarkModes {
                    lappend closestCoordWidths [string length $mode]
                }
                set Subwidgets(closestCoordLabel) [ttk::label $Subwidgets(toolbarFrame).closestCoordLabel\
                                                           -text {Closest crosshairs format:}]
                grid $Subwidgets(closestCoordLabel) -row 0 -column [incr butCount] -sticky e -padx {6 2}
                set Subwidgets(closestCoordComBox)\
                        [ttk::combobox $Subwidgets(toolbarFrame).closestCoordComBox\
                                 -values [my ClosestCoordinateLabels]\
                                 -width [expr {[::tcl::mathfunc::max {*}$closestCoordWidths]}]\
                                 -textvariable [self namespace]::ClosestCoordSelector -state disabled\
                                 -postcommand [namespace code {my UpdateClosestCoordinateModes}]]
                bind $Subwidgets(closestCoordComBox) <<ComboboxSelected>>\
                        [namespace code {my SelectClosestCoordinateMode}]
                grid $Subwidgets(closestCoordComBox) -row 0 -column [incr butCount] -sticky ns
            }
            # These methods are valid in both toolbar and context-menu modes.
            # They already tolerate the toolbar comboboxes not existing.
            my UpdateCrosshairsModes
            my UpdateClosestCoordinateModes
            my ApplyCrosshairsMode
        }
        ##### axes scale-toggle activation
        if {[dict exists $arguments scaletoggle]} {
            my setAxisActiveScale [dict get $arguments scaletoggle]
        }
        ##### active legend activation
        if {[dict exists $arguments activelegend]} {
            my EnableActiveLegend
        }
        ##### context-menu control surface
        if {$ControlMode eq {contextmenu}} {
            my CreateContextMenu [dict exists $arguments zoom] [dict exists $arguments crosshairs]
        }
        # Right-click is installed in BOTH modes.
        #
        # During a transient zoom/pan operation it cancels that operation
        # and consumes the event.  Otherwise, in context-menu mode, it posts
        # the context menu.
        my EnableRightClick
        # Finish construction of the megawidget command.
        rename ::$frameName ::$frameName.fr
        rename [self] ::$frameName

        bind $frameName <Destroy> +[list [self] destroy]
    }
    destructor {
        # Cancels asynchronous operations owned by the megawidget.
        #
        # Currently this includes the transient zoom-title timer.
        foreach timer {titleTimer} {
            if {[info exists ZoomInfo($timer)]} {
                after cancel $ZoomInfo($timer)
            }
        }
    }
    method names {} {
        # Returns the names of all subwidgets created for this instance.
        #
        # The set depends on -controlmode and on which optional features were enabled at construction time. `graph`
        # and `toolbarFrame` always # exist.
        #
        # Returns: List of subwidget names.
        foreach widget [array names Subwidgets] {
            lappend names $widget
        }
        return $names
    }
    method subwidget {name} {
        # Returns the Tk pathname of a named subwidget.
        #  name - subwidget name as returned by [names].
        #
        # Common names include `graph`, `toolbarFrame`, and, in context-menu mode, `contextMenu`. Toolbar mode may
        # additionally create snapshot, PostScript, zoom, and crosshair control widgets.
        #
        # Returns: Tk pathname of the requested subwidget.
        if {[info exists Subwidgets($name)]} {
            return $Subwidgets($name)   
        } else {
            return -code error "Widget with name '$name' does not exist in a list of availible subwidgets"
        }
    }
    method graph {args} {
        # Forwards a command directly to the embedded Rbc graph widget.
        #  args - graph widget command and arguments.
        #
        # For example:
        # ```tcl
        # .gtb graph axis configure x -min 0 -max 10
        # .gtb graph element create trace -data {0 0 1 1 2 4}
        # ```
        #
        # Returns: Result returned by the embedded graph command.
        # Synopsis: ?graphCommand ...?
        tailcall $Subwidgets(graph) {*}$args
    }
    method widgetcommand {args} {
        # Invokes a command on a named graphtoolbar subwidget.
        #  args - subwidget name followed by its command and arguments.
        #
        # For example:
        # ```tcl
        # .gtb widgetcommand graph configure -title Example
        # ```
        #
        # Returns: Result of the subwidget command.
        # Synopsis: subwidgetName ?command ...?
        set widget [lindex $args 0]
        if {![info exists Subwidgets($widget)]} {
            return -code error "Widget with name '$widget' does not exist in a list of availible subwidgets"
        }
        [set Subwidgets($widget)] {*}[lrange $args 1 end]
    }

    #### general private methods
    method CreateContextMenu {hasZoom hasCrosshairs} {
        # Creates the right-click control menu for context-menu mode.
        #  hasZoom - true when zoom/navigation controls were enabled.
        #  hasCrosshairs - true when enhanced crosshair controls were enabled.
        #
        # The menu always contains snapshot and PostScript commands. Zoom and # crosshair-related entries are added
        # only when the corresponding # facilities were enabled at construction time.
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        set menu $graph.contextMenu
        menu $menu -tearoff no
        set Subwidgets(contextMenu) $menu
        bind $menu <Unmap> [namespace code {
            after idle [namespace code {
                my RestoreContextMenuCrosshairs
            }]
        }]
        $menu add command -label {Make snapshot...}  -command [namespace code {my MakeSnapshot}]
        $menu add command -label {PostScript...}  -command [namespace code {my PostScriptDialog}]
        if {$hasZoom} {
            $menu add separator
            $menu add command -label {Reset view} -command [namespace code {my ResetAllZoom}]
            $menu add command -label {Previous view} -command [namespace code {my ResetZoom}]
        }
        if {$hasCrosshairs} {
            $menu add separator
            set crossMenu $menu.crosshairs
            menu $crossMenu -tearoff no
            foreach mode [my CrosshairsModes] {
                set label [my CrosshairsModeLabel $mode]
                $crossMenu add radiobutton -label $label -variable [self namespace]::CrosshairsSelector -value $label\
                        -command [namespace code {my SelectCrosshairsMode}]
            }
            $menu add cascade -label {Crosshairs mode} -menu $crossMenu
            set closestMenu $menu.closest
            menu $closestMenu -tearoff no -postcommand [namespace code {my UpdateClosestContextMenu}]
            $menu add cascade -label {Closest crosshairs format} -menu $closestMenu
        }
    }
    method AddBitmapPoint {name xValue yValue {mapx {}} {mapy {}}} {
        # Creates a pointer bitmap marker at a graph-coordinate position.
        #  name - marker name.
        #  xValue - X coordinate.
        #  yValue - Y coordinate.
        #  mapx - optional X axis mapping.
        #  mapy - optional Y axis mapping.
        #
        # The bitmap and mask are loaded from the graphtoolbar library directory.
        #
        # Returns: Nothing.
        set mapopts [list]
        if {$mapx ne {}} {
            lappend mapopts -mapx $mapx
        }
        if {$mapy ne {}} {
            lappend mapopts -mapy $mapy
        }
        $Subwidgets(graph) marker create bitmap -name $name -coords [list $xValue $yValue]\
                -bitmap "@[file join $::rbc::graphtoolbar::libDir pointer.xbm]"\
                -mask "@[file join $::rbc::graphtoolbar::libDir pointer_mask.xbm]" -under no\
                -state disabled {*}$mapopts {*}[my configure -pointeropts]
    }
    method BindTagName {name} {
        # Returns the private graphtoolbar bindtag for one interaction.
        #  name - interaction-specific tag suffix.
        #
        # Every bindtag created by graphtoolbar uses the reserved `gtb-` prefix followed by the embedded graph
        # pathname. Applications should not use this prefix for their own binding tags.
        #
        # Returns: Private bindtag name.
        return "gtb-${name}-$Subwidgets(graph)"
    }
    method AddBindTag {widget tag {after {}}} {
        # Adds or repositions a bindtag on a widget.
        #  widget - Tk widget pathname.
        #  tag - bindtag to install.
        #  after - optional existing tag after which the new tag is inserted.
        #
        # An existing occurrence of `tag` is removed first. With no `after` argument, the tag is placed first so
        # graphtoolbar interaction sees the event before ordinary widget bindings.
        #
        # Returns: Nothing.
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
        # Removes a bindtag from a widget if present.
        #  widget - Tk widget pathname.
        #  tag - bindtag to remove.
        #
        # Returns: Nothing.
        set oldTagList [bindtags $widget]
        set index [lsearch $oldTagList $tag]
        if {$index >= 0} {
            bindtags $widget [lreplace $oldTagList $index $index]
        }
    }
    method CheckBindTagExistence {widget tag} {
        # Tests whether a widget currently contains a bindtag.
        #  widget - Tk widget pathname.
        #  tag - bindtag to test.
        #
        # Returns: Boolean value.
        if {[lsearch -exact [bindtags $widget] $tag]<0} {
            return false
        } else {
            return true
        }
    }
    method ChangeToolbarState {action} {
        # Temporarily disables or restores toolbar controls.
        #  action - `disable` or `restore`.
        #
        # Original states are retained individually so read-only comboboxes and other controls return to their exact
        # preceding state.
        #
        # In context-menu mode the toolbar frame has no children, so this method is intentionally a harmless no-op.
        #
        # Returns: Nothing.
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
        # Calculates the physical size of a text annotation.
        #  text - possibly multiline text.
        #  options - text-marker option dictionary containing `-font`, `-padx`, and `-pady`.
        #
        # Width is the maximum measured width of any line plus horizontal padding on both sides. Height is the font
        # line space multiplied by the number of lines plus vertical padding on both sides.
        #
        # Tk screen-distance values in `-padx` and `-pady` are converted to physical pixels relative to the graph
        # display.
        #
        # Returns: Two-element list `{width height}` in physical widget pixels.
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
        # Calculates the physical bounding box of a text annotation.
        #  x - physical widget X coordinate of the text-marker position.
        #  y - physical widget Y coordinate of the text-marker position.
        #  text - annotation text.
        #  options - text-marker option dictionary.
        #  anchor - optional anchor override; an empty value uses `-anchor` from `options`.
        #
        # Text dimensions include the marker's horizontal and vertical padding. The returned right and bottom
        # coordinates follow RBC text-marker inclusive extents:
        # ```text
        # right  = left + width  - 1
        # bottom = top  + height - 1
        # ```
        #
        # Returns: Four-element list `{left top right bottom}` in physical widget pixels.
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
        # Offsets a text marker away from its reference point.
        #  x - physical widget X coordinate of the reference point.
        #  y - physical widget Y coordinate of the reference point.
        #  anchor - text-marker anchor.
        #  offset - separation in physical pixels; the default is `3`.
        #
        # The marker is moved in the direction in which the selected anchor causes the text to extend. A `center`
        # anchor has no natural displacement and is # left unchanged.
        #
        # Returns: Two-element list `{x y}` containing the shifted text-marker position.
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
        # Selects an annotation anchor which keeps the complete text inside the plotting area.
        #  x - physical widget X coordinate of the annotation reference point.
        #  y - physical widget Y coordinate of the annotation reference point.
        #  text - annotation text.
        #  options - text-marker option dictionary containing the preferred `-anchor`, font, and padding values.
        #
        # The preferred anchor is tried first, followed by alternative corner, edge, and centre anchors. Each
        # candidate is evaluated at its actual offset text position.
        #
        # If no candidate fits completely inside the plotting area, the preferred anchor is returned.
        #
        # Returns: Selected Tk/Rbc anchor name.
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
        # Creates or updates a fixed-pixel background behind a text annotation.
        #  name - polygon-marker name.
        #  x - physical widget X position of the text marker.
        #  y - physical widget Y position of the text marker.
        #  text - text whose pixel extents determine the background.
        #  textOptions - text-marker option dictionary.
        #  boxOptions - polygon-marker option dictionary.
        #  mapx - X axis through which pixel positions are converted.
        #  mapy - Y axis through which pixel positions are converted.
        #
        # RBC markers are represented in graph coordinates, while the annotation # box must retain a constant physical
        # size. The calculated pixel corners are therefore transformed back through the selected axes each time the 
        # box is drawn.
        #
        # If both `-fill` and `-outline` are empty, an existing box is removed.
        #
        # Returns: Nothing.
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
                    -state disabled {*}$boxOptions
        }
    }
    method UsedAxes {dimension} {
        # Returns every axis participating in the displayed graph for one data dimension.
        #  dimension - data dimension, `x` or `y`.
        #
        # An axis is considered used when it is:
        # - installed in the corresponding graph margin;
        # - mapped by an element; or
        # - for a Polar/Smith graph, mapped by the specialized grid.
        #
        # Hidden element-only axes and hidden grid axes therefore still participate in navigation operations because
        # they remain part of the displayed coordinate system. Completely unused axes are excluded.
        #
        # Duplicate names are removed while preserving discovery order.
        #
        # Returns: Ordered list of axis names.
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
        # Returns axes which directly control the Polar/Smith grid.
        #
        # This differs from [UsedAxes]: only the axes mapped through the grid's `-mapx` and `-mapy` options are
        # returned. Margin-only, element-only, and other auxiliary axes are excluded.
        #
        # Duplicate X/Y mappings are returned only once.
        #
        # For non-Polar graph types, the result is empty.
        #
        # Returns: List of Polar/Smith grid axis names.
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
        # Returns visible used axes for one data dimension.
        #  dimension - data dimension, `x` or `y`.
        #
        # The ordering from [UsedAxes] is preserved. Axes configured with `-hide yes` are omitted.
        #
        # Returns: Ordered list of visible axis names.
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
        # Returns axes used for current-coordinate interaction.
        #  dimension - data dimension, `x` or `y`.
        #
        # For an ordinary graph this is the set of visible used axes for the requested dimension.
        #
        # For a Polar/Smith graph, the axis mapped to the specialized grid is also included even when that axis is
        # hidden or is not installed in a graph margin. This ensures semantic coordinate overlays can always refer to 
        # the coordinate system actually displayed by the grid.
        #
        # Returns: Ordered list of axis names.
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
        # Converts physical widget coordinates to X/Y data-dimension pixels.
        #  x - physical widget X coordinate.
        #  y - physical widget Y coordinate.
        #
        # With graph `-invertxy no`, the coordinates are returned unchanged. With `-invertxy yes`, physical X/Y are
        # exchanged so `xPixel` always follows the X data dimension and `yPixel` always follows the Y data dimension.
        #
        # Returns: Two-element list `{xPixel yPixel}`.
        if {[$Subwidgets(graph) cget -invertxy]} {
            return [list $y $x]
        }
        return [list $x $y]
    }
    method AxisPixelsToWidget {xPixel yPixel} {
        # Converts X/Y data-dimension pixel coordinates to physical widget coordinates.
        #  xPixel - pixel coordinate along the X data dimension.
        #  yPixel - pixel coordinate along the Y data dimension.
        #
        # With graph `-invertxy no`, the coordinates are returned unchanged. With `-invertxy yes`, the two physical
        # dimensions are exchanged.
        #
        # Returns: Two-element list `{x y}` in physical widget coordinates.
        if {[$Subwidgets(graph) cget -invertxy]} {
            return [list $yPixel $xPixel]
        }
        return [list $xPixel $yPixel]
    }
    method WidgetToAxisValues {x y mapx mapy} {
        # Converts physical widget coordinates to values on named axes.
        #  x - physical widget X coordinate.
        #  y - physical widget Y coordinate.
        #  mapx - X data axis.
        #  mapy - Y data axis.
        #
        # Handles graph `-invertxy` and rounds the resulting axis-pixel coordinates because RBC `axis invtransform`
        # requires integer pixels.
        #
        # Returns: Two-element list `{xValue yValue}`.
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
        # Returns physical orientations of the X and Y data dimensions.
        #
        # Returns: `{h v}` for a normal graph and `{v h}` when `-invertxy` is enabled.
        if {[$Subwidgets(graph) cget -invertxy]} {
            return {v h}
        }
        return {h v}
    }
    method FormatRealImagMarkerValue {value formatReal formatImag} {
        # Formats real and imaginary components as two labelled marker lines.
        #  value - two-element `{real imag}` list.
        #  formatReal - Tcl `format` conversion body for the real component.
        #  formatImag - Tcl `format` conversion body for the imaginary component.
        #
        # Labels include the physical data orientation, for example `real(h)` and
        # `imag(v)`. Textual negative zero is suppressed.
        #
        # Returns: Formatted marker text.
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
        # Builds current-coordinate text from all interaction axes.
        #  xPixel - pixel coordinate along the X data dimension.
        #  yPixel - pixel coordinate along the Y data dimension.
        #  formatx - X value format body.
        #  formaty - Y value format body.
        #
        # Ordinary graphs report visible axes. Polar/Smith graphs additionally include specialized grid axes, even
        # when those axes are hidden.
        #
        # Returns: Dictionary containing `text`, `mapx`, `mapy`, `xValue`, and `yValue`.
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
        # Returns the semantic name used for an axis in coordinate annotations.
        #  axis - Rbc axis name.
        #  dimension - `x` or `y`.
        #
        # Polar/Smith grid axes are displayed as `real` and `imag`; auxiliary axes retain their actual Rbc axis names.
        #
        # Returns: Display name.
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
        # Formats a complex value in `a+jb` form.
        #  name - quantity name displayed before `=`.
        #  value - two-element `{real imag}` list.
        #  formatReal - real-component format body.
        #  formatImag - imaginary-component format body.
        #  unit - optional unit appended to the result.
        #
        # Returns: Formatted marker text.
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
        # Converts Cartesian coordinates to radius and angle.
        #  xValue - Cartesian X component.
        #  yValue - Cartesian Y component.
        #
        # The angle is returned in degrees in the range `[0,360)`. The zero vector is assigned angle zero.
        #
        # Returns: `{radius angle}`.
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
        # Formats radius and angle as a two-line Polar annotation.
        #  radius - radius value.
        #  angle - angle in degrees.
        #  formatRadius - radius format body.
        #  formatAngle - angle format body.
        #
        # Returns: Formatted marker text.
        set radiusText [format "%$formatRadius" $radius]
        set angleText [format "%$formatAngle" $angle]
        return "r=$radiusText\nangle=$angleText deg"
    }
    method SmithGammaToNormalized {gammaReal gammaImag mode} {
        # Converts reflection coefficient Gamma to normalized impedance or normalized admittance.
        #  gammaReal - real component of Gamma.
        #  gammaImag - imaginary component of Gamma.
        #  mode - `normalizedimpedance` or `normalizedadmittance`.
        #
        # Uses:
        #
        #       1 + Gamma                 1 - Gamma
        #   z = ---------             y = ---------
        #       1 - Gamma                 1 + Gamma
        #
        # Open-circuit singularities are represented using `Inf`.
        #
        # Returns: `{real imag}`.
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
        # Builds coordinate-marker data for a selected semantic representation.
        #  xPixel - pixel coordinate along the X data dimension.
        #  yPixel - pixel coordinate along the Y data dimension.
        #  formatx - first-component format body.
        #  formaty - second-component format body.
        #  mode - coordinate representation.
        #
        # `auto` is first resolved through [EffectiveCoordinateMode]. Unsupported or unavailable semantic mappings
        # fall back to ordinary axis values.
        #
        # Returns: Dictionary containing marker text, mapped axes, and graph coordinates.
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
    #### context-menu methods
    method CancelTransientOperation {x y} {
        # Cancels an unfinished modal graph operation.
        #  x - physical widget X coordinate of the cancellation event.
        #  y - physical widget Y coordinate of the cancellation event.
        #
        # Panning takes precedence over rectangle zoom. An idle navigation history state is deliberately not popped
        # by this method.
        #
        # Returns: True if an operation was cancelled, otherwise false.

        # Panning is modal from ButtonPress until ButtonRelease.
        if {[info exists PanInfo(active)] && $PanInfo(active)} {
            my CancelPan $x $y
            return true
        }
        # Zoom corner B means a box selection is currently in progress.
        if {[info exists ZoomInfo(corner)] && ($ZoomInfo(corner) eq {B})} {
            my ResetZoom $x $y
            return true
        }
        return false
    }
    method EnableRightClick {} {
        # Installs graphtoolbar Button-3 dispatch on the embedded graph.
        #
        # An active pan or rectangle zoom is cancelled and consumes the first Button-3 event. When idle, context-menu
        # mode posts the control menu; toolbar mode leaves Button-3 otherwise unused.
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        bind [my BindTagName rightclick] <ButtonPress-3> [namespace code {
            if {[my CancelTransientOperation %x %y]} {
                break
            }
            if {$ControlMode eq {contextmenu}} {
                my PostContextMenu %x %y %X %Y
                break
            }
        }]
        my AddBindTag $graph [my BindTagName rightclick]
    }
    method PostContextMenu {x y rootX rootY} {
        # Posts the right-click control menu.
        #  x - graph-local pointer X coordinate.
        #  y - graph-local pointer Y coordinate.
        #  rootX - root-window pointer X coordinate.
        #  rootY - root-window pointer Y coordinate.
        #
        # Crosshair marker graphics and XOR hairs are suspended while the popup is active because posting a Tk menu
        # does not provide the graph with the normal Leave/Enter sequence.
        #
        # Windows `tk_popup` is synchronous, while X11 requires restoration after the menu Unmap event. Both paths
        # eventually call [RestoreContextMenuCrosshairs].
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        set menu $Subwidgets(contextMenu)
        set ContextMenuPosted true
        # A popup menu does not give the graph its normal <Leave>
        # handling, so explicitly suspend all crosshair graphics.
        if {[info exists crosshairsopts]} {
            my DeleteCrosshairsMarkers
            $graph crosshairs off
            # Do not let the old crosshair mode process Enter/Motion events
            # while the popup is being dismissed.  The selected mode is
            # rebuilt by RestoreContextMenuCrosshairs.
            my RemoveBindTag $graph [my BindTagName crosshairs-marker]
            my RemoveBindTag $graph [my BindTagName crosshairs]
        }
        my UpdateContextMenu
        if {[tk windowingsystem] eq {win32}} {
            # On Windows tk_popup is synchronous.  It returns only after
            # the popup has disappeared, so this is the reliable place to
            # restore the crosshair state.
            tk_popup $menu $rootX $rootY
            set ContextMenuPosted false
            my RestoreContextMenuCrosshairs
        } else {
            # On X11 tk_popup returns immediately.  There we need to wait
            # for the menu to be unmapped.
            bind $menu <Unmap> [namespace code {
                bind %W <Unmap> {}
                after idle [namespace code {
                    set ContextMenuPosted false
                    my RestoreContextMenuCrosshairs
                }]
            }]
            tk_popup $menu $rootX $rootY
        }
    }
    method RestoreContextMenuCrosshairs {} {
        # Restores enhanced crosshairs after the context menu is dismissed.
        #
        # The current physical pointer position is converted back to graph-local coordinates. The RBC hotspot is
        # updated while the XOR hairs are hidden, then the selected enhanced mode and any text marker are rebuilt.
        #
        # If crosshairs were not enabled for this instance, or the pointer is no longer over the graph, no annotation
        # is recreated.
        #
        # Returns: Nothing.
        if {![info exists crosshairsopts]} {
            return
        }
        set graph $Subwidgets(graph)
        # At this point the popup really is gone. Query the pointer now.
        set rootX [winfo pointerx $graph]
        set rootY [winfo pointery $graph]
        set x [expr {$rootX-[winfo rootx $graph]}]
        set y [expr {$rootY-[winfo rooty $graph]}]
        # The pointer may no longer be over the graph.
        if {[winfo containing $rootX $rootY] ne $graph} {
            $graph crosshairs off
            my DeleteCrosshairsMarkers
            return
        }
        # Keep the hairs hidden while updating RBC's stored hotspot.
        $graph crosshairs off
        $graph crosshairs configure -position @${x},$y
        # Restore whatever mode is currently selected.  If that mode uses
        # visible RBC hairs they are now first drawn at the NEW hotspot.
        my ApplyCrosshairsMode
        # Recreate current/closest marker information at exactly the same
        # location.
        my RefreshCrosshairsMarker $x $y
    }

    #### axes toggle methods
    method ToggleAxisScale {graph} {
        # Toggles the scale type of the currently active axis.
        #  graph - embedded Rbc graph pathname.
        #
        # A visible Polar/Smith grid axis cannot be switched from linear to logarithmic because the specialized grid
        # requires linear Cartesian axes. The reverse transition is permitted.
        #
        # Returns: Nothing.
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
        # Configures interactive linear/logarithmic scale toggling for axes.
        #  axes - list of axis names, or `all` for every current graph axis.
        #  -disabled - removes interactive scale toggling from the selected axes instead of enabling it.
        #  -event event - event which toggles the active axis. The default is `ButtonPress-1`.
        #  -background color - temporary axis background displayed while the pointer is over the active axis
        #   line. The default is `grey`.
        #
        # Only the actual axis line responds to the toggle event.
        #
        # A visible Polar/Smith grid axis cannot be toggled from linear to logarithmic because the specialized grid
        # requires linear Cartesian axes. Logarithmic-to-linear conversion is still allowed so an externally
        # misconfigured grid can be recovered.
        #
        # Rectangle zoom and panning temporarily disable this interaction and # restore it when the transient operation
        # finishes.
        #
        # Returns: Nothing.
        # Synopsis: axes ?-disabled? ?-event event? ?-background color?
        argparse -pfirst {
            {axes -type list}
            {-disabled -boolean}
            {-event= -default {ButtonPress-1}}
            {-background= -default grey}
        }
        set graph $Subwidgets(graph)
        set tag [my BindTagName active-axis]
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
        # Returns axes currently enabled for interactive scale toggling.
        #
        # Returns: List of axis names carrying the graphtoolbar active-scale binding tag.
        set graph $Subwidgets(graph)
        set tag [my BindTagName active-axis]
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
        # Enables interactive legend-element state control.
        #
        # Pointer entry temporarily activates the element under the legend entry and pointer leave removes that
        # transient activation.
        #
        # Button-1 advances the element through three persistent states:
        #
        # ```text
        # normal -> active -> plot-hidden -> normal
        # ```
        #
        # The corresponding legend reliefs are:
        #
        # ```text
        # flat       normal
        # raised     active
        # sunken     plot-hidden
        # ```
        #
        # Plot-hidden state uses element `-hideplot yes`, keeping the legend entry available while suppressing the
        # element from the plot.
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        $graph legend bind all <Enter> [namespace code {my ActivateLegend}]
        $graph legend bind all <Leave> [namespace code {my DeactivateLegend}]
        $graph legend bind all <ButtonPress-1> [namespace code {my ToggleLegendElement}]
    }
    method LegendInteractionSuppressed {} {
        # Tests whether active-legend interaction is temporarily suppressed.
        #
        # Returns: True during rectangle selection or panning.
        return [expr {([info exists ZoomInfo(corner)] && ($ZoomInfo(corner) eq {B})) ||\
                              ([info exists PanInfo(active)] && $PanInfo(active))}]
    }
    method ActivateLegend {} {
        # Activates the element underneath the current legend entry on pointer # entry unless another modal graph
        # interaction is active.
        #
        # Returns: Nothing.
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
        # Removes transient legend activation from the current legend entry.
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        set elem [$graph legend get current]
        if {$elem ne {}} {
            $graph legend deactivate $elem
        }
    }
    method ToggleLegendElement {} {
        # Advances the current legend element through its three persistent states.
        #
        # The sequence is:
        #
        #     normal -> active -> plot-hidden -> normal
        #
        # `-labelrelief` records normal/active/hidden visual state and `-hideplot`
        # suppresses plotting while preserving the legend entry.
        #
        # Returns: Nothing.
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
        # Tests whether a crosshair annotation can be rebuilt at a position.
        #  x - graph-local pointer X coordinate.
        #  y - graph-local pointer Y coordinate.
        #
        # Marker state and the marker bindtag must both exist, coordinates must be supplied, and the position must be
        # inside the plot area.
        #
        # Returns: Boolean value.
        set graph $Subwidgets(graph)
        if {![info exists CrosshairsMarkerInfo]} {
            return false
        }
        if {![my CheckBindTagExistence $graph [my BindTagName crosshairs-marker]]} {
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
        # Deletes every private graphtoolbar crosshair marker.
        #
        # All markers owned by the enhanced crosshair implementation use the reserved `gtbCrosshairs`
        # prefix. Application markers are therefore never selected by this cleanup operation.
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        set markerNames [$graph marker names gtbCrosshairs*]
        if {[llength $markerNames]} {
            $graph marker delete {*}$markerNames
        }
    }
    method RefreshCrosshairsMarker {x y} {
        # Rebuilds the current/closest annotation at a graph-local pointer position.
        #  x - pointer X coordinate.
        #  y - pointer Y coordinate.
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        if {![my CanRefreshCrosshairsMarker $x $y]} {
            return
        }
        my DeleteCrosshairsMarkers
        my CrosshairsMarkerMotion $graph $x $y {*}$CrosshairsMarkerInfo
    }
    method CrosshairsMarkerMotion {graph x y options mode interpolate halo single} {
        # Implements Motion handling for current and closest annotation markers.
        #  graph - graph pathname.
        #  x - physical pointer X coordinate.
        #  y - physical pointer Y coordinate.
        #  options - crosshair text-marker option dictionary.
        #  mode - `current` or `closest`.
        #  interpolate - closest-search interpolation flag.
        #  halo - closest-search halo in pixels.
        #  single - whether to report one globally closest element.
        #
        # Current mode formats the pointer coordinates. Closest mode searches either globally or once per visible
        # element and selects point/bar annotation rendering as appropriate.
        #
        # Returns: Nothing.
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
            my DrawTextBackground gtbCrosshairsTextBox $textX $textY $text $options $boxOptions $mapx $mapy
            lassign [my WidgetToAxisValues $textX $textY $mapx $mapy] textXValue textYValue
            $graph marker create text -name gtbCrosshairsText -text $text -coords [list $textXValue $textYValue] -mapx\
                    $mapx -mapy $mapy -state disabled {*}[dict remove $options -formatx -formaty -formatparam]
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
                    my CreateClosestBarMarker gtbCrosshairsClosestText $element $pointVar(x) $pointVar(y)\
                            $pointVar(left) $pointVar(top) $pointVar(right) $pointVar(bottom) $options $boxOptions\
                            $closestInfo
                } else {
                    my CreateClosestMarker $graph gtbCrosshairsClosestText gtbCrosshairsClosestBitmap $element\
                            $pointVar(x) $pointVar(y) $options $closestInfo
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
            set marker gtbCrosshairsClosestText$i

            if {([$graph element type $element] eq {BarElement}) && [info exists pointVar(left)] &&\
                        [info exists pointVar(top)] && [info exists pointVar(right)] &&\
                        [info exists pointVar(bottom)]} {
                my CreateClosestBarMarker $marker $element $pointVar(x) $pointVar(y) $pointVar(left) $pointVar(top)\
                        $pointVar(right) $pointVar(bottom) $options $boxOptions $closestInfo
            } else {
                my CreateClosestMarker $graph gtbCrosshairsClosestText$i gtbCrosshairsClosestBitmap$i $element $pointVar(x)\
                        $pointVar(y) $options $closestInfo
            }
            incr i
        }
    }
    method CrosshairsMotion {graph x y {hide no}} {
        # Updates the underlying RBC crosshair position for a Motion event.
        #  graph - graph pathname.
        #  x - pointer X coordinate.
        #  y - pointer Y coordinate.
        #  hide - if true, leave the raw RBC hairs hidden.
        #
        # Existing enhanced crosshair marker graphics are removed before moving the hairs.
        #
        # Returns: Nothing.
        set markerNames [$graph marker names gtbCrosshairs*]
        $graph marker delete {*}$markerNames
        if {!$hide} {
            $graph crosshairs configure -position @${x},$y
        }
    }
    method CrosshairsModeLabel {mode} {
        # Returns the user-visible label corresponding to an internal crosshair mode.
        #  mode - internal mode name.
        #
        # Returns: Display label.
        classvariable CrosshairsModeLabels
        return [dict get $CrosshairsModeLabels $mode]
    }
    method CrosshairsModeFromLabel {label} {
        # Converts a user-visible crosshair-mode label to its internal value.
        #  label - displayed selector label.
        #
        # Returns: Internal crosshair mode.
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
        # Returns display labels for all enhanced crosshair modes in selector order.
        #
        # Returns: List of labels.
        set labels [list]
        foreach mode [my CrosshairsModes] {
            lappend labels [my CrosshairsModeLabel $mode]
        }
        return $labels
    }
    method SelectCrosshairsMode {} {
        # Applies the crosshair mode selected by a toolbar or context-menu control.
        #
        # Returns: Nothing.
        set mode [my CrosshairsModeFromLabel $CrosshairsSelector]
        my configure -crosshairsmode $mode
    }
    method UpdateCrosshairsModes {} {
        # Synchronizes the toolbar crosshair-mode combobox with the property.
        #
        # Does nothing in context-menu mode where the combobox does not exist.
        #
        # Returns: Nothing.
        if {![info exists Subwidgets(crosshairsComBox)]} {
            return
        }
        $Subwidgets(crosshairsComBox) configure -values [my CrosshairsModeLabels]
        set CrosshairsSelector [my CrosshairsModeLabel [my configure -crosshairsmode]]
    }
    method ApplyCrosshairsMode {} {
        # Rebuilds crosshair bindings and graphics for the current mode.
        #
        # Existing marker artifacts and mode-specific bindtags are removed first.  # The selected mode then installs
        # only the bindings it requires.
        #
        # `CrosshairsMarkerInfo` stores the canonical argument tuple used by both # normal Motion handling and explicit
        # marker refresh after axis transforms.
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        set tagCrosshairs [my BindTagName crosshairs]
        set tagCrosshairsMarker [my BindTagName crosshairs-marker]
        # A mode change invalidates any marker representation produced by
        # the previous mode.  This is especially important when changing
        # from current/closest to none or disabled.
        my DeleteCrosshairsMarkers
        my RemoveBindTag $graph $tagCrosshairs
        my RemoveBindTag $graph $tagCrosshairsMarker
        #
        # The bindtags are reused by all crosshair modes.  Removing a tag
        # from the widget does not remove the binding scripts stored on the
        # tag itself.  Clear the previous mode's scripts before installing
        # the new mode so, for example, a current-mode <Enter> binding cannot
        # survive into closest mode with -hide yes.
        #
        foreach sequence [bind $tagCrosshairs] {
            bind $tagCrosshairs $sequence {}
        }
        foreach sequence [bind $tagCrosshairsMarker] {
            bind $tagCrosshairsMarker $sequence {}
        }
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
        # Returns the automatic current-marker coordinate representation for the selected Smith grid.
        #
        # Returns: `normalizedimpedanceri` or `normalizedadmittanceri`.
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
        # Resolves `-coordmark auto` to the effective current-marker format.
        #
        # Ordinary and Polar representations resolve to `axis`. Smith representation resolves according to
        # [DefaultSmithCoordinateMode].
        #
        # Returns: Effective coordinate mode.
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
        # Returns the selector label for a closest-coordinate mode.
        #  mode - internal mode.
        #
        # Returns: Display label.
        classvariable CoordClosestMarkLabels
        return [dict get $CoordClosestMarkLabels $mode]
    }
    method ClosestCoordinateModeFromLabel {label} {
        # Converts a closest-coordinate selector label to its internal mode.
        #  label - display label.
        #
        # Returns: Internal mode name.
        classvariable CoordClosestMarkLabels
        dict for {mode text} $CoordClosestMarkLabels {
            if {$text eq $label} {
                return $mode
            }
        }
        return -code error "unknown closest coordinate label '$label'"
    }
    method ClosestCoordinateLabels {} {
        # Returns display labels for closest-coordinate modes valid in the current
        # graph representation.
        #
        # Returns: List of labels.
        set labels [list]
        foreach mode [my ClosestCoordinateModes] {
            lappend labels [my ClosestCoordinateLabel $mode]
        }
        return $labels
    }
    method SelectClosestCoordinateMode {} {
        # Applies the closest-coordinate format selected by the toolbar or context menu.
        #
        # Returns: Nothing.
        set mode [my ClosestCoordinateModeFromLabel $ClosestCoordSelector]
        my configure -coordclosestmark $mode
    }
    method ClosestCoordinateModes {} {
        # Returns closest-coordinate representations valid for the current graph.
        #
        # Ordinary graphs support only `axis`; Polar and Smith representations expose their corresponding semantic
        # coordinate forms.
        #
        # Returns: List of internal mode names.
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
        # Returns a valid default closest-coordinate mode for the current representation and Smith-grid selection.
        #
        # Returns: Internal mode name.
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
        # Rebuilds and synchronizes the toolbar closest-coordinate selector.
        #
        # If the currently selected format becomes invalid after changing graph representation, a valid default is
        # selected automatically.
        #
        # Returns: Nothing.
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
    method UpdateClosestContextMenu {} {
        # Rebuilds the dynamic closest-coordinate context submenu.
        #
        # The submenu is reconstructed when posted because changing between Polar # and Smith representation changes
        # the set of valid coordinate formats.
        #
        # Returns: Nothing.
        set menu $Subwidgets(contextMenu).closest
        $menu delete 0 end
        set modes [my ClosestCoordinateModes]
        if {$coordclosestmark ni $modes} {
            my configure -coordclosestmark [my DefaultClosestCoordinateMode]
        }
        set ClosestCoordSelector [my ClosestCoordinateLabel $coordclosestmark]
        foreach mode $modes {
            set label [my ClosestCoordinateLabel $mode]
            $menu add radiobutton -label $label -variable [self namespace]::ClosestCoordSelector -value $label\
                    -command [namespace code {my SelectClosestCoordinateMode}]
        }
    }
    method UpdateContextMenu {} {
        # Synchronizes context-menu entry states before posting.
        #
        # The closest-coordinate submenu is enabled only in `closest` crosshair mode.
        #
        # Returns: Nothing.
        if {![info exists Subwidgets(contextMenu)]} {
            return
        }
        set menu $Subwidgets(contextMenu)
        if {[info exists CrosshairsSelector]} {
            if {[my configure -crosshairsmode] eq {closest}} {
                set state normal
            } else {
                set state disabled
            }
            $menu entryconfigure {Closest crosshairs format} -state $state
        }
    }
    method ClosestAxisFormattedValue {axis value formatSpec} {
        # Formats a closest-point value using the mapped axis formatter.
        #  axis - Rbc axis name.
        #  value - numeric graph value.
        #  formatSpec - graphtoolbar fallback format body.
        #
        # If the axis has a `-command` formatter, this method reproduces the default label passed by RBC and invokes
        # that formatter. Formatter errors fall back to the graphtoolbar numeric representation.
        #
        # Returns: Formatted value.
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
        # Builds axis-style closest-point annotation text.
        #  element - element name.
        #  xValue - selected X value.
        #  yValue - selected Y value.
        #  options - crosshair marker options.
        #
        # PolarElement Cartesian axes receive semantic `real`/`imag` names; ordinary elements retain their actual
        # mapped axis names.
        #
        # Returns: Marker text.
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
        # Builds semantic closest-point annotation text.
        #  element - selected element.
        #  xValue - selected X coordinate.
        #  yValue - selected Y coordinate.
        #  options - marker formatting options.
        #  closestInfo - dictionary made from the `element closest` result.
        #
        # Supports axis, complex, Polar, Gamma, impedance, and admittance representations as appropriate to the graph
        # type. Optional `param` metadata is appended when available.
        #
        # Returns: Marker text, or an empty result when the requested semantic information is unavailable.
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
        # Draws a closest-point bitmap and its associated text annotation.
        #  graph - graph pathname.
        #  textMarker - text marker name.
        #  bitmapMarker - pointer bitmap marker name.
        #  element - selected element.
        #  xValue - selected X value.
        #  yValue - selected Y value.
        #  options - text-marker options.
        #  closestInfo - closest-search metadata.
        #
        # Text is positioned in widget pixels so the complete annotation remains inside the plotting area, then
        # transformed back through the element's mapped axes.
        #
        # Returns: Nothing.
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
                -mapy $mapy -state disabled {*}[dict remove $options -formatx -formaty -formatparam]
        #
        # Bitmap remains exactly at the real selected element point.
        #
        my AddBitmapPoint $bitmapMarker $xValue $yValue $mapx $mapy
    }
    method ClosestBarLayout {left top right bottom text textOptions} {
        # Calculates dimension-line and text layout for a closest bar.
        #  left - displayed bar left edge in widget pixels.
        #  top - displayed bar top edge.
        #  right - displayed bar right edge.
        #  bottom - displayed bar bottom edge.
        #  text - annotation text.
        #  textOptions - text-marker options.
        #
        # Normal graphs place the annotation beside the bar; inverted graphs place it above or below. The preferred
        # side is changed when necessary to keep the annotation inside the plot.
        #
        # Returns: `{lineX1 lineY1 lineX2 lineY2 textX textY anchor}`.
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
        # Calculates the longitudinal screen depth of a bar arrowhead.
        #  lineOptions - closest-bar line-marker option dictionary.
        #
        # Returns: Arrowhead depth in pixels.
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
        # Creates or updates one closest-bar line marker.
        #  marker - marker name.
        #  coords - line coordinates in mapped axis values.
        #  mapx - X axis mapping.
        #  mapy - Y axis mapping.
        #  lineOptions - line-marker options.
        #  arrow - RBC line-marker arrow mode.
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        if {[$graph marker exists $marker]} {
            $graph marker configure $marker -coords $coords -mapx $mapx -mapy $mapy {*}$lineOptions -arrow $arrow
        } else {
            $graph marker create line -name $marker -coords $coords -mapx $mapx -mapy $mapy -bindtags {} -under no\
                    -state disabled {*}$lineOptions -arrow $arrow
        }
    }
    method CreateClosestBarMarker {marker element xValue yValue left top right bottom options boxOptions closestInfo} {
        # Draws the closest-result annotation for a bar element.
        #
        # The displayed bar extent is annotated with a dimension line and text. If the bar is too short for two
        # non-overlapping internal arrowheads, the main line is drawn without heads and two external arrow markers
        # point inward at the bar boundaries.
        #
        # The ordinary closest-point bitmap is not used for bars.
        #
        # Returns: Nothing.
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
        set lineMarker [string map {gtbCrosshairsClosestText gtbCrosshairsClosestBarLine} $marker]
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
        set bitmapMarker [string map {gtbCrosshairsClosestText gtbCrosshairsClosestBitmap} $marker]
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
                    -mapy $mapy -bindtags {} -under no -state disabled {*}$textOptions
        }
    }
    #### zoom methods
    method InitZoomStack {} {
        # Initializes rectangle-zoom and navigation-history state.
        #
        # The `ZoomInfo` array stores:
        #
        # - `A,x` and `A,y` - physical widget coordinates of corner A.
        # - `A,xPixel` and `A,yPixel` - A coordinates along the X/Y data dimensions.
        # - `B,x` and `B,y` - physical widget coordinates of corner B.
        # - `B,xPixel` and `B,yPixel` - B coordinates along the X/Y data dimensions.
        # - `stack` - saved graph-state restoration commands.
        # - `corner` - current rectangle-selection state, `A` or `B`.
        #
        # Setting `corner` to `A` represents an idle rectangle-selection state.
        #
        # Returns: Nothing.
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
        # Initializes rectangle-zoom state and installs zoom bindings.
        #  start - event component which starts rectangle selection.
        #  end - event component which finishes rectangle selection.
        #  reset - event component which restores the previous view or aborts an active selection.
        #
        # The configured zoom modifier is applied to the start and reset events. The finish event deliberately has
        # no modifier requirement, allowing the modifier key to be released before the selection button.
        #
        # Previous-view handling is accepted only when the event occurs inside the plotting area.
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        my InitZoomStack
        set modifier $ZoomMod
        bind [my BindTagName zoom] <${modifier}${start}> [namespace code {my StartZoom %x %y}]
        bind [my BindTagName zoom] <${end}> [namespace code {my FinishZoom %x %y}]
        bind [my BindTagName zoom] <${modifier}${reset}> [namespace code {
            if {[%W inside %x %y]} {
                my ResetZoom %x %y
            }
        }]
        my AddBindTag $graph [my BindTagName zoom]
    }
    method EnableWheelZoom {modifier scale} {
        # Installs mouse-wheel zoom bindings.
        #  modifier - Tk event modifier prefix.
        #  scale - multiplicative zoom step, greater than 1.0.
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        if {![string is double -strict $scale] || !isfinite($scale) || ($scale <= 1.0)} {
            return -code error "wheel zoom scale must be a finite number greater than 1.0"
        }
        bind [my BindTagName zoom] <${modifier}MouseWheel> [namespace code [list my WheelZoom %W %D %x %y %s $scale]]
    }
    method EnablePan {start end modifier} {
        # Installs plot-area panning bindings.
        #  start - event component which starts a pan.
        #  end - event component which finishes a pan.
        #  modifier - Tk event modifier prefix required when starting the pan.
        #
        # The modifier is applied only to the start event. Once panning has started, ordinary Motion and the
        # configured release event continue the operation even if the modifier key is released first.
        #
        # The transient pan Motion binding itself is installed later by [StartPan].
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        bind [my BindTagName pan] <${modifier}${start}> [namespace code {
            if {[my StartPan %x %y]} {
                break
            }
        }]
        # Deliberately do not include $modifier here.  Otherwise releasing
        # Shift before Button-1 could leave the pan operation active.
        bind [my BindTagName pan] <$end> [namespace code {
            if {[my FinishPan %x %y]} {
                break
            }
        }]
        my AddBindTag $graph [my BindTagName pan]
    }
    method SaveZoomState {} {
        # Saves the current graph view as one navigation-history entry.
        #
        # For every used X and Y axis, the method saves configured `-min`, `-max`, and `-logscale` values.
        #
        # Configured limits are read with `cget`, rather than from `axis limits`, so an automatically scaled `{}`
        # limit is preserved as automatic instead of being replaced by the current calculated numerical value.
        #
        # The resulting restoration commands are pushed onto the front of the shared zoom/pan navigation stack.
        #
        # Returns: Nothing.
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
        # Saves one rectangle-zoom corner in widget and axis-pixel coordinates.
        #  x - physical widget X coordinate.
        #  y - physical widget Y coordinate.
        #  index - zoom corner identifier, `A` or `B`.
        #
        # Returns: Nothing.
        set ZoomInfo($index,x) $x
        set ZoomInfo($index,y) $y
        lassign [my WidgetToAxisPixels $x $y] xPixel yPixel
        set ZoomInfo($index,xPixel) $xPixel
        set ZoomInfo($index,yPixel) $yPixel
    }
    method MarkZoomPoint {index} {
        # Creates or updates the coordinate annotation for a zoom-box corner.
        #  index - corner `A` or `B`.
        #
        # Annotation values use the selected `-coordmark` representation.
        #
        # Returns: Nothing.
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
                    -text $text -state disabled {*}[dict remove $options -formatx -formaty -formatparam]
            
        }
    }
    method DestroyZoomTitle {} {
        # Removes the transient zoom-title marker when no rectangle selection is active and clears its timer state.
        #
        # Returns: Nothing.
        unset -nocomplain ZoomInfo(titleTimer)
        if {$ZoomInfo(corner) eq {A}} {
            catch {$Subwidgets(graph) marker delete gtbZoomTitle} errorStr
        }
    }
    method WheelZoom {graph delta x y state step} {
        # Applies one reversible wheel-zoom operation.
        #  graph - graph pathname.
        #  delta - MouseWheel delta.
        #  x - event X coordinate.
        #  y - event Y coordinate.
        #  state - Tk event state mask.
        #  step - multiplicative zoom step.
        #
        # Inside the plot area every used axis scales around the value beneath the pointer. Over an axis only that
        # axis is normally scaled; Polar grid axes are scaled as a pair to preserve equal-unit geometry.
        #
        # Returns: Tk `break` after a successful zoom so the wheel event is consumed.

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
        # Restores saved graph navigation state.
        #  single - if true, restore only the most recent history entry; if false, restore the oldest saved state and
        #   clear the complete history.
        #  x - optional physical pointer X coordinate used to refresh a crosshair marker after the transform changes.
        #  y - optional physical pointer Y coordinate used to refresh a crosshair marker after the transform changes.
        #
        # Saved axis commands restore explicit/automatic limits together with each axis logarithmic-scale setting.
        #
        # Crosshair marker graphics are removed before applying the old transform and rebuilt only after the graph
        # layout is current. Zoom-level title state is updated consistently with the restored history level.
        #
        # If the history is empty, no axis configuration is changed.
        #
        # Returns: Nothing.
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
        # Applies the currently saved rectangle-zoom selection.
        #
        # Corner A and B pixel coordinates are converted independently through every used X and Y axis. The resulting
        # values become the new explicit axis limits, with reversed drag directions normalized automatically.
        #
        # Before changing the axes, the current graph state is saved as one reversible navigation-history entry.
        #
        # A zero-width or zero-height selection is ignored.
        #
        # After the limits change, the graph layout is made current and any crosshair marker is rebuilt at corner B
        # using the new transform.
        #
        # Returns: Nothing.
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
        # Calculates axis limits after scaling around a selected centre.
        #  axis - Rbc axis name.
        #  factor - positive scale factor; values below `1.0` zoom in and values above `1.0` zoom out.
        #  center - optional graph value which remains fixed during scaling. An empty value uses the centre of the
        #   current axis range.
        #
        # Linear axes are scaled arithmetically around `center`.
        #
        # Logarithmic axes are transformed to logarithmic space, scaled there, and converted back. Logarithmic limits
        # and an explicitly supplied logarithmic centre must be positive.
        #
        # Invalid, non-finite, overflowing, or underflowing results are rejected.
        #
        # Returns: Two-element list `{min max}` containing the new numeric axis limits.
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
        # Cancels a transient navigation operation or restores the previous view.
        #  x - optional physical pointer X coordinate used when restoring crosshair markers.
        #  y - optional physical pointer Y coordinate used when restoring crosshair markers.
        #
        # If a pan is active, it is cancelled through [CancelPan].
        #
        # If a rectangle selection is active, the selection is cancelled without changing the navigation
        # history. Temporary zoom bindings, markers, disabled axis interactions, crosshair-marker interaction, and
        # toolbar state are restored.
        #
        # If no transient operation is active, one navigation-history entry is restored through [PopZoom].
        #
        # Returns: Nothing.
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
                my AddBindTag $graph [my BindTagName crosshairs-marker] [my BindTagName crosshairs]
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
            my RemoveBindTag $graph [my BindTagName select-region]
        }
    }
    method FinishZoomTitle {} {
        # Displays the final zoom-level title and schedules its removal.
        #
        # Returns: Nothing.
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
        # Displays the zoom-level number which would result from the pending navigation operation.
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        set level [expr {[llength $ZoomInfo(stack)]+1}]
        if {[$graph cget -invertxy]} {
            set coords {-Inf -Inf}
        } else {
            set coords {-Inf Inf}
        }
        $graph marker create text -name gtbZoomTitle -text "Zoom #$level" -coords $coords -bindtags {} -state disabled\
                {*}[my configure -zoomtitleopts]
    }
    method ZoomTitleLast {} {
        # Displays the current navigation-history level when nonzero.
        #
        # Returns: Nothing.
        set level [llength $ZoomInfo(stack)]
        if {$level > 0} {
            $Subwidgets(graph) marker create text -name gtbZoomTitle -text "Zoom #$level" -state disabled\
                    {*}[my configure -zoomtitleopts]
        }
    }
    method ClampToPlot {x y} {
        # Clamps physical widget coordinates to the plotting area.
        #  x - physical widget X coordinate.
        #  y - physical widget Y coordinate.
        #
        # Coordinates already inside the plot are unchanged. Coordinates outside it are moved to the nearest plot
        # boundary.
        #
        # Returns: Two-element list `{x y}` containing the clamped coordinates.
        lassign [$Subwidgets(graph) extents plotarea] left top width height
        set right  [expr {$left+$width-1}]
        set bottom [expr {$top+$height-1}]
        set x [expr {max($left,min($right,$x))}]
        set y [expr {max($top,min($bottom,$y))}]
        return [list $x $y]
    }
    method ZoomPointInLegend {x y} {
        # Tests whether a graph-local position lies inside a visible legend drawn in the plotting area.
        #  x - physical widget X coordinate.
        #  y - physical widget Y coordinate.
        #
        # Legends outside the plot area and hidden/empty legends never block a zoom or pan start.
        #
        # Returns: Boolean value.
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
        # Constrains a rectangle-zoom corner for a Polar/Smith graph.
        #  x - proposed physical widget X coordinate of corner B.
        #  y - proposed physical widget Y coordinate of corner B.
        #
        # Polar and Smith plots require equal physical scale for the two Cartesian grid dimensions. An arbitrary
        # rectangular zoom would change the X/Y range ratio and cause the graph to resize its plot area.
        #
        # This method adjusts corner B so the selection box has the same physical aspect ratio as the current
        # plotting area. It first attempts to extend the shorter box dimension; if the plot boundary prevents that, the
        # longer dimension is reduced instead.
        #
        # Ordinary graph types are returned unchanged.
        #
        # Returns: Two-element list `{x y}` containing the constrained physical widget coordinates.
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
        # Starts a rectangle-zoom operation.
        #  x - physical widget X coordinate of the initial pointer press.
        #  y - physical widget Y coordinate of the initial pointer press.
        #
        # The operation starts only inside the plotting area and not over a visible legend drawn in the plot area. A
        # pan already in progress or an already-active rectangle selection also prevents a new zoom operation.
        #
        # Corner A is saved in both widget and data-dimension pixel coordinates. During selection, active-axis scale
        # toggling and crosshair marker interaction are temporarily disabled, toolbar controls are disabled, and a
        # transient Motion bindtag is installed to update corner B.
        #
        # Optional zoom-corner and zoom-level annotations are created at this stage.
        #
        # Returns: Nothing.
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
        bind [my BindTagName select-region] <${modifier}Motion> [namespace code {my DragZoom %x %y}]
        # Disable active-axis interaction while selecting.
        set ZoomTransientChecks(activeAxes) [my getAxisActiveScale]
        my setAxisActiveScale $ZoomTransientChecks(activeAxes) -disabled
        # Disable closest/current crosshair marker interaction while
        # selecting.
        set ZoomTransientChecks(crosshairsMarker) false
        if {[my CheckBindTagExistence $graph [my BindTagName crosshairs-marker]]} {
            set ZoomTransientChecks(crosshairsMarker) true
            my RemoveBindTag $graph [my BindTagName crosshairs-marker]
        }
        if {$ZoomMark} {
            my MarkZoomPoint A
        }
        if {[my configure -zoomtitle]} {
            my ZoomTitleNext
        }
        my AddBindTag $graph [my BindTagName select-region]
        my ChangeToolbarState disable
        # corner B also acts as the "drag selection active" flag.
        set ZoomInfo(corner) B
    }
    method DragZoom {x y} {
        # Updates the second corner of an active rectangle-zoom selection.
        #  x - physical widget X coordinate of the current pointer position.
        #  y - physical widget Y coordinate of the current pointer position.
        #
        # Coordinates outside the plotting area are clamped to its boundary. For Polar/Smith graphs, the B corner is
        # adjusted so the selected box preserves the physical aspect ratio of the plot.
        #
        # If zoom-corner markers are enabled, the B annotation is updated before the selection outline is redrawn.
        #
        # If no rectangle selection is active, the method does nothing.
        #
        # Returns: Nothing.
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
        # Finishes an active rectangle-zoom operation.
        #  x - physical widget X coordinate of the release event.
        #  y - physical widget Y coordinate of the release event.
        #
        # The release position is always used as the final B corner, even when no final Motion event
        # occurred. Coordinates outside the plot are clamped to its boundary. Polar/Smith selections are additionally\
        # constrained to preserve the current plot aspect ratio.
        #
        # Transient bindings and disabled interactions are restored before the selected axis limits are applied
        # through [PushZoom].
        #
        # If no rectangle selection is active, the method does nothing.
        #
        # Returns: Nothing.
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
        my RemoveBindTag $graph [my BindTagName select-region]
        # Restore active-axis behaviour.
        if {[info exists ZoomTransientChecks(activeAxes)]} {
            my setAxisActiveScale $ZoomTransientChecks(activeAxes)
            unset ZoomTransientChecks(activeAxes)
        }
        # Restore crosshair-marker behaviour.
        if {[info exists ZoomTransientChecks(crosshairsMarker)]} {
            if {$ZoomTransientChecks(crosshairsMarker)} {
                my AddBindTag $graph [my BindTagName crosshairs-marker] [my BindTagName crosshairs]
            }
            unset ZoomTransientChecks(crosshairsMarker)
        }
        my PushZoom
        my ChangeToolbarState restore
        set ZoomInfo(corner) A
    }
    method Box {} {
        # Draws or updates the current rectangle-zoom selection box.
        #
        # The saved A and B zoom points are converted through the first available interaction X/Y axes. A bitmap
        # marks corner A and a line marker draws the rectangular outline.
        #
        # Polar/Smith grid axes may be used even when their ordinary graph axes are hidden.
        #
        # Returns: Nothing.
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
            $graph marker create line -name gtbZoomOutline -coords $coords -mapx $mapx -mapy $mapy -state disabled\
                    {*}[my configure -zoomboxopts]
        }
    }
    method StartPan {x y} {
        # Begins a plot-area pan.
        #  x - physical pointer X coordinate.
        #  y - physical pointer Y coordinate.
        #
        # The original limits and pixels-per-unit relation of every used axis are captured once. Subsequent Motion
        # events are always calculated from this original state instead of incrementally.
        #
        # Conflicting crosshair, axis-toggle, toolbar, and zoom interactions are suspended until the operation finishes.
        #
        # Returns: True when the press is consumed as a pan start, otherwise false.
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
                #
                # An axis which has an explicit scrolling boundary, or which
                # drives a scrollbar through -scrollcommand, constrains pan.
                #
                # "axis view" returns normalized fractions in physical screen
                # orientation (left-to-right or top-to-bottom), already taking
                # descending axes and graph orientation into account.
                #
                set scrollFirst {}
                set scrollLast {}
                set scrollMin [$graph axis cget $axis -scrollmin]
                set scrollMax [$graph axis cget $axis -scrollmax]
                set scrollCommand [$graph axis cget $axis -scrollcommand]
                if {($scrollMin ne {}) || ($scrollMax ne {}) || ($scrollCommand ne {})} {
                    if {![catch {$graph axis view $axis} scrollView]} {
                        lassign $scrollView first last
                        if {[string is double -strict $first] && [string is double -strict $last] &&\
                                    isfinite($first) && isfinite($last) && ($last > $first)} {
                            set scrollFirst $first
                            set scrollLast $last
                        }
                    }
                }
                dict set axes $axis [list $dimension $min $max $logscale $unitsPerPixel\
                                             [expr {abs(double($pixelSpan))}] $scrollFirst $scrollLast]
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
        set PanTransientChecks(crosshairs) [my CheckBindTagExistence $graph [my BindTagName crosshairs]]
        set PanTransientChecks(crosshairsMarker) [my CheckBindTagExistence $graph [my BindTagName crosshairs-marker]]
        # Preserve whether the RBC crosshairs themselves were displayed.
        set PanTransientChecks(crosshairsHidden) [$graph crosshairs cget -hide]
        my RemoveBindTag $graph [my BindTagName crosshairs-marker]
        my RemoveBindTag $graph [my BindTagName crosshairs]
        # Remove marker text/background before the axes begin moving.
        if {$PanTransientChecks(crosshairsMarker)} {
            my DeleteCrosshairsMarkers
        }
        # Erase the crosshair lines themselves.
        $graph crosshairs off
        bind [my BindTagName pan-region] <Motion> [namespace code {
            my DragPan %x %y
            break
        }]
        my AddBindTag $graph [my BindTagName pan-region]
        my ChangeToolbarState disable
        return true
    }
    method ConstrainPanDelta {dimension deltaPixel} {
        # Constrains a physical pan displacement to axis scrolling regions.
        #  dimension - data dimension, `x` or `y`.
        #  deltaPixel - requested physical displacement along that data
        #    dimension.
        #
        # For every scroll-constrained axis, the current viewport is known as
        # normalized `first last` fractions of its scrolling world. Moving the
        # plotted contents by `deltaPixel` moves that normalized viewport in
        # the opposite direction.
        #
        # All axes of one data dimension must remain synchronized. Therefore
        # this method intersects the displacement permitted by every
        # constrained axis and returns one common physical displacement.
        #
        # Reaching -scrollmin or -scrollmax stops translation at the boundary;
        # it never changes the width of a linear viewport or the ratio of a
        # logarithmic viewport.
        #
        # Returns: Constrained physical displacement in pixels.
        set minDelta {}
        set maxDelta {}
        dict for {axis state} $PanInfo(axes) {
            lassign $state axisDimension min max logscale unitsPerPixel pixelSpan scrollFirst scrollLast
            if {($axisDimension ne $dimension) || ($scrollFirst eq {}) || ($scrollLast eq {})} {
                continue
            }
            set windowSize [expr {$scrollLast-$scrollFirst}]
            if {($windowSize <= 0.0) || ($pixelSpan <= 0.0)} {
                continue
            }
            #
            # axis view reports:
            #
            #     scrollFirst ... scrollLast
            #
            # in physical scrollbar orientation.
            #
            # The current viewport occupies "windowSize" of the scrolling
            # world and "pixelSpan" physical pixels. Therefore:
            #
            #     pixelsPerFraction = pixelSpan / windowSize
            #
            # Dragging the contents in the positive physical direction moves
            # the scrollbar viewport toward zero.
            #
            set pixelsPerFraction [expr {$pixelSpan/$windowSize}]
            #
            # Lower displacement bound:
            #
            #     newLast <= 1
            #
            # Upper displacement bound:
            #
            #     newFirst >= 0
            #
            set axisMinDelta [expr {($scrollLast-1.0)*$pixelsPerFraction}]
            set axisMaxDelta [expr {$scrollFirst*$pixelsPerFraction}]
            if {($minDelta eq {}) || ($axisMinDelta > $minDelta)} {
                set minDelta $axisMinDelta
            }
            if {($maxDelta eq {}) || ($axisMaxDelta < $maxDelta)} {
                set maxDelta $axisMaxDelta
            }
        }
        #
        # Every valid axis-view interval contains zero, so this should only be
        # possible because of inconsistent or numerically degenerate state.
        # Freezing the displacement is safer than changing relative axis
        # mappings.
        #
        if {($minDelta ne {}) && ($maxDelta ne {}) && ($minDelta > $maxDelta)} {
            return 0.0
        }
        if {($minDelta ne {}) && ($deltaPixel < $minDelta)} {
            set deltaPixel $minDelta
        }
        if {($maxDelta ne {}) && ($deltaPixel > $maxDelta)} {
            set deltaPixel $maxDelta
        }
        return $deltaPixel
    }
    method DragPan {x y} {
        # Updates an active pan from the current pointer position.
        #  x - physical pointer X coordinate.
        #  y - physical pointer Y coordinate.
        #
        # Linear limits are translated additively; logarithmic limits are
        # translated multiplicatively in log space.
        #
        # Axes with an explicit scrolling region or -scrollcommand constrain
        # the common physical displacement for their data dimension. Reaching
        # a scrolling boundary stops translation without changing viewport
        # scale.
        #
        # The navigation-history entry is created only on the first real
        # movement.
        #
        # Returns: Nothing.
        if {![info exists PanInfo(active)] || !$PanInfo(active)} {
            return
        }
        set graph $Subwidgets(graph)
        lassign [my WidgetToAxisPixels $x $y] xPixel yPixel
        set xPixel [expr {round($xPixel)}]
        set yPixel [expr {round($yPixel)}]
        set dx [expr {$xPixel-$PanInfo(start,xPixel)}]
        set dy [expr {$yPixel-$PanInfo(start,yPixel)}]
        # Constrain the displacement itself rather than individual axis
        # endpoints. This preserves both viewport scale and alignment between
        # multiple axes mapped to the same data dimension.
        set dx [my ConstrainPanDelta x $dx]
        set dy [my ConstrainPanDelta y $dy]
        if {($dx == $PanInfo(dx)) && ($dy == $PanInfo(dy))} {
            return
        }
        # Prepare every new limit pair first. Do not leave some axes modified
        # if another axis overflows.
        set cmds [list]
        set changed false
        dict for {axis state} $PanInfo(axes) {
            lassign $state dimension min max logscale unitsPerPixel pixelSpan scrollFirst scrollLast
            if {$dimension eq {x}} {
                set deltaPixel $dx
            } else {
                set deltaPixel $dy
            }
            if {$logscale} {
                # Translate in log space. Multiplying both limits by the same
                # factor preserves max/min exactly.
                set logShift [expr {-$unitsPerPixel*$deltaPixel}]

                if {[catch {expr {exp($logShift)}} factor] || ![string is double -strict $factor] ||\
                            !isfinite($factor) || ($factor <= 0.0)} {
                    return
                }
                set newMin [expr {$min*$factor}]
                set newMax [expr {$max*$factor}]
            } else {
                # Ordinary linear translation. Adding the same shift to both
                # limits preserves max-min exactly.
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
        # starting location, these commands also restore the original limits.
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
        # Restores interactions suspended by [StartPan].
        #  x - final/cancellation pointer X coordinate.
        #  y - final/cancellation pointer Y coordinate.
        #
        # Crosshair markers are reconstructed only after the final graph transform is current.
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        my RemoveBindTag $graph pan-region-$graph
        if {[info exists PanTransientChecks(activeAxes)]} {
            my setAxisActiveScale $PanTransientChecks(activeAxes)
            unset PanTransientChecks(activeAxes)
        }
        # Restore the ordinary crosshair Motion/Enter/Leave tag first.
        if {[info exists PanTransientChecks(crosshairs)] && $PanTransientChecks(crosshairs)} {
            my AddBindTag $graph [my BindTagName crosshairs]
        }
        # Restore the marker tag in its normal position after the
        # crosshair tag.
        set refreshCrosshairs false
        if {[info exists PanTransientChecks(crosshairsMarker)] && $PanTransientChecks(crosshairsMarker)} {
            my AddBindTag $graph [my BindTagName crosshairs-marker] [my BindTagName crosshairs]
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
        # Finishes an active pan.
        #  x - ButtonRelease X coordinate.
        #  y - ButtonRelease Y coordinate.
        #
        # The real release position is processed even if no final Motion event  occurred. A move which returns exactly
        # to the starting point is reduced to a no-op and its temporary history entry is removed.
        #
        # Returns: True when an active pan was finished, otherwise false.
        if {![info exists PanInfo(active)] || !$PanInfo(active)} {
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
        # Cancels an active pan and restores the preceding graph state.
        #  x - optional pointer X coordinate used when refreshing interactions.
        #  y - optional pointer Y coordinate.
        #
        # If movement had already created a history entry, that entry is popped to restore the original limits.
        #
        # Returns: True when an active pan was cancelled, otherwise false.
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
        # Restores the oldest saved navigation state and clears all zoom/pan history.
        #
        # Pending rectangle-selection bindings and temporary zoom markers are also removed.
        #
        # Returns: Nothing.
        set graph $Subwidgets(graph)
        my PopZoom no
        my RemoveBindTag $graph [my BindTagName select-region]
        my InitZoomStack
        $graph marker delete {*}[$graph marker names gtbZoom*]
    }
    ### snapshot methods
    method MakeSnapshot {} {
        # Saves a PNG snapshot of the embedded graph.
        #
        # A standard Tk save-file dialog requests the destination. Cancelling the dialog performs no operation.
        #
        # Returns: Nothing.
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
        # Opens the graphtoolbar PostScript configuration dialog.
        #
        # The dialog mirrors the supported Rbc postscript configuration values and allows them to be edited before
        # writing output.
        #
        # Returns: Nothing.
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
        # Applies values from the PostScript dialog and writes PostScript output.
        #
        # Individual invalid option values are rolled back to their previous Rbc value. A save-file dialog selects
        # the final destination.
        #
        # Returns: Nothing.
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
