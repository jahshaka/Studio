#include "bridge/renderroute.h"

RenderRouteChoice chooseRenderRoute(const RenderRouteInputs &in)
{
    const bool offscreen = in.platform == QLatin1String("offscreen");
    if (!offscreen) {
        // The windowed platform renders on screen; the headless host's request has no meaning there.
        if (in.renderOffscreen)
            return { RenderRoute::Refused,
                     QStringLiteral("--render-offscreen is the headless host's (it needs --headless and a script, "
                                    "pool or MCP run): the '%1' platform renders on screen").arg(in.platform) };
        return { RenderRoute::VulkanWindowed, QString() };
    }
    if (!in.renderOffscreen) return { RenderRoute::Null, QString() };
    if (!in.displayNamed)
        return { RenderRoute::Refused,
                 QStringLiteral("--render-offscreen needs an X display (DISPLAY is unset): Ogre-Next's Vulkan render "
                                "system connects to $DISPLAY at plugin load even for an offscreen view "
                                "(VulkanXcbSupport) - run it with DISPLAY=:NN of the rig's Xvfb") };
    return { RenderRoute::VulkanOffscreen, QString() };
}

const char *renderRouteName(RenderRoute route)
{
    switch (route) {
    case RenderRoute::Null:            return "null";
    case RenderRoute::VulkanOffscreen: return "vulkan-offscreen";
    case RenderRoute::VulkanWindowed:  return "vulkan-windowed";
    case RenderRoute::Refused:         break;
    }
    return "refused";
}

RenderRouteChoice RenderRouteLatch::decide(const RenderRouteInputs &in)
{
    if (mDecided)
        return { RenderRoute::Refused,
                 QStringLiteral("the render system is chosen once per process and this one already chose '%1'")
                     .arg(QLatin1String(renderRouteName(mFirst.route))) };
    mDecided = true;
    mFirst = chooseRenderRoute(in);
    return mFirst;
}

RenderRouteLatch &processRenderRoute()
{
    static RenderRouteLatch latch;
    return latch;
}
