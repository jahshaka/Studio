// The X error counter of view.window_on_render_thread, in its own TU: Xlib's macros (None, Bool, Status ...) must
// never meet the engine's headers.
#include <X11/Xlib.h>
#include <atomic>
#include <cstdio>

static std::atomic<int> gXErrors{0};
static int countXError(Display *, XErrorEvent *e)
{
    std::printf("X ERROR: code %d request %d minor %d\n", int(e->error_code), int(e->request_code), int(e->minor_code));
    ++gXErrors;
    return 0;
}
void installXErrorCounter() { XSetErrorHandler(countXError); }
int xErrorCount() { return gXErrors.load(); }
