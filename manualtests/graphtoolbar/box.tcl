package require rbc
namespace import rbc::*

graph .g -width 500 -height 400
pack .g -fill both -expand yes

.g axis configure x -min 0 -max 10
.g axis configure y -min 0 -max 10
.g configure -marginy 0
.g marker create polygon \
    -name box \
    -coords {2 10.3 5 10.3 5 5 2 5} \
    -fill yellow \
    -outline grey \
    -linewidth 1
