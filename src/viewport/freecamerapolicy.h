/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef FREECAMERAPOLICY_H
#define FREECAMERAPOLICY_H

// What the app's two FREE cameras — the editor explorer and the player's fly
// camera — do that an AUTHORED scene camera must not.
//
// THE WIDE-ASPECT FRAMING HOLD (owner report 2026-09-07; RE-SCOPED after the
// owner-blocking regression of 2026-09-08 — the history is the policy, so it is
// written down here in full).
//
// The document stores a VERTICAL angle of view (iris::CameraNode::angle, 45 by
// default) and the engine hands it straight to Ogre's setFOVy, so the
// HORIZONTAL angle is whatever the window's aspect makes of it:
//
// (measured, cameras.document's framing-hold sweep, at the default 45-degree lens)
//
//      4:3    -> 57.8 degrees wide     16:9  ->  72.7 degrees wide
//      21:9   -> 88.1 degrees wide     32:9  -> 111.7 degrees wide
//      5:1    -> 128.5 degrees wide
//
// Past ~110 degrees that is a fisheye — straight lines bow, objects at the
// edges stretch, and moving forward feels like falling. It is also exactly the
// monitor someone buys to see MORE of a scene, so the naive behaviour punishes
// the hardware it should reward.
//
// THE POLICY: HOLD THE 16:9 FRAMING BEYOND 16:9. A free camera renders EXACTLY
// its authored vertical angle on every window up to and including 16:9 — 4:3,
// 3:2, 16:10 and 16:9 are the identity, bit for bit, no arithmetic runs at all.
// Only on a window WIDER than 16:9 does the vertical angle narrow, and only by
// enough to keep the horizontal extent the shot has at 16:9
// (iris::lens::verticalFovDegForFramingAspect document-side,
// jahshaka::engine::verticalFovForFramingAspect engine-side; the two agree to
// better than a ten-thousandth of a degree, which tests/picking's fov_cap
// suite pins). So the ultra-wide monitor gets the SAME picture as a 16:9 one, in a
// wider frame, with more of the world at the sides than a 16:9 window shows and
// none of the fisheye.
//
// WHY NOT A FIXED HORIZONTAL DEGREE CAP (what shipped 2026-09-07 and broke).
// The first version capped the horizontal angle at 95 degrees for any free
// camera. A 45-degree lens is 72.7 wide at 16:9, comfortably inside it — but
// the cap bites as a function of the LENS, and a 75-degree lens crosses 95
// horizontal at 1.42:1. The shipped Grand Showroom stores a 75-degree editor
// camera, so on every monitor wider than 3:2 it rendered at 63 degrees vertical
// instead of 75: the whole scene zoomed in by 1.25x, which the owner read as
// "imported assets and avatars have the wrong scale". A degree cap cannot
// express "leave ordinary windows alone", because what counts as ordinary
// depends on the camera's own angle. An aspect can, for every lens at once.
//
// AUTHORED CAMERAS ARE NEVER HELD. A scene camera's angle is a lens choice; the
// engine defaults CameraDesc::framingAspect to 0 (off) and only a host pushing
// its OWN free camera passes this value.

namespace freecam {

/// The aspect a free camera's framing is held at: at or below it the authored
/// angle is rendered exactly, above it the vertical angle narrows to hold the
/// horizontal extent this aspect gives. 16:9 — the shape virtually every scene
/// is composed and every sample authored on.
constexpr float kFreeCameraFramingAspect = 16.0f / 9.0f;

}   // namespace freecam

#endif // FREECAMERAPOLICY_H
