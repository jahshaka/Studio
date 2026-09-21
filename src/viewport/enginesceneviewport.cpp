#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "viewport/devicelossend.h"
#include "viewport/statsrows.h"
#include "services/scenestats.h"
#include "viewport/enginesceneviewport.h"

#include <QShowEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include "bridge/sceneworkerthreads.h"
#include "bridge/offscreenrenderscope.h"
#include "bridge/stableoffscreenrender.h"
#include "viewport/scenepicker.h"
#include "player/playback.h"
#include "irisgl/core/viewport.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/input/inputmap.h"
#include "viewport/translationgizmo.h"
#include "viewport/rotationgizmo.h"
#include "viewport/scalegizmo.h"
#include "viewport/gizmooverlay.h"
#include "viewport/cameracontrollerbase.h"
#include "viewport/editorcameracontroller.h"
#include "viewport/orbitalcameracontroller.h"
#include <QWheelEvent>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include "data/constants.h"
#include "data/primitives.h"
#include "bridge/enginehost.h"
#include "shell/mainwindow.h"
#include "data/project.h"
#include "data/database/database.h"
#include "io/assetmanager.h"
#include "ui/panels/scenehierarchywidget.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "commands/changematerialpropertycommand.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "irisgl/core/math/intersectionhelper.h"
#include "irisgl/core/geometry/trimesh.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"

#include "viewport/enginerenderdriver.h"
#include "viewport/previewframing.h"
#include "viewport/snapsettings.h"
#include "viewport/freecamerapolicy.h"
#include "bridge/secondarysurfacetonemap.h"
#include "services/editgate.h"
#include "services/materialpreviewservice.h"
#include "services/engineerrorpump.h"
#include "services/framemonitor.h"
#include "services/loadtimeline.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "commands/transformscenenodecommand.h"
#include <QUndoStack>
#include "irisgl/mirror/scenemirror.h"
#include "viewport/editordata.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "data/settingsmanager.h"
#include <QSettings>
#include "ui/controls/assetdrag.h"



using namespace jahshaka::engine;

EngineSceneViewport::EngineSceneViewport(const std::shared_ptr<Engine> &engine,
                                         EngineRenderDriver *driver, QWidget *parent)
    : EngineViewWidget(parent), mEngine(engine), mDriver(driver)
{
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);          // hover highlights gizmo handles
    mTranslateGizmo = new TranslationGizmo();
    mRotateGizmo    = new RotationGizmo();
    mScaleGizmo     = new ScaleGizmo();
    mGizmo = mTranslateGizmo;
    mFreeCam  = new EditorCameraController(this);
    mOrbitCam = new OrbitalCameraController(this);
    mPlayback = new PlayBack();
    mPlayback->setEditorViewport(this);
    mPlayback->init();                 // GL-free init: physics, animation, controllers
    resetEditorCam();
    setCameraController(mFreeCam);
    // The selection-preview preferences (CAMERAS_SPEC D3). Read once here
    // rather than per frame; the Preferences page and editor.setPip write them
    // back through setPipEnabled/setPipSize.
    if (QSettings *st = SettingsManager::getDefaultManager()->settings) {
        mPipEnabled = st->value("camera/pip", true).toBool();
        mPipSize    = qBound(0.08, st->value("camera/pip_size", 0.28).toDouble(), 0.6);
    }
    mFrameTimer.start();
    if (mDriver)
        // A lambda, not a direct member connect: syncFrame grew a default
        // argument (the fixed-dt override) and Qt's new-style connect refuses a
        // slot that takes more arguments than the signal provides.
        connect(mDriver, &EngineRenderDriver::beforeFrame, this, [this]() {
            // Every driver tick draws over whatever is on screen — including a
            // cover this viewport presented inline. presentCovered's dedupe key
            // needs a MONOTONIC "has anything drawn since" counter, and
            // View::framesPresented is not one: it RESETS to 0 whenever a scene
            // is bound, which made a close/reopen of the same world collide with
            // a previous cover and skip the present that should have raised it.
            ++mFrameEpoch;
            syncFrame();
            // The cover comes down here, one frame BEHIND the present that
            // earned it: framesPresented counts frames already on screen.
            refreshOverlay();
        });
}

EngineSceneViewport::~EngineSceneViewport()
{
    cleanup();
    delete mFreeCam; delete mOrbitCam;
    delete mPlayback;
    delete mTranslateGizmo; delete mRotateGizmo; delete mScaleGizmo;
}

void EngineSceneViewport::setCameraController(CameraControllerBase *c)
{
    if (mCamController && mCamController != c) mCamController->end();
    mCamController = c;
    if (mCamController) {
        mCamController->resetMouseStates();
        mCamController->setCamera(viewCamera());   // the piloted camera while piloting
        mCamController->start();
    }
    // Free <-> arcball inside an axis view: the incoming controller has to
    // arrive already locked, or the first drag after the switch would rotate.
    applyRotationLock();
}

void EngineSceneViewport::setFreeCameraMode()   { setCameraController(mFreeCam); }
void EngineSceneViewport::setArcBallCameraMode() { setCameraController(mOrbitCam); }
QString EngineSceneViewport::cameraMode() const
{ return mCamController == mOrbitCam ? QStringLiteral("orbit") : QStringLiteral("free"); }

// Views dropdown / view.* shortcuts / editor.setView verb: snap to a canonical
// view. Every view remembers its camera between visits (per viewport session):
// switching saves the outgoing view's camera and restores the incoming one's —
// perspective keeps its full free/orbit pose across trips into ortho views,
// each ortho view keeps its own pan + zoom. A first visit to an axis view gets
// the standard framing (the orbital controller animates there via its lerp;
// the free camera turns in place). Re-picking the current view re-snaps it.
// The grid's effective state for the current canonical view (owner rule,
// 2026-09-06): scenes ship a tiled floor, so the perspective view shows the
// grid only when the user turns it on (mShowGrid, the View-menu toggle); the
// axis views ALWAYS show it — that is where a grid earns its keep for
// alignment — oriented into the plane that faces the view axis, because a
// floor grid seen edge-on from `front` or `left` is one useless line.
static void gridStateForView(const QString &view, bool showPref,
                             bool &on, SceneMirror::GridPlane &plane)
{
    using GridPlane = SceneMirror::GridPlane;
    plane = GridPlane::Floor;
    on = showPref;
    if (view == QLatin1String("perspective")) return;
    on = true;
    if (view == QLatin1String("front") || view == QLatin1String("back"))
        plane = GridPlane::FrontXY;
    else if (view == QLatin1String("left") || view == QLatin1String("right"))
        plane = GridPlane::SideYZ;
    // top / bottom keep the floor plane.
}

// Everything else the grid needs from the current view (owner report
// 2026-09-07 — "the axis-view grid is not there, and where it is it is white"):
//
//   * WHAT COLOUR it is. One blue-grey for every plane made an axis view read
//     as "white lines". Each axis view is tinted by the axis its grid plane
//     FACES — Y green for top/bottom, Z blue for front/back, X red for
//     left/right — the gizmo's own axis colours, so the view announces itself.
//     Perspective keeps the neutral editor grid.
static void gridColoursForView(const QString &view,
                               jahshaka::engine::Colour &minor,
                               jahshaka::engine::Colour &major)
{
    using Colour = jahshaka::engine::Colour;
    // The perspective default: the neutral blue-grey SceneMirror ships.
    minor = Colour(0.46f, 0.48f, 0.52f, 0.28f);
    major = Colour(0.62f, 0.64f, 0.68f, 0.50f);
    if (view == QLatin1String("perspective")) return;
    // Axis views: brighter (they ARE the alignment aid there) and tinted.
    if (view == QLatin1String("top") || view == QLatin1String("bottom")) {
        minor = Colour(0.36f, 0.62f, 0.40f, 0.42f);   // Y — green
        major = Colour(0.48f, 0.86f, 0.54f, 0.75f);
    } else if (view == QLatin1String("front") || view == QLatin1String("back")) {
        minor = Colour(0.34f, 0.50f, 0.74f, 0.42f);   // Z — blue
        major = Colour(0.44f, 0.66f, 0.98f, 0.75f);
    } else {
        minor = Colour(0.72f, 0.38f, 0.38f, 0.42f);   // X — red
        major = Colour(0.94f, 0.48f, 0.48f, 0.75f);
    }
}

// The one push: visibility + plane + spacing + floor offset + colours, from
// the current canonical view. Called from every place that syncs the mirror.
void EngineSceneViewport::pushGridForView(bool helpers)
{
    if (!mMirror) return;
    bool on = false;
    SceneMirror::GridPlane plane = SceneMirror::GridPlane::Floor;
    gridStateForView(mCameraView, mShowGrid, on, plane);
    jahshaka::engine::Colour minor, major;
    gridColoursForView(mCameraView, minor, major);
    mMirror->setGridColours(minor, major);
    mMirror->setGrid(on && helpers, SnapSettings::translateSize(), plane);
    // What was actually pushed, for editor.overlays().gridPlane: the plane
    // follows the VIEW, never the camera's pose, so panning inside an axis view
    // can never tip the grid out of the view plane — and a script can now
    // assert that instead of taking it on trust.
    mGridPlanePushed = plane == SceneMirror::GridPlane::FrontXY ? QStringLiteral("frontXY")
                     : plane == SceneMirror::GridPlane::SideYZ  ? QStringLiteral("sideYZ")
                                                                : QStringLiteral("floor");
}

// EVERY IN-VIEWPORT EDITOR HELPER, PUSHED FROM ONE PLACE.
//
// G (Game View) hides them all; play mode hides the grid, the camera bodies and
// the GI boxes too (the Unreal look), while the rest keep their play behaviour.
//
// IT IS A FUNCTION BECAUSE IT HAS TO BE REVERSIBLE (SS1 review item 3). A user's
// screenshot leaves the helpers out (owner, 2026-09-13), which means something
// has to take them away for the duration of one picture and PUT THEM BACK — and
// "put them back" cannot be approximated, because the very next thing a script
// may ask for is the plain readback the entire pixel corpus asserts never moves.
// The first cut cleared five switches by hand and trusted refreshOverlay() to
// restore them; refreshOverlay only pushes the OVERLAY description, so the
// restore happened by accident at the next syncFrame and two screenshots in one
// script run returned a helper-less second picture. Now both callers push the
// same state from the same owned fields, so the round trip is exact by
// construction rather than by hope.
void EngineSceneViewport::pushEditorHelpers(bool helpers)
{
    if (!mMirror) return;
    // THE EDITOR NEVER HIDES THE DEFAULT FLOOR (PLAYER-FLOOR-1, owner
    // 2026-09-18: the setting is about the PLAYER). Said here, on every push,
    // because the mirror is SHARED with the Player's view: the Player switches
    // the hide on before its own sync and this is where the editor takes it
    // back, so a page switch in either direction is exact and neither host
    // inherits the other's answer. Game View is still the editor — a scene
    // being looked at without furniture is not the finished thing.
    mMirror->setHideDefaultFloor(false);
    mMirror->setLightWires(mShowLightWires && helpers);
    // Camera bodies + frustum wires (CAMERAS_SPEC D2). Same "editor helper"
    // rule as the light wires — G (Game View) and play hide them — but a
    // separate toggle, because they are a different object and the View
    // Options row for light wires must not silently govern cameras.
    mMirror->setCameraBodies(helpers && !mPlaying);
    mMirror->setHighlightWireframe(mSelectionWireframe);
    // No selection outline for the World root (the whole scene would glow)
    // or for the built-in ground PLANE — owner ask 2026-08-31. The first
    // implementation tested `isBuiltIn`, which every Add-menu primitive
    // carries (addBuiltinPrimitive sets it on cubes, spheres, capsules —
    // and the sample scenes are assembled from exactly those), so every
    // primitive silently lost its outline while selection/gizmo/panel kept
    // working (2026-09-06 sighting; cost a day of misattributed reports).
    // The exclusion is the GROUND MESH specifically, nothing wider.
    // The SET, member by member (EDITOR_MULTISELECT_SPEC §2.3) — the two
    // exclusions above apply per member, not to the selection as a whole.
    QList<iris::SceneNodePtr> highlight;
    if (helpers) {
        for (const auto &node : mSelectedSet) {
            if (!node) continue;
            if (mScene && node == mScene->getRootNode()) continue;
            if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
                const auto mn = node.staticCast<iris::MeshNode>();
                if (mn->isBuiltIn && mn->meshPath == QStringLiteral(":/models/ground.obj"))
                    continue;
            }
            highlight.append(node);
        }
    }
    // The PRIMARY goes over EXPLICITLY (EDITOR_MULTISELECT_SPEC D4 b): the
    // two exclusions above run per member, so the primary can be filtered
    // out of `highlight` while secondaries survive — and then "the first
    // entry" would hand the brighter outline to a node that is not the
    // primary. The mirror re-checks membership and drops a primary that is
    // not in the list.
    mMirror->setHighlightedNodes(highlight, mSelectedNode);
    // Grid spacing = the translate snap size ([ and ] re-space it live).
    pushGridForView(helpers && !mPlaying);
    // The GI volume boxes (LIGHTING_FIX fix 9): an editor helper like the
    // rest, so Game View and play hide them.
    mMirror->setGiVolumeOverlay(mShowGiVolume && helpers && !mPlaying);
}

// The canonical AXIS views and the orientation each one snaps to. File scope
// because two questions need it: which view a name IS (setCameraView) and
// whether the current view is an axis one (the rotation lock).
namespace {
struct AxisView { const char *name = nullptr; float yaw = 0.0f; float pitch = 0.0f; };
const AxisView kAxisViews[] = {
    { "top", 0.f, -90.f }, { "bottom", 0.f, 90.f },
    { "left", 90.f, 0.f }, { "right", -90.f, 0.f },
    { "front", 0.f, 0.f }, { "back", 180.f, 0.f },
};
const AxisView *findAxisView(const QString &view)
{
    for (const auto &v : kAxisViews)
        if (view == QLatin1String(v.name)) return &v;
    return nullptr;
}
}   // namespace

// THE AXIS-VIEW ROTATION LOCK (owner report 2026-09-08: "when in Top/Left/
// Right/Bottom views we should not be able to rotate the camera — only pan and
// zoom; the camera should be locked top-down, bottom-up etc").
//
// An axis view is a MEASURING view: it is orthographic, its grid is turned into
// the view plane, and the one thing every editor guarantees about it is that it
// keeps looking down its axis. Rotating out of it left the user in a tilted
// orthographic view that still called itself "top" — the state the owner
// reported. So while an axis view is current, rotation GESTURES are ignored
// (the controllers do the ignoring — CameraControllerBase::setRotationLocked).
//
// PILOTING is the exception: a piloted scene camera is being FLOWN, and the
// canonical view the explorer was left in says nothing about it (setCameraView
// refuses while piloting for the same reason). Placement VERBS are the other:
// editor.setCamera/frameNode write the pose they are given — the lock is about
// what a drag may do, not about what the camera may ever be.
bool EngineSceneViewport::cameraRotationLocked() const
{
    if (mPilot || !findAxisView(mCameraView)) return false;
    // AND STILL ORTHOGRAPHIC. The lock's subject is the orthographic measuring
    // view, not the name. The toolbar's projection button used to flip the
    // projection behind setCameraView's back; it goes through applyCameraView
    // now (mainwindow.cpp, changeProjection), so the two agree — but reading
    // the projection here rather than trusting the view name keeps the lock
    // from ever outliving the state it exists to protect, whoever changed it.
    return mEditorCam && mEditorCam->projMode == iris::CameraProjection::Orthogonal;
}

void EngineSceneViewport::applyRotationLock()
{
    const bool locked = cameraRotationLocked();
    if (mFreeCam) mFreeCam->setRotationLocked(locked);
    if (mOrbitCam) mOrbitCam->setRotationLocked(locked);
}

bool EngineSceneViewport::setCameraView(const QString &view)
{
    const AxisView *axis = findAxisView(view);
    const bool persp = (view == QLatin1String("perspective"));
    if (!axis && !persp) return false;   // unknown name: refuse before touching state
    // Canonical views are the EXPLORER's — its per-view camera memory, its
    // projection switch. Snapping one while piloting would silently fly the
    // user's camera to an axis view and overwrite their shot; refuse instead
    // (eject first, which the dropdown and editor.pilot(null) both do).
    if (mPilot) return false;

    // Save the outgoing view's camera — but not on a re-pick of the current
    // view, which must re-snap (the pre-memory behavior), not restore what
    // was saved a moment ago.
    const bool switching = (view != mCameraView);
    if (switching) saveViewState();

    const auto projection = persp ? iris::CameraProjection::Perspective
                                  : iris::CameraProjection::Orthogonal;
    if (mEditorCam) mEditorCam->setProjection(projection);
    if (mScene && mScene->camera && mScene->camera != mEditorCam)
        mScene->camera->setProjection(projection);

    if (switching && restoreViewState(view)) {
        mCameraView = view;
        applyRotationLock();
        return true;
    }

    // First visit (or a re-pick): the standard framing. "perspective" keeps
    // the current orientation — it only ever lands here before its pose has
    // been saved once, i.e. when it IS the current pose already.
    if (axis) {
        // The snap itself is a navigation and must not be refused by the lock
        // it is about to arm — arm it AFTER the camera has been placed. (The
        // arcball's snap is a lerp inside update(); it does not consult the
        // lock either, which is why the order only matters for reading well.)
        if (mCamController == mOrbitCam && mOrbitCam)
            mOrbitCam->setAxisView(axis->yaw, axis->pitch);
        else if (mFreeCam)
            mFreeCam->setAxisView(axis->yaw, axis->pitch);
    }
    mCameraView = view;
    applyRotationLock();
    return true;
}

void EngineSceneViewport::saveViewState()
{
    if (!mEditorCam) return;
    ViewCameraState s;
    s.pos = mEditorCam->getLocalPos();
    s.rot = mEditorCam->getLocalRot();
    s.orthoSize = mEditorCam->orthoSize;
    if (mOrbitCam) s.distFromPivot = mOrbitCam->distFromPivot;
    mViewStates.insert(mCameraView, s);
}

bool EngineSceneViewport::restoreViewState(const QString &view)
{
    const auto it = mViewStates.constFind(view);
    if (it == mViewStates.constEnd() || !mEditorCam) return false;
    const ViewCameraState &s = *it;
    mEditorCam->setLocalPos(s.pos);
    mEditorCam->setLocalRot(s.rot);
    mEditorCam->setOrthagonalZoom(s.orthoSize);   // ortho zoom; inert in perspective
    mEditorCam->update(0);
    // Resync the active controller with the restored pose — the same resync
    // focusOnNode/setEditorData use. The free cam re-derives yaw/pitch (and
    // wheel zoom from orthoSize); the orbital cam re-derives its pivot from
    // the restored pose + orbit distance, targets matched so nothing lerps.
    if (mCamController == mOrbitCam && mOrbitCam) {
        mOrbitCam->distFromPivot = s.distFromPivot;
        mOrbitCam->setCamera(mEditorCam);
    } else if (mCamController) {
        mCamController->setCamera(mEditorCam);
    }
    return true;
}

void EngineSceneViewport::clearViewStates()
{
    mViewStates.clear();
    mCameraView = QStringLiteral("perspective");
    applyRotationLock();   // back to perspective: the camera turns again
}

// The resync every camera mover in this file owes the active controller: the
// controllers steer with (yaw, pitch) — plus a pivot, for the arcball — and a
// camera that moved by any other route has to be re-read.
//
// It is a READ. Both setCamera()s used to finish by REBUILDING the pose from
// what they had just decomposed (updateCameraRot), which round-trips exactly
// for a roll-free rotation and silently destroys any roll otherwise — so
// adoption alone flattened socketed and authored cameras, permanently, on the
// document (fixed 2026-09-06; the contract now lives on
// CameraControllerBase::setCamera). Nothing here writes a camera any more.
void EngineSceneViewport::resyncCameraController(float orbitDistance)
{
    if (!mCamController || !viewCamera()) return;
    if (mCamController == mOrbitCam && mOrbitCam) {
        if (orbitDistance > 0.0f) mOrbitCam->distFromPivot = orbitDistance;
        mOrbitCam->setCamera(viewCamera());
    } else {
        mCamController->setCamera(viewCamera());
    }
}

// editor.setCamera: place the camera outright. Every field is optional, so
// "move here, keep looking the same way" is a position on its own.
bool EngineSceneViewport::setCameraPose(const EditorCameraPose &pose)
{
    // The VIEW camera: while piloting, "place the camera" means the camera the
    // user is looking through, which is the piloted one — piloting doubles as
    // placement (CAMERAS_SPEC D8).
    const iris::CameraNodePtr cam = viewCamera();
    if (!cam) return false;
    if (pose.hasPosition) cam->setLocalPos(pose.position);
    if (pose.hasLookAt) {
        cam->lookAt(pose.lookAt);
        // A target beyond the far plane renders as an empty frame and looks
        // like the verb did nothing — grow the plane to contain it, the same
        // adaptation focusOnNode makes (previewframing.h's rationale).
        const float dist = cam->getLocalPos().distanceToPoint(pose.lookAt);
        cam->farClip = qMax(cam->farClip, dist * 2.0f);
    } else if (pose.hasRotation) {
        cam->setLocalRot(pose.rotation);
    }
    if (pose.fovDegrees > 0.0f) cam->angle = pose.fovDegrees;
    cam->update(0.0f);
    // The arcball keeps its orbit distance; its pivot is re-derived from the
    // new pose inside setCamera().
    resyncCameraController();
    return true;
}

