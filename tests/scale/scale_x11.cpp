// The rig window for the scale suites' WINDOW view — kept in its own translation unit
// because Xlib's macros (None, Bool, Status) collide with the document's enums.
#include <X11/Xlib.h>

namespace scale {

bool openRigWindow(unsigned w, unsigned h, void **display, unsigned long *window)
{
    Display *d = XOpenDisplay(nullptr);
    if (!d) return false;
    const int sc = DefaultScreen(d);
    const ::Window win = XCreateSimpleWindow(d, RootWindow(d, sc), 0, 0, w, h, 0, BlackPixel(d, sc),
                                             BlackPixel(d, sc));
    XSync(d, False);
    *display = d;
    *window = win;
    return win != 0;
}

void closeRigWindow(void *display, unsigned long window)
{
    Display *d = static_cast<Display *>(display);
    if (!d) return;
    if (window) XDestroyWindow(d, window);
    XSync(d, False);
    XCloseDisplay(d);
}

}   // namespace scale
