# Standalone widgets suite; make test also runs the other runtime suites.
set argv [linsert $argv 0 widgets]
source [file join [file dirname [info script]] runtimeTests.tcl]
