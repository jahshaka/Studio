// jahlab's window — every Xlib call of the lab (see labwindow.h for why Xlib and why its own TU).
#include "labwindow.h"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cctype>

namespace lab {

LabWindow::~LabWindow() { close(); }

bool LabWindow::open(unsigned w, unsigned h, const std::string &title, std::string &error)
{
    // THE ENGINE'S RENDER THREAD USES THIS CONNECTION TOO (EngineConfig::renderThread: the fork's Vulkan window reads
    // its attributes there), while this thread pumps its events: Xlib must be told before the first connection.
    XInitThreads();
    Display *d = XOpenDisplay(nullptr);
    if (!d) {
        error = "no X display (DISPLAY is unset or unreachable): the window and --headless both need one (Ogre-Next's Vulkan connects at boot)";
        return false;
    }
    const int sc = DefaultScreen(d);
    const ::Window win = XCreateSimpleWindow(d, RootWindow(d, sc), 0, 0, w, h, 0, BlackPixel(d, sc), BlackPixel(d, sc));
    XSelectInput(d, win, KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                             StructureNotifyMask | FocusChangeMask);
    Atom del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, win, &del, 1);
    // A held key reports ONE press and ONE release, not the server's repeat pairs (the fly keys are held state).
    XkbSetDetectableAutoRepeat(d, True, nullptr);
    XStoreName(d, win, title.c_str());
    XMapWindow(d, win);
    XSync(d, False);
    mDisplay = d;
    mWindow = win;
    mDeleteAtom = del;
    mWidth = w;
    mHeight = h;
    return win != 0;
}

void LabWindow::setTitle(const std::string &title)
{
    if (!mDisplay) return;
    XStoreName(static_cast<Display *>(mDisplay), mWindow, title.c_str());
    XFlush(static_cast<Display *>(mDisplay));
}

static std::string keyName(XKeyEvent *ev)
{
    KeySym sym = XLookupKeysym(ev, 0);
    const char *n = sym != NoSymbol ? XKeysymToString(sym) : nullptr;
    std::string s = n ? n : "";
    if (s.size() == 1) s[0] = char(std::tolower(static_cast<unsigned char>(s[0])));
    return s;
}

void LabWindow::pump(WindowInput &in)
{
    in.pressed.clear();
    in.dx = in.dy = 0;
    in.resized = false;
    in.closeRequested = false;
    if (!mDisplay) return;
    Display *d = static_cast<Display *>(mDisplay);
    while (XPending(d)) {
        XEvent ev;
        XNextEvent(d, &ev);
        switch (ev.type) {
        case KeyPress: {
            const std::string k = keyName(&ev.xkey);
            if (k.empty()) break;
            if (!mHeld.count(k)) in.pressed.push_back(k);   // a held key's repeat is no new press
            mHeld.insert(k);
            break;
        }
        case KeyRelease: mHeld.erase(keyName(&ev.xkey)); break;
        case ButtonPress:
            if (ev.xbutton.button == Button3) { mRight = true; mHaveLast = false; }
            break;
        case ButtonRelease:
            if (ev.xbutton.button == Button3) mRight = false;
            break;
        case MotionNotify:
            if (mRight) {
                if (mHaveLast) { in.dx += ev.xmotion.x - mLastX; in.dy += ev.xmotion.y - mLastY; }
                mLastX = ev.xmotion.x;
                mLastY = ev.xmotion.y;
                mHaveLast = true;
            }
            break;
        case FocusOut: mHeld.clear(); mRight = false; break;
        case ConfigureNotify:
            if (unsigned(ev.xconfigure.width) != mWidth || unsigned(ev.xconfigure.height) != mHeight) {
                mWidth = unsigned(ev.xconfigure.width);
                mHeight = unsigned(ev.xconfigure.height);
                in.resized = true;
            }
            break;
        case ClientMessage:
            if (static_cast<unsigned long>(ev.xclient.data.l[0]) == mDeleteAtom) in.closeRequested = true;
            break;
        default: break;
        }
    }
    in.held = mHeld;
    in.rightButton = mRight;
    in.width = mWidth;
    in.height = mHeight;
}

void LabWindow::close()
{
    if (!mDisplay) return;
    Display *d = static_cast<Display *>(mDisplay);
    if (mWindow) XDestroyWindow(d, mWindow);
    XSync(d, False);
    XCloseDisplay(d);
    mDisplay = nullptr;
    mWindow = 0;
}

}   // namespace lab
