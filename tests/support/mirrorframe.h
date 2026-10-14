#pragma once
// THE FRAME'S VIEWS FOR A SUITE (ENGINE-UPGRADE-1 U0 3a). The mirror's four apply verbs are gone: the sky, the
// environment, a further view's chain and a camera are parts of the frame's input that SceneMirror::sync emits once
// (Engine::submitFrame), applied at the top of the next renderOneFrame. A suite states them as sync's FrameViews:
//
//     mirror.sync(mirrorframe::world(view, camera));          // the sky, the environment and the camera
//     mirror.sync(mirrorframe::environment(view));             // the environment alone
//     mirror.sync(mirrorframe::camera(view, camera));          // a camera alone
//
// Several syncs before one frame merge (the newest value of each field). A suite that READS an engine state the record
// sets renders a frame first: nothing is applied on the spot.
#include "irisgl/mirror/scenemirror.h"

namespace mirrorframe {

inline SceneMirror::FrameViews one(const SceneMirror::FrameView &v) { return SceneMirror::FrameViews{ { v } }; }

inline SceneMirror::FrameViews sky(jahshaka::engine::View *view)
{
    SceneMirror::FrameView v;
    v.view = view;
    v.sky = true;
    return one(v);
}

inline SceneMirror::FrameViews environment(jahshaka::engine::View *view)
{
    SceneMirror::FrameView v;
    v.view = view;
    v.environment = true;
    return one(v);
}

inline SceneMirror::FrameViews camera(jahshaka::engine::View *view, const iris::CameraNodePtr &cam,
                                      float framingAspect = 0.0f)
{
    SceneMirror::FrameView v;
    v.view = view;
    v.camera = cam;
    v.framingAspect = framingAspect;
    return one(v);
}

/// A further view of the same world (the eyes, the recorder): its chain under `cam`'s lens, no pose.
inline SceneMirror::FrameViews viewEnvironment(jahshaka::engine::View *view, const iris::CameraNodePtr &cam = {},
                                               bool offscreenChain = false)
{
    SceneMirror::FrameView v;
    v.view = view;
    v.postFx = true;
    v.camera = cam;
    v.pose = false;
    v.offscreenChain = offscreenChain;
    return one(v);
}

/// The whole of a host's frame for one view: the sky, the environment and (when given) the camera.
inline SceneMirror::FrameViews world(jahshaka::engine::View *view, const iris::CameraNodePtr &cam = {},
                                     float framingAspect = 0.0f)
{
    SceneMirror::FrameView v;
    v.view = view;
    v.sky = true;
    v.environment = true;
    v.camera = cam;
    v.framingAspect = framingAspect;
    return one(v);
}

}   // namespace mirrorframe
