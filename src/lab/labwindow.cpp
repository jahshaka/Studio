// jahlab's window — every Xlib call of the lab (see labwindow.h for why Xlib and why its own TU).
#include "labwindow.h"

#include "labcommands.h"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <cctype>

namespace lab {

namespace {
unsigned long namedPixel(Display *d, const char *spec, unsigned long fallback)
{
    XColor c, exact;
    return XAllocNamedColor(d, DefaultColormap(d, DefaultScreen(d)), spec, &c, &exact) ? c.pixel : fallback;
}
}   // namespace

LabWindow::~LabWindow() { close(); }

bool LabWindow::open(unsigned w, unsigned h, const std::string &title, std::string &error)
{
    // THE ENGINE PRESENTS THROUGH THIS CONNECTION (the fork's Vulkan window reads its attributes from it). The engine runs
    // on this thread (labsession.cpp boot()); XInitThreads before the first connection is kept because the driver's
    // window-system code may touch the connection from threads of its own (unverified) and the call costs nothing.
    XInitThreads();
    Display *d = XOpenDisplay(nullptr);
    if (!d) {
        error = "no X display (DISPLAY is unset or unreachable): the window and --headless both need one (Ogre-Next's Vulkan connects at boot)";
        return false;
    }
    const int sc = DefaultScreen(d);
    const unsigned strip = unsigned(kStripHeight);
    const ::Window win = XCreateSimpleWindow(d, RootWindow(d, sc), 0, 0, w, h + strip, 0, BlackPixel(d, sc), BlackPixel(d, sc));
    XSelectInput(d, win, KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                             StructureNotifyMask | FocusChangeMask);
    Atom del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, win, &del, 1);
    // A held key reports ONE press and ONE release, not the server's repeat pairs (the fly keys are held state).
    XkbSetDetectableAutoRepeat(d, True, nullptr);
    XStoreName(d, win, title.c_str());
    // THE STRIP (its clicks and its exposures are its own) and THE VIEW CHILD (it selects nothing: its keys, motion and
    // right button reach the top-level's mask), both MAPPED with the top-level before the engine makes its view
    mStripBg = namedPixel(d, "rgb:20/22/26", BlackPixel(d, sc));
    mButtonBg = namedPixel(d, "rgb:44/4a/56", WhitePixel(d, sc));
    mInk = namedPixel(d, "rgb:e8/e8/e8", WhitePixel(d, sc));
    const ::Window stripWin = XCreateSimpleWindow(d, win, 0, 0, w, strip, 0, mStripBg, mStripBg);
    XSelectInput(d, stripWin, ExposureMask | ButtonPressMask);
    const ::Window viewWin = XCreateSimpleWindow(d, win, 0, int(strip), w, h, 0, BlackPixel(d, sc), BlackPixel(d, sc));
    XMapWindow(d, stripWin);
    XMapWindow(d, viewWin);
    XMapWindow(d, win);
    XFontStruct *font = XLoadQueryFont(d, "fixed");
    GC gc = XCreateGC(d, stripWin, 0, nullptr);
    if (font) XSetFont(d, gc, font->fid);
    XSync(d, False);
    mDisplay = d;
    mWindow = win;
    mStrip = stripWin;
    mViewChild = viewWin;
    mGc = gc;
    mFont = font;
    mDeleteAtom = del;
    mWidth = w;
    mHeight = h;
    return win != 0;
}

void LabWindow::setStripText(const std::string &text)
{
    mStripText = text;
    drawStrip();
}

void LabWindow::drawStrip()
{
    if (!mDisplay || !mStrip) return;
    Display *d = static_cast<Display *>(mDisplay);
    GC gc = static_cast<GC>(mGc);
    const XFontStruct *font = static_cast<const XFontStruct *>(mFont);
    const int base = font ? (kStripHeight + font->ascent - font->descent) / 2 : kStripHeight - 8;
    XSetForeground(d, gc, mStripBg);
    XFillRectangle(d, mStrip, gc, 0, 0, std::max(1u, mWidth), unsigned(kStripHeight));
    for (const StripButton &b : stripButtons()) {
        XSetForeground(d, gc, mButtonBg);
        XFillRectangle(d, mStrip, gc, b.x0, 2, unsigned(b.x1 - b.x0), unsigned(kStripHeight - 4));
        XSetForeground(d, gc, mInk);
        const int len = int(std::string(b.label).size());
        const int tw = font ? XTextWidth(const_cast<XFontStruct *>(font), b.label, len) : 6 * len;
        XDrawString(d, mStrip, gc, (b.x0 + b.x1 - tw) / 2, base, b.label, len);
    }
    XSetForeground(d, gc, mInk);
    XDrawString(d, mStrip, gc, kStripTextX, base, mStripText.c_str(), int(mStripText.size()));
    XFlush(d);
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
    in.stripClicks.clear();
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
            if (ev.xbutton.window == mStrip && ev.xbutton.button == Button1)
                in.stripClicks.emplace_back(ev.xbutton.x, ev.xbutton.y);   // the session runs its button's line
            if (ev.xbutton.button == Button3) { mRight = true; mHaveLast = false; }
            break;
        case Expose:
            if (ev.xexpose.window == mStrip && ev.xexpose.count == 0) drawStrip();
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
        case ConfigureNotify: {
            // the TOP-LEVEL's size: the strip keeps its height across it, the view child takes the rest
            if (ev.xconfigure.window != mWindow) break;
            const unsigned w = std::max(1, ev.xconfigure.width);
            const unsigned h = unsigned(std::max(1, ev.xconfigure.height - kStripHeight));
            if (w != mWidth || h != mHeight) {
                mWidth = w;
                mHeight = h;
                XResizeWindow(d, mStrip, w, unsigned(kStripHeight));
                XResizeWindow(d, mViewChild, w, h);
                in.resized = true;
                drawStrip();
            }
            break;
        }
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
    if (mGc) XFreeGC(d, static_cast<GC>(mGc));
    if (mFont) XFreeFont(d, static_cast<XFontStruct *>(mFont));
    mGc = mFont = nullptr;
    if (mWindow) XDestroyWindow(d, mWindow);   // its children with it
    XSync(d, False);
    XCloseDisplay(d);
    mDisplay = nullptr;
    mWindow = mStrip = mViewChild = 0;
}

}   // namespace lab
