#ifndef RENDERROUTE_H
#define RENDERROUTE_H

// THE ONE RENDER-SYSTEM DECISION (TEST-STREAMLINE-1 stage 3a): which render system this process boots, from what it
// was launched as. A pure function of its inputs (no Qt GUI, no engine) so its table is a code test; EngineHost::start
// asks it once, through the process's latch, and nothing revises the answer later in the run — Ogre::Root is a
// singleton and its render system is chosen once (Types.h EngineConfig::headless: there is no mixed mode and no
// upgrade from one to the other).
//
// The routes:
//   * Null            — the offscreen QPA platform (`--headless`, `--dump-api-docs`): the NULL render system, no
//                       display, no GPU, no View of any kind; the document verbs on the document-only viewport.
//   * VulkanOffscreen — the SCRIPTABLE HEADLESS HOST: the offscreen QPA platform AND `--render-offscreen` — no window
//                       and no UI, the engine on Vulkan, the editor viewport over an offscreen View by declaration
//                       (createOffscreenEditorViewport). It still needs an X DISPLAY: Ogre-Next's Vulkan render system
//                       builds its VulkanXcbSupport at plugin load, whose constructor connects to $DISPLAY
//                       (OgreVulkanXcbSupport.cpp:48 xcb_connect) and whose config throws "Malformed resolution
//                       string" when that connection made no video mode (:134 Full Screen "Yes", :193, :220) — the
//                       VkInstance itself needs none (OgreVulkanDevice.cpp:401). So the route is window-free, not
//                       display-free: a missing DISPLAY is refused here with that reason, never left to Ogre's line.
//   * VulkanWindowed  — every windowed platform: the editor on screen, as it always was.
//   * Refused         — the inputs ask for something no route gives, or the decision was already made (`why` says).

#include <QString>

enum class RenderRoute { Null, VulkanOffscreen, VulkanWindowed, Refused };

struct RenderRouteInputs {
    QString platform;               ///< QGuiApplication::platformName()
    bool renderOffscreen = false;   ///< `--render-offscreen` (the headless host's Vulkan request)
    bool displayNamed = false;      ///< $DISPLAY is set and non-empty (Ogre-Next's Vulkan connects to it at boot)
};

struct RenderRouteChoice {
    RenderRoute route = RenderRoute::Refused;
    QString why;                    ///< the refusal, one line; empty on a route
};

/// The table: a pure function of the inputs.
RenderRouteChoice chooseRenderRoute(const RenderRouteInputs &in);

/// "null" | "vulkan-offscreen" | "vulkan-windowed" | "refused" — the name the session log and the verbs print.
const char *renderRouteName(RenderRoute route);

/// ONE DECISION PER PROCESS. The first decide() answers chooseRenderRoute(); every later one is Refused, naming the
/// route already taken — whatever the inputs (a second engine boot in one process is the defect, not a new question).
class RenderRouteLatch {
public:
    RenderRouteChoice decide(const RenderRouteInputs &in);
    bool decided() const { return mDecided; }
    /// The route the first decide() answered (Refused before any).
    RenderRoute route() const { return mFirst.route; }

private:
    bool mDecided = false;
    RenderRouteChoice mFirst;
};

/// The process's latch (EngineHost::start's).
RenderRouteLatch &processRenderRoute();

#endif // RENDERROUTE_H
