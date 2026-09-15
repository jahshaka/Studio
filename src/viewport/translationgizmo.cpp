/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "viewport/translationgizmo.h"
#include <QApplication>

#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/core/math/intersectionhelper.h"
#include "irisgl/core/math/mathhelper.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/core/math/mathhelper.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "services/services.h"
#include "services/sceneeditservice.h"
#include "viewport/gizmomeshes.h"

#include "viewport/snapsettings.h"

#include <QLineF>
#include <cmath>

// THE CENTRE BALL'S PICK RADIUS lives in gizmomeshes.h, beside the geometry it
// has to stay clear of (GizmoMeshes::kCentreBallPickRadius): the plane frames'
// inner corner is the gizmo's ORIGIN since item 1, so the ball's sphere sits
// inside all three squares and the two numbers are one decision.
//
// It was a 0.015 #define here — THREE times the ball's drawn 0.005 — and item 1
// made that untenable: a ray cast at a square whose diagonal runs towards the
// camera (the ground square from any 3/4 view is the worst case: only 0.577 of
// its length survives the projection) passed within 0.015 of the origin as far
// out as 0.52 of the square's 0.71 diagonal — the ball swallowed most of every
// plane handle. At 0.010 the ball keeps the pixels a user aims at it and the
// frames keep the rest; the crossover is printed by gizmo.plane_handles F.
#define CENTER_CIRCLE_RADIUS (GizmoMeshes::kCentreBallPickRadius)

TranslationHandle::TranslationHandle(Gizmo* gizmo, GizmoAxis axis)
{
	this->gizmo = gizmo;
	this->axis = axis;

	switch (axis) {
	case GizmoAxis::Center:
		handleExtent = iris::Vec3(0, 0, 0);
		//planes.append(iris::Vec3(0, 1, 0)); // this will change based on the view direction
		setHandleColor(QColor(255, 255, 255));
		break;
	case GizmoAxis::X:
		handleExtent = iris::Vec3(1, 0, 0);
		planes.append(iris::Vec3(0, 1, 0));
		planes.append(iris::Vec3(0, 0, 1));
		setHandleColor(QColor(237, 66, 66));
		break;
	case GizmoAxis::Y:
		handleExtent = iris::Vec3(0, 1, 0);
		planes.append(iris::Vec3(1, 0, 0));
		planes.append(iris::Vec3(0, 0, 1));
		setHandleColor(QColor(122, 204, 44));
		break;
	case GizmoAxis::Z:
		handleExtent = iris::Vec3(0, 0, 1);
		planes.append(iris::Vec3(1, 0, 0));
		planes.append(iris::Vec3(0, 1, 0));
		setHandleColor(QColor(58, 122, 240));
		break;
	// THE PLANE HANDLES (GIZMO-1 item 3). Each lies in the plane its two axes
	// span and wears their two colours MIXED, so which two axes a drag will
	// move is readable without a legend.
	case GizmoAxis::XYPlane:
		planeU = iris::Vec3(1, 0, 0); planeV = iris::Vec3(0, 1, 0);
		setHandleColor(QColor(179, 135, 55));      // red + green
		break;
	case GizmoAxis::YZPlane:
		planeU = iris::Vec3(0, 1, 0); planeV = iris::Vec3(0, 0, 1);
		setHandleColor(QColor(90, 163, 142));      // green + blue
		break;
	case GizmoAxis::XZPlane:
		planeU = iris::Vec3(1, 0, 0); planeV = iris::Vec3(0, 0, 1);
		setHandleColor(QColor(147, 94, 153));      // red + blue
		break;
	default:
		break;
	}
	if (!planeU.isNull())
		planeNormal = iris::Vec3::crossProduct(planeU, planeV).normalized();
}

