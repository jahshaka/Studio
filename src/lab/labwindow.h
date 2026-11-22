// jahlab — THE WINDOW (LAB-1 stage 1; SPECS/LAB_1_SPEC.md, PREFLIGHT CORRECTIONS 1).
//
// ONE X11 window on the display the lab is launched on, through Xlib: the engine's on-screen View takes exactly an
// Xlib Display* (EngineConfig::display) and an X11 Window (NativeWindowHandle) and marks the window external, and
// Ogre-Next's own window creation is unreachable through the engine — so Xlib is the smaller route (it is the route
// tests/scale/scale_x11.cpp and tests/engine/streaming_picture_x11.cpp already take). Xlib's macros (None, Bool,
// Status) collide with the document's enums, so every X call lives in labwindow.cpp and this header names none.
// The engine never reads X events: the lab selects its own mask and pumps them once per frame.
// THE BUTTON STRIP (LAB-SCRIPTS-1 stage 2): the top-level holds two children - the STRIP (kStripHeight px, core-X text:
// labcommands.h stripButtons) above the VIEW child the engine draws into - both mapped before the engine's view is made
// (a Vulkan present into a not-yet-mapped child does not survive its map). A left click on the strip is reported as a
// strip point; the session turns it into its button's command line (labcommands.h stripHit).
#pragma once

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace lab {

/// What one pump of the window's events gathered.
struct WindowInput
{
    /// Key presses of this pump, by X keysym name ("F2", "w", "Escape") — auto-repeats dropped.
    std::vector<std::string> pressed;
    /// Keys held down now (lower-case letters, "Shift_L", ...).
    std::set<std::string> held;
    bool rightButton = false;          ///< the mouse look's button
    int dx = 0, dy = 0;                ///< pointer motion while the right button is held, in pixels
    std::vector<std::pair<int, int>> stripClicks;   ///< left clicks on the strip this pump, in strip pixels
    bool resized = false;
    unsigned width = 0, height = 0;    ///< the VIEW child's size after the pump (the picture's: the strip excluded)
    bool closeRequested = false;       ///< the window manager's close
};

class LabWindow
{
public:
    LabWindow() = default;
    ~LabWindow();
    LabWindow(const LabWindow &) = delete;
    LabWindow &operator=(const LabWindow &) = delete;

    /// Opens the display ($DISPLAY) and maps a window whose picture (the view child) is w x h, the strip above it. False
    /// (with `error`) when there is no display.
    bool open(unsigned w, unsigned h, const std::string &title, std::string &error);
    void setTitle(const std::string &title);
    /// The strip's text beside its buttons (the script and its state); redrawn now.
    void setStripText(const std::string &text);
    /// Drains every pending event into `in` (held keys and the button persist across pumps).
    void pump(WindowInput &in);
    void close();

    /// The Xlib Display* (EngineConfig::display), the top-level X11 Window (`LAB WINDOW`: the drivers' xdotool target;
    /// the strip sits at its 0,0) and the VIEW child (the View's handle), as integers.
    unsigned long long displayHandle() const { return reinterpret_cast<unsigned long long>(mDisplay); }
    unsigned long long windowHandle() const { return mWindow; }
    unsigned long long viewHandle() const { return mViewChild; }
    bool isOpen() const { return mDisplay != nullptr; }
    /// The X connection's file descriptor (ConnectionNumber): a lab that draws no frame unasked (--no-idle-frames)
    /// blocks on it beside stdin, so the window's events are served without a frame and without a busy wait.
    int connectionFd() const;

private:
    void *mDisplay = nullptr;
    unsigned long mWindow = 0;
    unsigned long mDeleteAtom = 0;
    unsigned long mStrip = 0, mViewChild = 0;
    void *mGc = nullptr, *mFont = nullptr;
    unsigned long mStripBg = 0, mButtonBg = 0, mInk = 0;
    std::string mStripText;
    void drawStrip();
    unsigned mWidth = 0, mHeight = 0;
    int mLastX = 0, mLastY = 0;
    bool mHaveLast = false;
    std::set<std::string> mHeld;
    bool mRight = false;
};

}   // namespace lab
