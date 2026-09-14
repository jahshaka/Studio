/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ROTATIONGIZMO_H
#define ROTATIONGIZMO_H

//#include "gizmoinstance.h"
#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "viewport/gizmo.h"
#include "viewport/gizmomeshes.h"

#include <QPointF>
#include <QString>

/// HOW CLOSE THE CURSOR HAS TO BE TO A RING, in logical (device-independent)
/// pixels — the units a QMouseEvent carries, so this is the same physical
/// distance on a HiDPI screen as on a plain one. The rings are drawn one pixel
/// wide, so the tolerance IS the click target; 7 px matches the arrow handles'
/// effective target and is what the picking suite asserts against.
constexpr float kRingPickTolerancePx = 7.0f;
/// Two rings crossing under the cursor are a TIE within this many pixels, and a
/// tie is won by the ring that faces the camera more (the one the user can see
/// as a ring rather than as a line).
constexpr float kRingPickTiePx = 2.0f;

class RotationHandle : public GizmoHandle
{
public:
	Gizmo* gizmo;

	GizmoAxis axis;
	iris::Vec3 plane;// the ring's own plane: its NORMAL in gizmo space
	/// THE ROTATION GIZMO'S SIZE, from the one constant that derives it from
	/// the translate/scale gizmos' reach (GIZMO-2 item 2, owner §366). It was
	/// a bare 0.08 here.
	float handleScale = GizmoMeshes::kRotationHandleScale;
	float handleRadius = 1.0f;
	/// The ring's radius in handle-local units: 1 for the three axis rings,
	/// GizmoMeshes::kScreenRingRadius for the outer screen-facing one — the
	/// same number the mesh is built with, so the circle picked IS the circle
	/// drawn.
	float ringRadius = 1.0f;
	/// THE VIEW AXIS, for the Screen handle only, in WORLD space: the direction
	/// the camera looks. Written by RotationGizmo::refreshFrame(), which means
	/// it is FROZEN for the length of a drag exactly like the gizmo's frame.
	iris::Vec3 screenAxis;

	RotationHandle(Gizmo* gizmo, GizmoAxis axis);

	/// The frame this ring is drawn and picked in, relative to the gizmo's own
	/// frozen frame: identity for an axis ring, the camera-facing turn for the
	/// screen ring. Unit scale — callers apply gizmoScale * handleScale.
	iris::Mat4 ringFrame() const;

	/// HOW FAR THE CURSOR IS FROM THIS RING, ON SCREEN (smoke S15).
	///
	/// The ring is projected exactly as it is DRAWN — the unit circle of
	/// gizmomeshes::rotationRing, in the plane whose normal is `plane`, scaled
	/// by the gizmo's screen-constant scale — and the answer is the distance in
	/// pixels from `cursor` to that projected ellipse. It is defined for EVERY
	/// camera angle, which is the whole point: the 3D annulus test this
	/// replaced (a ray/plane intersection with the hit radius in [0.8, 1.2])
	/// could not hit a ring the camera looked along, so at any moment one of
	/// R/G/B was unclickable and which one followed the camera.
	///
	/// `facing` comes back as |cos| between the ring's axis and the view
	/// direction: 1 = face-on, 0 = edge-on. It breaks ties between two rings
	/// crossing under the cursor. False when there is no pick view or the ring
	/// projects nowhere (entirely behind the eye).
	bool screenDistance(const QPointF& cursor, float& distancePx, float& facing) const;

	/// The angle of the cursor AROUND this ring, in degrees — the quantity the
	/// drag differences. Well conditioned at every camera angle: the ray is
	/// resolved against the handle's SPHERE (the near hemisphere, falling back
	/// to the ray's closest approach outside the silhouette, which is the same
	/// point at the silhouette, so the two agree where they meet) and the
	/// result is folded into the ring's plane. The old form intersected the
	/// ring's PLANE, which runs away to infinity as the camera comes into that
	/// plane — the drag half of the same defect the hit test had.
	bool getHitAngle(iris::Vec3 rayPos, iris::Vec3 rayDir, float& angle);

	/// "x" | "y" | "z" | "screen" — the verb surface's name for this ring.
	QString axisName() const;
};

class RotationGizmo : public Gizmo
{
	QVector<iris::MeshPtr> handleMeshes;
	/// THE DRAG MARKER (GIZMO-2 item 3): one hub disc per handle and one axis
	/// arrow per AXIS handle (the screen ring's axis points at the eye, so it
	/// has no arrow — index kScreenHandle is null there). Drawn only while
	/// `dragging`, in the dragged ring's colour, in that ring's frozen frame.
	QVector<iris::MeshPtr> hubMeshes;
	QVector<iris::MeshPtr> arrowMeshes;

	/// X, Y, Z and — since GIZMO-1 item 2 — SCREEN: the outer grey ring is a
	/// fourth handle, not decoration. It is last on purpose: it is drawn last
	/// (on top of the three axis rings) and it LOSES a pick tie to any of
	/// them, so grabbing an axis ring where the two cross still gets the axis.
	static constexpr int kScreenHandle = 3;
	RotationHandle* handles[4];

	float startAngle;
	iris::Quat nodeStartRot;
	RotationHandle* draggedHandle;

	/// THE GIZMO'S OWN FRAME, and it is FROZEN FOR THE LENGTH OF A DRAG
	/// (owner report 2026-09-15, §345: "they seem to move on their own with
	/// the asset").
	///
	/// In LOCAL space the gizmo is drawn from the node's CURRENT rotation, so
	/// a drag on the X ring turned the object about its local X and the ring
	/// turned with it — the ring ran away from under the cursor while the
	/// VALUE was right (the drag maths has always differenced against the
	/// frame captured at startDragging). Unreal and Blender freeze the picture
	/// for the length of the drag and re-orient at release, and so do we: this
	/// member is refreshed by refreshFrame(), which does nothing while
	/// `dragging`, and every drawn item and every pick reads it through
	/// getTransform().
	iris::Mat4 trans;
	bool dragging;

	/// Re-reads the gizmo's frame from the node — UNLESS a drag is running, in
	/// which case the frame captured at startDragging stands. Called at the top
	/// of everything that draws or picks.
	void refreshFrame();
public:
	RotationGizmo();

	void loadAssets();

	virtual bool isDragging();
	virtual void startDragging(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);
	virtual void endDragging();
	virtual void drag(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);

	virtual bool isHit(iris::Vec3 rayPos, iris::Vec3 rayDir);

	/// The ring the pick ray hits, and the angle of the cursor around it.
	RotationHandle* getHitHandle(iris::Vec3 rayPos, iris::Vec3 rayDir, float& hitAngle);

	/// THE PICK ITSELF, in pixels (smoke S15). The nearest ring to `cursor`
	/// comes back in `distancePx` whether or not it is within tolerance, and
	/// the return is null when the nearest one is further away than
	/// kRingPickTolerancePx. Public because `editor.gizmoHitTest` is exactly
	/// this question asked from a script, and a picking test must be able to
	/// ask it without synthesizing mouse events.
	RotationHandle* ringAtPixel(const QPointF& cursor, float& distancePx);
	/// The same answer as a name: "x" | "y" | "z" | "screen", or an empty string.
	QString ringNameAtPixel(const QPointF& cursor, float& distancePx);

	iris::Mat4 getTransform() override;
	void setTransformSpace(GizmoTransformSpace transformSpace) override;
	void setSelectedNode(iris::SceneNodePtr node) override;
public:
	QVector<GizmoDrawItem> drawItems(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir) override;
};

#endif // ROTATIONGIZMO_H
