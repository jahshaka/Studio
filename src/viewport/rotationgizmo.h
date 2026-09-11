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
	float handleScale = 0.08f;
	float handleRadius = 1.0f;

	RotationHandle(Gizmo* gizmo, GizmoAxis axis);

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

	/// "x" | "y" | "z" — the verb surface's name for this ring.
	QString axisName() const;
};

class RotationGizmo : public Gizmo
{
	iris::MeshPtr screenRingMesh;   // camera-facing outer ring (visual only)
	QVector<iris::MeshPtr> handleMeshes;


	RotationHandle* handles[3];

	float startAngle;
	iris::Quat nodeStartRot;
	RotationHandle* draggedHandle;

	iris::Mat4 trans;
	bool dragging;
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
	/// The same answer as a name: "x" | "y" | "z", or an empty string.
	QString ringNameAtPixel(const QPointF& cursor, float& distancePx);

	iris::Mat4 getTransform() override;
	void setTransformSpace(GizmoTransformSpace transformSpace) override;
	void setSelectedNode(iris::SceneNodePtr node) override;
public:
	QVector<GizmoDrawItem> drawItems(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir) override;
};

#endif // ROTATIONGIZMO_H