QString TranslationHandle::axisName() const
{
	switch (axis) {
	case GizmoAxis::Center:  return QStringLiteral("center");
	case GizmoAxis::X:       return QStringLiteral("x");
	case GizmoAxis::Y:       return QStringLiteral("y");
	case GizmoAxis::Z:       return QStringLiteral("z");
	case GizmoAxis::XYPlane: return QStringLiteral("xy");
	case GizmoAxis::YZPlane: return QStringLiteral("yz");
	case GizmoAxis::XZPlane: return QStringLiteral("xz");
	default: return QString();
	}
}

namespace {

/// THE DRAWN ARROW SHAFT'S HALF-WIDTH, in handle-local units — gizmomeshes.cpp's
/// kShaftRadius, which is private to the mesh builder and is the width of the
/// LINE a user aims at. The axis bands below are this plus the pick tolerance.
constexpr float kArrowShaftRadius = 0.0175f;

/// Distance in pixels from a point to a segment (Qt has no such call on QLineF
/// that answers for a SEGMENT rather than an infinite line).
float pointToSegmentPx(const QPointF &p, const QPointF &a, const QPointF &b)
{
	const double vx = b.x() - a.x(), vy = b.y() - a.y();
	const double wx = p.x() - a.x(), wy = p.y() - a.y();
	const double len2 = vx * vx + vy * vy;
	double t = len2 > 1e-12 ? (wx * vx + wy * vy) / len2 : 0.0;
	t = qBound(0.0, t, 1.0);
	const double dx = wx - t * vx, dy = wy - t * vy;
	return float(std::sqrt(dx * dx + dy * dy));
}

}  // namespace

// EDGE-ON IS NOT A HANDLE. Read from the pick view's camera — the same camera
// the quad is projected through — so the picture and the pick agree by
// construction. No pick view (a document-only stand-in) means no plane handles,
// exactly as it means no ring picking.
bool TranslationHandle::planeFacesCamera() const
{
	if (!isPlane()) return false;
	const GizmoPickView &view = gizmo->pickView();
	if (!view.isValid()) return false;
	const iris::Mat4 t = gizmo->getTransform();
	const iris::Vec3 n = (t * iris::Vec4(planeNormal, 0)).toVector3D().normalized();
	const iris::Vec3 forward =
		view.camera->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();
	return std::fabs(iris::Vec3::dotProduct(n, forward)) >
	       std::sin(float(qDegreesToRadians(kPlaneEdgeOnDegrees)));
}

