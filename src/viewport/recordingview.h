#ifndef RECORDINGVIEW_H
#define RECORDINGVIEW_H

// THE RECORDER'S VIEW, MADE IN ONE PLACE (VIDEO-REC-1's view; VIDEO-REC-2 shares it).
//
// The video recorder draws a SEPARATE offscreen view of the editor's scene
// (VIDEO_CAPTURE_SPEC §10.2): a StillPicture view with its own full chain, the
// helper channel per the switch, and the NV12 readback on. Two callers build it
// and they must build the SAME view:
//
//   EngineSceneViewport::beginRecordingView — the recording itself;
//   the startup shader gate (shaderbuildgate.cpp) — which draws one behind the
//     splash so the first recording of a session compiles nothing on the UI
//     thread (the NV12 compute job compiles on its first dispatch, and the
//     view's chain at the recording's size is its own workspace).
//
// A second hand-written copy in the gate would warm a view that is not the one
// the recorder makes the day either of them changes.

#include <QString>

#include <string>

#include "irisgl/document/scenegraph/cameranode.h"

namespace jahshaka { namespace engine { class Engine; class Scene; class View; } }
class SceneMirror;

namespace recordingview {

/// The recording's picture: 1920x1080 (owner §10.2: "a separate 1080p render").
constexpr unsigned kWidth = 1920, kHeight = 1080;

/// Creates the recording view of `scene`: `width` x `height` offscreen,
/// StillPicture, the editor's helpers drawn only when `helpers`, the NV12
/// readback on. Null with `why` set when the engine refuses any step (the view
/// is then destroyed again).
jahshaka::engine::View *create(jahshaka::engine::Engine &engine, jahshaka::engine::Scene *scene,
                               const std::string &name, unsigned width, unsigned height,
                               bool helpers, QString *why);

/// The per-frame push of the recording view, beside the editor's own: the sky,
/// the per-view environment with the WHOLE chain in one push (offscreenChain),
/// and the camera. Never applyEnvironment (its scene half counts GI settle
/// frames and belongs to the editor's view alone).
void push(SceneMirror &mirror, jahshaka::engine::View *view,
          const iris::CameraNodePtr &camera, float framingAspect);

}   // namespace recordingview

#endif // RECORDINGVIEW_H
