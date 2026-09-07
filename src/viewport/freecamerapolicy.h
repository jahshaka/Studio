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
// THE WIDE-ASPECT FOV CAP (owner report 2026-09-07). The document stores a
// VERTICAL angle of view (iris::CameraNode::angle, 45 by default) and the
// engine hands it straight to Ogre's setFOVy, so the HORIZONTAL angle is
// whatever the window's aspect makes of it:
//
// (measured, cameras.document's fov_clamp sweep, at the default 45-degree lens)
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
// The policy: hold the horizontal angle at kFreeCameraMaxHorizontalFovDegrees
// once the aspect would take it past that, and let the vertical angle narrow
// instead (jahshaka::engine::verticalFovForHorizontalCap does the arithmetic).
// 95 degrees is chosen so EVERY ORDINARY ASPECT IS UNTOUCHED — 16:9 is 72.7
// degrees wide and even 21:9 is only 88, both comfortably inside it, so the
// common case is bit-for-bit what it always was and every existing 16:9 pixel
// assertion still holds. The cap first bites at about 2.65:1, which is where
// the super-ultra-wide panels start; it is also roughly where game cameras
// top out.
//
// AUTHORED CAMERAS ARE NEVER CAPPED. A scene camera's angle is a lens choice;
// the engine defaults CameraDesc::maxHorizontalFovDegrees to 0 (off) and only a
// host pushing its OWN free camera passes this value.

namespace freecam {

/// The horizontal angle of view, in degrees, that a free camera will not exceed.
constexpr float kFreeCameraMaxHorizontalFovDegrees = 95.0f;

}   // namespace freecam

#endif // FREECAMERAPOLICY_H
