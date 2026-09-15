/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef TRANSLATIONGIZMO_H
#define TRANSLATIONGIZMO_H

#include "irisgl/core/math/vec.h"
#include "viewport/gizmo.h"
#include "irisgl/irisglfwd.h"

#include <QPointF>
#include <QString>

class Gizmo;

/// HOW CLOSE THE CURSOR HAS TO BE TO A PLANE HANDLE, in logical pixels — the
/// units a QMouseEvent carries (GIZMO-1 item 3). The quad has AREA, so unlike
/// the rotation rings' 7 px this is only a forgiving border around it: a pixel
/// inside the square is a hit at distance 0.
constexpr float kPlanePickTolerancePx = 4.0f;
/// A PLANE HANDLE SEEN EDGE-ON IS NOT A HANDLE. Within this many degrees of
/// edge-on the square projects to a line: it would be a lottery to click and a
/// grazing ray/plane intersection to drag, so it is not drawn and not picked
/// (Unreal hides its plane handles the same way).
constexpr float kPlaneEdgeOnDegrees = 10.0f;

class TranslationHandle : public GizmoHandle
{
    public:
    Gizmo* gizmo;

	GizmoAxis axis;
    iris::Vec3 handleExtent;// local extent of the gizmo
	QVector<iris::Vec3> planes;// for hit detection
	float handleScale = 0.05f;
	float handleRadius = 0.05f;
	float handleLength = 1.7f;

	/// A PLANE HANDLE (GIZMO-1 item 3): the two axes its square lies in, and
	/// the plane's normal — all in gizmo space. Null for the centre and the
	/// three arrows; `isPlane()` is the test.
	iris::Vec3 planeU, planeV, planeNormal;
	bool isPlane() const { return !planeNormal.isNull(); }
	/// The last plane point this handle resolved, in gizmo space — the answer a
	/// grazing ray falls back to so a drag can never teleport the node.
	iris::Vec3 lastPlaneHit;

	TranslationHandle(Gizmo* gizmo, GizmoAxis axis);

	// check if an actual hit is made
    bool isHit(iris::Vec3 rayPos, iris::Vec3 rayDir);

	// assumes hit was already confirmed
    iris::Vec3 getHitPos(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);

	/// "center" | "x" | "y" | "z" | "xy" | "yz" | "xz" — the verb surface's
	/// name for this handle (`editor.gizmoHitTest`).
	QString axisName() const;

	/// THE PLANE HANDLE'S SQUARE, IN PIXELS (GIZMO-1 item 3). The quad is
	/// projected exactly as it is DRAWN and the answer is the cursor's distance
	/// from it — 0 inside, the distance to the nearest edge outside. False for
	/// a handle that is not a plane, when there is no pick view, when a corner
	/// falls behind the eye, or when the plane is within kPlaneEdgeOnDegrees of
	/// edge-on (where it is not drawn either).
	///
	/// `onAxisBand`, when given, comes back TRUE where the cursor is on one of
	/// the two ARROW SHAFTS the square's inner sides run along (GIZMO-2 round 2,
	/// second reader). Since item 1 anchored the square at the origin, its two
	/// inner sides ARE the X/Y/Z shafts between 0 and kPlaneHandleSpan, and a
	/// square answers 0 px over its whole area — so without this a press on the
	/// drawn shaft could never reach the arrow, and worse, an axis lies in TWO
	/// squares that both answer 0, which handed the drag to whichever came
	/// first in handle order (XY for X and Y, YZ for Z): the object moved in a
	/// plane the user did not aim at. The callers use it to let the ARROWS
	/// answer first there.
	bool planeDistance(const QPointF& cursor, float& distancePx,
	                   bool* onAxisBand = nullptr) const;
	/// True while this plane handle is worth drawing and clicking — the
	/// edge-on rule, read from the pick view's camera.
	bool planeFacesCamera() const;
};

class TranslationGizmo : public Gizmo
{
    iris::MeshPtr handleMesh;
	iris::MeshPtr centerMesh;
	iris::MeshPtr circleMesh;
    QVector<iris::MeshPtr> handleMeshes;


    QVector<TranslationHandle*> handles;

    // initial hit position
    iris::Vec3 hitPos;
    iris::Vec3 nodeStartPos;
    TranslationHandle* draggedHandle;

    bool dragging;

public:
	TranslationGizmo();

	void loadAssets();

	bool isDragging();
	void startDragging(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);
	void endDragging();
	void drag(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);

	bool isHit(iris::Vec3 rayPos, iris::Vec3 rayDir);

	// hitPos is the hit position of the hit handle
	TranslationHandle* getHitHandle(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir, iris::Vec3& hitPos);

	/// WHICH PLANE HANDLE A PIXEL HITS, and how far the cursor is from the
	/// nearest one — the translate gizmo's half of `editor.gizmoHitTest`.
	/// "xy" | "yz" | "xz", or an empty string when none is within tolerance.
	QString planeNameAtPixel(const QPointF& cursor, float& distancePx);
public:
	QVector<GizmoDrawItem> drawItems(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir) override;
};


#endif // TRANSLATIONGIZMO_H