bool TranslationHandle::planeDistance(const QPointF& cursor, float& distancePx,
                                      bool* onAxisBand) const
{
	distancePx = -1.0f;
	if (onAxisBand) *onAxisBand = false;
	if (!planeFacesCamera()) return false;
	const float scale = handleScale * gizmo->getGizmoScale();
	if (!(scale > 0.0f)) return false;

	// THE SQUARE THE FRAME OUTLINES: gizmomeshes::planeHandle draws its four
	// sides from the same two axes over the same [0, span], under the same
	// transform — so the area measured here is the area the frame encloses,
	// and a cursor INSIDE the frame is a hit at 0 px (GIZMO-2 item 1: "the
	// frame is what is drawn, the plane is what is picked").
	const iris::Mat4 t = gizmo->getTransform();
	const float f = GizmoMeshes::kPlaneHandleSpan;
	const iris::Vec3 corners[4] = {
		iris::Vec3(0, 0, 0),         planeU * f,
		planeU * f + planeV * f,     planeV * f,
	};
	QPointF px[4];
	for (int i = 0; i < 4; ++i)
		if (!gizmo->projectToPixel(t * (corners[i] * scale), px[i])) return false;

	// THE TWO ARROW SHAFTS THE SQUARE IS BUILT ON (GIZMO-2 round 2). px[0] is
	// the origin, px[1] and px[3] the ends of the two inner sides — which are
	// the X/Y/Z shafts themselves. A cursor within the drawn shaft's own
	// half-width plus the pick tolerance of either one is ON AN ARROW, and the
	// callers hand it to the arrows rather than to this square.
	//
	// The half-width is converted to pixels with the local scale ACROSS the
	// axis in question — the projected length of the OTHER inner side, which is
	// the direction the band is measured in — so the band follows the
	// foreshortening of the very square it is carved out of.
	if (onAxisBand) {
		const float spanPx[2] = { float(QLineF(px[0], px[1]).length()),
		                          float(QLineF(px[0], px[3]).length()) };
		const float shaftPx[2] = { kArrowShaftRadius * spanPx[1] / f,   // across U: V's scale
		                           kArrowShaftRadius * spanPx[0] / f }; // across V: U's scale
		const float dU = pointToSegmentPx(cursor, px[0], px[1]);
		const float dV = pointToSegmentPx(cursor, px[0], px[3]);
		*onAxisBand = dU <= shaftPx[0] + kPlanePickTolerancePx ||
		              dV <= shaftPx[1] + kPlanePickTolerancePx;
	}

	// Inside the projected quad? A convex quadrilateral: the cursor is inside
	// when it is on the same side of all four edges.
	bool positive = false, negative = false;
	for (int i = 0; i < 4; ++i) {
		const QPointF &a = px[i], &b = px[(i + 1) % 4];
		const double cross = (b.x() - a.x()) * (cursor.y() - a.y()) -
		                     (b.y() - a.y()) * (cursor.x() - a.x());
		if (cross > 0.0) positive = true;
		if (cross < 0.0) negative = true;
	}
	if (!(positive && negative)) { distancePx = 0.0f; return true; }

	float best = -1.0f;
	for (int i = 0; i < 4; ++i) {
		const float d = pointToSegmentPx(cursor, px[i], px[(i + 1) % 4]);
		if (best < 0.0f || d < best) best = d;
	}
	distancePx = best;
	return true;
}

bool TranslationHandle::isHit(iris::Vec3 rayPos, iris::Vec3 rayDir)
{
    auto gizmoTrans = gizmo->getTransform();

	// A PLANE HANDLE IS PICKED IN PIXELS (GIZMO-1 item 3): it is a flat square
	// with area, so "is the cursor on it" is a 2D question and stays well
	// conditioned however the camera is turned — the same argument the rotation
	// rings' screen-space pick rests on (smoke S15).
	if (isPlane()) {
		QPointF cursor;
		if (!gizmo->rayPixel(rayPos, rayDir, gizmoTrans.column(3).toVector3D(), cursor))
			return false;
		float d = -1.0f;
		return planeDistance(cursor, d) && d <= kPlanePickTolerancePx;
	}

	if (this->axis == GizmoAxis::Center) {
		// sphere center intersection
		float t;
		iris::Vec3 hitPoint;
		return iris::IntersectionHelper::raySphereIntersects(rayPos, rayDir, gizmoTrans * iris::Vec3(0, 0, 0), gizmo->getGizmoScale() * CENTER_CIRCLE_RADIUS, t, hitPoint);
	}
	else {

		// calculate world space position of the segment representing the handle
		auto p1 = gizmoTrans * iris::Vec3(0, 0, 0);
		auto q1 = gizmoTrans * (handleExtent * handleLength * gizmo->getGizmoScale() * handleScale);

		auto p2 = rayPos;
		auto q2 = rayPos + rayDir * 100000;// (nick) use segment instead of ray pos and ray dir

		float s, t;
		iris::Vec3 c1, c2;
		auto dist = iris::MathHelper::closestPointBetweenSegments(p1, q1, p2, q2, s, t, c1, c2);
		if (dist < handleScale * gizmo->getGizmoScale() * handleScale) {
			return true;
		}

		return false;
	}
}

