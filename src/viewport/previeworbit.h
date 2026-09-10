#ifndef PREVIEWORBIT_H
#define PREVIEWORBIT_H

// The arcball, in one place.
//
// There were FOUR implementations of "rotation from (pitch, yaw, 0), position
// at pivot + forward * distance" in this tree: OrbitalCameraController (the
// maintained one, but welded to the editor — it needs an IEditorViewport for
// the gizmo guard and a SettingsManager for the mouse-mode guard) and one
// hand-copy each in EngineAssetScene, EngineMaterialPreviewScene and
// AvatarPreviewScene, all three carrying the comment "the same orbit maths as
// <the other one>". ENGINEERING_DEBT_SPEC item 6.
//
// The split this header makes is the honest one:
//
//   `orbitmath` — the POSE. Three pure functions with no state and no policy,
//   used by all four call sites, including the editor controller. This is the
//   maths that was genuinely identical everywhere.
//
//   `PreviewOrbit` — the preview STATE MACHINE: yaw/pitch plus their lerp
//   targets, the pivot, the orbit distance, which mouse buttons are down. Every
//   preview surface drives its camera through exactly this, so a change to
//   drag/orbit/lerp behaviour lands in all of them at once.
//
// What deliberately did NOT move here: the ZOOM policy (the asset viewer stops
// at 0, the material dock at 0.5, the avatar page scales the step by the
// subject's radius and re-derives clip planes) and the PAN speed (the avatar
// page scales that by radius too). Those differ per preview on purpose, so
// they stay at their call sites where the difference is visible.
//
// Nor does the editor's OrbitalCameraController become a PreviewOrbit: it lerps
// only while a navigation is pending (adopting a camera must never rewrite the
// node — see cameracontrollerbase.h), whereas a preview writes its camera on
// every frame because nothing else does. Same maths, different contract.

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include <Qt>

namespace orbitmath {

inline float lerp(float a, float b, float t) { return a * (1 - t) + b * t; }

/// The arcball pose: look direction from (pitch, yaw, 0), camera parked one
/// orbit radius behind the pivot along it.
inline void applyPose(const iris::CameraNodePtr &camera, const iris::Vec3 &pivot,
                      float pitch, float yaw, float distFromPivot)
{
    if (!camera) return;
    const auto rot = iris::Quat::fromEulerAngles(pitch, yaw, 0);
    const auto localPos = rot.rotatedVector(iris::Vec3(0, 0, 1));
    camera->setLocalPos(pivot + localPos * distFromPivot);
    camera->setLocalRot(rot);
    camera->update(0);
}

/// ADOPT a camera: derive the pivot (one orbit radius ahead of it) and the
/// yaw/pitch the arcball steers with. Reads only — see cameracontrollerbase.h
/// on why adoption must never write the node.
inline void decompose(const iris::CameraNodePtr &camera, float distFromPivot,
                      iris::Vec3 &pivotOut, float &pitchOut, float &yawOut)
{
    if (!camera) return;
    const auto viewVec = camera->getLocalRot().rotatedVector(iris::Vec3(0, 0, -1));  // forward is -z
    pivotOut = camera->getLocalPos() + viewVec * distFromPivot;
    float roll;
    camera->getLocalRot().getEulerAngles(&pitchOut, &yawOut, &roll);
}

/// Screen-space drag of the pivot, in the camera's own axes.
inline iris::Vec3 panDelta(const iris::CameraNodePtr &camera, int dx, int dy, float speed)
{
    if (!camera) return iris::Vec3(0, 0, 0);
    return camera->getLocalRot().rotatedVector(iris::Vec3(dx * speed, -dy * speed, 0));
}

}   // namespace orbitmath

/// The orbit a preview surface steers its camera with (see the header note).
class PreviewOrbit
{
public:
    void mouseDown(Qt::MouseButton b)
    {
        if (b == Qt::LeftButton)   left = true;
        if (b == Qt::RightButton)  right = true;
        if (b == Qt::MiddleButton) middle = true;
    }
    void mouseUp(Qt::MouseButton b)
    {
        if (b == Qt::LeftButton)   left = false;
        if (b == Qt::RightButton)  right = false;
        if (b == Qt::MiddleButton) middle = false;
    }

    /// One mouse-move: left or right drag orbits, middle drag pans the pivot.
    /// The caller applies the result (apply()) — unconditionally, exactly as
    /// all three previews did, so a move with no button down still settles the
    /// camera on the current orbit.
    void drag(const iris::CameraNodePtr &camera, int dx, int dy, float panSpeed)
    {
        if (left || right) orbit(dx * rotationSpeed, dy * rotationSpeed);
        if (middle) pivot += orbitmath::panDelta(camera, dx, dy, panSpeed);
    }

    /// Turn by whole angles. Any lerp in flight is landed first, so a drag
    /// never fights the animation.
    void orbit(float yawDegrees, float pitchDegrees)
    {
        yaw = targetYaw + yawDegrees;
        pitch = targetPitch + pitchDegrees;
        targetYaw = yaw;
        targetPitch = pitch;
    }

    /// One frame of the approach to the targets (the previews' step()).
    void advance()
    {
        yaw = orbitmath::lerp(yaw, targetYaw, 0.8f);
        pitch = orbitmath::lerp(pitch, targetPitch, 0.8f);
    }

    void apply(const iris::CameraNodePtr &camera) const
    {
        orbitmath::applyPose(camera, pivot, pitch, yaw, distFromPivot);
    }

    /// Take yaw/pitch/pivot from where the camera currently stands.
    void adopt(const iris::CameraNodePtr &camera)
    {
        orbitmath::decompose(camera, distFromPivot, pivot, pitch, yaw);
        targetYaw = yaw;
        targetPitch = pitch;
    }

    /// Point at an angle with no animation (a fresh framing).
    void set(float yawDegrees, float pitchDegrees)
    {
        yaw = targetYaw = yawDegrees;
        pitch = targetPitch = pitchDegrees;
    }

    float yaw = 0, pitch = 0, targetYaw = 0, targetPitch = 0;
    float rotationSpeed = 0.5f;
    iris::Vec3 pivot;
    float distFromPivot = 5.0f;
    bool left = false, right = false, middle = false;
};

#endif   // PREVIEWORBIT_H
