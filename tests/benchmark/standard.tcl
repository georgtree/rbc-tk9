
set benchmarkDir [file normalize [file dirname [info script]]]
set platform $::tcl_platform(platform)

exec [info nameofexecutable] [file join $benchmarkDir run.tcl] -renderer native -antialias default -profile standard -csv-dir [file join $benchmarkDir results $platform native]
exec [info nameofexecutable] [file join $benchmarkDir run.tcl] -renderer cairo -antialias none -profile standard -csv-dir [file join $benchmarkDir results $platform cairo-none]
exec [info nameofexecutable] [file join $benchmarkDir run.tcl] -renderer cairo -antialias default -profile standard -csv-dir [file join $benchmarkDir results $platform cairo-default]

exec [info nameofexecutable] [file join $benchmarkDir compare.tcl]\
        -input Windows-PC [file join $benchmarkDir results $platform native]\
        -input Windows-PC [file join $benchmarkDir results $platform cairo-none]\
        -input Windows-PC [file join $benchmarkDir results $platform cairo-default]\
        -out [file join $benchmarkDir results comparison $platform]

# exec [info nameofexecutable] [file join $benchmarkDir compare.tcl]\
#         -input Linux-WSL2 [file join $benchmarkDir results $platform native]\
#         -input Linux-WSL2 [file join $benchmarkDir results $platform cairo-none]\
#         -input Linux-WSL2 [file join $benchmarkDir results $platform cairo-default]\
#         -out [file join $benchmarkDir results comparison $platform]