iris::Vec3 TranslationHandle::getHitPos(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir)
{
	bool hit = false;
	iris::Vec3 finalHitPos;
	float closestDist = 10000000;

	// transform rayPos and rayDir to gizmo space
	auto gizmoTransform = gizmo->getTransform();
	auto worldToGizmo = gizmoTransform.inverted();
	rayPos = worldToGizmo * rayPos;
	rayDir = iris::Quat::fromRotationMatrix(worldToGizmo.normalMatrix()).rotatedVector(rayDir);

	// A PLANE DRAG IS THE RAY MEETING THAT PLANE (GIZMO-1 item 3): the node
	// follows the cursor across the plane through the gizmo's origin, so the
	// motion is exactly the two axes the square spans and nothing else. The
	// grazing guard cannot normally fire — the handle is hidden and unpickable
	// within kPlaneEdgeOnDegrees of edge-on, and the camera cannot move during
	// a gizmo drag — but a drag that survives one anyway holds still instead of
	// teleporting the node to infinity.
	if (isPlane()) {
		float t = 0.0f;
		iris::Vec3 hitPoint;
		if (std::fabs(iris::Vec3::dotProduct(rayDir.normalized(), planeNormal)) > 0.02f &&
		    iris::IntersectionHelper::intersectSegmentPlane(
		        rayPos, rayPos + rayDir * 10000000, iris::Plane(planeNormal, 0), t, hitPoint))
			lastPlaneHit = hitPoint;
		return gizmoTransform * lastPlaneHit;
	}

	if (this->axis == GizmoAxis::Center) {
		// sphere center intersection
		float t;
		iris::Vec3 hitPoint;
		//iris::IntersectionHelper::raySphereIntersects(rayPos, rayDir, iris::Vec3(0, 0, 0), 1, t, hitPoint);

		//return gizmoTransform * hitPoint;
		auto normal = iris::Quat::fromRotationMatrix(worldToGizmo.normalMatrix()).rotatedVector(-viewDir);
		iris::IntersectionHelper::intersectSegmentPlane(rayPos, rayPos + rayDir * 10000000, iris::Plane(normal, 0), t, hitPoint);

		return gizmoTransform * hitPoint;
	}
	else {

		// loop through planes
		for (auto normal : planes) {
			float t;
			iris::Vec3 hitPos;

			// flip normal so its facing the ray source
			if (iris::Vec3::dotProduct(normal, rayPos) < 0)
				normal = -normal;

			if (iris::IntersectionHelper::intersectSegmentPlane(rayPos, rayPos + rayDir * 10000000, iris::Plane(normal, 0), t, hitPos)) {
				// ignore planes at grazing angles
				if (qAbs(iris::Vec3::dotProduct(rayDir, normal)) < 0.1f)
					continue;

				auto hitResult = handleExtent * hitPos;

				// this isnt the first hit, but if it's the closest one then use it
				if (hit) {
					if (rayPos.distanceToPoint(hitResult) < closestDist) {
						finalHitPos = hitResult;
						closestDist = rayPos.distanceToPoint(hitResult);
					}
				}
				else {
					// first hit, just assign it
					finalHitPos = hitResult;
					closestDist = rayPos.distanceToPoint(hitResult);
				}

				hit = true;
			}
		}
	}

	if (!hit) {
		float dominantExtent = iris::MathHelper::sign(iris::Vec3::dotProduct(rayDir.normalized(), handleExtent));// results in -1 or 1
		finalHitPos = dominantExtent * handleExtent * 10000;
	}

	// now convert it back to world space
	finalHitPos = gizmoTransform * finalHitPos;

	return finalHitPos;
}

TranslationGizmo::TranslationGizmo() :
	Gizmo()
{
	handles.append(new TranslationHandle(this, GizmoAxis::Center));
	handles.append(new TranslationHandle(this, GizmoAxis::X));
	handles.append(new TranslationHandle(this, GizmoAxis::Y));
	handles.append(new TranslationHandle(this, GizmoAxis::Z));
	handles.append(new TranslationHandle(this, GizmoAxis::XYPlane));
	handles.append(new TranslationHandle(this, GizmoAxis::YZPlane));
	handles.append(new TranslationHandle(this, GizmoAxis::XZPlane));

	loadAssets();

	dragging = false;
	draggedHandle = nullptr;
}

