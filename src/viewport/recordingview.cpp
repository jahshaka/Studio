#include "viewport/recordingview.h"

#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

namespace recordingview {

jahshaka::engine::View *create(jahshaka::engine::Engine &engine, jahshaka::engine::Scene *scene,
                               const std::string &name, unsigned width, unsigned height,
                               bool helpers, QString *why)
{
    using jahshaka::engine::View;
    const auto refuse = [why, &engine](View *made) -> View * {
        if (why) *why = QString::fromStdString(engine.lastError());
        if (made) engine.destroyView(made);
        return nullptr;
    };
    View *rv = engine.createOffscreenView(name, width, height, jahshaka::engine::Colour(0.10f, 0.11f, 0.14f));
    if (!rv) return refuse(nullptr);
    // A StillPicture view: it gathers where the scene gathers, exactly like the
    // photo, rather than taking the Live contract's field-only answer.
    rv->setOffscreenContract(jahshaka::engine::OffscreenContract::StillPicture);
    // THE SWITCH (owner §10.5): the helper channel of the view's passes — graph
    // shape, set once here. The engine ANDs the mask with its reserved flags
    // (ENGINE trap 6 lives inside setHelpersVisible). The wearer's VR channel
    // stays shut: a recording of the desk is never a recording of a headset.
    rv->setHelpersVisible(helpers);
    if (!rv->setScene(scene)) return refuse(rv);
    if (!rv->setVideoReadback(true)) return refuse(rv);
    return rv;
}

void push(SceneMirror &mirror, jahshaka::engine::View *view, const iris::CameraNodePtr &camera,
          float framingAspect)
{
    if (!view) return;
    mirror.applySky(view);
    mirror.applyViewEnvironment(view, camera, /*offscreenChain=*/true);
    if (camera) mirror.applyCamera(camera, view, framingAspect);
}

}   // namespace recordingview
