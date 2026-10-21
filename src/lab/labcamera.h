// jahlab — THE FLY CAMERA (LAB-1 stage 1): a document CameraNode the lab moves and hands the mirror each frame (the
// editor's free camera is not in the scene tree either). WASD in the view plane, Q/E down/up, Shift x4, the mouse
// look while the right button is held; the speed is the scene's extent over ten seconds of frames (1/60 s each —
// frames, never wall time).
#pragma once

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/vec.h"

#include <set>
#include <string>

namespace lab {

class LabCamera
{
public:
    LabCamera();
    const iris::CameraNodePtr &node() const { return mNode; }

    /// Places the camera at `pos` looking at `target`.
    void set(const iris::Vec3 &pos, const iris::Vec3 &target);
    /// The scene's extent (its bounding box's diagonal, metres) — what the fly speed is scaled to.
    void setExtent(float metres) { mExtent = metres > 0.01f ? metres : 1.0f; }
    void setAspect(float aspect);
    /// One frame of input: held keys and the mouse delta (pixels). True when the camera moved.
    bool fly(const std::set<std::string> &held, int dx, int dy);

    iris::Vec3 position() const { return mPos; }
    /// The point the camera looks at: the last target set, carried along the view by the fly.
    iris::Vec3 target() const;

private:
    void apply();
    iris::CameraNodePtr mNode;
    iris::Vec3 mPos;
    float mYaw = 0.0f, mPitch = 0.0f;   ///< radians; yaw 0 looks down -Z
    float mTargetDistance = 10.0f;
    float mExtent = 10.0f;
};

}   // namespace lab