void TranslationGizmo::loadAssets()
{
	// Procedural handles (gizmomeshes.cpp): thin axis lines ending in small
	// cones, small ball at the core. Same local reach as the old OBJs, so the
	// analytic hit-testing below is untouched.
	handleMeshes.append(GizmoMeshes::centerSphere());
	handleMeshes.append(GizmoMeshes::translateHandle(GizmoAxis::X));
	handleMeshes.append(GizmoMeshes::translateHandle(GizmoAxis::Y));
	handleMeshes.append(GizmoMeshes::translateHandle(GizmoAxis::Z));
	handleMeshes.append(GizmoMeshes::planeHandle(GizmoAxis::XYPlane));
	handleMeshes.append(GizmoMeshes::planeHandle(GizmoAxis::YZPlane));
	handleMeshes.append(GizmoMeshes::planeHandle(GizmoAxis::XZPlane));

	centerMesh = GizmoMeshes::centerSphere();

	// create circle (kept CPU-side; unused by the engine path today)
	QVector<float> points;
	for (float i = 0; i < 360; i += 1) {
		auto x = qCos(qDegreesToRadians(i));
		auto y = qSin(qDegreesToRadians(i));
		points.append(x); points.append(y); points.append(0);

		x = qCos(qDegreesToRadians(i + 1));
		y = qSin(qDegreesToRadians(i + 1));
		points.append(x); points.append(y); points.append(0);
	}

	iris::VertexLayout layout;
	layout.addAttrib(iris::VertexAttribUsage::Position, iris::AttribTypeFloat, 3, sizeof(float) * 3);

	auto vb = iris::VertexBuffer::create(layout);
	vb->setData((void*)points.constData(), points.size() * sizeof(float));

	circleMesh = iris::Mesh::create();
	circleMesh->addVertexBuffer(vb);
	circleMesh->setPrimitiveMode(iris::PrimitiveMode::Lines);
	circleMesh->setVertexCount(360 * 2);
}

bool TranslationGizmo::isDragging()
{
	return dragging;
}

void TranslationGizmo::startDragging(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir)
{
	draggedHandle = getHitHandle(rayPos, rayDir, viewDir, hitPos);
	if (draggedHandle == nullptr) {
		dragging = false; // end dragging if no handle was actually hit
		return;
	}

	nodeStartPos = selectedNode->getGlobalPosition();
	dragging = true;
	setInitialTransform();
}

void TranslationGizmo::endDragging()
{
	dragging = false;
	draggedHandle = nullptr;

	// undo-redo
	createUndoAction();
}