// editor.frameNode: focusOnNode's framing maths with the view direction taken
// from {yaw, pitch} instead of from wherever the camera happens to be. The
// yaw/pitch convention is the arcball's own (updateCameraRot): the eye sits at
// pivot + Quat::fromEulerAngles(pitch, yaw, 0) * (0,0,1) * distance, which is
// also what setCameraView's axis table uses (top = yaw 0, pitch -90).
bool EngineSceneViewport::frameNode(iris::SceneNodePtr sceneNode, const EditorFraming &framing)
{
    const iris::CameraNodePtr cam = viewCamera();   // the piloted camera while piloting
    if (!sceneNode || !cam) return false;
    sceneNode->update(0.0f);

    iris::Vec3 target = sceneNode->getGlobalPosition();
    float radius = 1.0f;
    const iris::AABB bounds = preview::worldBoundingBox(sceneNode);
    if (bounds.getMin().x() <= bounds.getMax().x()) {   // non-empty (meshes exist)
        target = bounds.getCenter();
        radius = qMax(0.05f, bounds.getSize().length() * 0.5f);
    }
    const float dist = framing.distance > 0.0f
                           ? framing.distance
                           : qMax(1.0f, preview::framingDistance(radius, cam->effectiveFovDegrees()));

    // Missing yaw/pitch = "keep looking from where I look now", which makes
    // frameNode(id) the verb form of the F key.
    float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
    cam->getLocalRot().getEulerAngles(&pitch, &yaw, &roll);
    if (framing.hasYaw) yaw = framing.yawDegrees;
    if (framing.hasPitch) pitch = framing.pitchDegrees;

    const iris::Quat rot = iris::Quat::fromEulerAngles(pitch, yaw, 0.0f);
    const iris::Vec3 offset = rot.rotatedVector(iris::Vec3(0, 0, 1));
    cam->setLocalPos(target + offset * dist);
    cam->setLocalRot(rot);
    float nearClip, farClip;
    preview::clipPlanesForFraming(dist, radius, nearClip, farClip);
    cam->farClip = qMax(cam->farClip, farClip);
    cam->update(0.0f);

    // The arcball adopts THIS pivot: hand it the distance we framed at and let
    // setCamera() re-derive the pivot, which lands back on `target` exactly
    // (the pose above is already the controller's own orbit formula).
    resyncCameraController(dist);
    mLastOrbitPivot = target;   // the working distance a later Alt+drag falls back to
    return true;
}

void EngineSceneViewport::setActiveGizmo(Gizmo *g)
{
    if (mGizmo == g) return;
    if (mGizmo && mGizmo->isDragging()) mGizmo->endDragging();
    mMouseDrag = false;              // whoever owned that drag, it is over
    mGizmo = g;
    if (mGizmo) { if (mSelectedNode) mGizmo->setSelectedNode(mSelectedNode); else mGizmo->clearSelectedNode(); }
}
void EngineSceneViewport::setGizmoLoc()   { setActiveGizmo(mTranslateGizmo); }
void EngineSceneViewport::setGizmoRot()   { setActiveGizmo(mRotateGizmo); }
void EngineSceneViewport::setGizmoScale() { setActiveGizmo(mScaleGizmo); }
void EngineSceneViewport::setGizmoTransformToLocal()
{
    mTranslateGizmo->setTransformSpace(GizmoTransformSpace::Local);
    mRotateGizmo->setTransformSpace(GizmoTransformSpace::Local);
    mScaleGizmo->setTransformSpace(GizmoTransformSpace::Local);
}
void EngineSceneViewport::setGizmoTransformToGlobal()
{
    mTranslateGizmo->setTransformSpace(GizmoTransformSpace::Global);
    mRotateGizmo->setTransformSpace(GizmoTransformSpace::Global);
    mScaleGizmo->setTransformSpace(GizmoTransformSpace::Global);
}

QString EngineSceneViewport::gizmoTransformSpace() const
{
    // The three gizmos are always set together, so any one of them answers.
    return (mTranslateGizmo && mTranslateGizmo->getTransformSpace() == GizmoTransformSpace::Local)
               ? QStringLiteral("local") : QStringLiteral("global");
}

// THE CAMERA THE PICTURE IS TAKEN THROUGH (PLAY-SELECT-1). Outside play this
// is viewCamera() — renderCamera's own first term — so nothing but a run can
// make the two differ. Inside one, the document decides (Scene::renderCamera:
// the armed active camera, or the host's again while an avatar is possessed),
// and clicking during play has to unproject the frustum the user is LOOKING
// THROUGH, not the explorer's.
iris::CameraNodePtr EngineSceneViewport::pickCamera() const
{
    const iris::CameraNodePtr host = viewCamera();
    return mScene ? mScene->renderCamera(host) : host;
}

// The widget's rect in its WINDOW's coordinates — read from the live layout
// (mapTo), so a moved dock, a hidden tray or a resize can never make it stale.
QRect EngineSceneViewport::widgetRectInWindow() const
{
    const QWidget *top = window();
    if (!top || top == this) return QRect();
    return QRect(mapTo(const_cast<QWidget *>(top), QPoint(0, 0)), size());
}

bool EngineSceneViewport::playPossessing() const
{
    if (!mScene) return false;
    const iris::AvatarPossession *possession = mScene->getPossession();
    return possession && possession->isPossessing();
}

// THE ONE OWNERSHIP PREDICATE (PLAY-SELECT-1, owner R13). A run owns the
// pointer only when something is really driving it — a possessed avatar — and
// never while ejected. Everything else about play is unchanged: the right
// button, the wheel and the gameplay keys stay with the run (its fly and its
// character), because with nobody possessed the run flies the very camera the
// editor owns (PlayBack::playCamera) and the Gameplay shortcut rows have to
// keep meaning what they say. What comes back to the editor here is the plain
// LEFT CLICK: the pick, the selection and the gizmo.
bool EngineSceneViewport::runOwnsPointer() const
{
    if (!mPlaying) return false;
    if (mPlayEjected) return false;
    return playPossessing();
}

QString EngineSceneViewport::playInputOwner() const
{
    return runOwnsPointer() ? QStringLiteral("controller") : QStringLiteral("editor");
}

void EngineSceneViewport::setPlayEjected(bool ejected)
{
    if (mPlayEjected == ejected) return;
    mPlayEjected = ejected;
    // A HAND-OVER DROPS THE INPUT STATE ON BOTH SIDES (§8.3 rule 1's argument,
    // applied to the eject edge): a key held at the moment of the hand-over
    // produces no release edge on the side that was listening, so W held while
    // ejecting would walk the character for ever and an arrow held while
    // possessing again would fly the editor camera for ever.
    if (mPlayback) mPlayback->clearInputState();
    if (mCamController) mCamController->clearKeys();
    iris::InputSystem::instance().clearKeys();
    mVertexSnapHeld = false;
}

bool EngineSceneViewport::mouseRay(iris::Vec3 &rayPos, iris::Vec3 &rayDir, iris::Vec3 &viewDir) const
{
    const iris::CameraNodePtr cam = pickCamera();   // the PILOT, or the run's own shot
    if (!cam) return false;
    viewDir = cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1));
    // THE VIEW THE GIZMO IS PICKED IN (smoke S15). Every gizmo hit test and
    // every drag in this widget goes through a ray built here, so the pixel
    // context screen-space ring picking needs is set here too — one place, and
    // it cannot be out of step with the ray built two lines below.
    const QRectF picture = pictureRect();
    if (mGizmo) mGizmo->setPickView(cam, float(picture.width()), float(picture.height()),
                                    float(devicePixelRatioF()));
    if (!mHaveMouse) return false;
    iris::Vec3 a, b;
    pictureSegment(cam, mMousePos, a, b);
    rayPos = a; rayDir = (b - a).normalized();
    return true;
}

QRectF EngineSceneViewport::pictureRect() const
{
    const QRectF whole(0.0, 0.0, width(), height());
    const iris::CameraNodePtr cam = viewCamera();
    // The same condition the frame tick uses to leave the aspect alone
    // (syncFrame: a piloted camera that constrains its aspect keeps the
    // authored number, and the engine letterboxes to it).
    if (!mPilot || !cam || !cam->constrainAspect || width() <= 0 || height() <= 0) return whole;
    const double aspect = cam->aspectRatio > 0.01f ? cam->aspectRatio : 16.0 / 9.0;
    const double target = double(width()) / double(height());
    // chain::letterboxRect (OgreChain.cpp), in pixels.
    if (aspect > target) {                 // wider than the widget: bars top and bottom
        const double h = double(width()) / aspect;
        return QRectF(0.0, (double(height()) - h) * 0.5, double(width()), h);
    }
    const double w = double(height()) * aspect;   // taller: bars left and right
    return QRectF((double(width()) - w) * 0.5, 0.0, w, double(height()));
}

void EngineSceneViewport::pictureSegment(const iris::CameraNodePtr &cam, const QPointF &point,
                                         iris::Vec3 &segStart, iris::Vec3 &segEnd) const
{
    // screenSegment sets the camera's aspect from the size it is given (it has
    // to: it unprojects through the camera's matrices). Given the WIDGET, that
    // silently rewrote a piloted, constrained camera's authored aspect with
    // the viewport's on every hover — the letterbox then matched the widget
    // and vanished, and a save wrote the viewport's shape into the camera.
    // Given the picture, the aspect it sets is the one already there.
    const QRectF picture = pictureRect();
    ScenePicker::screenSegment(cam, picture.width(), picture.height(),
                               point - picture.topLeft(), segStart, segEnd);
}

// THE ONE SCENE, BUILT WHEN SOMEBODY WHO DRAWS IT ASKS — and NOT gated on this
// widget's own View any more (SMOKE-FIX-1, 2026-09-18).
//
// The Player page is a second view on THIS scene (lane PLAYER-1), so a session
// that reaches the Player without ever showing the editor — the desktop tile's
// Play button, a `--vr` boot that starts on the Desktop page — asked for a
// scene that did not exist and got null: EnginePlayerScene::attach refused, and
// the Player's window showed the stale pixels of the page underneath. The scene
// is what the Player needs; this widget's own on-screen View is not.
//
// AN EXPLICIT CALL, NEVER A GETTER (the fix round's F1). `engineScene()` and
// `sceneMirror()` are read from per-frame paths — VrApi::pushProxies rides the
// render driver's beforeFrame, which ticks from the shell's constructor onwards
// — so building the scene inside them made EVERY windowed process construct the
// editor scene, its worker pool and its SceneMirror on its first tick, editor
// or no editor, and hammer Engine::mLastError while the Hlms did not exist yet.
// The two callers entitled to ask are the ones that DRAW this scene without
// being this widget: EnginePlayerView::adoptEditorScene and EditorVrPreview.
//
// THE PIN'S STARTUP-ORDER LAW IS STILL OBEYED, by the engine rather than by a
// guess: `createScene` returns null before the first View exists in the process
// (Engine.h, "ORDER MATTERS"), so asking early is safe and simply answers "not
// yet" — which is the honest answer, and the one the Player's refusal repeats.
// In practice a View always exists by the time either caller asks: the Player's
// own is created in its show event before the page's start(), and a VR session
// begins from a page that has one.
bool EngineSceneViewport::ensureEngineScene()
{
    if (mEngineScene) return true;
    if (!mEngine) return false;
    // THE scene the user watches at frame rate: it gets the machine's worker
    // threads, not the engine's historical 2 (fps audit F3,
    // bridge/sceneworkerthreads.h).
    mEngineScene = mEngine->createScene("editor-" + std::to_string(++mViewSerial) + "-" +
                                        std::to_string(reinterpret_cast<uintptr_t>(this)),
                                        sceneworkers::count(sceneworkers::Tier::Primary));
    if (!mEngineScene) return false;
    mEngineScene->setAmbient(Colour(0.25f, 0.27f, 0.32f), Colour(0.15f, 0.15f, 0.18f));
    // The mirror before the bind: bindViewToScene drops its environment latches
    // (see there), and on this path there is nothing to drop — but the order has
    // to be the same one on both paths or that line reads a null.
    mMirror.reset(new SceneMirror(mEngineScene));
    mOverlay.reset(new GizmoOverlay(mEngineScene));
    if (mScene) mMirror->setSource(mScene);
    bindViewToScene();
    return true;
}

// Binds THIS widget's View to the one scene, with the view-side state that goes
// with it. Separate from ensureEngineScene because the two no longer happen at
// the same moment: the scene can be born before this widget has a View at all
// (the Player asked for it first), and the View can be born — or reborn — after
// the scene (the show event, a native-window recreation).
void EngineSceneViewport::bindViewToScene()
{
    if (!view() || !mEngineScene) return;
    // ONLY ON A REAL BIND. Everything below is per-VIEW state that the document
    // then owns through the mirror, and the mirror DEBOUNCES on "already
    // pushed" — so re-asserting it on an already-bound view (this is called at
    // every editor page entry) would turn shadows back on behind a world that
    // has them off, with nothing to correct it. The engine refuses a second
    // setScene on a bound View anyway (Engine.h).
    if (view()->scene() == mEngineScene) return;
    view()->setScene(mEngineScene);
    view()->setShadows(true);           // directional PSSM; lights opt in via the document
    // THE WEARER'S OWN FURNITURE IS DRAWN AT THE DESK TOO (VR_SPEC §5 phase 4;
    // owner 2026-09-17). The VR channel (Scene::setNodeVrHelper) carries the
    // controller proxies — and later the controller ray and the in-VR gizmo —
    // and this is the ONE desktop view that opens it: the editor's. A
    // thumbnail, a preview, the Player's window and the offscreen view a user's
    // screenshot renders through all leave it shut, which is what keeps a
    // wearer's hands out of pictures that are not theirs.
    view()->setVrHelpersVisible(true);
    // AND THE MIRROR'S PER-VIEW LATCHES GO (the same line, for the same reason,
    // as EnginePlayerScene::attach): part of what applyEnvironment pushes is per
    // VIEW — the whole post chain, MSAA, the shadow flag — and the mirror
    // debounces on "already pushed", which may be true OF THE PLAYER'S VIEW.
    // Dropping the latches is what makes this view's chain get built at all when
    // the Player got here first.
    if (mMirror) mMirror->invalidateEnvironment();
}

void EngineSceneViewport::viewRecreated()
{
    // The old View took its camera, workspace and scene binding with it.
    bindViewToScene();
    // A fresh View starts its present count at zero, so the baseline must too —
    // otherwise presentsSinceBind() reads a subtraction of a larger number and
    // the cover never comes down again.
    mPresentBaseline = 0;
    refreshOverlay();
}

void EngineSceneViewport::resizeEvent(QResizeEvent *e)
{
    EngineViewWidget::resizeEvent(e);   // records the pending resize on the View
    // See the header. The two frames presentCovered draws are exactly what is
    // needed: the first applies the pending resize (the engine defers it to
    // frame time), the second presents the cover at the new size.
    if (mCoverUp) presentCovered();
}

void EngineSceneViewport::showEvent(QShowEvent *e)
{
    EngineViewWidget::showEvent(e);
    // The native window exists now: bind a View to it, then the engine scene.
    if (!view() && mEngine)
        createView(mEngine, "editor-viewport-" + QString::number(reinterpret_cast<uintptr_t>(this)),
                   Colour(0.10f, 0.11f, 0.14f));
    // …and bind it, whether the scene is born here or was born earlier for the
    // Player (ensureEngineScene returns early then, so the bind is its own call).
    ensureEngineScene();
    bindViewToScene();
    // Becoming visible with nothing presented yet is exactly the moment the
    // stale pixels underneath would show through — and on the FIRST open there
    // was no View at all until three lines ago, so there is nothing of ours in
    // that window and the X server is still showing the page we came from.
    //
    // MECHANISM M2 (STATS_OVERLAY_SPEC §6.3): present the covered frames HERE,
    // synchronously, inside the show handler. Qt sends showEvent around the
    // XMapWindow, so this is the closest analogue there is to the Qt cover's
    // repaint() — and it is a race with the map rather than an ordering ahead
    // of it, which is why §6.3 rates it weaker than M1 and stronger than
    // nothing. presentCovered draws TWO frames, because a Vulkan present is
    // queued and the first is not yet on screen (M3).
    presentCovered();
}

iris::SceneNodePtr EngineSceneViewport::pickAt(const QPointF &point, bool selectRootObject,
                                                iris::Vec3 *hitPoint, bool forcePickable)
{
    const iris::CameraNodePtr cam = pickCamera();   // the PILOT, or the run's own shot
    if (!mScene || !cam) return iris::SceneNodePtr();
    iris::Vec3 a, b;
    pictureSegment(cam, point, a, b);
    const auto hits = ScenePicker::pickAll(mScene, a, b, cam->getGlobalPosition(), forcePickable);
    const ScenePick best = ScenePicker::nearest(hits);
    if (hitPoint && best.node) *hitPoint = best.hitPoint;
    // MEMBERSHIP, not equality (EDITOR_MULTISELECT_SPEC §3.5): Ctrl+clicking a
    // part of an asset that is already IN the set must behave like clicking a
    // part of the selected asset, not like a fresh click on the asset root.
    return ScenePicker::resolveRootSelection(best.node, mSelectedSet, selectRootObject);
}

// WHAT A DROP AT THIS PIXEL LANDS ON, AND WHETHER IT IS LOCKED (lane SPACE-2
// item 5, owner correction 2026-09-15).
//
// THE MODEL, in the owner's words: "we can select the floor like any other
// asset, it is just LOCKED by default (and has no outline); if I unlock it I
// can click to select it; you can't drop a material on it while it is locked."
// The code already says exactly that with ONE flag — `pickable`. The hierarchy
// row's lock icon IS setPickable (scenehierarchywidget.cpp lockItemAndChildren
// / releaseItemAndChildren), and the default floor ships with it off
// (services/defaultfloor.cpp). So there is no second concept to unify: locked
// == !isPickable().
//
// A locked node therefore takes no drop — but the drop must SAY SO rather than
// vanish, which is what it used to do: the material and texture branches
// resolved their target with an ordinary pick, so the floor was simply not
// there. A material dragged onto it did nothing at all, silently, and a
// texture fell into the "empty space" branch and spawned a floating image
// plane. This resolves what is under the cursor whether it is locked or not
// and REPORTS the lock; the callers refuse with a toast that names the node.
//
// `selectRootObject` stays FALSE: a drop applies to the surface under the
// cursor, not to the whole imported asset it belongs to.
iris::SceneNodePtr EngineSceneViewport::dropTargetAt(const QPointF &point, bool *locked)
{
    if (locked) *locked = false;
    const iris::CameraNodePtr cam = viewCamera();
    if (!mScene || !cam) return iris::SceneNodePtr();
    iris::Vec3 a, b;
    pictureSegment(cam, point, a, b);
    // NO ICONS IN THE WAY, either: a material and an image both need a SURFACE,
    // so a light's icon, a camera's body and a decal's box — none of which can
    // wear one — must not swallow a drop meant for the wall behind them.
    const auto hits = ScenePicker::pickAll(mScene, a, b, cam->getGlobalPosition(),
                                           /*forcePickable*/ true, /*includeLights*/ false,
                                           /*includeDecals*/ false, /*refreshTransforms*/ true,
                                           /*includeCameras*/ false);
    const iris::SceneNodePtr node = ScenePicker::nearest(hits).node;
    if (node && locked) *locked = !node->isPickable();
    return node;
}

// A LOCKED NODE REFUSES THE DROP, OUT LOUD (owner correction, 2026-09-15).
// Returns true when the drop was refused, so each branch can stop right there —
// nothing applied, nothing spawned, and a toast that names the node and the
// one thing the user has to do about it.
bool EngineSceneViewport::refuseDropOnLocked(const iris::SceneNodePtr &node, const QString &what)
{
    if (!node || node->isPickable()) return false;
    if (mMainWindow)
        mMainWindow->showViewportToast(
            tr("Locked"),
            tr("%1 is locked — unlock it in the hierarchy to apply %2.")
                .arg(node->getName(), what));
    return true;
}

iris::Vec3 EngineSceneViewport::dropPositionAt(const QPointF &point)
{
    iris::Vec3 hit;
    if (pickAt(point, false, &hit, true)) return hit;
    // Ground plane (y = 0), like the legacy viewport's sceneFloor.
    const iris::CameraNodePtr cam = pickCamera();   // the PILOT, or the run's own shot
    if (!cam) return iris::Vec3();
    iris::Vec3 a, b;
    pictureSegment(cam, point, a, b);
    const iris::Plane floor = iris::IntersectionHelper::computePlaneND(iris::Vec3(100, 0, 100), iris::Vec3(-100, 0, 100), iris::Vec3(-100, 0, -100));
    float t; iris::Vec3 q;
    if (iris::IntersectionHelper::intersectSegmentPlane(a, a + (b - a).normalized() * 1024.0f, floor, t, q)) return q;
    return iris::Vec3();
}

