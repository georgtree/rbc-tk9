package require rbc
namespace import rbc::*

vector create z -type complex
z set {
    {0 0}
    {1 0}
    {1 1}
    {0 2}
    {-2 1}
    {-1 -2}
}

polar .p -width 600 -height 600 -aspect 1.0 -radiallabelanchor auto -anglelabelanchor auto
# .p configure \
#     -anglemajorticks {0 45 90 135 180 225 270 310} \
#     -angleminorticks {22.5 67.5 112.5 157.5 202.5 247.5 292.5 337.5}
proc radians {widget degrees} {
    return [format "%.3g rad" \
        [expr {$degrees * acos(-1) / 180.0}]]
}

.p configure -anglemajorticks {0 45 90 135 180 225 270 315} -anglecommand radians

grid .p -row 0 -column 0 -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 1

.p grid configure -hide no -minor no

.p element create trace -cdata z -symbol circle -linewidth 2

# vector create gamma -type complex
# gamma set {
#     {0.0 0.0}
#     {0.2 0.1}
#     {0.4 0.3}
#     {0.6 0.2}
#     {0.8 0.0}
# }

# polar .p \
#     -width 600 \
#     -height 600 \
#     -representation smith -smithgrid admittance

# pack .p -fill both -expand yes

# .p grid configure -hide no -minor no

# .p element create trace \
#     -cdata gamma \
#     -symbol circle \
#     -linewidth 2