void TranslationGizmo::drag(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir)
{
	if (draggedHandle == nullptr) {
		return;
	}

	auto slidingPos = draggedHandle->getHitPos(rayPos, rayDir, viewDir);

	// move node along line
	// do snapping here as well
	auto diff = slidingPos - hitPos;

	// apply snapping (relative snapping)
	auto mods = QApplication::keyboardModifiers();
	if (mods.testFlag(Qt::ControlModifier)) {
		if (draggedHandle->isPlane()) {
			// A PLANE DRAG SNAPS ON BOTH OF ITS AXES (GIZMO-1 item 3), which is
			// what "snap as the arrows do" means for two degrees of freedom:
			// the arrows snap the one distance they can move, so each of the
			// plane's two components is snapped to the same grid. Snapping the
			// LENGTH of a two-axis move instead (the arrows' formula, applied
			// blindly) would leave both components off the grid.
			const iris::Mat4 gizmoTransform = this->getTransform();
			const iris::Vec3 local = (gizmoTransform.inverted() * iris::Vec4(diff, 0)).toVector3D();
			const float grid = SnapSettings::translateSize();
			const iris::Vec3 snapped =
				draggedHandle->planeU * Gizmo::snap(iris::Vec3::dotProduct(local, draggedHandle->planeU), grid) +
				draggedHandle->planeV * Gizmo::snap(iris::Vec3::dotProduct(local, draggedHandle->planeV), grid);
			diff = (gizmoTransform * iris::Vec4(snapped, 0)).toVector3D();
		} else {
			float length = diff.length();
			float snapLength = Gizmo::snap(length, SnapSettings::translateSize());
			diff = diff.normalized() * snapLength;
		}
	}

	// apply diff in global space
	auto targetPos = nodeStartPos + diff;

	// bring to local space
	auto parentNode = selectedNode->getParent();
	auto localTarget = parentNode ? parentNode->getGlobalTransform().inverted() * targetPos
	                              : targetPos;
	
	//selectedNode->setLocalPos(localTarget);
	selectedNode->setGlobalPos(targetPos);
	// The rest of the selection follows the primary's delta (one place,
	// EDITOR_MULTISELECT_SPEC §2.4); a no-op when nothing else is selected.
	applyGroupDelta();
	if (services && services->sceneEdit) services->sceneEdit->notifyTransformChanged();
}

bool TranslationGizmo::isHit(iris::Vec3 rayPos, iris::Vec3 rayDir)
{
	for (auto i = 0; i< handles.size(); i++) {
		if (handles[i]->isHit(rayPos, rayDir))
		{
			return true;
		}
	}

	return false;
}

// WHAT A PRESS GRABS, in three passes with an explicit precedence.
//
// CENTRE first (it always did: the smallest target, and it sits under
// everything) — and since GIZMO-2 item 1 that matters more than it did: the
// plane frames' inner corner is AT the origin, so the ball's pick sphere
// (CENTER_CIRCLE_RADIUS * gizmoScale = 0.010; the ball is DRAWN at kCentreBallRadius
// 0.12 of the handle scale = 0.006 of gizmoScale, so the pick sphere still exceeds it)
// sits INSIDE all three squares and wins there, which is what keeps a click on
// the white ball a click on the ball.
//
// Then the PLANE handles: each square spans [0, kPlaneHandleSpan] on its two
// axes, so it now COVERS the inner quarter of the two arrows it lies between —
// a press there grabs the plane, and each arrow keeps the outer 74 % of its
// length (0.50..1.90 of 1.90) to itself. Then the arrows, nearest first.
//
// The arrows' pass also fixes a stale read that was there since 2016: `dist`
// was measured from `hitPos`, the caller's OUT parameter, before this call had
// written it — so the "closest" arrow was ranked by the PREVIOUS press's hit
// point (or by uninitialised memory on the first one).
TranslationHandle* TranslationGizmo::getHitHandle(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir, iris::Vec3& hitPos)
{
	for (auto i = 0; i < handles.size(); i++) {
		if (handles[i]->axis != GizmoAxis::Center) continue;
		if (!handles[i]->isHit(rayPos, rayDir)) continue;
		hitPos = handles[i]->getHitPos(rayPos, rayDir, viewDir);
		return handles[i];
	}

	TranslationHandle* nearestPlane = nullptr;
	float nearestPlanePx = -1.0f;
	// A PLANE WHOSE ANSWER SITS ON AN ARROW SHAFT (GIZMO-2 round 2). The
	// square's two inner sides ARE the shafts, so a press there has to reach
	// the arrow — but if no arrow answers at that pixel (the arrows are picked
	// in 3D against their own tube, this square in pixels), the plane still
	// takes it rather than leaving a dead pixel on a drawn handle.
	TranslationHandle* bandPlane = nullptr;
	float bandPlanePx = -1.0f;
	QPointF cursor;
	const bool havePixel =
		rayPixel(rayPos, rayDir, getTransform().column(3).toVector3D(), cursor);
	if (havePixel) {
		for (auto i = 0; i < handles.size(); i++) {
			if (!handles[i]->isPlane()) continue;
			float d = -1.0f;
			bool onAxis = false;
			if (!handles[i]->planeDistance(cursor, d, &onAxis)) continue;
			if (d > kPlanePickTolerancePx) continue;
			if (onAxis) {
				if (!bandPlane || d < bandPlanePx) { bandPlane = handles[i]; bandPlanePx = d; }
				continue;
			}
			if (!nearestPlane || d < nearestPlanePx) { nearestPlane = handles[i]; nearestPlanePx = d; }
		}
	}
	if (nearestPlane) {
		hitPos = nearestPlane->getHitPos(rayPos, rayDir, viewDir);
		return nearestPlane;
	}

	TranslationHandle* closestHandle = nullptr;
	float closestDistance = 10000000;
	for (auto i = 0; i < handles.size(); i++)
	{
		if (handles[i]->isPlane() || handles[i]->axis == GizmoAxis::Center) continue;
		if (handles[i]->isHit(rayPos, rayDir)) {
			auto hit = handles[i]->getHitPos(rayPos, rayDir, viewDir);
			auto dist = hit.distanceToPoint(rayPos);
			if (dist < closestDistance) {
				closestHandle = handles[i];
				closestDistance = dist;
				hitPos = hit;
			}
		}
	}
	if (!closestHandle && bandPlane) {
		hitPos = bandPlane->getHitPos(rayPos, rayDir, viewDir);
		return bandPlane;
	}

	return closestHandle;
}

