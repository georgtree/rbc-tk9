# Keyboard-focus setup for tests that deliver real Tk key events.
# Each caller sources this file directly so standalone test runs work too.
namespace eval ::rbcTest::focus {
    proc Acquire {window {timeout 2000}} {
        set deadline [expr {[clock milliseconds] + $timeout}]
        while {1} {
            update
            if {![winfo exists $window]} {
                error "focus target '$window' was destroyed during test setup"
            }
            if {[winfo viewable $window]} {
                if {[focus -displayof $window] eq $window} {
                    return
                }
                # On X11 this request and the resulting FocusIn notification
                # are asynchronous. One update need not observe that reply.
                focus -force $window
            }
            if {[clock milliseconds] >= $deadline} {
                error "timed out acquiring test focus for '$window': viewable=[winfo viewable $window], focus='[focus -displayof $window]'"
            }
            # Give the window system time to reply, then service events again.
            after 10
        }
    }
}
