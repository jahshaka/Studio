#include "labcamera.h"

#include "irisgl/document/scenegraph/cameranode.h"

#include <algorithm>
#include <cmath>

namespace lab {

namespace {
/// THE FREE-CAMERA FRAMING (the editor's freecam::kFreeCameraFramingAspect, src/viewport/freecamerapolicy.h — a
/// number, not a dependency): at or below 16:9 the authored vertical angle renders as authored, wider holds the 16:9
/// horizontal extent. The lab's camera is a free camera exactly like the editor's.
constexpr float kFreeCameraFramingAspect = 16.0f / 9.0f;
constexpr float kLookRadiansPerPixel = 0.0035f;
}   // namespace

LabCamera::LabCamera() : mNode(iris::CameraNode::create())
{
    mNode->setFramingAspect(kFreeCameraFramingAspect);
    set(iris::Vec3(0, 5, 14), iris::Vec3(0, 0, 0));   // the editor's default camera
}

void LabCamera::set(const iris::Vec3 &pos, const iris::Vec3 &target)
{
    mPos = pos;
    iris::Vec3 d = target - pos;
    const float len = d.length();
    if (len > 1e-5f) {
        d = d / len;
        mTargetDistance = len;
        mYaw = std::atan2(-d.x(), -d.z());
        mPitch = std::asin(std::max(-1.0f, std::min(1.0f, d.y())));
    }
    apply();
}

void LabCamera::setAspect(float aspect)
{
    if (aspect > 0.0f) mNode->setAspectRatio(aspect);
    mNode->update(0.0f);
}

iris::Vec3 LabCamera::target() const
{
    const iris::Vec3 f(-std::sin(mYaw) * std::cos(mPitch), std::sin(mPitch), -std::cos(mYaw) * std::cos(mPitch));
    return mPos + f * mTargetDistance;
}

void LabCamera::apply()
{
    mNode->setLocalPos(mPos);
    mNode->lookAt(target());
    mNode->update(0.0f);
}

bool LabCamera::fly(const std::set<std::string> &held, int dx, int dy)
{
    bool moved = false;
    if (dx || dy) {
        mYaw -= float(dx) * kLookRadiansPerPixel;
        mPitch = std::max(-1.55f, std::min(1.55f, mPitch - float(dy) * kLookRadiansPerPixel));
        moved = true;
    }
    const iris::Vec3 f(-std::sin(mYaw) * std::cos(mPitch), std::sin(mPitch), -std::cos(mYaw) * std::cos(mPitch));
    const iris::Vec3 right = iris::Vec3::crossProduct(f, iris::Vec3(0, 1, 0)).normalized();
    iris::Vec3 v(0, 0, 0);
    if (held.count("w")) v += f;
    if (held.count("s")) v -= f;
    if (held.count("d")) v += right;
    if (held.count("a")) v -= right;
    if (held.count("e")) v += iris::Vec3(0, 1, 0);
    if (held.count("q")) v -= iris::Vec3(0, 1, 0);
    if (v.length() > 1e-5f) {
        // the scene's extent in ten seconds of 1/60 s frames; Shift x4
        float step = mExtent / 600.0f;
        if (held.count("Shift_L") || held.count("Shift_R")) step *= 4.0f;
        mPos += v.normalized() * step;
        moved = true;
    }
    if (moved) apply();
    return moved;
}

}   // namespace lab