IEditorViewport::GizmoPickResult EngineSceneViewport::gizmoHitTest(const QPointF &point) const
{
    GizmoPickResult out;
    if (!mGizmo || !mSelectedNode) return out;
    const iris::CameraNodePtr cam = viewCamera();
    if (!cam) return out;
    // Size and view exactly as a mouse pick would, then ask the same question
    // at the given pixel — a script and a click cannot disagree.
    const QRectF picture = pictureRect();
    mGizmo->updateSize(cam);
    mGizmo->setPickView(cam, float(picture.width()), float(picture.height()),
                        float(devicePixelRatioF()));
    const QPointF local = point - picture.topLeft();

    if (mGizmo == mRotateGizmo) {
        out.tolerancePx = kRingPickTolerancePx;
        float distancePx = -1.0f;
        out.handle = mRotateGizmo->ringNameAtPixel(local, distancePx);
        out.distancePx = distancePx;
        return out;
    }

    // THE TRANSLATE GIZMO (GIZMO-1 item 3). Its PLANE handles are picked in
    // pixels like the rotation rings, so they answer with a distance; its
    // arrows and its centre are picked in 3D against their own geometry, so
    // they answer with a NAME and no distance (-1).
    //
    // THE PRESS'S OWN CALL DECIDES (GIZMO-2 round 2). This used to ask
    // planeNameAtPixel FIRST and return its answer, which is not the order a
    // press takes: getHitHandle tries the CENTRE BALL before the planes, so
    // every pixel where the ball wins — the inner part of all three squares,
    // since item 1 anchored them at the origin — was reported as a plane while
    // a click there grabbed the ball. One call now answers both, and
    // planeDistance is asked afterwards only to fill in the pixel distance a
    // plane result carries.
    if (mGizmo == mTranslateGizmo) {
        out.tolerancePx = kPlanePickTolerancePx;
        iris::Vec3 a, b;
        pictureSegment(cam, point, a, b);
        const iris::Vec3 viewDir = cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1));
        iris::Vec3 hit;
        if (auto *handle = mTranslateGizmo->getHitHandle(a, (b - a).normalized(), viewDir, hit)) {
            out.handle = handle->axisName();
            float distancePx = -1.0f;    // a 3D pick (ball, arrow) has none
            if (handle->isPlane()) handle->planeDistance(local, distancePx);
            out.distancePx = distancePx;
            return out;
        }
        // A MISS still reports how far the nearest plane handle is — the one
        // measurable distance this gizmo has.
        float distancePx = -1.0f;
        mTranslateGizmo->planeNameAtPixel(local, distancePx);
        out.distancePx = distancePx;
        return out;
    }
    return out;
}

bool EngineSceneViewport::dropPointAt(const QPointF &point, iris::Vec3 *out)
{
    // ONE function behind the drop and the verb (S2): if these two ever
    // disagreed, a script that "places where the cursor is" would place
    // somewhere a drag never lands.
    if (!out || !viewCamera()) return false;
    *out = dropPositionAt(point);
    return true;
}

// ---- drag and drop from the asset panel: ported from SceneViewWidget ----------------

/// ONE decoder (ui/controls/assetdrag.h) — this used to be a local copy of the
/// same four-slot read, beside two more in the property widgets.
static QMap<int, QVariant> dragRoleData(const QMimeData *mime)
{
    return AssetDrag::roles(mime);
}

/// A material carried by a drag: the payload string the ONE resolver
/// understands, or empty when this drag is not a material at all.
///
/// A SHADER TILE IS A MATERIAL TOO (MATERIAL-PREVIEW-1). The Materials module
/// files its graphs as Shader rows, and one of those dragged in used to be
/// accepted by dragEnter, ignored by dragMove and dropped into nothing — a
/// gesture that looked like it worked and did not. Both types come here and the
/// resolver decides; a payload it cannot resolve is REFUSED, visibly, by the
/// cursor.
QString EngineSceneViewport::materialDragSource(const QMimeData *mime)
{
    const QMap<int, QVariant> role = dragRoleData(mime);
    const int type = role.value(0).toInt();
    if (type != static_cast<int>(ModelTypes::Material) &&
        type != static_cast<int>(ModelTypes::Shader))
        return QString();
    return role.value(3).toString();
}

MaterialPreviewService *EngineSceneViewport::materialPreview() const
{
    return mServices ? mServices->materialPreview : nullptr;
}

void EngineSceneViewport::dragEnterEvent(QDragEnterEvent *event)
{
    if (mHierarchyDragSource && event->source() == mHierarchyDragSource) {
        event->ignore();
        return;
    }
    if (!AssetDrag::isAssetDrag(event->mimeData())) return;

    // THE CURSOR TELLS THE TRUTH (MATERIAL-PREVIEW-1). A material drag is
    // accepted only when its payload RESOLVES to a material — the same call
    // that will show it a moment later and commit it on release. Anything that
    // does not resolve (a graph with no baked material, a row whose definition
    // is gone) is refused here, where the cursor can say so, instead of being
    // accepted and silently doing nothing on the drop.
    mDragMaterialSource = materialDragSource(event->mimeData());
    if (!mDragMaterialSource.isEmpty()) {
        auto *preview = materialPreview();
        if (!preview || !preview->canPreview(mDragMaterialSource)) {
            mDragMaterialSource.clear();
            event->ignore();
            return;
        }
    }
    event->acceptProposedAction();
}

void EngineSceneViewport::dragLeaveEvent(QDragLeaveEvent *)
{
    if (auto *preview = materialPreview()) preview->end();
    mDragMaterialSource.clear();
}

void EngineSceneViewport::dragMoveEvent(QDragMoveEvent *event)
{
    const QMap<int, QVariant> role = dragRoleData(event->mimeData());
    const int type = role.value(0).toInt();
    if (!mDragMaterialSource.isEmpty()) {
        // THE WHOLE HOVER PREVIEW, in one call. dropTargetAt, not pickAt: a
        // LOCKED node is under the cursor as much as any other, and the drop
        // has to know it is there to refuse it by name — the service refuses to
        // preview it, and a null target ENDS the preview (which is what the two
        // inline early-outs here used to forget to do, leaving the borrowed
        // material on the mesh).
        bool lockedTarget = false;
        iris::SceneNodePtr node = dropTargetAt(event->position(), &lockedTarget);
        if (lockedTarget) node.reset();
        if (auto *preview = materialPreview()) preview->begin(node, mDragMaterialSource);
    } else if (type == static_cast<int>(ModelTypes::Object) || type == static_cast<int>(ModelTypes::ParticleSystem)
               || type == static_cast<int>(ModelTypes::Texture)
               || type == static_cast<int>(ModelTypes::Avatar)
               || type == static_cast<int>(ModelTypes::Animation)) {
        // Texture too (IMAGE_PLANE_SPEC §2): an image dropped on empty space
        // becomes an image plane at the drop point, so the drag must track it
        // exactly like Object drags.
        //
        // AVATAR and ANIMATION too (S9, 2026-09-11): the avatar drop already
        // passed mDragScenePos on to avatar.spawn, but nothing UPDATED it for
        // an avatar drag — so every character landed wherever the last tracked
        // drag ended, i.e. on top of the previous one or at the origin. The
        // clip drag tracks for the same reason: its drop resolves the node
        // under the cursor.
        mDragScenePos = dropPositionAt(event->position());
    }
    event->acceptProposedAction();
}

void EngineSceneViewport::dropEvent(QDropEvent *event)
{
    // THE EDIT GATE, FOR EVERY DROP TYPE AT ONCE (round 2, item 1). A drop is
    // a document write whichever branch below it takes — a model, a primitive,
    // an image plane, an avatar, a clip, a material, a texture, a sky — and
    // two of those branches do NOT end at the undo spine: the clip drop calls
    // AvatarApi::loadClip directly (no verb dispatch, so no verb scope), and
    // it ATTACHES the clips before it pushes, so a refusal at the push left
    // the clips on the node with no undo step and a "clip added" toast. One
    // question here covers the lot, including the branches written after this.
    if (editgate::blocked()) {
        // The hover preview borrowed the mesh's material while the drag was
        // over it, and the drag ENDS here — no dragLeaveEvent is coming to put
        // it back. Restore first, then refuse: a refused drop must leave the
        // document exactly as the drag found it.
        if (auto *preview = materialPreview()) preview->end();
        mDragMaterialSource.clear();
        editgate::refuse();          // counts it and raises the run's notice
        event->ignore();
        return;
    }
    const QMap<int, QVariant> role = dragRoleData(event->mimeData());
    const int type = role.value(0).toInt();
    if (type == static_cast<int>(ModelTypes::ParticleSystem)) {
        emit mEvents.addDroppedParticleSystem(true, mDragScenePos, role.value(3).toString(), role.value(1).toString());
    } else if (type == static_cast<int>(ModelTypes::Object)) {
        // A PRIMITIVE TILE, by guid — OLD GUIDS INCLUDED. `primitives::byGuid`
        // maps the pre-2026-09-19 numbering (the range that collided with
        // BuiltinShaders') onto the current rows, so a favourite a user saved
        // before the renumber still drops (src/data/primitives.h).
        if (const primitives::Def *prim = primitives::byGuid(role.value(3).toString())) {
            // WITH THE DROP POINT (smoke S2). This branch was the one dropped
            // asset that carried no position — every primitive dragged into
            // the viewport landed in front of the camera instead of under the
            // cursor, while the mesh branch one line down has passed
            // mDragScenePos since the beginning.
            emit mEvents.addPrimitive(QString::fromLatin1(prim->name), mDragScenePos);
            return;
        }
        emit mEvents.addDroppedMesh(QDir(mProject->getProjectFolder()).filePath(role.value(2).toString()),
                                    true, mDragScenePos, role.value(3).toString(), role.value(1).toString());
    } else if (type == static_cast<int>(ModelTypes::Avatar)) {
        // AVATAR_ASSET_SPEC §5.5: dropping an avatar row spawns a LINKED
        // instance of the project's version — the same `avatar.spawn` verb the
        // drawer's "Add to Scene" and a script call, at the tracked drop point.
        if (mMainWindow) mMainWindow->spawnAvatarAsset(role.value(3).toString(), mDragScenePos, true);
    } else if (type == static_cast<int>(ModelTypes::Animation)) {
        // A CLIP IS WORN, NOT PLACED (S9): dropping an animation row on a
        // character assigns it through avatar.loadClip; on empty space it says
        // so in a toast instead of silently doing nothing (which is exactly
        // what this branch's absence used to do).
        if (mMainWindow)
            mMainWindow->assignAnimationAsset(role.value(3).toString(),
                                              pickAt(event->position(), true));
    } else if (!materialDragSource(event->mimeData()).isEmpty()) {
        const QString source = materialDragSource(event->mimeData());
        auto *preview = materialPreview();
        // The hover preview refuses a locked node, so there is no preview to
        // apply — say why, rather than dropping the gesture on the floor (the
        // owner's report: a material dragged onto the Ground did nothing at
        // all, silently).
        bool lockedTarget = false;
        const iris::SceneNodePtr under = dropTargetAt(event->position(), &lockedTarget);
        if (lockedTarget && refuseDropOnLocked(under, tr("a material"))) {
            if (preview) preview->end();
            mDragMaterialSource.clear();
            event->acceptProposedAction();
            return;
        }
        // The drop TARGET is the node the preview was showing on — never the
        // selection. The old order applied to whatever was selected before the
        // drag (usually a different node, or a container the apply silently
        // refused) while the leaked preview material made the drop LOOK
        // successful; the document never held the material, so it vanished on
        // reopen. `applyMaterial` ends the preview itself before it pushes, so
        // the undo step captures the TRUE original.
        // (A mesh wearing NO material gets no preview — the service refuses it —
        // but it takes the drop: the node under the cursor, which is what the
        // preview's node is whenever there is one.)
        const iris::SceneNodePtr target =
            preview && preview->active() ? iris::SceneNodePtr(preview->node()) : under;
        // The preview ends BEFORE anything reads the node — the selection mounts
        // a Properties panel on it (code review, F10).
        if (preview) preview->end();
        if (target && target->getSceneNodeType() == iris::SceneNodeType::Mesh
            && mServices && mServices->sceneEdit) {
            if (mMainWindow) mMainWindow->sceneNodeSelected(target);
            mServices->sceneEdit->applyMaterial(source, target);
        }
        mDragMaterialSource.clear();
    } else if (type == static_cast<int>(ModelTypes::Texture)) {
        // IMAGE_PLANE_SPEC §2: on a mesh the image retextures it; on empty
        // space it spawns an image plane at the tracked drop point.
        const QString textureGuid = role.value(3).toString();
        // The same drop-target rule as the material branch: an image dropped on
        // an UNLOCKED floor retextures the floor (lane SPACE-2 item 5), a drop
        // on a LOCKED node is refused by name — and, in particular, does not
        // quietly become an image plane hanging in front of it. Only a drop
        // that hits NOTHING — the sky, past the edge of the ground — spawns a
        // plane, which is what the else below still does.
        bool lockedTarget = false;
        iris::SceneNodePtr node = dropTargetAt(event->position(), &lockedTarget);
        if (lockedTarget && refuseDropOnLocked(node, tr("an image"))) {
            event->acceptProposedAction();
            return;
        }
        if (node && node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            auto meshNode = node.staticCast<iris::MeshNode>();
            // Bytes resolve pin-first through the CAS — the flat
            // projectFolder join this branch used pointed at a folder the
            // pin world no longer populates (the drop was dead code).
            QString texPath;
            if (mDatabase) {
                QSqlDatabase conn = QSqlDatabase::database();
                texPath = AssetCas::resolvePinned(conn, AssetStorePaths::root(),
                                                  mProject ? mProject->getProjectGuid() : QString(),
                                                  textureGuid);
            }
            if (texPath.isEmpty()) return;
            if (auto pbr = meshNode->getMaterial().dynamicCast<iris::PbrMaterial>()) {
                // The PBR repair: baseColorMap, undoable like the panel edit.
                QVariant oldMap;
                for (auto *prop : pbr->properties)
                    if (prop->name == QStringLiteral("baseColorMap")) { oldMap = prop->getValue(); break; }
                if (mServices && mServices->undo) {
                    mServices->undo->push(new ChangeMaterialPropertyCommand(
                        pbr, QStringLiteral("baseColorMap"), oldMap, texPath));
                } else {
                    pbr->setValue(QStringLiteral("baseColorMap"), texPath);
                }
                if (mMainWindow) mMainWindow->sceneNodeSelected(node);
            }
            // (The CustomMaterial branch that dropped the texture into the
            // shader's first texture slot went with the class — every mesh
            // carries a PbrMaterial now, so the branch above is the only one.)
        } else {
            emit mEvents.addDroppedImagePlane(mDragScenePos, textureGuid);
        }
    } else if (type == static_cast<int>(ModelTypes::Sky)) {
        if (!mScene || !mDatabase) return;
        const QString skyGuid = role.value(3).toString();
        const QJsonObject skyDefinition = QJsonDocument::fromJson(mDatabase->fetchAssetData(skyGuid)).object();
        const QJsonObject skyProperties = QJsonDocument::fromJson(mDatabase->fetchAsset(skyGuid).properties).object();
        const int skyTypeIndex = skyProperties.value("sky").toObject().value("type").toInt();
        mScene->skyData.insert(mScene->skyTypeToStr[skyTypeIndex], skyDefinition);
        mScene->skyType = static_cast<iris::SkyType>(skyTypeIndex);
        emit mEvents.changeSkyFromAssetWidget(skyTypeIndex);
    }
}

// THE FLY'S HELD-KEY SET, READABLE (ledger §356). A key stuck in it moves
// nothing and logs nothing — Left and Right held together cancel — so the only
// way to see the state was to notice the camera refusing to fly. Now
// `editor.viewportState()` reports it.
QStringList EngineSceneViewport::heldFlyKeys() const
{
    QStringList names;
    if (!mCamController) return names;
    for (int key : mCamController->heldKeyCodes()) {
        switch (key) {
        case Qt::Key_Up:       names << QStringLiteral("Up"); break;
        case Qt::Key_Down:     names << QStringLiteral("Down"); break;
        case Qt::Key_Left:     names << QStringLiteral("Left"); break;
        case Qt::Key_Right:    names << QStringLiteral("Right"); break;
        case Qt::Key_PageUp:   names << QStringLiteral("PageUp"); break;
        case Qt::Key_PageDown: names << QStringLiteral("PageDown"); break;
        case Qt::Key_Shift:    names << QStringLiteral("Shift"); break;
        default: {
            // Every key the viewport saw goes into the set, not only the fly
            // ones — report whatever Qt calls it, and the raw code when it has
            // no name.
            const QString named = QKeySequence(key).toString();
            names << (named.isEmpty() ? QStringLiteral("0x%1").arg(key, 0, 16) : named);
            break;
        }
        }
    }
    names.sort();
    return names;
}

bool EngineSceneViewport::flying() const
{
    return mCamController && mCamController->isFlying();
}

void EngineSceneViewport::mousePressEvent(QMouseEvent *e)
{
    // Accept explicitly: an ignored press propagates to sceneContainer, whose
    // MainWindow::eventFilter re-sends it here — an infinite loop (each round
    // re-picked and rebuilt the property panel). The legacy widget accepts too.
    e->accept();
    setFocus();
    // THE PLAY SPLIT (PLAY-SELECT-1, owner R13: "in play mode I can select only
    // through the scene graph — I want viewport selection during play"). While
    // a run is in flight the editor keeps exactly ONE gesture, the plain left
    // click — the pick, the selection and the gizmo — and the run keeps the
    // rest: the right button's look, the middle button, the wheel. The moment
    // the run really consumes input (a possessed avatar) it takes the left
    // button back too, until the user EJECTS (setPlayEjected), which hands the
    // whole widget over to the editor with the simulation still running.
    if (mPlaying && mPlayback && !mPlayEjected &&
        (runOwnsPointer() || e->button() != Qt::LeftButton)) {
        mPlayback->mousePressEvent(e);
        return;
    }
    mMousePos = mPrevMousePos = e->position(); mHaveMouse = true;
    // IS THE EDITOR'S CAMERA OURS TO MOVE? Not during a run we have not been
    // ejected from: the run flies its own camera through PlayBack (and with an
    // active camera armed that is not this widget's camera at all), so an
    // Alt-orbit or a left-drag pan here would turn a camera nobody is looking
    // through. Ejected, the editor has the whole widget back and its camera
    // with it.
    const bool editorCameraIsOurs = !mPlaying || mPlayEjected;
    if (e->button() == Qt::LeftButton) {
        iris::Vec3 rayPos, rayDir, viewDir;
        const bool haveRay = mouseRay(rayPos, rayDir, viewDir);
        // A hit on the active gizmo starts a drag and keeps the selection —
        // unless a SCRIPT owns the document (owner, ledger §423;
        // services/editgate.h). The gate is asked before the drag begins, not
        // when it ends: a drag writes the node live, and letting it run only to
        // drop its undo step would move the object and then snap it back. The
        // click still lands as a click (the selection below is a read), and
        // every camera gesture in this handler is untouched — the editor is
        // non-editable while a script runs, not frozen.
        // ...AND NOT A DRAG THE WEARER IS HOLDING (VR phase 4b stage 2, the
        // ownership rule on mMouseDrag): a gizmo already being dragged by the
        // OTHER host is not this one's to grab. A drag this viewport itself
        // started and never saw released (a lost release) still restarts here,
        // exactly as it always did.
        const bool heldElsewhere = mGizmo && mGizmo->isDragging() && !mMouseDrag;
        if (haveRay && mSelectedNode && mGizmo && !heldElsewhere
            && mGizmo->isHit(rayPos, rayDir) && !editgate::refuse()) {
            // Alt+drag duplicates first, then drags the COPY — one undo macro
            // covers duplicate + move (EDITOR_SHORTCUTS_SPEC §4).
            // ...BUT NEVER DURING A RUN (PLAY-SELECT-1): stop restores the
            // transforms of the nodes play STARTED with and knows nothing about
            // a node born mid-run, so an Alt+drag during play would leave a copy
            // behind after the rest of the run was thrown away. Moving what is
            // already there is the capability this lane ships; creating is not.
            if (!mPlaying && (e->modifiers() & Qt::AltModifier) && mServices && mServices->sceneEdit &&
                mServices->undo && mServices->undo->stack() && mSelectedNode->isDuplicable()) {
                mServices->undo->stack()->beginMacro(QStringLiteral("Duplicate + Move"));
                if (mSelectedSet.size() > 1) {
                    // THE WHOLE SET (EDITOR_MULTISELECT_SPEC §2.4): duplicate
                    // it, select the copies — which re-points the gizmo and its
                    // group through the service fan-out — and drag those.
                    const auto copies = mServices->sceneEdit->duplicateNodes(mSelectedSet);
                    if (!copies.isEmpty()) mAltDragMacroOpen = true;
                    else mServices->undo->stack()->endMacro();
                } else {
                    iris::SceneNodePtr copy = mServices->sceneEdit->duplicateNode(mSelectedNode);
                    if (copy) {
                        mAltDragMacroOpen = true;
                        setSelectedNode(copy);              // re-points the gizmo too
                        emit mEvents.sceneNodeSelected(copy);
                    } else {
                        mServices->undo->stack()->endMacro();
                    }
                }
            }
            // The gesture's modifiers, from the event driving it (SCALE-LOCK-1;
            // gizmo.h's setDragModifiers): Shift on a scale axis handle means
            // "scale all three by this drag's ratio".
            mGizmo->setDragModifiers(e->modifiers());
            // A DRAG DURING A RUN IS NOT AN UNDO STEP (PLAY-SELECT-1). The run's
            // edits are thrown away at stop (PlayBack's snapshot), so an undo
            // entry for one would rewind a node to a pose that stopped existing
            // the moment the user pressed Stop. The transform still lands, live;
            // only the stack is spared.
            mGizmo->setTransientDrag(mPlaying);
            mGizmo->startDragging(rayPos, rayDir, viewDir);
            mMouseDrag = mGizmo->isDragging();     // this host owns what it started
            if (mMouseDrag && mPlaying) beginPlayDragOverPhysics();
        } else if (editorCameraIsOurs && (e->modifiers() & Qt::AltModifier)) {
            // Alt+LMB anywhere BUT the gizmo orbits around THE POINT UNDER THE
            // CURSOR (Maya/Unreal; owner report §353). The gizmo hit-test above
            // ran first on purpose: Alt ON the gizmo keeps meaning
            // duplicate-while-transforming. Orbiting must not re-pick the
            // SELECTION, so the selection is left alone — the pick below only
            // answers "what is under the cursor", it selects nothing.
            if (mCamController) mCamController->setAltOrbit(true, altOrbitPivotAt(e->position()));
        } else {
            iris::SceneNodePtr picked = pickAt(e->position(), true);
            // Ctrl TOGGLES, Shift ADDS (D3 b — Unreal's viewport rule). Both
            // are free on a LMB press: Ctrl only ever mattered DURING a drag
            // (snapping) and Shift only with RMB held (fly speed). A modified
            // click on EMPTY SPACE keeps the set — clearing it would make a
            // slightly-missed Ctrl+click undo the whole selection.
            const bool ctrl  = e->modifiers() & Qt::ControlModifier;
            const bool shift = e->modifiers() & Qt::ShiftModifier;
            if ((ctrl || shift) && mServices && mServices->selection) {
                if (!picked) { /* keep the set */ }
                else if (picked->isRootNode()) mServices->selection->select(picked);  // D6
                else if (ctrl) mServices->selection->toggle(picked);
                else           mServices->selection->add(picked);
            } else {
                setSelectedNode(picked);
                emit mEvents.sceneNodeSelected(picked);
            }
        }
    }
    if (editorCameraIsOurs && mCamController) mCamController->onMouseDown(e->button());
}

