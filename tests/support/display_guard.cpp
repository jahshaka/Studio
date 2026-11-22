// display_guard.cpp — NO TEST BINARY REACHES A DISPLAY THAT IS NOT A RIG (DEV_ERRORS 37 + 38, 2026-11-22; CODING_RULES §7).
//
// WHY. A builder ran a Qt test binary DIRECTLY from its build tree, twice in one night, with the agent shell's DISPLAY=:0 and
// WAYLAND_DISPLAY inherited: the QGuiApplication connected to THE OWNER'S display for ~0.2-1 s. A rule kept by memory failed
// twice, so the class is closed in the binary itself. Measured (spikes/display-guard/repro/before.txt, a private Wayland
// socket and our own rig): with XDG_SESSION_TYPE=wayland (this box's session) Qt tries WAYLAND FIRST, and with
// WAYLAND_DISPLAY unset libwayland still connects to $XDG_RUNTIME_DIR/wayland-0 - so unsetting the two variables is not
// enough: only QT_QPA_PLATFORM decides where Qt goes. An engine fixture reaches X too, with no Qt at all: Ogre's
// VulkanXcbSupport calls xcb_connect(0, ..) at plugin load (DISPLAY), so it needs a rig display, honestly.
//
// WHAT. This file is compiled into EVERY executable target under tests/ (tests/CMakeLists.txt, jah_display_guard_all - no
// target names it; source.display_guard_linked holds every binary ctest runs to it). Before main, and so before any
// Q*Application or Ogre Root exists:
//   - WAYLAND_DISPLAY and WAYLAND_SOCKET are unset (no test runs on Wayland; the rig is X);
//   - DISPLAY a RIG display (:60-:99, a screen suffix allowed - the ONE definition, scripts/display-guard.sh) -> kept, and
//     QT_QPA_PLATFORM pinned to xcb when it is unset or names anything but xcb / offscreen / minimal;
//   - any other DISPLAY (:0 the OWNER's, :1, a name) or none -> DISPLAY unset and QT_QPA_PLATFORM forced to offscreen (an
//     offscreen value with its own options, "offscreen:configfile=...", is kept), one line on stderr when a DISPLAY was dropped;
//   - REFUSED, exit 2, before main: a run that DEMANDS X (QT_QPA_PLATFORM=xcb) on a DISPLAY that is not a rig - its subject
//     needs a real display and offscreen would be a false verdict;
//   - THE PIN IS THE WAYLAND DOOR: QT_QPA_PLATFORM always leaves here as xcb, offscreen or minimal, so Qt never loads its
//     Wayland plugin and libwayland's default $XDG_RUNTIME_DIR/wayland-0 is never tried (XDG_RUNTIME_DIR itself is left
//     alone: D-Bus and the runtime's other users read it). Nothing else in a test opens Wayland: the fork's Vulkan windowing
//     on Linux is xcb only (RenderSystems/Vulkan/CMakeLists.txt globs src/Windowing/X11; its "wayland" option is commented
//     out, OgreVulkanSupport.cpp:60);
//   - dumpability is NOT touched: apport ignores an unpackaged binary's crash (/usr/share/apport/apport, "executable does not
//     belong to a package"), so a test binary's own abort files no report; a crash report on this box comes from a PACKAGED
//     wrapper that re-raises its child's signal (strace, the uutils timeout: spikes/display-guard/apport/), and a
//     non-dumpable test binary would only blind a debugger (measured: strace no longer decodes its calls).
// There is no way to allow a non-rig display from a test: the app's own launch for the owner is not a test. The app binary
// and jahlab are not test targets and do not carry this (they run through rig.sh / the owner's launch).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace {

// scripts/display-guard.sh jah_rig_display: ":NN" or ":NN.S" with 60 <= NN <= 99, S digits.
bool rigDisplay(const char *d)
{
    if (!d || d[0] != ':' || d[1] < '6' || d[1] > '9' || d[2] < '0' || d[2] > '9')
        return false;
    const char *p = d + 3;
    if (*p == '\0')
        return true;
    if (*p != '.' || p[1] == '\0')
        return false;
    for (++p; *p; ++p)
        if (*p < '0' || *p > '9')
            return false;
    return true;
}

// the first platform name of a QT_QPA_PLATFORM value ("xcb", "offscreen:configfile=x", "wayland;xcb" -> wayland)
bool firstPlatformIs(const char *v, const char *name)
{
    const size_t n = std::strlen(name);
    return v && std::strncmp(v, name, n) == 0 && (v[n] == '\0' || v[n] == ':' || v[n] == ';');
}

__attribute__((constructor(101))) void jahDisplayGuard()
{
#if defined(__linux__)
    unsetenv("WAYLAND_DISPLAY");
    unsetenv("WAYLAND_SOCKET");
    const char *d = getenv("DISPLAY");
    const char *plat = getenv("QT_QPA_PLATFORM");
    const bool own = plat && (firstPlatformIs(plat, "xcb") || firstPlatformIs(plat, "offscreen") ||
                              firstPlatformIs(plat, "minimal"));
    if (rigDisplay(d)) {
        if (!own)
            setenv("QT_QPA_PLATFORM", "xcb", 1);
        return;
    }
    if (d && *d) {
        if (firstPlatformIs(plat, "xcb")) {
            std::fprintf(stderr, "display-guard: REFUSED (exit 2): QT_QPA_PLATFORM=xcb on DISPLAY=%s, which is not a rig "
                                 "display (:60-:99; :0 is the OWNER's) - nothing run. A rig: <tree>/scripts/rig.sh up, then "
                                 "rig.sh run <label> -- <this command>\n", d);
            std::fflush(stderr);
            _exit(2);
        }
        std::fprintf(stderr, "display-guard: DISPLAY=%s is not a rig display (:60-:99; :0 is the OWNER's) - unset, "
                             "QT_QPA_PLATFORM=offscreen (a row that needs a display: rig.sh run <label> -- <command>)\n", d);
        unsetenv("DISPLAY");
    }
    if (!firstPlatformIs(plat, "offscreen") && !firstPlatformIs(plat, "minimal"))
        setenv("QT_QPA_PLATFORM", "offscreen", 1);
#endif
}

} // namespace

// The marker source.display_guard_linked reads from the section headers (readelf -S): the object that runs the guard is linked.
extern "C" __attribute__((used, section("jah_display_guard"))) const char jah_display_guard_marker[] =
    "jah_display_guard 1 (tests/support/display_guard.cpp)";
