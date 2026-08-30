/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef GIZMOMESHES_H
#define GIZMOMESHES_H

// GizmoMeshes — procedural geometry for the transform gizmo handles.
//
// The look is inspired by modern DCC gizmos (thin axis lines ending in small
// cones/cubes, thin rotation circles) but every mesh here is ORIGINAL code:
// plain parametric cylinders, cones, cubes, spheres and tori written from
// scratch for Jahshaka's MIT licence. Dimensions live in one place below so
// the three gizmos stay visually consistent.
//
// Meshes are triangle soups (positions + normals, no indices) in the same
// local space the old OBJ handles used: handles run along +axis, roughly
// 0..1.9 units for translate and 0..1.46 for scale, rings have radius 1.
// The gizmos scale them by gizmoScale * handleScale exactly as before, so
// hit-testing (which is analytic, not mesh-based) is untouched.

#include "irisgl/irisglfwd.h"
#include "gizmo.h"

namespace GizmoMeshes
{
    /// Thin shaft ending in a small cone along +axis (translate handle).
    iris::MeshPtr translateHandle(GizmoAxis axis);
    /// Thin shaft ending in a small cube along +axis (scale handle).
    iris::MeshPtr scaleHandle(GizmoAxis axis);
    /// Small ball for the translate gizmo's core.
    iris::MeshPtr centerSphere();
    /// Small cube for the scale gizmo's core (uniform scale).
    iris::MeshPtr centerCube();
    /// Thin ring (torus) of radius 1 in the plane perpendicular to `axis`.
    iris::MeshPtr rotationRing(GizmoAxis axis);
    /// Slightly larger thin ring in the XY plane; the rotation gizmo orients it
    /// to face the camera each frame (the screen-space outer ring).
    iris::MeshPtr screenRing();
}

#endif // GIZMOMESHES_H
