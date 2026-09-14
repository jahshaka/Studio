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
#include "viewport/gizmo.h"

namespace GizmoMeshes
{
    /// The outer (screen-facing) rotation ring's radius, in the same handle-local
    /// units the axis rings' 1.0 is in. PUBLIC because that ring is a HANDLE now
    /// (GIZMO-1 item 2): the circle picking projects has to be the circle the
    /// mesh draws, so both read this one number.
    constexpr float kScreenRingRadius = 1.18f;

    /// THE THREE GIZMOS' REACH, in handle-local units — the numbers the meshes
    /// below are built to and the numbers the sizes of the OTHER two gizmos are
    /// derived from (GIZMO-2 item 2). Multiply by a gizmo's handleScale and by
    /// gizmoScale for world units.
    constexpr float kHandleScale     = 0.05f;   // translate & scale handles
    constexpr float kTranslateEnd    = 1.90f;   // the arrow's tip
    constexpr float kScaleEnd        = 1.46f;   // the scale cube's outer face
    /// What the translate / scale gizmo REACHES, per unit of gizmoScale.
    constexpr float kTranslateReach  = kTranslateEnd * kHandleScale;   // 0.0950
    constexpr float kScaleReach      = kScaleEnd * kHandleScale;       // 0.0730

    /// THE ROTATION GIZMO'S SIZE (GIZMO-2 item 2, owner §366: "the rotation
    /// gizmo is TOO LARGE — the same size as translate and scale, just a little
    /// bigger, not the current radius").
    ///
    /// ONE constant. The rotation gizmo's OUTER extent — the grey screen ring,
    /// its largest drawn feature — is the SCALE gizmo's reach times this ratio,
    /// and the three axis rings sit inside it at the fixed kScreenRingRadius
    /// proportion, so the whole gizmo scales from one number.
    ///
    /// WHY THE SCALE GIZMO'S REACH IS THE REFERENCE, measured rather than
    /// assumed (spikes/gizmo-2, one pose, one cube): the translate gizmo's
    /// arrow tip and the rotation gizmo's outer ring were ALREADY the same
    /// distance from the pivot — 180 px and 185 px in a 900 px frame — so
    /// "1.15 x the translate gizmo's reach" would have GROWN the rings 15 %,
    /// the opposite of the report. What makes the rotation gizmo read as the
    /// larger object is that a circle occupies its whole diameter (370 px)
    /// where three arrows occupy one quadrant; and GIZMO-1's new outer ring had
    /// just pushed the gizmo's extent 18 % past the axis rings it used to end
    /// at. Against the more compact of the other two gizmos — the scale
    /// handles' 1.46 reach — 1.15 puts the outer ring back at 0.0839 of
    /// gizmoScale, within 5 % of the 0.08 the gizmo ended at before GIZMO-1,
    /// and takes 11 % off the whole thing. Retune here, nowhere else.
    constexpr float kRotationExtentRatio = 1.15f;
    /// The rotation gizmo's outer extent, per unit of gizmoScale (0.0839).
    constexpr float kRotationOuterExtent = kScaleReach * kRotationExtentRatio;
    /// ...and therefore the rotation handles' handleScale: the axis rings are
    /// the unit circle of rotationRing(), the screen ring kScreenRingRadius of
    /// it (0.0711).
    constexpr float kRotationHandleScale = kRotationOuterExtent / kScreenRingRadius;

    /// THE PLANE HANDLES' SQUARE, in the same handle-local units the arrows'
    /// 1.9 reach is in (GIZMO-2 item 1, owner §366/§368): its inner corner is
    /// AT the gizmo's origin — inside the white centre ball — and its two sides
    /// run out ALONG the two arrows it lies between, so it spans [0, span] on
    /// both of its axes and reads as one corner of a small box at the centre.
    /// It is drawn as a FRAME (four thin lines, kPlaneFrameRadius); the PICK is
    /// the whole square's area, so it stays as easy to grab as a filled quad.
    ///
    /// 0.50 — smaller than GIZMO-1's 0.45..1.05 square (side 0.60), which is
    /// what the owner asked for, and it leaves the outer 74 % of every arrow
    /// (0.50..1.90) to the arrow itself. PUBLIC because the square is picked in
    /// PIXELS (project these four corners): the square the cursor is tested
    /// against IS the square drawn.
    constexpr float kPlaneHandleSpan = 0.50f;
    /// The frame's line thickness, chosen so it draws the same width as the
    /// rotation rings do: their tube is 0.01 at handleScale 0.0712, i.e.
    /// 0.000712 of gizmoScale, and 0.0142 * 0.05 is the same number.
    constexpr float kPlaneFrameRadius = 0.0142f;

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

    /// THE DRAG MARKER (GIZMO-2 item 3, owner §366/§371 — Unreal's shape): a
    /// small filled disc at the centre, in the dragged ring's own plane, and a
    /// line with an arrowhead running from it out ALONG that ring's axis to the
    /// ring's radius. Drawn only while a ring is being dragged, in the ring's
    /// colour, in the ring's frozen frame — so it says which axis the turn is
    /// about while the turn is happening. Both are built in the same
    /// handle-local units the rings are (the axis ring is the unit circle).
    ///
    /// For GizmoAxis::Screen the axis IS the view direction, so the arrow would
    /// project to a point: RotationGizmo draws the hub alone there.
    iris::MeshPtr dragHub(GizmoAxis axis);
    iris::MeshPtr dragArrow(GizmoAxis axis);
    /// A thin square FRAME in the plane spanned by a plane axis's two axes
    /// (XYPlane / YZPlane / XZPlane), with its inner corner at the origin and
    /// its two sides running out along those axes (GIZMO-2 item 1). Four thin
    /// tubes — the same line the rotation rings are drawn as — so the handle
    /// is an outline with nothing inside it.
    iris::MeshPtr planeHandle(GizmoAxis axis);
}

#endif // GIZMOMESHES_H