QString TranslationGizmo::planeNameAtPixel(const QPointF& cursor, float& distancePx)
{
	distancePx = -1.0f;
	if (!selectedNode || !pickView().isValid()) return QString();
	TranslationHandle* nearest = nullptr;
	for (auto i = 0; i < handles.size(); i++) {
		if (!handles[i]->isPlane()) continue;
		float d = -1.0f;
		bool onAxis = false;
		if (!handles[i]->planeDistance(cursor, d, &onAxis)) continue;
		// ON A SHAFT IS THE ARROW'S PIXEL (GIZMO-2 round 2): the verb's caller
		// (EngineSceneViewport::gizmoHitTest) asks this first and falls through
		// to getHitHandle when it answers nothing, which is where the arrows —
		// and the band's own fallback — are. Reporting a plane here would say
		// one thing and the press would do another.
		if (onAxis) continue;
		if (distancePx < 0.0f || d < distancePx) { distancePx = d; nearest = handles[i]; }
	}
	if (!nearest) return QString();
	return distancePx <= kPlanePickTolerancePx ? nearest->axisName() : QString();
}

QVector<GizmoDrawItem> TranslationGizmo::drawItems(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir)
{
	QVector<GizmoDrawItem> items;
	if (!selectedNode) return items;
	const QColor highlight(255, 255, 0);
	if (dragging) {
		for (int i = 0; i < handles.size(); i++) {
			if (handles[i] != draggedHandle) continue;
			auto transform = this->getTransform();
			transform.scale(getGizmoScale() * handles[i]->handleScale);
			items.append({ handleMeshes[i], transform, highlight });
		}
		return items;
	}
	iris::Vec3 hitPos;
	auto hitHandle = getHitHandle(rayPos, rayDir, viewDir, hitPos);
	for (int i = 0; i < handles.size(); i++) {
		// A PLANE SEEN EDGE-ON IS NOT DRAWN (GIZMO-1 item 3) — it would be a
		// line the user cannot aim at, and the pick refuses it for the same
		// reason, so the picture and the pick say the same thing.
		if (handles[i]->isPlane() && !handles[i]->planeFacesCamera()) continue;
		auto transform = this->getTransform();
		transform.scale(getGizmoScale() * handles[i]->handleScale);
		items.append({ handleMeshes[i], transform, handles[i] == hitHandle ? highlight : handles[i]->getHandleColor() });
	}
	return items;
}