void EngineSceneViewport::mouseMoveEvent(QMouseEvent *e)
{
    e->accept();
    if (mPlaying && mPlayback && !mPlayEjected) {
        // Play-in-place is still THE EDITOR: free mouse-look grabbed the
        // cursor the instant Play started ("my mouse should not be linked to
        // play scene" — owner, 2026-09-06). Look only while RMB is held, the
        // same gesture as the editor fly camera; the dedicated Player page
        // (EnginePlayerView) keeps unrestricted look — a page you switched to
        // is a game surface, the editor viewport is not.
        if (e->buttons() & Qt::RightButton) { mPlayback->mouseMoveEvent(e); return; }
        // The run consuming input keeps the pointer whole — exactly today's
        // behaviour, swallowed rather than picked.
        if (runOwnsPointer()) return;
        // ...and otherwise the motion falls through to the editor's half
        // (PLAY-SELECT-1): the pick ray the gizmo hover reads, and a live
        // left-button gizmo drag. Nothing below moves the editor's camera
        // without a button press this handler gave it (onMouseDown is withheld
        // during a run), so a bare hover cannot pan anything.
    }
    mMousePos = e->position(); mHaveMouse = true;
    const QPointF dir = mMousePos - mPrevMousePos;
    mPrevMousePos = mMousePos;
    // OUR OWN DRAG ONLY (the ownership rule, mMouseDrag): mouse motion over the
    // viewport while the WEARER is dragging a handle must not drive that drag
    // with a pixel ray from the desk's camera, nor overwrite the modifiers the
    // controller is pushing.
    if (mGizmo && mMouseDrag && mGizmo->isDragging()) {
        // V held during a translate drag: snap the dragged node's pivot to the
        // nearest vertex of the triangle under the cursor on OTHER meshes
        // (EDITOR_SHORTCUTS_SPEC §4). No target under the cursor -> plain drag.
        if (mVertexSnapHeld && mGizmo == mTranslateGizmo && snapDragToVertexUnderCursor())
            return;
        iris::Vec3 rayPos, rayDir, viewDir;
        // Per move, so a modifier pressed or released mid-drag takes effect for
        // the remainder of the gesture (SCALE-LOCK-1).
        mGizmo->setDragModifiers(e->modifiers());
        if (mouseRay(rayPos, rayDir, viewDir)) mGizmo->drag(rayPos, rayDir, viewDir);
        return;
    }
    // The run flies its own camera while we are not ejected from it (see
    // editorCameraIsOurs in the press handler).
    if (mCamController && (!mPlaying || mPlayEjected))
        mCamController->onMouseMove(-int(dir.x()), -int(dir.y()));
}

void EngineSceneViewport::mouseReleaseEvent(QMouseEvent *e)
{
    e->accept();
    // THE PRESS'S OWNER OWNS THE RELEASE (PLAY-SELECT-1): a left button the
    // editor picked with must end here, or a gizmo drag started during play
    // would never be closed and the next click would find a live drag.
    if (mPlaying && mPlayback && !mPlayEjected &&
        (runOwnsPointer() || e->button() != Qt::LeftButton)) {
        mPlayback->mouseReleaseEvent(e);
        return;
    }
    if (e->button() == Qt::LeftButton && mMouseDrag) {
        mMouseDrag = false;
        // The body the gizmo was holding still goes back to the simulation
        // where the hand left it (endPlayDragOverPhysics); a no-op outside play
        // and for a node with no rigid body.
        if (mPlaying) endPlayDragOverPhysics();
        // ...and a click that ends NOTHING of ours ends nothing at all: a stray
        // press and release while the wearer holds a handle used to call
        // endDragging on their drag, which commits a second undo entry for one
        // gesture (endDragging always calls createUndoAction).
        if (mGizmo && mGizmo->isDragging()) mGizmo->endDragging();
    }
    // The Alt+drag macro closes AFTER endDragging pushed its transform command,
    // so duplicate + move undo as one step.
    if (e->button() == Qt::LeftButton && mAltDragMacroOpen) {
        mAltDragMacroOpen = false;
        if (mServices && mServices->undo && mServices->undo->stack())
            mServices->undo->stack()->endMacro();
    }
    // Alt+LMB orbit ends with the drag (the free camera returns to fly).
    if (e->button() == Qt::LeftButton && mCamController && mCamController->isAltOrbiting())
        mCamController->setAltOrbit(false, iris::Vec3());
    if (mCamController && (!mPlaying || mPlayEjected)) mCamController->onMouseUp(e->button());
}

void EngineSceneViewport::mouseDoubleClickEvent(QMouseEvent *e)
{
    // QWidget's default forwards to mousePressEvent (a second pick + panel rebuild).
    e->accept();
    if (mPlaying && mPlayback && !mPlayEjected) mPlayback->mouseDoubleClickEvent(e);
}

void EngineSceneViewport::wheelEvent(QWheelEvent *e)
{
    e->accept();
    // The wheel is the RUN's (its camera speed / dolly) unless we are ejected —
    // the editor's dolly would move a camera the run is not rendering through.
    if (mPlaying && mPlayback && !mPlayEjected) { mPlayback->wheelEvent(e); return; }
    if (mCamController) mCamController->onMouseWheel(e->angleDelta().y());
}

void EngineSceneViewport::keyPressEvent(QKeyEvent *e)
{
    // THE KEYS ARE THE RUN'S UNTIL THE USER EJECTS (PLAY-SELECT-1). Unlike the
    // left button, they are not split: W/A/S/D and Space are the Gameplay rows
    // (AVATAR_LOCOMOTION_SPEC §8.2) for as long as the run has the keyboard,
    // and F8 is what takes the whole keyboard back — tool shortcuts, the fly
    // arrows, V-hold and all (the ShortcutOverride gate in event() keys on the
    // same flag, so the two can never disagree).
    if (mPlaying && mPlayback && !mPlayEjected) { mPlayback->keyPressEvent(e); return; }
    // Auto-repeat presses would be harmless (the key is already in the held
    // set) but auto-repeat RELEASES would clear it mid-hold — skip both.
    if (e->isAutoRepeat()) return;
    // EJECT (CAMERAS_SPEC D8). Not a ShortcutRegistry entry: Esc is a viewport
    // MODE key like V-hold, it only does anything while piloting, and a global
    // shortcut for it would take Esc away from every dialog in the app.
    if (e->key() == Qt::Key_Escape && mPilot) { pilotCamera(iris::CameraNodePtr()); return; }
    if (e->key() == Qt::Key_V) mVertexSnapHeld = true;
    if (mCamController) mCamController->onKeyPressed(static_cast<Qt::Key>(e->key()));
}

void EngineSceneViewport::keyReleaseEvent(QKeyEvent *e)
{
    if (mPlaying && mPlayback && !mPlayEjected) { mPlayback->keyReleaseEvent(e); return; }
    if (e->isAutoRepeat()) return;
    if (e->key() == Qt::Key_V) mVertexSnapHeld = false;
    if (mCamController) mCamController->keyReleaseEvent(e);
}

void EngineSceneViewport::focusOutEvent(QFocusEvent *e)
{
    // Keys released while another widget has focus never reach us — drop the
    // held set so fly keys cannot stick down. The same argument applies to the
    // gameplay input state (AVATAR_LOCOMOTION_SPEC §8.2): a W released over
    // another widget would walk the possessed avatar forever.
    if (mCamController) mCamController->clearKeys();
    iris::InputSystem::instance().clearKeys();
    mVertexSnapHeld = false;
    EngineViewWidget::focusOutEvent(e);
}

// The V-hold vertex snap: pick under the cursor against every OTHER mesh
// (document CPU picking already reports the hit triangle), then move the
// dragged node's pivot to the hit triangle's nearest corner. Runs inside the
// live translate drag, so endDragging()'s undo command covers it.
bool EngineSceneViewport::snapDragToVertexUnderCursor()
{
    const iris::CameraNodePtr cam = pickCamera();   // the PILOT, or the run's own shot
    if (!mSelectedNode || !mScene || !cam || !mHaveMouse) return false;
    iris::Vec3 a, b;
    pictureSegment(cam, mMousePos, a, b);
    // refreshTransforms = false: this runs on every mouse move inside a live
    // translate drag, and the mirror's sync() already updated the document's
    // global transforms this frame. The update is a full recursive walk.
    // ...and no helper spheres either: V-hold snaps to a TRIANGLE CORNER, so a
    // light, decal or camera origin sphere is noise it would have to filter out
    // of every hit list anyway.
    const auto hits = ScenePicker::pickAll(mScene, a, b, cam->getGlobalPosition(),
                                           true, false, false, false, false);
    ScenePick best;
    for (const auto &h : hits) {
        if (!h.node || h.triangleIndex < 0) continue;
        bool own = false;                       // never snap to the dragged subtree
        for (auto n = h.node; n; n = n->getParent())
            if (n.data() == mSelectedNode.data()) { own = true; break; }
        if (own) continue;
        if (!best.node || h.distanceFromCameraSqrd < best.distanceFromCameraSqrd) best = h;
    }
    if (!best.node) return false;
    auto meshNode = best.node.staticCast<iris::MeshNode>();
    auto mesh = meshNode->getMesh();
    if (!mesh || !mesh->getTriMesh() ||
        best.triangleIndex >= mesh->getTriMesh()->triangles.size())
        return false;
    const iris::Triangle &tri = mesh->getTriMesh()->triangles[best.triangleIndex];
    const iris::Mat4 &xf = meshNode->getGlobalTransform();
    const iris::Vec3 corners[3] = { xf * tri.a, xf * tri.b, xf * tri.c };
    iris::Vec3 vertex = corners[0];
    for (int i = 1; i < 3; ++i)
        if ((corners[i] - best.hitPoint).lengthSquared() <
            (vertex - best.hitPoint).lengthSquared())
            vertex = corners[i];
    mSelectedNode->setGlobalPos(vertex);
    if (mServices && mServices->sceneEdit) mServices->sceneEdit->notifyTransformChanged();
    return true;
}

bool EngineSceneViewport::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        const int key = static_cast<QKeyEvent *>(e)->key();
        // THE PLAY-MODE KEY PATH (AVATAR_LOCOMOTION_SPEC §8.3). While the scene
        // is playing, every key the InputMap binds belongs to the game, not to
        // the editor: `tool.translate` is on W and `tool.cycle` is on Space,
        // both Qt::WindowShortcut, so without this W silently switched the
        // gizmo mode instead of walking and Space cycled it instead of jumping.
        // Keyed on mPlaying — the SAME member keyPressEvent branches on and the
        // one IEditorViewport::isPlaying()/editor.playing() reports, so the
        // override and the routing can never disagree.
        //
        // Only while playing: when the scene is stopped the gizmo shortcuts are
        // untouched, which is the whole contract the shortcuts.registry gate
        // asserts.
        //
        // ...AND ON THE EJECT (PLAY-SELECT-1): an ejected run has handed the
        // keyboard back, so the gameplay claim stands down with the routing
        // above it — W is the translate tool again while the simulation keeps
        // running, which is the whole point of ejecting.
        if (iris::gameplayClaimsKey(mPlaying && !mPlayEjected, key)) {
            e->accept();
            return true;
        }
        // While the right mouse button is held in free-camera mode, the ARROW
        // CLUSTER belongs to the fly camera — accept the ShortcutOverride so
        // the raw key events reach keyPressEvent instead of whatever
        // WindowShortcut or focused list would otherwise eat them
        // (EDITOR_SHORTCUTS_SPEC §2). Arrows are Qt::WindowShortcut material
        // elsewhere in the app (list navigation, the hierarchy tree), so
        // without this claim the fly would fight whatever widget last had
        // focus.
        //
        // W/A/S/D/Q/E ARE NO LONGER CLAIMED (owner decision 2026-09-09): the
        // editor's fly moved to the arrows and the six letters are free, which
        // is the whole point of the move — a tool shortcut on W now works
        // while the right button is down instead of being swallowed.
        if (mCamController == mFreeCam && mFreeCam && mFreeCam->isFlying()) {
            switch (key) {
            case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left: case Qt::Key_Right:
            case Qt::Key_PageUp: case Qt::Key_PageDown: case Qt::Key_Shift:
                e->accept();
                return true;
            default:
                break;
            }
        }
    }
    return EngineViewWidget::event(e);
}

void EngineSceneViewport::setScene(iris::ScenePtr scene)
{
    // A PAUSED scene is still playing as far as PlayBack is concerned (mPlaying
    // is false but the physics world and saved transforms are live) — it has to
    // be stopped before the document underneath it is swapped.
    if (mPlaying || (mPlayback && mPlayback->isScenePlaying())) stopPlayingScene();
    mScene = scene;
    mSelectedNode.clear();
    // After clearScene() the engine scene is gone but the view survives;
    // rebuild the scene-scoped objects now (mirror/overlay pick up mScene).
    if (scene && view() && !mEngineScene) ensureEngineScene();
    if (mPlayback && scene) {
        // Like the legacy viewport: the editor camera doubles as the play camera.
        if (mEditorCam) scene->setCamera(mEditorCam);
        mPlayback->setScene(scene);
    }
    if (mMirror) mMirror->setSource(scene);
    // A new document scene means no frame of IT has presented yet, even when
    // the engine scene underneath is the same object (close/open reuses it).
    mPresentBaseline = view() ? qulonglong(view()->framesPresented()) : 0;
    refreshOverlay();
}

void EngineSceneViewport::startPlayingScene()
{
    if (!mScene || !mPlayback) return;
    // playScene() knows the difference between a cold start and a resume; the
    // viewport flag only says whether syncFrame drives the simulation.
    if (!mPlaying) mPlayback->playScene();
    mPlaying = true;
    // EVERY RUN STARTS POSSESSED (PLAY-SELECT-1): the eject latch is a state of
    // the run in flight, not a setting — nothing about it is persisted and a
    // second Play never inherits the first one's hand-over. A resume from pause
    // takes the same line, which is what the user means by pressing Play.
    mPlayEjected = false;
}

void EngineSceneViewport::pausePlayingScene()
{
    if (!mPlaying) return;
    mPlaying = false;                 // time is not reset, like the legacy viewport
    // The pause is a state on PlayBack too, otherwise the next
    // startPlayingScene() re-entered play: animation clock back to zero, the
    // mid-play pose saved over the originals, and a second set of rigid bodies
    // and character controllers added to the physics world.
    if (mPlayback) mPlayback->pause();
}

void EngineSceneViewport::stopPlayingScene()
{
    if (!mPlaying && !(mPlayback && mPlayback->isScenePlaying())) return;
    mPlaying = false;
    mPlayEjected = false;
    // A GESTURE CANNOT OUTLIVE THE RUN IT WAS MADE IN (PLAY-SELECT-1): a gizmo
    // drag still held when Stop is pressed is closed here, and closed the way
    // the run would have closed it (transient — no undo step, the physics hand
    // back), before PlayBack puts every transform back. Left open, the next
    // press in the restored scene would find a live drag anchored to a pose
    // that no longer exists.
    if (mMouseDrag && mGizmo && mGizmo->isDragging()) {
        endPlayDragOverPhysics();
        mGizmo->endDragging();
        mMouseDrag = false;
    }
    if (mPlayback) mPlayback->stopScene();
    if (mScene) mScene->updateSceneAnimation(0.0f);
    // THE SELECTION SURVIVES THE RUN (PLAY-SELECT-1). Nothing below clears it —
    // stop restores transforms, not the graph — but the gizmo and the outline
    // are anchored on POSES that just moved back, so they are re-pointed at the
    // same nodes to re-read them. A node deleted during the run is dropped by
    // the same call.
    refreshSelectionAfterPlay();
}

// The selection as it stood at Stop, re-anchored on the restored document. Any
// node that is gone (deleted mid-run) drops out; the rest keep the selection,
// the outline and the properties column they had.
void EngineSceneViewport::refreshSelectionAfterPlay()
{
    if (mSelectedSet.isEmpty() && !mSelectedNode) return;
    QList<iris::SceneNodePtr> alive;
    for (const iris::SceneNodePtr &n : mSelectedSet)
        if (n && n->getScene()) alive.append(n);
    const bool primaryGone = mSelectedNode && !mSelectedNode->getScene();
    if (primaryGone || alive.size() != mSelectedSet.size()) {
        // Something in the set died during the run. Through the SERVICE, so the
        // outliner, the properties column and this widget agree on what is
        // selected — the viewport-local setter would leave the panels pointing
        // at a node that is gone.
        if (mServices && mServices->selection) mServices->selection->select(alive);
        else setSelectedSet(alive);
        return;
    }
    // Same set, same primary — re-point the gizmo so it reads the transforms
    // the restore just wrote.
    if (mGizmo && mSelectedNode) mGizmo->setSelectedNode(mSelectedNode);
    pushGizmoGroup();
}

// ---- THE GIZMO WINS WHILE IT IS HELD (PLAY-SELECT-1) ----------------------
//
// A rigid body under the gizmo has two authors: Bullet, which writes the node
// every step from the body's world transform (iris::Scene::advance), and the
// hand. `SceneNode::disablePhysicsTransform` is the document's own answer to
// exactly that — a flag Scene::advance has always honoured and nothing ever
// set. The drag sets it on every node it is moving, so the hand writes alone;
// the release hands the pose to the BODY (Environment::syncBodyToNode: the
// world transform, its motion state, velocities zeroed, the body woken) and
// clears the flag, so the object carries on falling from where it was put
// instead of snapping back to where the simulation last had it.
void EngineSceneViewport::beginPlayDragOverPhysics()
{
    mPlayDragBodies.clear();
    if (!mScene) return;
    auto claim = [this](const iris::SceneNodePtr &n) {
        if (!n || n->disablePhysicsTransform) return;   // already somebody's
        if (!n->isPhysicsBody) return;
        n->disablePhysicsTransform = true;
        mPlayDragBodies.append(n);
    };
    claim(mSelectedNode);
    for (const iris::SceneNodePtr &n : mSelectedSet) if (n != mSelectedNode) claim(n);
}

void EngineSceneViewport::endPlayDragOverPhysics()
{
    if (mPlayDragBodies.isEmpty()) return;
    iris::Environment *env = mScene ? mScene->getPhysicsEnvironment().data() : nullptr;
    for (const iris::SceneNodePtr &n : mPlayDragBodies) {
        if (!n) continue;
        n->disablePhysicsTransform = false;
        if (env) env->syncBodyToNode(n);
    }
    mPlayDragBodies.clear();
}

// "Simulate physics" — run the document's physics world in the editor, without
// entering play mode. Same three calls as the legacy viewport
// (sceneviewwidget.cpp); the per-frame stepping happens in syncFrame.
void EngineSceneViewport::startPhysicsSimulation()
{
    if (!mScene) return;
    mScene->getPhysicsEnvironment()->initializePhysicsWorldFromScene(mScene->getRootNode());
    mScene->getPhysicsEnvironment()->simulatePhysics();
    mScene->simulationClock().reset();
}

void EngineSceneViewport::restartPhysicsSimulation()
{
    if (!mScene) return;
    mScene->getPhysicsEnvironment()->restartPhysics();
    mScene->getPhysicsEnvironment()->restoreNodeTransformations(mScene->getRootNode());
    mScene->simulationClock().reset();
}

void EngineSceneViewport::stopPhysicsSimulation()
{
    if (!mScene) return;
    // Stop = the restart shape, not a bare stopPhysics(): stopPhysics() leaves
    // `simulationStarted` true, so Scene::advance kept taking the simulating
    // branch every frame and the body->node copy overwrote every node's
    // transform for the rest of the session (A4.2 code review N1, pre-existing).
    // Tearing the world down, restoring the pre-simulate transforms and
    // resetting the clock is what "Simulate off" means — and what the
    // editor.simulate doc promises ("reset by editor.simulate").
    mScene->getPhysicsEnvironment()->restartPhysics();
    mScene->getPhysicsEnvironment()->restoreNodeTransformations(mScene->getRootNode());
    mScene->simulationClock().reset();
}

// The setter does NOT emit: MainWindow calls it in response to sceneNodeSelected,
// so emitting here would loop. Only picking (below) announces a selection.
void EngineSceneViewport::setSelectedNode(iris::SceneNodePtr sceneNode)
{
    mSelectedNode = sceneNode;
    mSelectedSet.clear();
    if (sceneNode) mSelectedSet.append(sceneNode);
    if (mGizmo) { if (sceneNode) mGizmo->setSelectedNode(sceneNode); else mGizmo->clearSelectedNode(); }
    pushGizmoGroup();
}

// The SET (EDITOR_MULTISELECT_SPEC §2.3): the primary still drives the gizmo's
// pivot and the pick rule; the rest of the set drives the outline, the group
// transform and the focus/orbit/floor unions.
void EngineSceneViewport::setSelectedSet(const QList<iris::SceneNodePtr> &nodes)
{
    mSelectedSet.clear();
    for (const auto &n : nodes) if (n) mSelectedSet.append(n);
    mSelectedNode = mSelectedSet.isEmpty() ? iris::SceneNodePtr() : mSelectedSet.first();
    if (mGizmo) {
        if (mSelectedNode) mGizmo->setSelectedNode(mSelectedNode);
        else               mGizmo->clearSelectedNode();
    }
    pushGizmoGroup();
}

