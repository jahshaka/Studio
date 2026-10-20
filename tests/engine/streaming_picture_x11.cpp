// frame.streaming_picture's window and its photograph - Xlib in its own translation unit (Xlib's macros - None, Bool,
// Status - collide with the engine's enums; tests/scale/scale_x11.cpp's rule).
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <vector>

namespace streamingpicture {

bool openWindow(unsigned w, unsigned h, void **display, unsigned long *window)
{
    Display *d = XOpenDisplay(nullptr);
    if (!d) return false;
    const int sc = DefaultScreen(d);
    const ::Window win = XCreateSimpleWindow(d, RootWindow(d, sc), 0, 0, w, h, 0, BlackPixel(d, sc), BlackPixel(d, sc));
    XMapWindow(d, win);
    XSync(d, False);
    *display = d;
    *window = win;
    return win != 0;
}

/// What the X server holds at the window's rectangle, from the ROOT (a Vulkan window's own XGetImage reads black on the
/// rig: DOCS/traps/GATE_AND_RIG.md) - the presented picture. rgb, row-major.
bool grab(void *display, unsigned w, unsigned h, std::vector<unsigned char> &rgb)
{
    Display *d = static_cast<Display *>(display);
    XSync(d, False);
    XImage *img = XGetImage(d, RootWindow(d, DefaultScreen(d)), 0, 0, w, h, AllPlanes, ZPixmap);
    if (!img) return false;
    rgb.resize(size_t(w) * h * 3u);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            const unsigned long p = XGetPixel(img, int(x), int(y));
            unsigned char *o = &rgb[(size_t(y) * w + x) * 3u];
            o[0] = (unsigned char)((p & img->red_mask) >> 16);
            o[1] = (unsigned char)((p & img->green_mask) >> 8);
            o[2] = (unsigned char)(p & img->blue_mask);
        }
    XDestroyImage(img);
    return true;
}

void closeWindow(void *display, unsigned long window)
{
    Display *d = static_cast<Display *>(display);
    if (!d) return;
    if (window) XDestroyWindow(d, window);
    XSync(d, False);
    XCloseDisplay(d);
}

}   // namespace streamingpicture
