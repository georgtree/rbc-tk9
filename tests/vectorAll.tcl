# Standalone vector suite; make test also runs the other runtime suites.
set argv [linsert $argv 0 vector]
source [file join [file dirname [info script]] runtimeTests.tcl]