// The gizmo's group is the D5-REDUCED set: a member whose ancestor is also
// selected already moves with that ancestor, and transforming both would move
// it twice.
void EngineSceneViewport::pushGizmoGroup()
{
    if (!mGizmo) return;
    if (mSelectedSet.size() < 2) { mGizmo->setGroup(QList<iris::SceneNodePtr>()); return; }

    const auto effective = SceneEditService::effectiveSet(mSelectedSet);
    // The gizmo's subclasses write the PRIMARY and the base class applies that
    // delta to the rest, so the primary MUST be a member of the group it is
    // deriving the delta from. When the primary is a descendant of another
    // selected node (D5 dropped it), the group's own ancestor takes the pivot —
    // otherwise the primary would be moved twice, once by its ancestor and once
    // by the gizmo.
    bool primaryInGroup = false;
    for (const auto &n : effective)
        if (mSelectedNode && n.data() == mSelectedNode.data()) { primaryInGroup = true; break; }
    if (!primaryInGroup && mSelectedNode) {
        for (auto p = mSelectedNode->getParent(); !!p; p = p->getParent()) {
            bool found = false;
            for (const auto &n : effective) if (n.data() == p.data()) { found = true; break; }
            if (found) { mGizmo->setSelectedNode(p); break; }
        }
    }
    mGizmo->setGroup(effective);
}

void EngineSceneViewport::clearSelectedNode()
{
    setSelectedNode(iris::SceneNodePtr());
}

// F / editor.focusSelection(): frame the node Unreal-style — keep the current
// view direction, back off far enough for the node's world bounds to fill the
// view (preview framing math), and adapt the far plane so a huge subject can
// never clip away (EDITOR_SHORTCUTS_SPEC §2).
void EngineSceneViewport::focusOnNode(iris::SceneNodePtr sceneNode)
{
    if (!sceneNode) return;
    sceneNode->update(0.0f);

    iris::Vec3 target = sceneNode->getGlobalPosition();
    float radius = 1.0f;
    const iris::AABB bounds = preview::worldBoundingBox(sceneNode);
    if (bounds.getMin().x() <= bounds.getMax().x()) {   // non-empty (meshes exist)
        target = bounds.getCenter();
        radius = qMax(0.05f, bounds.getSize().length() * 0.5f);
    }
    focusOnTarget(target, radius, sceneNode);
}

// F's framing, over a point and a radius. Split out of focusOnNode so the
// SELECTION SET can be framed as one union without a node to hand the orbital
// controller (EDITOR_MULTISELECT_SPEC §2.3) — `orbitNode` is null in that case
// and the controller re-derives its pivot from the moved camera instead.
void EngineSceneViewport::focusOnTarget(const iris::Vec3 &target, float radius,
                                        const iris::SceneNodePtr &orbitNode)
{
    const iris::CameraNodePtr cam = viewCamera();   // F focuses whatever you fly
    if (!cam) return;
    mLastOrbitPivot = target;   // the working distance a later Alt+drag over empty space falls back to
    const float dist = qMax(1.0f, preview::framingDistance(radius, cam->effectiveFovDegrees()));

    float nearClip, farClip;
    preview::clipPlanesForFraming(dist, radius, nearClip, farClip);

    // IN A LOCKED AXIS VIEW, F CENTRES — IT DOES NOT TURN (the axis-view lock,
    // owner report 2026-09-08). The lookAt below is a rotation, and in a top
    // view it would tilt the camera off its axis while the view still called
    // itself "top": the same defect the drag lock exists to remove, reached
    // with a key instead of the mouse. So the orientation is kept, the eye
    // slides along the view axis until the subject is centred, and the FRAMING
    // is done by the ortho zoom — backing off is invisible in an orthographic
    // projection, which is what makes this a different operation rather than
    // the same one with the turn removed.
    if (cameraRotationLocked()) {
        const iris::Vec3 fwd = cam->getLocalRot().rotatedVector(iris::Vec3(0, 0, -1));
        cam->setLocalPos(target - fwd * dist);
        // orthoSize is HALF the vertical extent (Types.h), so the radius plus a
        // small margin is exactly "the node fills the view".
        cam->setOrthagonalZoom(qMax(0.1f, radius * 1.2f));
        cam->farClip = qMax(cam->farClip, farClip);
        cam->update(0.0f);
        // NOT OrbitalCameraController::focusOnNode here: that one lookAt()s the
        // node, which is the rotation this branch exists to avoid. Handing the
        // controller the moved camera plus the orbit distance re-derives its
        // pivot from the pose it already has.
        resyncCameraController(dist);
        return;
    }

    iris::Vec3 dir = (cam->getGlobalPosition() - target).normalized();
    if (dir.isNull()) dir = iris::Vec3(0.45f, 0.45f, 0.77f);
    cam->setLocalPos(target + dir * dist);
    cam->lookAt(target);
    cam->farClip = qMax(cam->farClip, farClip);
    cam->update(0.0f);

    // Resync the active controller with the moved camera (free cam re-derives
    // yaw/pitch; the orbital cam re-derives its pivot and orbit distance).
    if (mCamController == mOrbitCam && mOrbitCam && orbitNode) mOrbitCam->focusOnNode(orbitNode);
    else if (mCamController == mOrbitCam && mOrbitCam) resyncCameraController(dist);
    else if (mCamController) mCamController->setCamera(cam);
}

// F frames the WHOLE SET (EDITOR_MULTISELECT_SPEC §2.3): one framing over the
// union of every member's world bounds, so five selected objects all end up on
// screen instead of the primary filling it.
void EngineSceneViewport::focusOnSelection()
{
    if (mSelectedSet.size() <= 1) {
        if (mSelectedNode) focusOnNode(mSelectedNode);
        return;
    }
    iris::AABB unionBounds;
    if (!selectionBounds(unionBounds)) return;
    focusOnTarget(unionBounds.getCenter(),
                  qMax(0.05f, unionBounds.getSize().length() * 0.5f),
                  iris::SceneNodePtr());
}

/// The union of the selection's world bounds. A member with no meshes
/// contributes its ORIGIN (a light or an empty is a point, not nothing), so a
/// set of lights still frames. False when the set is empty.
bool EngineSceneViewport::selectionBounds(iris::AABB &out) const
{
    bool any = false;
    for (const auto &node : mSelectedSet) {
        if (!node) continue;
        node->update(0.0f);
        const iris::AABB b = preview::worldBoundingBox(node);
        const bool hasBounds = b.getMin().x() <= b.getMax().x();
        const iris::Vec3 lo = hasBounds ? b.getMin() : node->getGlobalPosition();
        const iris::Vec3 hi = hasBounds ? b.getMax() : node->getGlobalPosition();
        if (!any) { out = iris::AABB(); any = true; }
        out.merge(lo);
        out.merge(hi);
    }
    return any;
}

// WHAT AN ALT+DRAG ORBITS AROUND (owner report 2026-09-15, ledger §353: "it
// should not refocus but rotate around the point of the empty Alt+click").
//
// THE POINT UNDER THE CURSOR, by an ordinary scene pick — the thing the user
// pointed at, which is the only pivot that makes an orbit feel like turning an
// object in your hand. It used to be the SELECTION's centre, so orbiting while
// looking somewhere else swung the whole view across the screen.
//
// With nothing under the cursor there is still a point to orbit: the one on the
// view ray at the distance the camera is already working at — the last thing F
// framed, else the world origin, floored so a camera sitting on its own pivot
// still has a radius. The pick is forcePickable so a LOCKED node (the default
// floor) is a surface to orbit around like any other; nothing is selected by it.
iris::Vec3 EngineSceneViewport::altOrbitPivotAt(const QPointF &point)
{
    iris::Vec3 hit;
    if (pickAt(point, false, &hit, true)) return hit;
    const iris::CameraNodePtr cam = viewCamera();
    if (!cam) return mLastOrbitPivot;
    iris::Vec3 a, b;
    pictureSegment(cam, point, a, b);
    const iris::Vec3 eye = cam->getGlobalPosition();
    float distance = eye.distanceToPoint(mLastOrbitPivot);
    if (!(distance > 0.01f)) distance = 15.0f;
    return eye + (b - a).normalized() * distance;
}

QString EngineSceneViewport::gizmoMode() const
{
    if (mGizmo == mRotateGizmo) return QStringLiteral("rotate");
    if (mGizmo == mScaleGizmo)  return QStringLiteral("scale");
    return QStringLiteral("translate");
}

void EngineSceneViewport::setEditorCamera(iris::CameraNodePtr camera)
{
    if (camera) adoptEditorCamera(camera);
    if (mCamController) mCamController->setCamera(mEditorCam);
}

void EngineSceneViewport::resetEditorCam()
{
    clearViewStates();   // a fresh camera invalidates every remembered view pose
    adoptEditorCamera(iris::CameraNode::create());
    mEditorCam->setLocalPos(iris::Vec3(0, 5, 14));
    mEditorCam->lookAt(iris::Vec3(0, 0, 0));
    mEditorCam->angle = 45.0f;
    mEditorCam->nearClip = 0.1f;
    mEditorCam->farClip = 1000.0f;
    if (mCamController) mCamController->setCamera(mEditorCam);
}

void EngineSceneViewport::setEditorData(EditorData *data)
{
    mEditorData = data;
    if (data) {
        if (data->editorCamera) {
            // Project open: another scene's camera — its remembered view
            // poses do not apply (per-view memory is per scene session).
            if (data->editorCamera != mEditorCam) clearViewStates();
            adoptEditorCamera(data->editorCamera);
        }
        mShowLightWires = data->showLightWires;
        mShowGrid = data->showGrid;
        mShowDebugDraw = data->showDebugDrawFlags;
    }
    // The controller must steer the SAME camera the view renders; without this a
    // project load leaves the mouse driving the old, no-longer-rendered camera.
    if (mCamController) mCamController->setCamera(mEditorCam);
    if (mPlayback && mScene && mEditorCam) mScene->setCamera(mEditorCam);
}

// ---------------------------------------------------------------------------
// PILOT MODE (CAMERAS_SPEC D8) and the SELECTION PiP (D3).
//
// Piloting is one substitution and a sweep: viewCamera() answers the piloted
// camera instead of the explorer, and everything that used to read mEditorCam
// for a PIXEL question — the pick rays, the gizmo's screen size, the orbit
// pivot, F-focus, the camera pushed to the view, the screenshot camera — now
// reads that. What still reads mEditorCam is the explorer's own state: the
// per-view camera memory, resetEditorCam, the EditorData round trip.
//
// The flown pose is KEPT on eject (piloting doubles as placement) and lands on
// the undo stack as ONE command, pushed when piloting ends. A command per
// mouse event would be correct and useless.
bool EngineSceneViewport::pilotCamera(iris::CameraNodePtr camera)
{
    if (camera == mPilot) return true;

    // FLYING A SCENE CAMERA IS AN EDIT — it moves a document node — so it is
    // refused while a script owns the document (ledger §423). EJECTING is
    // always allowed (camera is null there): being unable to leave a camera
    // would be a lock, and the rule is non-editable, not locked.
    if (camera && editgate::refuse()) return false;

    // ---- leaving: one undo command for the whole flight -------------------
    if (mPilot) {
        const iris::Vec3 endPos = mPilot->getLocalPos();
        const iris::Quat endRot = mPilot->getLocalRot();
        const bool moved = !qFuzzyCompare(endPos.x(), mPilotStartPos.x()) ||
                           !qFuzzyCompare(endPos.y(), mPilotStartPos.y()) ||
                           !qFuzzyCompare(endPos.z(), mPilotStartPos.z()) ||
                           !qFuzzyCompare(endRot.x(), mPilotStartRot.x()) ||
                           !qFuzzyCompare(endRot.y(), mPilotStartRot.y()) ||
                           !qFuzzyCompare(endRot.z(), mPilotStartRot.z()) ||
                           !qFuzzyCompare(endRot.scalar(), mPilotStartRot.scalar());
        // A RUN THAT STARTED MID-FLIGHT (the entry above is refused, but a
        // script can begin while somebody is already piloting): the flight is
        // not recorded, so the camera goes back to where the flight began
        // rather than keeping a move with no undo step behind it.
        if (moved && editgate::blocked()) {
            mPilot->setLocalPos(mPilotStartPos);
            mPilot->setLocalRot(mPilotStartRot);
        }
        else if (moved && mServices && mServices->undo) {
            // The scale never changes while flying, so both ends carry the
            // node's current one — TransformSceneNodeCommand wants a full TRS.
            const iris::Vec3 scale = mPilot->getLocalScale();
            mServices->undo->push(new TransformSceneNodeCommand(
                mPilot.staticCast<iris::SceneNode>(),
                mPilotStartPos, mPilotStartRot, scale, endPos, endRot, scale));
        }
        mPilot.reset();
    }

    // ---- entering ---------------------------------------------------------
    if (camera) {
        if (!mScene || !mScene->cameras.contains(camera->getGUID())) return false;
        mPilot = camera;
        mPilotStartPos = camera->getLocalPos();
        mPilotStartRot = camera->getLocalRot();
    }
    // The controller must steer the camera the view now renders, and the
    // orbital one must re-derive its pivot from that camera's pose.
    resyncCameraController();
    // A piloted camera is never rotation-locked, and ejecting back into an
    // axis view re-arms the lock (cameraRotationLocked's pilot exception).
    applyRotationLock();
    // The engine's letterbox flag rides the CameraDesc, so it follows on the
    // next applyCamera; nothing else here has to know about it.
    return true;
}

// THE WIDE-ASPECT FRAMING HOLD, for this viewport's OWN camera only
// (viewport/freecamerapolicy.h). The explorer is a free camera and gets the
// hold; a PILOTED scene camera is authored and never does — its lens is the
// shot.
float EngineSceneViewport::freeCameraFramingAspect() const
{
    // ONE SOURCE OF TRUTH, and it is the camera itself (2026-09-07 picking fix).
    // adoptEditorCamera stamps the policy onto the explorer and onto nothing
    // else, so "is this a free camera" is answered by the node the ray is cast
    // through — the document's projection and the engine's now read the same
    // field instead of two hosts agreeing by convention.
    const iris::CameraNodePtr cam = viewCamera();
    return cam ? cam->framingAspect() : 0.0f;
}

void EngineSceneViewport::adoptEditorCamera(iris::CameraNodePtr camera)
{
    if (!camera) return;
    mEditorCam = camera;
    mEditorCam->setFramingAspect(freecam::kFreeCameraFramingAspect);
}

void EngineSceneViewport::setPipEnabled(bool on)
{
    if (on == mPipEnabled) return;
    mPipEnabled = on;
    if (QSettings *st = SettingsManager::getDefaultManager()->settings)
        st->setValue("camera/pip", on);
}

void EngineSceneViewport::setPipSize(double fraction)
{
    const double f = qBound(0.08, fraction, 0.6);
    if (qFuzzyCompare(f, mPipSize)) return;
    mPipSize = f;
    if (QSettings *st = SettingsManager::getDefaultManager()->settings)
        st->setValue("camera/pip_size", f);
}

// THE SELECTION PREVIEW (D3), pushed once a frame from syncFrame. It exists
// only while the conditions all hold, and each of them is a decision:
//   * the preference is on;
//   * a SCENE CAMERA is selected (the explorer is not a scene node, so it can
//     never be here);
//   * it is not the camera being PILOTED — you are already looking through it,
//     and an inset of your own view is a hall of mirrors;
//   * helpers are visible (Game View hides it, like every other editor aid)
//     and the scene is not playing (play is the shot).
// Anything else pushes a disabled desc, which the engine guarantees is
// byte-exact — no workspace, no trace.
void EngineSceneViewport::syncPip()
{
    if (!mMirror || !view()) return;
    iris::CameraNodePtr cam;
    if (mPipEnabled && !mGameView && !mPlaying) {
        cam = mSelectedNode.dynamicCast<iris::CameraNode>();
        if (cam && cam == mPilot) cam.reset();
    }
    jahshaka::engine::ViewPipDesc pip;
    pip.enabled = !cam.isNull();
    if (pip.enabled) {
        // Bottom right, with a margin, sized as a fraction of the WIDTH. The
        // height follows the camera's aspect so the inset is the shape of the
        // shot rather than a fixed box the shot is letterboxed inside — the
        // letterbox is for a camera that constrains its aspect, not for the
        // preference's rounding.
        const float margin = 0.02f;
        const float w = float(mPipSize);
        const float viewAspect = height() ? float(width()) / float(height()) : 1.0f;
        float camAspect = cam->aspectRatio > 0.01f ? cam->aspectRatio : 16.0f / 9.0f;
        if (!cam->constrainAspect) camAspect = viewAspect;   // it fills, so preview it filling
        float h = w * viewAspect / camAspect;
        if (h > 0.6f) h = 0.6f;
        pip.left = 1.0f - margin - w;
        pip.top  = 1.0f - margin * viewAspect - h;
        pip.width = w;
        pip.height = h;
    }
    mMirror->applyPip(cam, view(), pip);
}

EditorData *EngineSceneViewport::getEditorData()
{
    if (!mEditorData) mEditorData = new EditorData();
    mEditorData->editorCamera = mEditorCam;
    mEditorData->showLightWires = mShowLightWires;
    mEditorData->showGrid = mShowGrid;
    mEditorData->showDebugDrawFlags = mShowDebugDraw;
    return mEditorData;
}

