// jahlab — THE STILLNESS DOOR'S MATERIAL CLASSES (PHOTON-2 stage 0): an environment's objects classified by what they
// are made of (the spec's glossy < 0.4, rough, foliage / alpha — and the mirror and the declared mover apart), and the
// class of every pixel of a view at a pose: the objects' boxes ray-cast from the camera. The fixtures are boxes and
// quads, so the nearest box a pixel's ray enters IS the object it shows. Its own TU at -O2 in Debug (two million rays
// a picture, integer-exact work no optimisation level changes).
#pragma once

#include "envs/env.h"

#include <string>
#include <vector>

namespace lab {

/// The classes, in the door's column order: plaster, rough, glossy, mirror, foliage, mover.
const std::vector<std::string> &stillnessClassNames();
/// An object's class, 1-based in stillnessClassNames' order: the mover first, alpha-tested (cut-out) = foliage, a metal under
/// roughness 0.05 = the mirror, under 0.4 = glossy (the ray tier's cut), under 0.8 = rough (the band between the
/// reflection's cut and Lumen's diffuse fade, where the rough specular is its own term - PHOTON-7 stage 7's), else
/// plaster (the specular is the diffuse's: PHOTON-7 stage 0, the walking row's five classes).
unsigned char stillnessClassOf(const envs::Object &o);
/// Per pixel (row-major, w x h) of a perspective view from `eye` towards `target` (world +Y up, `fovYDegrees` the
/// vertical field of view, the aspect w / h): the class of the nearest object box the pixel's ray enters, 0 = none.
std::vector<unsigned char> stillnessClassImage(const std::vector<envs::Object> &objects, const iris::Vec3 &eye,
                                               const iris::Vec3 &target, float fovYDegrees, unsigned w, unsigned h);

}   // namespace lab
