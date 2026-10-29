package require rbc
namespace import ::rbc::*

rbc::vector create xVec1 yVec1 xVec2 yVec2
xVec1 set {0.2 0.4 0.6 0.8 1.0 1.2 1.4 1.6 1.8 2.0}
yVec1 set {26.18 50.46 72.85 93.31 111.86 128.47 143.14 155.85 166.60 175.38}
xVec2 set {0.2 0.4 0.6 0.8 1.0 1.2 1.4 1.6 1.8 2.0}
yVec2 set {16.18 60.46 82.85 103.31 121.86 138.47 153.14 165.85 176.60 185.38}

set winSize {-width 800 -height 400}
set titleFont {-font {Helvetica 10}}
set widget [graphtoolbar .gtb {*}$winSize -type graph -controlmode context -zoom -zoommod {Any-} -pan -zoomtitle -zoommark\
                    -zoomboxopts {-linewidth 1} -zoommarkboxopts {-outline grey -fill #FFEB3B} -crosshairs\
                    -crosshairsmode none -crosshairsclosestopts {-single true -halo 50 -hide yes} -scaletoggle x\
                    -activelegend -zoomwheel]
#.gtb configure -crosshairsclosestopts {-hide yes}
$widget graph configure -invertxy no -barmode aligned -renderer cairo
$widget graph legend configure -position plotarea
$widget widgetcommand graph axis create time
$widget widgetcommand graph axis create time1
#$widget graph marker create line -name arrow -coords {1 50 1.5 100} -arrow both -mapy time -arrowshape {8p 10p 3p}
$widget graph yaxis use {time time1}
grid .gtb -row 0 -column 0 -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 1
$widget graph configure -title {Interactive crosshairs with closes element coordinate marker} {*}$titleFont
$widget graph element create line1 -xdata xVec1 -ydata yVec1 -scalesymbols no
$widget graph element create line2 -xdata xVec2 -ydata yVec2 -mapy time1 -scalesymbols no