void EngineSceneViewport::syncFrame(float dtOverride)
{
    if (!mActive || !view()) return;
    if (!ensureEngineScene()) return;
    // The wall clock, unless a caller supplied a step. mFrameTimer is restarted
    // either way: after a fixed-dt frame the NEXT free-running frame must not
    // charge the document for the time the scripted one took. Nanoseconds,
    // not the old integer milliseconds: the simulation clock accumulates what
    // it is handed, and a 60 Hz panel's 16.67 ms frames rounded to 16 and 17
    // would drift the grid against the wall.
    const float wall = float(double(mFrameTimer.nsecsElapsed()) * 1e-9);
    mFrameTimer.restart();
    const float dt = dtOverride >= 0.0f ? dtOverride : wall;
    // THE ONE CLOCK (ENGINEERING_DEBT_SPEC A4.2): `dt` goes to the document's
    // SimulationClock through exactly one of these two calls, and the seconds
    // it converts into grid steps come back as `simulated` — physics and
    // animation moved by that much, and the engine's own simulation (particles,
    // shader time) is told to move by the same amount below. In the editor
    // (not playing, not simulating) the document ignores the steps and only
    // the engine-side delta is produced.
    float simulated = 0.0f;
    // THE HOST'S STAGE TREE (RENDER_LOOP_MONITOR_SPEC §4.2). Each scope is a
    // pair of not-taken branches while no capture runs; inside one they report
    // EXCLUSIVE milliseconds into the frame record the engine is building, so
    // frames.jsonl carries one tree per frame — host stages and engine stages
    // in the same list, summing to the frame.
    framemonitor::Stage syncStage("host.sync");
    {
    framemonitor::Stage docStage("host.doc");
    if (mPlaying && mPlayback) {
        iris::Viewport vp; vp.width = width(); vp.height = height(); vp.pixelRatioScale = 1.0f;
        simulated = mPlayback->update(vp, dt);   // physics, animation, play controllers move the document
        // EJECTED: the editor's own camera controller runs too (PLAY-SELECT-1).
        // The fly is a per-frame integration of held keys, and while a run has
        // the keyboard nothing in this branch drives it — so without this the
        // ejected arrows would be a dead key rather than the editor's fly. It
        // is the controller's own update, clamped by the controller, exactly as
        // the editing branch below calls it.
        if (mPlayEjected && mCamController) {
            mCamController->setFlySuppressed(bool(mVrPreviewStep));
            mCamController->update(dt);
        }
    } else {
        // `dt` HERE IS THE WALL CLOCK of the frame just gone, and after a
        // UI-thread block that is the whole stall (ledger §356). The camera
        // controller clamps its own step to flystep::kMaxFlyStep — the
        // invariant lives with the controller so it holds for every caller —
        // and the DOCUMENT clock below still gets the real dt.
        // ...UNLESS THE FLY KEYS BELONG TO A WEARER (VR_SPEC §5 phase 4).
        // While a VR session previews this scene the same gesture — right
        // button, the arrow cluster, Shift, the editor's own speed — walks the
        // person in the headset instead of this camera: the step reads this
        // viewport's own heldFlyKeys() and moves the RIG. It stands exactly
        // where the controller's fly stood, so the two can never both move
        // somebody, and everything else about the camera — orbit, pan, dolly,
        // the axis views — is untouched. The desktop stays a full editor.
        //
        // ONLY THE FLY IS SUPPRESSED (VR-4-FIX finding 5): the controller's
        // update() still runs, because it is also what animates an axis-view
        // snap (OrbitalCameraController's lerp) — skipping the whole call froze
        // the Views dropdown for the length of a session. The suppression is a
        // flag ON the controller, so the invariant holds for every caller.
        //
        // AND THE CALLABLE IS COPIED BEFORE IT IS CALLED (finding 6): the step
        // can end the session — a runtime that stopped, a device lost — and
        // ending it clears mVrPreviewStep, which would destroy the closure that
        // is executing.
        if (mCamController) mCamController->setFlySuppressed(bool(mVrPreviewStep));
        if (const std::function<void()> step = mVrPreviewStep) step();
        if (mCamController)            mCamController->update(dt);
        // A PAUSED play-in-place (PlayBack still playing, this flag down so the
        // editor camera answers the mouse) holds the document AND the engine's
        // simulation: no clock step, a 0 delta below. Otherwise the editor's
        // clock ticks — for the Simulate physics and the engine's particles.
        if (mScene && !(mPlayback && mPlayback->isScenePaused()))
            simulated = mScene->advance(dt);
    }
    }
    // THE FOLLOW CAMERA (AVATAR_LOCOMOTION_SPEC §8.5). The arm is COMPUTED in
    // the document (Scene::advance, right after the movement step, so it never
    // lags the character by a frame); what the document cannot know is which
    // camera this viewport DRAWS with — `Scene::camera` and `viewCamera()` are
    // not always the same node in this tree (measured; reported upward). So the
    // host hands its own camera in, once per frame, after the playback update
    // and before applyCamera reads it.
    //
    // Called whether or not anything is possessed: with nothing possessed it
    // restores the camera once and then does nothing, which is what returns the
    // explorer to its exact pre-play pose on stop.
    //
    // PILOTING WINS — a user flying a scene camera asked for that shot — and
    // while piloting the arm is not applied at all, so the eventual restore
    // still puts the explorer back where play found it.
    //
    // ...AND AN EJECTED RUN DOES NOT HOLD THE CAMERA EITHER (PLAY-SELECT-1).
    // Ejecting means the editor has the input, and an editor whose fly is
    // overwritten by the arm every frame has a dead fly. The arm keeps its
    // saved pose latched while it stands down, so un-ejecting takes the shot
    // back and Stop still returns the explorer to where play found it.
    if (mScene && mScene->getPossession() && !mPilot && !mPlayEjected)
        mScene->getPossession()->applyToViewCamera(viewCamera());
    // Emitters used to be ticked here, one document node at a time, because the
    // document owned a CPU particle simulator. It does not any more
    // (PARTICLES_FX2_SPEC): the engine simulates every particle inside
    // renderOneFrame, in the editor and in play mode alike, and the document
    // only says WHAT to emit and how fast the clock runs. The editor's
    // Simulate (physics without play) rides the same Scene::advance above —
    // the document steps its world only while the environment is simulating.
    // The VIEW camera's aspect follows the viewport — except a camera that
    // CONSTRAINS its aspect while being piloted: that number is authored, the
    // engine letterboxes to it, and overwriting it here would silently rewrite
    // the user's shot every frame.
    if (const iris::CameraNodePtr vc = viewCamera()) {
        if (!(mPilot && vc->constrainAspect))
            vc->setAspectRatio(height() ? float(width()) / float(height()) : 1.0f);
    }
    // G (Game View) hides every in-viewport editor helper; play mode hides the
    // grid too (the Unreal look), while the other helpers keep their existing
    // play behaviour.
    const bool helpers = !mGameView;
    if (mMirror) {
        pushEditorHelpers(helpers);
        // The mirror reports its OWN sub-stages from inside sync() (it is the
        // only place that can see them); this scope is their parent, and the
        // subtraction keeps it exclusive.
        mMirror->setMonitorEngine(framemonitor::active() ? mEngine.get() : nullptr);
        framemonitor::Stage mirrorStage("host.mirror");
        mMirror->sync();
    }
    framemonitor::Stage overlayStage("host.overlay");
    if (mGizmo && viewCamera() && mSelectedNode) mGizmo->updateSize(viewCamera());
    if (mOverlay) {
        iris::Vec3 rayPos, rayDir, viewDir;
        mouseRay(rayPos, rayDir, viewDir);
        mOverlay->update((helpers && mSelectedNode) ? mGizmo : nullptr, rayPos, rayDir, viewDir);
    }
    overlayStage.end();
    {
        // env: the sky and the environment push — where a synchronous GI
        // rebuild happens, which is one of the "15 fps that feels like 15"
        // candidates the monitor exists to tell apart (the engine tags the
        // rebuild itself as an event with its cause).
        framemonitor::Stage envStage("host.env");
        if (mMirror) mMirror->applySky(view());
        if (mMirror) mMirror->applyEnvironment(view(), mEngine.get());
        // ...AND THE HEADSET'S EYES, WHICH ARE A VIEW OF THIS SCENE TOO (lane
        // EYE-GRADE-1). The session makes its own View inside the engine, so it
        // was the one view no mirror reached and the wearer got the renderer's
        // defaults instead of the project's grade. It is pushed here, beside
        // the desktop's, through the PER-VIEW half — never a second
        // applyEnvironment, whose scene half counts GI settle frames.
        //
        // THE SAME DRIVING CAMERA as the desktop view, because it is the same
        // shot: the rig is placed on the camera a render of this scene actually
        // looks through (EditorVrPreview::begin's renderCamera rule), so a
        // camera with its own exposure grades both pictures.
        if (mMirror && mEngine)
            if (jahshaka::engine::View *eyes = mEngine->vrView())
                mMirror->applyViewEnvironment(eyes, viewCamera());
    }
    {
        framemonitor::Stage camStage("host.camera");
        if (mMirror && viewCamera())
            mMirror->applyCamera(viewCamera(), view(), freeCameraFramingAspect());
        syncPip();
    }
    // A SCRIPTED step (editor.frame(n, dt)) has to be deterministic for the
    // particles too. They are simulated inside the engine, which has NO clock
    // of its own (Engine.h "Simulation clock"): every frame is told how many
    // seconds to simulate, and that is the document clock's answer for this
    // frame — the same grid steps physics and animation just took — times the
    // scene's particle time scale. A frame that bought no step (a 144 Hz
    // panel's odd frames, a paused scene) freezes the flame for that frame,
    // exactly as it freezes the falling crate.
    if (mEngine)
        mEngine->setFixedFrameDelta(simulated * (mScene ? mScene->particleTimeScale : 1.0f));
}

QImage EngineSceneViewport::takeScreenshot(QSize dimension)
{
    return takeScreenshot(dimension.width(), dimension.height());
}

bool EngineSceneViewport::planarReflectorAccepted(iris::SceneNodePtr node) const
{
    // "No engine to ask" answers true: a caller must not report a failure it
    // cannot see (headless runs, the document-only stand-in viewport).
    if (!mMirror || node.isNull() || !view() || !view()->scene()) return true;
    const jahshaka::engine::NodeId id = mMirror->engineNode(node.data());
    if (!id) return true;   // not mirrored yet — the next sync decides
    return view()->scene()->nodePlanarReflector(id);
}

IEditorViewport::GiVoxelStatsInfo EngineSceneViewport::giVoxelStats(int cascade)
{
    GiVoxelStatsInfo out;
    if (!view() || !view()->scene()) return out;   // available stays false
    const jahshaka::engine::GiVoxelStats st = view()->scene()->giVoxelStats(cascade);
    out.available = st.available;
    if (!st.available) return out;
    out.cascade = st.cascade;
    out.width = st.width; out.height = st.height; out.depth = st.depth;
    out.format = QString::fromStdString(st.format);
    out.formatMax = st.formatMax;
    out.multiplier = st.multiplier;
    out.peak = st.peak;
    out.peakDirect = st.peakDirect;
    out.meanLit = st.meanLit;
    out.voxelsLit = qint64(st.voxelsLit);
    out.voxelsAtMax = qint64(st.voxelsAtMax);
    out.directAtMax = qint64(st.directAtMax);
    out.voxels = qint64(st.voxels);
    out.voxelsAboveOne = qint64(st.voxelsAboveOne);
    return out;
}

IEditorViewport::GiStatusInfo EngineSceneViewport::giStatus() const
{
    GiStatusInfo out;
    if (!view() || !view()->scene()) return out;   // available stays false
    const jahshaka::engine::GiStatus st = view()->scene()->giStatus();
    out.available = true;
    switch (st.mode) {
    case jahshaka::engine::GiMode::Off:              out.mode = QStringLiteral("off"); break;
    case jahshaka::engine::GiMode::Vct:              out.mode = QStringLiteral("vct"); break;
    case jahshaka::engine::GiMode::VctPccHybrid:     out.mode = QStringLiteral("vct_pcc_hybrid"); break;
    }
    out.probeCount = st.probeCount;
    out.pccBound   = st.pccBound;
    out.vctBound   = st.vctBound;
    const auto q = [](const jahshaka::engine::Vec3 &v) { return QVector3D(v.x, v.y, v.z); };
    out.boundsMin      = q(st.boundsMin);
    out.boundsMax      = q(st.boundsMax);
    out.voxelMetres = st.voxelMetres;
    out.probeRegionMin = q(st.probeRegionMin);
    out.probeRegionMax = q(st.probeRegionMax);
    out.probeHdr       = st.probeHdr;
    out.probeCaptureSize   = st.probeCaptureSize;
    out.probesDropped      = st.probesDropped;
    out.probeGateCrossings = st.probeGateCrossings;
    out.probeShadows   = st.probeShadows;
    out.probeUpdatesPerFrame = st.probeUpdatesPerFrame;
    out.probeShapeMin        = q(st.probeShapeMin);
    out.probeShapeMax        = q(st.probeShapeMax);
    out.cubemapProbeSlotsPerCell = st.cubemapProbeSlotsPerCell;
    out.probesClampedToRegion    = st.probesClampedToRegion;
    out.probesExceedingCell      = st.probesExceedingCell;
    out.worstProbeShapeCellRatio = st.worstProbeShapeCellRatio;
    out.reusedLastRefresh    = st.reusedLastRefresh;
    out.ifdBound             = st.ifdBound;
    out.ifdProbes            = st.ifdProbes;
    out.ifdConverged         = st.ifdConverged;
    out.ifdProbesPerFrame    = st.ifdProbesPerFrame;
    out.ifdMin               = q(st.ifdMin);
    out.ifdMax               = q(st.ifdMax);
    out.ifdFollows           = quint64(st.ifdFollows);
    out.probeCapturesLastFrame = st.probeCapturesLastFrame;
    out.probeCapturesDeferred  = quint64(st.probeCapturesDeferred);
    out.staleProbes            = st.staleProbes;
    out.staleSerial            = quint64(st.staleSerial);
    out.rebuilds               = quint64(st.rebuilds);
    // MOBILITY (REALTIME_REFLECTIONS_SPEC §3.3.4): what the RENDERER holds —
    // the document's resolution reaches it through Scene::setNodeMovable, and
    // editor.mirrorStats().movableNodes is the document's own side of the same
    // question. The misses are the mirror's (only the thing walking the
    // document can notice an object that started moving with nothing predicting
    // it), reported here because this is where a reader is already looking at
    // what the room's lighting is made of.
    {
        const jahshaka::engine::MobilityStatus m = view()->scene()->mobilityStatus();
        out.movableItems     = int(m.movableItems);
        out.movableLights    = int(m.movableLights);
        out.mobilityRebuilds = quint64(m.mobilityRebuilds);
    }
    if (mMirror) {
        out.mobilityMisses   = mMirror->mobilityMissCount();
        out.lastMobilityMiss = mMirror->lastMobilityMiss();
    }
    switch (st.lastStaleReason) {
    case jahshaka::engine::GiStaleReason::None:     out.lastStaleReason = QStringLiteral("none"); break;
    case jahshaka::engine::GiStaleReason::Rebuild:  out.lastStaleReason = QStringLiteral("rebuild"); break;
    case jahshaka::engine::GiStaleReason::Refresh:  out.lastStaleReason = QStringLiteral("refresh"); break;
    case jahshaka::engine::GiStaleReason::Moved:    out.lastStaleReason = QStringLiteral("moved"); break;
    case jahshaka::engine::GiStaleReason::Light:    out.lastStaleReason = QStringLiteral("light"); break;
    case jahshaka::engine::GiStaleReason::Material: out.lastStaleReason = QStringLiteral("material"); break;
    case jahshaka::engine::GiStaleReason::Sky:      out.lastStaleReason = QStringLiteral("sky"); break;
    case jahshaka::engine::GiStaleReason::Ambient:  out.lastStaleReason = QStringLiteral("ambient"); break;
    case jahshaka::engine::GiStaleReason::Fog:      out.lastStaleReason = QStringLiteral("fog"); break;
    case jahshaka::engine::GiStaleReason::Mobility: out.lastStaleReason = QStringLiteral("mobility"); break;
    case jahshaka::engine::GiStaleReason::Camera: out.lastStaleReason = QStringLiteral("camera"); break;
    }
    out.cascadesAwaitingCamera = st.cascadesAwaitingCamera;
    out.awaitingVoxelTextures = st.awaitingVoxelTextures;
    out.cascadeVoxelLod = st.cascadeVoxelLod;
    out.cascadeProfileVr = st.cascadeProfileVr;
    out.cascades.clear();
    out.cascades.reserve(int(st.cascades.size()));
    for (const auto &c : st.cascades) {
        GiStatusInfo::CascadeInfo ci;
        ci.halfSize   = c.halfSize;
        ci.resolution = c.resolution;
        ci.cell       = c.cell;
        ci.step       = c.step;
        ci.guaranteedRadius = c.guaranteedRadius;
        ci.centre     = q(c.centre);
        ci.rebuilds   = quint64(c.rebuilds);
        ci.pending    = c.pending;
        ci.items      = c.items;
        ci.attached   = c.attached;
        ci.lastCpuMs  = c.lastCpuMs;
        ci.lodLevels.reserve(int(c.lodLevels.size()));
        for (int n : c.lodLevels) ci.lodLevels.append(n);
        ci.voxelTriangles = qint64(c.voxelTriangles);
        ci.voxelDispatches = qint64(c.voxelDispatches);
        out.cascades.append(ci);
    }
    out.cascadeFullRebuilds = quint64(st.cascadeFullRebuilds);
    out.cascadeDeferrals    = quint64(st.cascadeDeferrals);
    out.cascadeDirtyMajority = quint64(st.cascadeDirtyMajority);
    out.chainSweeps          = st.chainSweeps;
    out.chainSettles         = st.chainSettles;
    out.dragMovers           = st.dragMovers;
    out.dragMoverGestures    = double(st.dragMoverGestures);
    // THE RAY-QUERY TIER (PHOTON_SPEC §7 R1). A separate engine reading, not a
    // member of GiStatus: the tier is a geometry service, and GI is only its
    // first consumer.
    {
        const jahshaka::engine::RayQueryStatus rq = view()->scene()->rayQueryStatus();
        out.rayQuery.available    = rq.available;
        out.rayQuery.enabled      = rq.enabled;
        out.rayQuery.blasCount    = rq.blasCount;
        out.rayQuery.instances    = rq.instances;
        out.rayQuery.triangles    = rq.triangles;
        out.rayQuery.blasBytes    = quint64(rq.blasBytes);
        out.rayQuery.tlasBytes    = quint64(rq.tlasBytes);
        out.rayQuery.tlasMs       = rq.tlasMs;
        out.rayQuery.blasMs       = rq.blasMs;
        out.rayQuery.gatherMs     = rq.gatherMs;
        out.rayQuery.lastWasRefit = rq.lastWasRefit;
        out.rayQuery.tlasBuilds   = quint64(rq.tlasBuilds);
        out.rayQuery.tlasRefits   = quint64(rq.tlasRefits);
        out.rayQuery.blasBuilds   = quint64(rq.blasBuilds);
        out.rayQuery.reflect      = rq.reflect;
        out.rayQuery.reflectRays  = rq.reflectRays;
        out.rayQuery.reflectMs    = rq.reflectMs;
    }
    return out;
}

void EngineSceneViewport::renderFrames(int n)
{
    renderFrames(n, -1.0f);
}

bool EngineSceneViewport::canRenderFrames() const
{
    // The one thing renderFrames() itself tests before doing anything.
    return mEngine != nullptr;
}

void EngineSceneViewport::renderFrames(int n, float dt)
{
    // editor.frame(n, dt): the deterministic document→engine sync + render
    // pattern of the headless suites, synchronously — scripts step exact frames
    // instead of sleeping against the driver timer.
    //
    // WITHOUT `dt` this was only half deterministic: syncFrame pulls
    // mFrameTimer.restart(), so in PLAY mode each stepped frame advanced the
    // document's animation clock by however long the previous statement
    // happened to take. Every scripted play-mode assertion was therefore timing
    // dependent — the last thing standing between us and a play-mode pixel
    // gate, now that clip evaluation is the engine's and everything else in the
    // chain is driven by absolute time.
    if (!mEngine) return;
    for (int i = 0; i < n; ++i) {
        syncFrame(dt);
        ++mFrameEpoch;
        // A SCRIPTED frame is not a driver frame, and a capture taken while a
        // script runs must not read like the owner's own loop (§4.2's frame
        // reason). Set per iteration: the engine consumes the cause and resets
        // it to Driver on every frame.
        if (framemonitor::active())
            mEngine->setNextFrameCause(jahshaka::engine::FrameCause::Scripted);
        mEngine->renderOneFrame();
        // ...and the device-loss end for the same reason (lane XID-2): a
        // scripted run that loses the GPU would otherwise keep calling frames
        // that all throw -- measured at 4,348 VK_ERROR_DEVICE_LOST in one run,
        // painting nothing -- and end in a SEGV in the ordinary teardown.
        devicelossend::checkAfterFrame(mEngine.get());
        // The deterministic path bypasses EngineRenderDriver entirely, so it
        // has to drain the engine's error sink itself or a scripted/headless
        // run would be the one place failures stay silent — which is exactly
        // where the gates live (services/engineerrorpump.h).
        EngineErrorPump::instance().drain(mEngine.get());
        // AND THE MONITOR'S RING, for the same reason the driver drains at the
        // end of its tick: a capture taken while a script runs has no driver
        // ticks at all (the script holds the UI thread), so without this every
        // record would sit in the engine's ring until the capture stopped — and
        // a run that threw, or an app that quit, would take them with it.
        FrameMonitor::instance().noteTickEnd();
        // AND THE RENDER LOOP IS TOLD A FRAME HAPPENED (DOUBLE-FRAME-1). The
        // driver's Live pacing is "at most one frame per display period", and
        // its clock used to count only its OWN ticks — so a script stepping
        // frames got a driver frame in the gap between two verbs on top of the
        // one it had just drawn (measured: 1.35 engine frames per scripted
        // frame AT REST, 1.48-1.58 during a scripted drag, which is the render
        // audit's "two frames per document edit" seen from its real cause). A
        // no-op for every run that is not Live.
        if (mDriver) mDriver->noteExternalFrame();
    }
    // Scripted stepping is the deterministic path: editor.frame(2) must be
    // enough to take the cover down, exactly as two driver frames would.
    refreshOverlay();
}

IEditorViewport::ShadowStatusInfo EngineSceneViewport::shadowStatus() const
{
    ShadowStatusInfo out;
    if (!mEngine) return out;                 // available stays false
    const jahshaka::engine::ShadowStatus st = mEngine->shadowStatus();
    out.available = st.live;
    out.resolution = int(st.resolution);
    out.maps = int(st.maps);
    out.pssmSplits = int(st.pssmSplits);
    out.focusedMaps = int(st.focusedMaps);
    out.lightSlots = int(st.lightSlots);
    out.casters = int(st.casters);
    out.budget = int(st.budget);
    out.requestedBudget = int(st.requestedBudget);
    out.atlasWidth = int(st.atlasWidth);
    out.atlasHeight = int(st.atlasHeight);
    out.atlasBytes = qint64(st.atlasBytes);
    out.reflectAtlasBytes = qint64(st.reflectAtlasBytes);
    out.probeAtlasBytes = qint64(st.probeAtlasBytes);
    out.countersMeasured = st.countersMeasured;
    out.shadowPassesLastFrame = int(st.shadowPassesLastFrame);
    out.cachedMapRendersLastFrame = int(st.cachedMapRendersLastFrame);
    out.shaderLightMismatches = int(st.shaderLightMismatches);
    out.reflectPassesLastFrame = int(st.reflectPassesLastFrame);
    out.probePassesLastFrame = int(st.probePassesLastFrame);
    out.reflectLampPassesLastFrame = int(st.reflectLampPassesLastFrame);
    out.probeLampPassesLastFrame = int(st.probeLampPassesLastFrame);
    out.cachedInstances = int(st.cachedInstances);
    out.uncachedInstances = int(st.uncachedInstances);
    out.viewCached = st.viewCached;
    out.mapsDirtiedLastFrame = int(st.mapsDirtiedLastFrame);
    out.atlasRebuilds = int(st.atlasRebuilds);
    out.casterWalkItems = st.casterWalkItems;
    // The engine speaks NodeIds; the panel and the verb speak guids. The map is
    // built from the scene's own light list rather than from a second index in
    // the mirror: a scene has a handful of lights, this runs on a readback, and
    // an index that has to stay honest through every node removal is a bug
    // waiting for a rainy day.
    QHash<quint64, QString> guidOf;
    if (mMirror && !mScene.isNull()) {
        for (const auto &l : mScene->lights) {
            if (l.isNull()) continue;
            const jahshaka::engine::NodeId id = mMirror->engineNode(l.data());
            if (id) guidOf.insert(quint64(id), l->getGUID());
        }
    }
    for (const jahshaka::engine::ShadowMapInfo &m : st.mapped) {
        ShadowMapEntry e;
        e.slot = int(m.slot);
        e.node = guidOf.value(quint64(m.node));
        e.isCached = m.isCached;
        e.dirty = m.dirty;
        e.pssm = m.pssm;
        e.passesLastFrame = int(m.passesLastFrame);
        out.mapped.push_back(e);
    }
    for (jahshaka::engine::NodeId id : st.unmapped) {
        const QString guid = guidOf.value(quint64(id));
        if (!guid.isEmpty()) out.unmapped.push_back(guid);
    }
    return out;
}

IEditorViewport::MirrorStats EngineSceneViewport::mirrorStats() const
{
    MirrorStats s;
    if (!mMirror) return s;   // available stays false: no mirror, no counts
    s.available = true;
    s.giPushes = mMirror->giPushCount();
    s.giRefreshes = mMirror->giRefreshCount();
    s.giLightRefreshes = mMirror->giLightRefreshCount();
    s.giLightRefreshesAtRest = mMirror->giLightRefreshAtRestCount();
    // MOBILITY (REALTIME_REFLECTIONS_SPEC §3.3): what the DOCUMENT resolved.
    // The renderer's own records — and the play-time misses — are reported by
    // world.giStatus(), beside the probe and rebuild counters they belong with.
    s.movableNodes = mMirror->movableNodeCount();
    s.nodesVisited = quint64(mMirror->visitedCount());
    s.materialBuilds = mMirror->materialBuildCount();
    s.staticNodes = mMirror->staticNodeCount();
    s.staticRepromotions = mMirror->staticRepromotionCount();
    // THE DIRTY SET (DIRTY_SET_MIRROR_SPEC): what the document reported, what
    // the verifier re-read behind it, and whether this frame's sync was the
    // change list or the whole walk.
    s.dirtyNodes = mMirror->dirtyNodeCount();
    s.evictedNodes = mMirror->evictedNodeCount();
    s.verifierVisits = mMirror->verifierVisitCount();
    s.verifierCatches = mMirror->verifierCatchCount();
    s.pushes = mMirror->visitPushCount();
    s.walkMode = QString::fromLatin1(mMirror->walkMode());
    return s;
}

IEditorViewport::RigStatsInfo EngineSceneViewport::rigStats() const
{
    RigStatsInfo s;
    if (!mEngineScene) return s;   // available stays false: no engine, no counts
    const jahshaka::engine::RigStats r = mEngineScene->rigStats();
    s.available = true;
    s.rigged = int(r.rigged);
    s.instances = int(r.instances);
    s.shared = int(r.shared);
    s.streamedBones = int(r.streamedBones);
    // The push counter is the MIRROR's: the engine cannot know how often it was
    // told, only what it holds.
    if (mMirror) s.clipPushes = mMirror->clipStatePushes();
    return s;
}

QString EngineSceneViewport::dumpMaterial(const QString &nodeGuid) const
{
    if (!mMirror || !mEngineScene || !mScene) return QString();
    auto node = mScene->nodes.value(nodeGuid);
    if (!node) return QString();
    const auto material = mMirror->engineMaterial(node.data());
    if (!material) return QString();
    return QString::fromStdString(mEngineScene->dumpMaterial(material));
}

QImage EngineSceneViewport::takeScreenshot(int width, int height)
{
    // THE DEFAULT DOOR IS THE THUMBNAIL GRADE, and it is not the user's door.
    // Its callers are project preview tiles (ProjectService) and the asset
    // viewer — pictures OF CONTENT, taken in sweeps, which must stay cheap
    // (SS1 keeps `secondaryfx`'s minimal chain exactly where it belongs). The
    // USER's Screenshot action asks for ScreenshotGrade::Scene explicitly
    // (MainWindow::takeScreenshot); the SCRIPT door defaults to Plain,
    // deliberately, because that one is a measuring instrument and every pixel
    // suite in the tree asserts its exact colours.
    return takeScreenshot(width, height, ScreenshotGrade::Tonemap);
}

// ---- DOES A USER'S SCREENSHOT CONTAIN THE EDITOR'S HELPERS? ---------------
//
// NO — OWNER, 2026-09-13. A user's screenshot is a picture of the SCENE, so the
// transform gizmo, the selection outline, the light and camera wires, the grid
// and the GI-volume boxes are all left out of it, even while the viewport is
// showing them. SS1 asked the question rather than assuming an answer; this is
// the answer.
//
// It applies to ScreenshotGrade::Scene ONLY — the user's door. The script
// grades are measuring instruments whose contents are pinned by suites, and
// several of those suites photograph a gizmo on purpose (gizmo.screen_size,
// app.selection_outline, ui.shot_aspect). Flipping this constant is still the
// whole switch, in both directions.
static constexpr bool kUserShotKeepsEditorHelpers = false;

QImage EngineSceneViewport::takeScreenshot(int width, int height, ScreenshotGrade grade)
{
    // Offscreen render of the same engine scene at the requested size, then readback.
    if (!mEngine || !mEngineScene || width <= 0 || height <= 0) return QImage();
    View *shot = mEngine->createOffscreenView("screenshot-" + std::to_string(++mViewSerial),
                                             unsigned(width), unsigned(height),
                                             Colour(0.10f, 0.11f, 0.14f));
    if (!shot) return QImage();
    // THE USER'S PICTURE OPENS NEITHER HELPER CHANNEL (VR-4-FIX finding 2).
    //
    // `pushEditorHelpers(false)` below takes away the furniture the MIRROR
    // owns — the grid, the wires and icons, the outline, the gizmo — and that
    // is everything the editor puts in the scene ITSELF. It is not everything
    // in the scene: while a VR session runs, the wearer's CONTROLLER PROXIES
    // are pushed by the VR module, carry kHelperBit|kVrHelperBit, and a view
    // whose ordinary channel is open therefore drew them straight into the
    // user's screenshot. The channels are the structural answer — one mask,
    // set once at view creation, that no future helper can slip past — and
    // saying it here makes Engine.h's claim ("a view that hides the furniture
    // altogether … hides this too") true of a user's shot as well.
    //
    // ALWAYS for the Scene grade, Game View included: Game View has the mirror
    // push no furniture, but nothing in it excludes a proxy. The script grades
    // (Plain, Tonemap, Viewport) are measuring instruments whose contents
    // suites pin — several photograph a gizmo on purpose — so they keep the
    // channel, and kUserShotKeepsEditorHelpers stays the one switch.
    if (!kUserShotKeepsEditorHelpers && grade == ScreenshotGrade::Scene)
        shot->setHelpersVisible(false);
    shot->setScene(mEngineScene);

    // THE GIZMO IS SIZED FOR THE SHOT, NOT FOR THE WINDOW (hygiene lane,
    // 2026-09-09; the follow-up recorded with the free-camera framing fix).
    //
    // Gizmo::updateSize makes the handles a CONSTANT FRACTION of the frame
    // height, and it does that from `camera->effectiveFovDegrees()` — the
    // rendered vertical angle, which depends on the camera's ASPECT because of
    // the wide-aspect framing hold. The last call to it used the VIEWPORT's
    // aspect; a screenshot is a different frame shape (256x256 in most of the
    // corpus, against a 2.4:1 window) whose rendered angle is therefore
    // different, so the gizmo photographed at whatever size the window
    // happened to want. On a wide window the hold narrows the window's angle
    // and not the square shot's, and the handles came out ~30% small.
    //
    // So: re-size against the SHOT's aspect, re-push the overlay transforms,
    // take the picture, put the viewport's own sizing back. The rays are the
    // ones the last refreshOverlay used, deliberately — this must change the
    // gizmo's SIZE and nothing about which part is highlighted.
    //
    // THE CAMERA TAKES THE SHOT'S ASPECT FOR THE WHOLE SHOT, gizmo or not
    // (lane L11, platform audit C2b.2). For a free camera the engine already
    // resolves the frame from the shot's own target (OgreView::applyCamera),
    // so this changes no pixel of the scene; what it changes is that every
    // DOCUMENT-side reader — the gizmo's screen-constant size, the camera's
    // own projMatrix — describes the picture being taken instead of the
    // window beside it. ONE EXCEPTION, and it used to be a defect: a piloted
    // camera that CONSTRAINS its aspect renders letterboxed at its AUTHORED
    // aspect in any view, the shot included, so its aspect is the shot's
    // already. The old gizmo-only swap wrote the shot's shape over it, and a
    // shot of a 2.39 camera with a selection came out with no bars at all.
    //
    // What this does NOT make window-independent is a pose that was FRAMED for
    // the window: F / editor.focusSelection / editor.frameNode back off until
    // the subject fills the viewport's rendered angle, which on a window wider
    // than 16:9 depends on the window (the framing hold). A shot of that pose
    // differs between windows because the POSE does; a shot of a pose set with
    // editor.setCamera is the same picture at any window size (ui.shot_aspect).
    const iris::CameraNodePtr shotCam = viewCamera();
    // The helpers question, resolved once (see kUserShotKeepsEditorHelpers).
    // Only the USER's picture is affected: the script grades are measuring
    // instruments and their contents are pinned by suites.
    const bool hideHelpers = !kUserShotKeepsEditorHelpers &&
                             grade == ScreenshotGrade::Scene && !mGameView;
    const bool resizeGizmo = shotCam && mGizmo && mOverlay && mSelectedNode &&
                             !mGameView && !hideHelpers;
    const float viewportAspect = shotCam ? shotCam->aspectRatio : 0.0f;
    const bool authoredAspect = shotCam && mPilot && shotCam->constrainAspect;
    iris::Vec3 gizmoRayPos, gizmoRayDir, gizmoViewDir;
    if (resizeGizmo) mouseRay(gizmoRayPos, gizmoRayDir, gizmoViewDir);   // the viewport's rays
    if (shotCam && !authoredAspect) shotCam->setAspectRatio(float(width) / float(height));
    if (resizeGizmo) {
        mGizmo->updateSize(shotCam);
        mOverlay->update(mGizmo, gizmoRayPos, gizmoRayDir, gizmoViewDir);
    }

    if (mMirror) {
        if (hideHelpers) {
            // Game View's own answer, for the duration of one picture, through
            // the ONE function that knows what every helper should be — so the
            // restore below is exact rather than approximate (review item 3:
            // the first cut cleared five switches by hand and left the next
            // shot in the same run helper-less, which would have moved the
            // plain readback the whole pixel corpus asserts).
            pushEditorHelpers(false);
            if (mOverlay) {
                iris::Vec3 rayPos, rayDir, viewDir;
                mouseRay(rayPos, rayDir, viewDir);
                mOverlay->update(nullptr, rayPos, rayDir, viewDir);
            }
        }
        // AN EDITOR SHOT IS THE EDITOR'S PICTURE, at every grade
        // (PLAYER-FLOOR-1): the Player may have left its floor hide on the
        // SHARED mirror, and a plain readback that quietly lost the ground
        // would move the whole pixel corpus. The editor's frame loop says the
        // same thing in pushEditorHelpers; a shot does not go through it.
        mMirror->setHideDefaultFloor(false);
        mMirror->sync();
        // The shot view starts with a hardcoded background; give it the document
        // sky (flat colour) and world settings (shadows toggle) like the live view.
        // Textured skies are scene geometry and show up regardless.
        mMirror->applySky(shot);
        mMirror->applyEnvironment(shot);
        if (viewCamera()) mMirror->applyCamera(viewCamera(), shot, freeCameraFramingAspect());
        // applyEnvironment (and applyCamera, a line above) pushed the scene's
        // post-fx description — the VIEWPORT's description — which an offscreen
        // view ignores unless it is told otherwise (POST_CHAIN_SPEC §7.3).
        // `grade` is that opt-in, and IEditorViewport::ScreenshotGrade documents
        // the four answers; this is where each one is carried out:
        //   Plain     nothing at all — the neutral, exactly reproducible
        //             readback every pixel suite asserts;
        //   Tonemap   the deterministic filmic grade ONLY, at the SCENE's
        //             exposure — the thumbnail picture;
        //   Scene     the whole chain at the on-screen view's MEASURED
        //             exposure — the editor's own picture, what a user gets;
        //   Viewport  the whole chain with its own adaptive exposure re-seeded
        //             from the description — what `postFx: true` has always
        //             meant, and the door a pipped camera's exposure uses.
        if (grade == ScreenshotGrade::Scene) {
            // THE MEASURED EXPOSURE COMES FROM THE VIEW ON SCREEN, read here
            // because this is the only place that has both views: the shot can
            // never measure (two frames), the viewport has been measuring this
            // same scene for as long as it has been open. 0 (no on-screen view,
            // no HDR, nothing presented yet) falls back inside applyScene.
            secondaryfx::applyScene(shot, view() ? view()->measuredExposureScale() : 0.0f);
        } else if (grade == ScreenshotGrade::Viewport) {
            jahshaka::engine::PostFxDesc fx = shot->postFx();
            fx.allowOffscreen = true;
            shot->setPostFx(fx);
            // A ONE-SHOT RENDER CANNOT ADAPT (CAMERA_LENS_SPEC §4). The HDR
            // chain's auto exposure is a temporal filter seeded at workspace
            // build and converging at ~75%/s; this view lives for two frames,
            // so without this the shot grades at the SEED and the exposure the
            // caller asked for — the world's, or the driving camera's own —
            // barely reaches the picture. Re-seeding from the description just
            // pushed makes the first frame the right frame.
            shot->resetExposureHistory();
        } else if (grade == ScreenshotGrade::Tonemap) {
            // ...at the SCENE's exposure, which is what the description
            // applyEnvironment just pushed carries (SS1: this used to be the
            // caller's default and the World's value was thrown away, so every
            // regraded world photographed at +0.6).
            secondaryfx::apply(shot, true, shot->postFx().exposure);
        }
    }
    // A screenshot is an offscreen render of this same scene: without this
    // the on-screen viewport draws two extra full frames and presents them
    // (fps audit F5, bridge/offscreenrenderscope.h). The shot view is
    // offscreen, so it is untouched.
    OffscreenRenderScope quiet(mEngine.get());
    // Plus whatever the texture load-request counter still owes
    // (THREADING_ADOPTION_SPEC.md P2 item 4) — bridge/stableoffscreenrender.h.
    renderStableFrames(mEngine.get());
    Image img;
    QImage result;
    if (shot->readPixels(img) && img.width && img.height) {
        result = QImage(int(img.width), int(img.height), QImage::Format_RGBA8888);
        for (unsigned y = 0; y < img.height; ++y)
            memcpy(result.scanLine(int(y)), &img.rgba[size_t(y) * img.width * 4u], img.width * 4u);
    }
    mEngine->destroyView(shot);
    // ...and the viewport gets its own sizing back, before anything presents
    // another on-screen frame.
    if (shotCam && !authoredAspect) shotCam->setAspectRatio(viewportAspect);
    if (resizeGizmo) {
        mGizmo->updateSize(shotCam);
        mOverlay->update(mGizmo, gizmoRayPos, gizmoRayDir, gizmoViewDir);
    }
    if (hideHelpers) {
        // ...and the viewport gets its helpers back, in full, before the mirror
        // is synced by anything else — including the very next screenshot in
        // the same script run. The overlay half is syncFrame's own block: the
        // mirror carries the wires, the outline, the grid and the GI boxes,
        // the OVERLAY carries the gizmo, and a restore that did only one of
        // them would still be a restore that does not restore.
        pushEditorHelpers(!mGameView);
        if (mMirror) mMirror->sync();
        if (mGizmo && viewCamera() && mSelectedNode) mGizmo->updateSize(viewCamera());
        if (mOverlay) {
            iris::Vec3 rayPos, rayDir, viewDir;
            mouseRay(rayPos, rayDir, viewDir);
            mOverlay->update((!mGameView && mSelectedNode) ? mGizmo : nullptr,
                             rayPos, rayDir, viewDir);
        }
    }
    return result;
}

void EngineSceneViewport::begin()
{
    mActive = true;
    // Coming back from another space. ONE SCENE since lane PLAYER-1 — the
    // Player page is a second VIEW on this scene, drawn by this very mirror —
    // so there is no binding to take back. What there IS: the post chain, MSAA,
    // the shadow flag and the background are pushed PER VIEW, and the mirror's
    // latches say "already pushed" about the view that has just handed the
    // screen over. Dropping them is what gives THIS view its own chain again.
    // (The GI half is a binding re-assert, never a rebuild — see
    // SceneMirror::invalidateEnvironment.)
    if (mMirror) mMirror->invalidateEnvironment();
    if (view()) view()->setEnabled(true);
    // (NOTHING TO TAKE BACK FROM A HEADSET. F9's hand-the-mirror-over-and-back
    // dance is gone with lane MIRROR-LIVE-1: leaving this page ENDS the session
    // it hosted, so no session can be mirroring onto this view while the page
    // is away, and there is no mirror to re-take on the way in.)
    refreshOverlay();
}

void EngineSceneViewport::end()
{
    mActive = false;
    // THE HEADSET COMES OFF WITH THE PAGE (the owner, 2026-09-18, joint; lane
    // MIRROR-LIVE-1). A VR preview is hosted BY this page: its mirror paints
    // this view, its fly keys are this viewport's, and the wearer is standing
    // where this camera stood. Leaving the page used to keep all of that alive
    // behind another page — a workspace presenting the headset's eye into a
    // window nobody is looking at, every frame, for the rest of the session
    // (F9's half-answer was to hand the mirror back and forth instead).
    //
    // So the session ENDS, by its owner and by exactly the path `vr.end()`
    // takes: the desktop View, the rig, the locomotion overrides and the hands'
    // proxies are all restored the way a manual end restores them. No toast —
    // the person who left the page is the person who was wearing it.
    //
    // COPIED BEFORE IT IS CALLED, like the clearScene() call of the same hook:
    // ending the session clears this very std::function.
    if (const std::function<void()> ends = mVrPreviewEnds) ends();
    if (view()) view()->setEnabled(false);
}

// ---------------------------------------------------------------------------
// The cover: what this viewport looks like while the engine has no frame of the
// current world on screen.
//
// DRAWN BY THE ENGINE since owner decision D2 (STATS_OVERLAY_SPEC.md §6). It
// used to be ViewportCover — an ordinary Qt widget in the same grid cell as
// this one which, while it was up, owned its own native X window stacked above
// this one's by setAttribute(WA_NativeWindow) + raise(). That was the only
// arrangement that works for a Qt-painted cover on X11, and it was also a
// standing invitation to the whole X11 stacking-and-input family of defects: an
// extra surface whose stacking order, map state and input region are managed by
// nobody in particular, which deliberately swallowed clicks and therefore made
// the viewport look DEAD rather than covered whenever it was up at the wrong
// moment.
//
// The engine-drawn cover has no second window, no stacking order, no input
// region and no Qt clock. It is a panel and two text areas in the frame the
// engine was going to present anyway (irisgl/engine/src/OgreOverlayHud.cpp).
//
// WHAT SURVIVED, deliberately: presentationState(), framesPresented(),
// kPresentsBeforeReveal and the editor.viewportState() verb they feed. The
// state machine was never the problem — only its output device changed, and
// scripting.e2e.presentation_state is the unchanged gate that says so.

qulonglong EngineSceneViewport::presentsSinceBind() const
{
    if (!view()) return 0;
    const qulonglong now = qulonglong(view()->framesPresented());
    // The engine resets its own counter when a scene is (re)bound; then `now`
    // is below the baseline and IS the answer.
    return now >= mPresentBaseline ? now - mPresentBaseline : now;
}

QString EngineSceneViewport::presentationState() const
{
    // No render target of our own, or one that never reaches the widget: the
    // cover has no business here and the honest answer is "offscreen".
    if (!view() || view()->isOffscreen()) return QStringLiteral("offscreen");
    if (!mScene) return QStringLiteral("noscene");
    return presentsSinceBind() >= kPresentsBeforeReveal
               ? QStringLiteral("presenting")
               : QStringLiteral("loading");
}

qulonglong EngineSceneViewport::framesPresented() const
{
    return presentsSinceBind();
}

void EngineSceneViewport::setShowFps(bool value)
{
    if (mShowStats == value) return;
    mShowStats = value;
    mStatsLines.clear();
    mStatsClock.invalidate();   // rebuild the text on the very next refresh
    refreshOverlay();
}

jahshaka::engine::ViewOverlayDesc EngineSceneViewport::overlayDesc() const
{
    using Cover = jahshaka::engine::ViewOverlayDesc::Cover;
    jahshaka::engine::ViewOverlayDesc d;

    // ---- the stats readout (STATS_OVERLAY_SPEC §4) -------------------------
    // Composed HOST-SIDE, and that is the point of ViewOverlayDesc::lines: the
    // most useful number on this row — how long the tick's work took — belongs
    // to EngineRenderDriver, not to Ogre. Rate-limited to ~5 Hz.
    // ---- the PILOTING banner (CAMERAS_SPEC D8) ----------------------------
    // Engine-drawn, on the same overlay the stats readout uses, for the reason
    // the loading cover moved there: the viewport is a NATIVE window with
    // WA_PaintOnScreen and a null paint engine, so a Qt label over it is a
    // second native window and a fight (STATS_OVERLAY_SPEC's whole rationale).
    // Top-left, above the stats rows when both are up.
    if (mPilot) {
        d.stats = true;
        d.corner = jahshaka::engine::OverlayCorner::TopLeft;
        const QString name = mPilot->getName().isEmpty() ? tr("Camera") : mPilot->getName();
        d.lines.push_back(tr("Piloting: %1").arg(name).toStdString());
        d.lines.push_back(tr("Esc or the camera menu to eject").toStdString());
    }
    // The shadow-atlas inspector: engine-drawn, on the same HUD, and off in
    // every offscreen view (it never sets allowOffscreen) so screenshots and
    // pixel suites stay clean.
    d.shadowAtlas = mShowShadowAtlas;
    if (mShowStats) {
        d.stats = true;
        d.corner = jahshaka::engine::OverlayCorner::TopLeft;
        if (!mStatsClock.isValid() || mStatsClock.elapsed() >= kStatsRefreshMs) {
            mStatsClock.restart();
            mStatsLines.clear();
            jahshaka::engine::RenderStats rs;
            const bool haveRs = mEngine && mEngine->renderStats(rs);
            const EngineRenderDriver::Stats ds =
                mDriver ? mDriver->stats() : EngineRenderDriver::Stats{};
            // THE ROWS ARE COMPOSED BY A PURE FUNCTION (viewport/statsrows.h),
            // so the WORDING — which is the thing the owner's review was
            // actually about — has a unit test instead of a screenshot.
            //
            // The scene's own triangles come from the DOCUMENT, walked here
            // (services/scenestats.h): never from subtracting helpers out of
            // the GPU figure beside it. The two numbers are different
            // measurements of different things and both are labelled.
            statsrows::Input in;
            in.drawing = ds.drawing;
            in.fpsDrawn = ds.fpsDrawn;
            in.workMs = ds.workMs;
            in.sceneTriangles = scenestats::sceneGeometry(mScene).triangles;
            in.submittedTriangles = haveRs ? quint64(rs.triangles) : 0;
            in.draws = haveRs ? quint64(rs.draws) : 0;
            in.slowFramesLastMinute = ds.slowFramesLastMinute;
            mStatsLines = statsrows::compose(in);
        }
        for (const QString &line : mStatsLines) d.lines.push_back(line.toStdString());
    }

    // A world is on its way. Asked for explicitly by beginSceneLoad rather than
    // derived, because at that moment the OLD world is still bound (or none is)
    // and the state machine would answer "presenting" or "noscene".
    if (mSceneLoadPending) {
        d.cover = Cover::Loading;
        d.coverTitle = tr("Loading world…").toStdString();
        d.coverSubtitle = mLoadingTitle.toStdString();
        return d;
    }
    const QString state = presentationState();
    if (state == QLatin1String("loading")) {
        d.cover = Cover::Loading;
        d.coverTitle = tr("Loading world…").toStdString();
        d.coverSubtitle = mLoadingTitle.toStdString();
    } else if (state == QLatin1String("noscene")) {
        d.cover = Cover::NoScene;
        d.coverTitle = tr("No world open").toStdString();
        d.coverSubtitle = tr("Open or create a world from the Desktop").toStdString();
    }
    // "presenting" and "offscreen" both mean no cover: the engine owns these
    // pixels, or it never will and a cover drawn by it could not appear anyway.
    // The FAILED state has no cover at all any more — by definition nothing
    // will ever present into a widget whose View could not be created, so an
    // engine-drawn message there is a contradiction. It is respecced onto the
    // toast + return-to-Desktop path in MainWindow (STATS_OVERLAY_SPEC §6.4),
    // driven off viewCreationError().
    return d;
}

void EngineSceneViewport::refreshOverlay()
{
    if (!view()) return;
    // Presenting again: the load this viewport was told about is over.
    if (mSceneLoadPending && mScene && presentsSinceBind() >= kPresentsBeforeReveal)
        mSceneLoadPending = false;
    const jahshaka::engine::ViewOverlayDesc desc = overlayDesc();
    const bool wasUp = mCoverUp;
    mCoverUp = desc.cover != jahshaka::engine::ViewOverlayDesc::Cover::None;
    view()->setOverlay(desc);

    // A COVER THAT HAS JUST GONE UP MUST BE PRESENTED TO EXIST — and this is
    // THE structural difference between the deleted Qt cover and this one.
    //
    // ViewportCover was a widget: one repaint put its pixels in the backing
    // store and they stayed there, for free, however long the UI thread was
    // then blocked. An engine-drawn cover is only ever the contents of a frame,
    // so every raise needs a frame, and the moments a cover is raised are
    // exactly the moments the thread is about to stop drawing them — binding a
    // new scene (MainWindow::newProject builds and saves one inline), closing a
    // world, a page switch on the way into a load.
    //
    // Measured, not assumed: without this the editor page sat on its own
    // watermark for ~1 s of a project create where the Qt cover had been up,
    // and the A/B against the unmodified base is what found it.
    //
    // Only on the RISING edge, and never re-entrantly (presentCovered calls
    // this first). Lowering needs nothing: the next ordinary frame draws the
    // world, and there is always one — the cover only comes down BECAUSE
    // frames are being presented.
    if (mCoverUp && !wasUp && !mPresentingCover) presentCovered();
}

void EngineSceneViewport::presentCovered(int frames)
{
    // Nothing to present into: an offscreen fallback view owns no pixels of
    // this widget, and no view at all owns nothing.
    if (!view() || view()->isOffscreen() || !mEngine) return;
    // AND NOTHING TO PRESENT FOR: a hidden page has no pixels on screen, so a
    // frame drawn for it is pure UI-THREAD COST — the whole scene is still
    // rendered under the cover (the overlay pass composites onto a finished
    // frame; it does not save drawing what it hides). This is the deleted
    // widget's own contract, in its own words: showNow "does nothing when the
    // cover is not visible on screen".
    //
    // MEASURED, and the reason this line is not merely tidy: without it the
    // threaded open's beginSceneLoad drew two full frames of a world nobody
    // could see, on the thread the open is trying not to block, and
    // open.responsive's warm-open UI gap went from under its 500 ms budget to
    // 1346 ms. The covered frames that matter are the ones after the page
    // switch, and switchSpace(EDITOR) -> coverIfNotPresenting draws those.
    if (!isVisible()) return;
    // OUR PIXELS ARE ALREADY ON SCREEN. The reveal path calls this THREE times
    // inside one blocking stretch — showEvent, refreshOverlay's rising edge,
    // and switchSpace's coverIfNotPresenting — and the second and third have
    // nothing to add: same cover, same size, and nothing has presented in
    // between, so what is on screen is exactly what they would draw.
    //
    // That matters because a covered frame IS NOT CHEAP: the overlay pass
    // composites onto a FINISHED frame, so the whole scene is still rendered
    // under a cover that hides it (STATS_OVERLAY_SPEC §7.6 says so explicitly).
    // MEASURED: the duplicates put open.responsive's warm-open UI gap at
    // ~700 ms against a 500 ms budget; deduplicated it sits at ~290 ms, which
    // is the unmodified base's own number.
    //
    // The EPOCH is the load-bearing half of the key. Desc and size alone are
    // not enough: closing a world and reopening the same one produces an
    // identical desc, and skipping there left the cover down for the whole
    // reopen (caught by the xwd capture, not by reasoning). And the epoch has
    // to be OURS — View::framesPresented resets to 0 on every scene bind, so it
    // collides across exactly the close/reopen this key exists to distinguish.
    const jahshaka::engine::ViewOverlayDesc wanted = overlayDesc();
    if (wanted == mLastPresentedCover && view()->width() == mLastPresentedW &&
        view()->height() == mLastPresentedH &&
        mFrameEpoch == mLastPresentedEpoch)
        return;
    if (mPresentingCover) return;          // refreshOverlay's rising edge, already in hand
    mPresentingCover = true;
    refreshOverlay();
    const bool covered = view()->overlay().cover !=
                         jahshaka::engine::ViewOverlayDesc::Cover::None;
    // The View is enabled by widget VISIBILITY (EngineViewWidget::show/hideEvent)
    // and the driver skips a tick with nothing enabled — so on the path this
    // exists for (a page switch, a synchronous open) the view may still be
    // disabled. Enable it for exactly these frames and put it back.
    const bool wasEnabled = view()->isEnabled();
    view()->setEnabled(true);
    for (int i = 0; i < frames; ++i) {
        ++mFrameEpoch;
        mEngine->renderOneFrame();
        devicelossend::checkAfterFrame(mEngine.get());
        // These are frames on the display too (DOUBLE-FRAME-1): the pacing
        // clock counts every frame, not only the driver's own.
        if (mDriver) mDriver->noteExternalFrame();
    }
    view()->setEnabled(wasEnabled);
    // A COVERED frame IS NOT A FRAME OF THE WORLD, and presentsSinceBind means
    // exactly that: "how many frames of the world bound right now are on
    // screen". These frames show the cover, so they are rebased away.
    //
    // This is not bookkeeping pedantry — it is the contract
    // scripting.e2e.presentation_state asserts and the reason the cover comes
    // down at the right moment: without the rebase, the two frames this method
    // draws to PUT THE COVER UP would immediately satisfy kPresentsBeforeReveal
    // and take it straight back down over a world that has not drawn yet. (The
    // driver's own ticks while the cover is up DO count, exactly as they always
    // did — that is what eventually reveals the viewport.)
    if (covered) mPresentBaseline = qulonglong(view()->framesPresented());
    mLastPresentedCover = wanted;
    mLastPresentedW = view()->width();
    mLastPresentedH = view()->height();
    mLastPresentedEpoch = mFrameEpoch;
    mPresentingCover = false;
}

void EngineSceneViewport::beginSceneLoad(const QString &title)
{
    // The world on screen (if any) is about to be replaced: nothing presented
    // from here on belongs to the old one.
    mPresentBaseline = view() ? qulonglong(view()->framesPresented()) : 0;
    mSceneLoadPending = true;
    mLoadingTitle = title;
    // Loading, not the computed state: the caller is telling us a world is on
    // its way, and it is about to block this thread reading it. The Qt cover
    // called repaint() here for exactly that reason — a posted paint would
    // arrive after the load it exists to cover. The engine-drawn cover's
    // equivalent is to draw its frames inline, now, before we return.
    presentCovered();
}

void EngineSceneViewport::primeSceneGeometry()
{
    // The SLOW half of "opening a world" is not reading the document — it is
    // pushing it into the engine: every mesh, material and texture uploads on
    // the first SceneMirror::sync(). That used to happen on the first driver
    // tick AFTER the page switch, which is most of the time the viewport spent
    // with nothing of its own on screen. Doing it here pays for it while the
    // desktop page (and its progress dialog) is still what the user is looking
    // at, exactly as MainWindow::openProject's ordering intends.
    //
    // This is the same push the frame loop makes, from the same place between
    // frames — never inside one. It renders nothing: the view is disabled while
    // the editor page is hidden, and this deliberately does not enable it.
    // It is also entirely optional: on the very first open no View exists yet
    // (it is created when the page is first shown), so this returns and the
    // loading cover carries the wait instead.
    if (!mScene || !view() || !ensureEngineScene()) return;
    if (!mMirror) return;
    const bool helpers = !mGameView;
    // ...and this second, smaller push states it too (PLAYER-FLOOR-1): it is a
    // sync of the editor's own picture, and the Player may have left the hide
    // on behind it.
    mMirror->setHideDefaultFloor(false);
    mMirror->setLightWires(mShowLightWires && helpers);
    mMirror->setHighlightWireframe(mSelectionWireframe);
    pushGridForView(helpers);
    mMirror->setGiVolumeOverlay(mShowGiVolume && helpers);
    LoadTimeline::Accumulate mirror(QStringLiteral("engine:mirrorSync"));
    mMirror->sync();
}

void EngineSceneViewport::primeSceneEnvironment()
{
    // The other half (see primeSceneGeometry): sky, world settings, camera.
    // A separate event-loop turn in the threaded open.
    if (!mScene || !view() || !mMirror || !mEngineScene) return;
    {
        LoadTimeline::Accumulate sky(QStringLiteral("engine:applySky"));
        mMirror->applySky(view());
    }
    {
        // The world settings — and, at Epic, the VCT voxelize + light
        // injection, the shadow atlas resize and the post chain's shader
        // variants.
        LoadTimeline::Accumulate env(QStringLiteral("engine:applyEnvironment"));
        mMirror->applyEnvironment(view(), mEngine.get());
    }
    if (viewCamera()) mMirror->applyCamera(viewCamera(), view(), freeCameraFramingAspect());
}

unsigned EngineSceneViewport::warmUpShaders()
{
    // The third prime (SHADER_CACHE_SPEC §5). Runs on the open path, after the
    // geometry and environment pushes and BEFORE the page is revealed — the
    // loading cover is up, so the frame the engine renders to build its shaders
    // is invisible.
    if (!mEngine || !view() || !mEngineScene) return 0;

    unsigned before = 0, cached = 0, expected = 0;
    mEngine->shaderBuildProgress(before, cached, expected);

    // THE BUDGET, and it is measured, not guessed (open.responsive, Showroom,
    // this box):
    //
    //   no warm-up          cold open worst UI gap 1691-1789 ms, warm 439-476
    //   warm-up every open  cold 1723-1761 ms,                    warm 646-717
    //
    // The cold open is unchanged — the compiles happened either way, and the
    // gap there is dominated by other stages. What the second open pays is
    // ~250 ms for a frame that compiles NOTHING: the Hlms shader cache is
    // PROCESS-wide, so once this process has built a world's shaders, opening
    // another world mostly finds them already there. 250 ms of UI block for
    // nothing is exactly what the 500 ms responsiveness budget exists to catch,
    // and widening that budget to accommodate a no-op would be the wrong trade.
    //
    // So: warm up while it is still paying. If a warm-up compiles nothing, note
    // the compile count and skip the next one — until something compiles for
    // any OTHER reason (new content in a later world), which moves the count
    // and re-arms this. Self-correcting in both directions, and no heuristic
    // about what a world contains.
    if (mWarmUpIdleAt == before) return 0;

    {
        LoadTimeline::Accumulate warm(QStringLiteral("engine:warmUpShaders"));
        view()->warmUpShaders();
    }
    unsigned after = 0;
    mEngine->shaderBuildProgress(after, cached, expected);
    mWarmUpIdleAt = (after == before) ? before : kWarmUpAlwaysRun;
    return after - before;
}

void EngineSceneViewport::recordWarmUpSet()
{
    // WHY THIS RUNS WHERE IT RUNS (audit F1a + spec §7.9).
    //
    // The audit asked for a recording "after the first rendered frame of an
    // open". The mechanism says a frame is not what it needs: Ogre's
    // VertexFormatWarmUpStorage::analyze walks the scene's object memory
    // managers and reads each renderable's VAO declaration and Hlms hash
    // (OgreVertexFormatWarmUp.cpp:112-156), and the Hlms hash is assigned when
    // the DATABLOCK is bound, not when anything is drawn. So everything the
    // recording wants exists the moment SceneMirror has pushed the world —
    // which is on the open path, BEHIND the cover, where the spec's own
    // interaction note wants this work to live rather than after the reveal.
    //
    // Recording per world (rather than only at quit) is the whole fix: Ogre's
    // storage ACCUMULATES and de-duplicates by {Hlms hash, render queue}, so
    // recording every world as it goes IS the merged set — and a world that was
    // closed before the app quit used to be in no set at all.
    if (!mEngine || !mEngineScene) return;
    if (View *v = view()) {
        // Remember the shape THIS pass has, for the next launch's warm-up view
        // to match. sampleCount() is the ACHIEVED count (the driver may clamp
        // below what the scene asked for) — matching what was requested would
        // build variants this machine cannot render.
        EngineHost::rememberWarmUpShape({ v->sampleCount(), v->shadows() });
    }
    EngineHost::instance().recordWarmUpSetNow();
}

void EngineSceneViewport::coverIfNotPresenting()
{
    // Called right after the editor page is switched to, i.e. right after this
    // widget's native window was MAPPED. Until the engine presents into it the
    // X server shows whatever was on that part of the screen before — the page
    // we just left. So: cover, and present it inline rather than waiting for
    // the next 16 ms driver tick.
    //
    // Already presenting this world? presentCovered's refreshOverlay resolves
    // to Cover::None and the two frames are just two ordinary frames.
    presentCovered();
}

void EngineSceneViewport::cleanup()
{
    mActive = false;
    clearScene();
    destroyView();
}

void EngineSceneViewport::clearScene()
{
    // Project-swap teardown: destroy the scene-scoped objects (overlay, mirror,
    // engine scene) but keep the View — its native window and swapchain stay
    // valid across project close/open. Script sessions never leave the editor
    // page, so no showEvent would ever recreate a destroyed view (the
    // "engine viewport is not available after project.open" defect).
    // mActive is deliberately NOT cleared here: syncFrame's members are all
    // null-guarded, and clearing it would leave the viewport silently frozen
    // if no space switch follows (mActive belongs to begin()/end()).
    //
    // THE HEADSET COMES OFF FIRST (VR-4-FIX finding 1). A VR session renders
    // the engine scene this function is about to destroy and holds a raw
    // pointer to it — every frame, for its stereo quads, and again in its own
    // destructor — so a preview still running here was a use-after-free one
    // frame later. Its owner ends it properly (and takes its fly keys back off
    // this viewport, which is why the call is here and not in the engine
    // alone). COPIED BEFORE IT IS CALLED: ending the session clears this very
    // std::function, and a closure must not be destroyed while it runs.
    if (const std::function<void()> ends = mVrPreviewEnds) ends();
    // WRITE THE WORLD DOWN BEFORE IT GOES (audit F1a). This is the scene-close
    // half of the recording, and it is the half that was missing entirely:
    // recordWarmUpSet had exactly two callers, EngineHost::shutdown and the
    // verb, so a world the user opened, worked in and CLOSED contributed
    // nothing to the next launch's warm-up. The engine scene is still alive at
    // this point in the teardown, which is the only reason this line can be
    // here and not three lines down.
    recordWarmUpSet();
    if (mOverlay) { mOverlay->clear(); mOverlay.reset(); }
    if (mMirror) { mMirror->setSource(nullptr); mMirror.reset(); }
    if (view()) {
        view()->setScene(nullptr);
        // The next project may not drive the background (only SINGLE_COLOR
        // skies do) — reset to the editor grey the view was created with.
        view()->setBackground(Colour(0.10f, 0.11f, 0.14f));
    }
    if (mEngineScene && mEngine) { mEngine->destroyScene(mEngineScene); mEngineScene = nullptr; }
    mScene.clear();
    mSelectedNode.clear();
    // No world bound: the cover says so (and the next bind starts from here).
    // Whatever load was in flight is over — a close is not a load, and leaving
    // the flag set would caption the NoScene cover "Loading world…" for ever.
    mSceneLoadPending = false;
    mLoadingTitle.clear();
    mPresentBaseline = view() ? qulonglong(view()->framesPresented()) : 0;
    refreshOverlay();
}

IEditorViewport *createEngineSceneViewport(const std::shared_ptr<Engine> &engine,
                                           EngineRenderDriver *driver, QWidget *parent)
{
    return new EngineSceneViewport(engine, driver, parent);
}

void EngineSceneViewport::setServices(StudioServices *services)
{
    // The gizmos raise undo pushes / refreshes through the aggregate; the
    // viewport itself needs it for Alt+drag duplicate and End snap-to-floor.
    mServices = services;
    if (mTranslateGizmo) mTranslateGizmo->setServices(services);
    if (mRotateGizmo)    mRotateGizmo->setServices(services);
    if (mScaleGizmo)     mScaleGizmo->setServices(services);
}

// End / editor.snapToFloor(): drop the selection straight down onto the first
// scene surface BELOW its bounds; no hit means the y=0 ground plane (the
// dropPositionAt convention). Undoable (EDITOR_SHORTCUTS_SPEC §4).
// End: every MEMBER drops onto the surface under its OWN bounds, as one undo
// step (EDITOR_MULTISELECT_SPEC §2.3). Per member and not "the set as one
// rigid body": two objects on a staircase have to land on their own steps.
bool EngineSceneViewport::snapSelectionToFloor()
{
    // The edit gate (ledger §423), asked ONCE for the whole gesture: the
    // multi-node branch below opens a macro on the stack before the first
    // node, and an all-refused gesture would leave an empty entry behind. Keyed
    // on the calling context, so the End key is refused while a script runs and
    // editor.snapToFloor, arriving inside a verb, is not.
    if (editgate::refuse()) return false;
    if (mSelectedSet.size() > 1) {
        const auto targets = SceneEditService::effectiveSet(mSelectedSet);
        if (targets.isEmpty()) return false;
        QUndoStack *stack = (mServices && mServices->undo) ? mServices->undo->stack() : nullptr;
        if (stack) stack->beginMacro(tr("Snap %1 objects to floor").arg(targets.size()));
        bool any = false;
        for (const auto &node : targets) any = snapNodeToFloor(node) || any;
        if (stack) stack->endMacro();
        return any;
    }
    return snapNodeToFloor(mSelectedNode);
}

bool EngineSceneViewport::snapNodeToFloor(const iris::SceneNodePtr &node)
{
    if (!node || !mScene) return false;
    // The gate again, for the single-member entry point (the question, not the
    // notice — snapSelectionToFloor above already raised it for the gesture).
    if (editgate::blocked()) return false;
    node->update(0.0f);

    const iris::AABB bounds = preview::worldBoundingBox(node);
    const bool hasBounds = bounds.getMin().x() <= bounds.getMax().x();
    const iris::Vec3 pos = node->getGlobalPosition();
    const float bottom = hasBounds ? bounds.getMin().y() : pos.y();
    const iris::Vec3 centre = hasBounds ? bounds.getCenter() : pos;

    // Straight down from just under the selection's own bounds, so its own
    // meshes can never be the hit.
    const iris::Vec3 start(centre.x(), bottom - 0.001f, centre.z());
    const iris::Vec3 end = start + iris::Vec3(0.0f, -10000.0f, 0.0f);
    // Meshes only: this looks for the SURFACE under the selection (End = drop to
    // floor), and a light or camera origin sphere is not a floor.
    const auto hits = ScenePicker::pickAll(mScene, start, end, start, true, false,
                                           false, true, false);
    float targetY = 0.0f;                       // fallback: the y = 0 plane
    bool found = false;
    for (const auto &h : hits) {
        if (!h.node) continue;
        bool own = false;                       // ignore the selection's own subtree
        for (auto n = h.node; n; n = n->getParent())
            if (n.data() == node.data()) { own = true; break; }
        if (own) continue;
        if (!found || h.hitPoint.y() > targetY) { targetY = h.hitPoint.y(); found = true; }
    }

    const float delta = targetY - bottom;
    if (std::abs(delta) < 1e-5f) return true;   // already on the floor

    const iris::Vec3 oldLocalPos = node->getLocalPos();
    const iris::Quat rot = node->getLocalRot();
    const iris::Vec3 scale = node->getLocalScale();
    node->setGlobalPos(pos + iris::Vec3(0.0f, delta, 0.0f));
    const iris::Vec3 newLocalPos = node->getLocalPos();
    if (mServices && mServices->undo) {
        mServices->undo->push(new TransformSceneNodeCommand(node,
                                                            oldLocalPos, rot, scale,
                                                            newLocalPos, rot, scale));
    }
    return true;
}
