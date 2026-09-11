/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "viewport/rotationgizmo.h"
#include <QApplication>

#include "irisgl/document/assets/mesh.h"
#include "irisgl/core/math/intersectionhelper.h"
#include "irisgl/core/math/mathhelper.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "commands/transformscenenodecommand.h"
#include "irisgl/core/math/mathhelper.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "services/services.h"
#include "services/sceneeditservice.h"
#include "viewport/gizmomeshes.h"

#include "viewport/snapsettings.h"

#include <QLineF>
#include <cmath>

RotationHandle::RotationHandle(Gizmo* gizmo, GizmoAxis axis)
{
	this->gizmo = gizmo;
	this->axis = axis;

	switch (axis) {
	case GizmoAxis::X:
		plane = iris::Vec3(1, 0, 0);
		setHandleColor(QColor(237, 66, 66));
		break;
	case GizmoAxis::Y:
		plane = iris::Vec3(0, 1, 0);
		setHandleColor(QColor(122, 204, 44));
		break;
	case GizmoAxis::Z:
		plane = iris::Vec3(0, 0, 1);
		setHandleColor(QColor(58, 122, 240));
		break;
	}
}

namespace {

/// Distance in pixels from a point to a segment (Qt has no such call on
/// QLineF that answers for a SEGMENT rather than an infinite line).
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

/// How finely a ring is walked when it is projected. The rings are ~0.17 of the
/// viewport height in radius (Gizmo::updateSize keeps that constant), i.e.
/// ~190 px at 1080p, and 96 chords of such a circle sit within 0.1 px of it —
/// well inside the pick tolerance, and 288 projections for a whole gizmo.
constexpr int kRingSamples = 96;

}  // namespace

QString RotationHandle::axisName() const
{
	switch (axis) {
	case GizmoAxis::X: return QStringLiteral("x");
	case GizmoAxis::Y: return QStringLiteral("y");
	case GizmoAxis::Z: return QStringLiteral("z");
	default: return QString();
	}
}

bool RotationHandle::screenDistance(const QPointF& cursor, float& distancePx, float& facing) const
{
	distancePx = -1.0f;
	facing = 0.0f;
	const GizmoPickView &view = gizmo->pickView();
	if (!view.isValid()) return false;

	const float radius = handleRadius * handleScale * gizmo->getGizmoScale();
	if (!(radius > 0.0f)) return false;

	// THE RING AS IT IS DRAWN. gizmomeshes::rotationRing builds the unit circle
	// in the plane spanned by (U, V) of the axis frame, i.e. the plane whose
	// normal is this handle's `plane`; drawItems renders it under the gizmo's
	// transform scaled by handleScale * gizmoScale. Both are read from the same
	// two calls here, so the circle measured IS the circle on screen.
	const iris::Mat4 t = gizmo->getTransform();
	const iris::Vec3 centre = t.column(3).toVector3D();
	iris::Vec3 u, v, n;
	switch (axis) {
	case GizmoAxis::X: n = iris::Vec3(1, 0, 0); u = iris::Vec3(0, 1, 0); v = iris::Vec3(0, 0, 1); break;
	case GizmoAxis::Y: n = iris::Vec3(0, 1, 0); u = iris::Vec3(0, 0, 1); v = iris::Vec3(1, 0, 0); break;
	default:           n = iris::Vec3(0, 0, 1); u = iris::Vec3(1, 0, 0); v = iris::Vec3(0, 1, 0); break;
	}
	const auto toWorldDir = [&t](const iris::Vec3 &d) {
		return (t * iris::Vec4(d, 0)).toVector3D().normalized();
	};
	n = toWorldDir(n);
	u = toWorldDir(u) * radius;
	v = toWorldDir(v) * radius;

	// FACING: 1 when the ring is drawn face-on, 0 when it is drawn as a line.
	const iris::CameraNodePtr &cam = view.camera;
	const iris::Vec3 forward = cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();
	facing = std::fabs(iris::Vec3::dotProduct(n, forward));

	float best = -1.0f;
	QPointF firstPx, prevPx;
	bool havePrev = false, haveFirst = false;
	for (int i = 0; i < kRingSamples; ++i) {
		const float a = float(2.0 * M_PI * i / kRingSamples);
		QPointF px;
		if (!gizmo->projectToPixel(centre + u * qCos(a) + v * qSin(a), px)) {
			havePrev = false;      // that arc crosses behind the eye — no chord
			continue;
		}
		if (!haveFirst) { firstPx = px; haveFirst = true; }
		const float d = havePrev ? pointToSegmentPx(cursor, prevPx, px)
		                         : float(QLineF(cursor, px).length());
		if (best < 0.0f || d < best) best = d;
		prevPx = px;
		havePrev = true;
	}
	if (havePrev && haveFirst) {           // close the loop
		const float d = pointToSegmentPx(cursor, prevPx, firstPx);
		if (best < 0.0f || d < best) best = d;
	}
	if (best < 0.0f) return false;
	distancePx = best;
	return true;
}

bool RotationHandle::getHitAngle(iris::Vec3 rayPos, iris::Vec3 rayDir, float& angle)
{
	// Work in the SCALED gizmo space the ring is drawn in: there the ring is
	// the unit circle and the handle's sphere is the unit sphere.
	auto gizmoTransform = gizmo->getTransform();
	const float scale = handleScale * gizmo->getGizmoScale();
	if (!(scale > 0.0f)) return false;
	gizmoTransform.scale(scale);
	const auto worldToGizmo = gizmoTransform.inverted();

	const iris::Vec3 o = worldToGizmo * rayPos;
	iris::Vec3 d = (worldToGizmo * iris::Vec4(rayDir, 0)).toVector3D();
	if (d.lengthSquared() < 1e-12f) return false;
	d.normalize();

	// The ray's closest approach to the gizmo's centre, then — INSIDE the
	// handle's silhouette — the near intersection with its sphere. The two
	// meet exactly at the silhouette (the tangent point IS the closest
	// approach), so the construction is continuous across it and the angle
	// never jumps mid-drag. Unlike a ray/plane intersection it stays finite
	// when the camera lies in the ring's own plane, which is precisely the
	// case that made an edge-on ring undraggable.
	const float along = -iris::Vec3::dotProduct(o, d);
	iris::Vec3 hitPoint = o + d * along;
	const float h = hitPoint.length();
	if (h < handleRadius && along > 0.0f)
		hitPoint = o + d * (along - std::sqrt(handleRadius * handleRadius - h * h));

	// Fold onto the ring's own plane (a no-op for a point already in it).
	hitPoint = hitPoint - plane * iris::Vec3::dotProduct(plane, hitPoint);
	if (hitPoint.lengthSquared() < 1e-10f) return false;      // dead on the axis
	hitPoint.normalize();

	switch (axis)
	{
	case GizmoAxis::X:
		angle = -qAtan2(-hitPoint.y(), hitPoint.z());
		break;
	case GizmoAxis::Y:
		angle = -qAtan2(hitPoint.x(), hitPoint.z());
		break;
	case GizmoAxis::Z:
		angle = -qAtan2(hitPoint.y(), hitPoint.x());
		break;
	default:
		return false;
	}

	angle = qRadiansToDegrees(angle);

	return true;
}

RotationGizmo::RotationGizmo() :
	Gizmo()
{
	handles[0] = new RotationHandle(this, GizmoAxis::X);
	handles[1] = new RotationHandle(this, GizmoAxis::Y);
	handles[2] = new RotationHandle(this, GizmoAxis::Z);

	loadAssets();
	//handle->setHandleColor(QColor(255, 255, 255));

	dragging = false;
	draggedHandle = nullptr;
}

void RotationGizmo::loadAssets()
{
	// Procedural rings (gizmomeshes.cpp): one thin circle per axis, plus the
	// screen-facing outer ring drawItems adds. Unit radius, and picking
	// projects that same unit circle (screenDistance), so what is drawn and
	// what is clickable are one description.
	handleMeshes.append(GizmoMeshes::rotationRing(GizmoAxis::X));
	handleMeshes.append(GizmoMeshes::rotationRing(GizmoAxis::Y));
	handleMeshes.append(GizmoMeshes::rotationRing(GizmoAxis::Z));
	screenRingMesh = GizmoMeshes::screenRing();
}

bool RotationGizmo::isDragging()
{
	return dragging;
}

void RotationGizmo::startDragging(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir)
{
	trans = Gizmo::getTransform();
	//qDebug() << "drag starting";
	draggedHandle = getHitHandle(rayPos, rayDir, startAngle);
	if (draggedHandle == nullptr) {
		dragging = false; // end dragging if no handle was actually hit
		return;
	}

	nodeStartRot = selectedNode->getLocalRot().normalized();
	dragging = true;
	setInitialTransform();
}

void RotationGizmo::endDragging()
{
	dragging = false;
	draggedHandle = nullptr;
	trans = Gizmo::getTransform();

	// undo-redo
	createUndoAction();
}

void RotationGizmo::drag(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir)
{
	//qDebug() << "dragging";
	if (draggedHandle == nullptr) {
		//dragging = false;
		return;
	}
	//qDebug()<<"sliding";
	float hitAngle;
	draggedHandle->getHitAngle(rayPos, rayDir, hitAngle);

	// move node along line
	// do snapping here as well
	auto diff = startAngle - hitAngle;
	auto mods = QApplication::keyboardModifiers();
	if (mods.testFlag(Qt::ControlModifier)) {
		diff = Gizmo::snap(diff, SnapSettings::rotateSize());
	}

	iris::Quat rot;

	switch (draggedHandle->axis) {
		case GizmoAxis::X:
			rot = iris::Quat::fromEulerAngles(diff, 0, 0);
			break;
		case GizmoAxis::Y:
			rot = iris::Quat::fromEulerAngles(0, diff, 0);
			break;
		case GizmoAxis::Z:
			rot = iris::Quat::fromEulerAngles(0, 0, diff);
			break;
	}

	//qDebug() << rot.toEulerAngles();
	//selectedNode->setLocalRot(nodeStartRot * rot);
	if (transformSpace == GizmoTransformSpace::Global)
		selectedNode->setLocalRot(rot * nodeStartRot);
	else
		selectedNode->setLocalRot(nodeStartRot * rot);

	// The rest of the selection follows the primary's delta (one place,
	// EDITOR_MULTISELECT_SPEC §2.4); a no-op when nothing else is selected.
	applyGroupDelta();
	if (services && services->sceneEdit) services->sceneEdit->notifyTransformChanged();
}

// ---- PICKING, IN PIXELS (smoke S15) ----------------------------------------
//
// THE DEFECT THIS REPLACES. Every ring used to be hit-tested in 3D: transform
// the ray into gizmo space, intersect the ring's PLANE, accept the hit when the
// intersection's distance from the centre landed in [0.8, 1.2] and the point
// was on the camera-facing half. When the camera comes INTO a ring's plane —
// the Y ring seen from ground level, an X or Z ring after F on an axis — the
// ray meets that plane at a grazing angle and the intersection lands hundreds
// of units away, so the ring was unclickable ANYWHERE on screen. Which ring was
// dead followed the camera, which is exactly what the owner reported:
// "sometimes I can't click R, sometimes G or B". The near-half rule threw away
// half of every ring on top of that, and getHitHandle ranked the survivors by a
// STALE member (`hitPos`, left over from a previous hit), so a two-ring overlap
// could hand the drag to the wrong ring.
//
// WHAT IT DOES NOW. Each ring is projected exactly as it is drawn and the
// answer is the cursor's distance in pixels from that projected ellipse;
// nearest ring inside kRingPickTolerancePx wins, a tie goes to the ring that
// faces the camera more. An edge-on ring projects to a line — still there,
// still clickable, which is the acceptance ("every ring click-and-draggable
// from any view angle"). There is no near-half rule: the far half of a ring is
// drawn, so it is pickable.
RotationHandle* RotationGizmo::ringAtPixel(const QPointF& cursor, float& distancePx)
{
	distancePx = -1.0f;
	if (!selectedNode) return nullptr;
	if (!pickView().isValid()) return nullptr;
	trans = Gizmo::getTransform();

	RotationHandle* nearest = nullptr;
	float nearestDist = -1.0f, nearestFacing = -1.0f;
	for (auto i = 0; i < 3; i++) {
		float d = -1.0f, facing = 0.0f;
		if (!handles[i]->screenDistance(cursor, d, facing)) continue;
		const bool closer = nearest == nullptr || d < nearestDist - kRingPickTiePx;
		const bool tie    = nearest != nullptr && std::fabs(d - nearestDist) <= kRingPickTiePx;
		if (closer || (tie && facing > nearestFacing)) {
			nearest = handles[i];
			nearestDist = d;
			nearestFacing = facing;
		}
	}
	if (!nearest) return nullptr;
	distancePx = nearestDist;                       // reported hit or miss
	return nearestDist <= kRingPickTolerancePx ? nearest : nullptr;
}

QString RotationGizmo::ringNameAtPixel(const QPointF& cursor, float& distancePx)
{
	auto* handle = ringAtPixel(cursor, distancePx);
	return handle ? handle->axisName() : QString();
}

bool RotationGizmo::isHit(iris::Vec3 rayPos, iris::Vec3 rayDir)
{
	float hitAngle = 0.0f;
	return getHitHandle(rayPos, rayDir, hitAngle) != nullptr;
}

RotationHandle* RotationGizmo::getHitHandle(iris::Vec3 rayPos, iris::Vec3 rayDir, float& hitAngle)
{
	trans = Gizmo::getTransform();
	QPointF cursor;
	if (!rayPixel(rayPos, rayDir, trans.column(3).toVector3D(), cursor)) return nullptr;
	float distancePx = -1.0f;
	auto* handle = ringAtPixel(cursor, distancePx);
	if (handle) handle->getHitAngle(rayPos, rayDir, hitAngle);
	return handle;
}

iris::Mat4 RotationGizmo::getTransform()
{
	return trans;
	//return Gizmo::getTransform();
}

void RotationGizmo::setTransformSpace(GizmoTransformSpace transformSpace)
{
	this->transformSpace = transformSpace;
	trans = Gizmo::getTransform();
}

void RotationGizmo::setSelectedNode(iris::SceneNodePtr node)
{
	selectedNode = node;
	trans = Gizmo::getTransform();
}

QVector<GizmoDrawItem> RotationGizmo::drawItems(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir)
{
	QVector<GizmoDrawItem> items;
	if (!selectedNode) return items;
	const QColor highlight(255, 255, 0);
	if (dragging) {
		for (int i = 0; i < 3; i++) {
			if (handles[i] != draggedHandle) continue;
			auto transform = Gizmo::getTransform();
			transform.scale(getGizmoScale() * handles[i]->handleScale);
			items.append({ handleMeshes[i], transform, highlight });
		}
		return items;
	}
	float hitAngle = 0.0f;
	auto hitHandle = getHitHandle(rayPos, rayDir, hitAngle);
	for (int i = 0; i < 3; i++) {
		auto transform = Gizmo::getTransform();
		transform.scale(getGizmoScale() * handles[i]->handleScale);
		items.append({ handleMeshes[i], transform, handles[i] == hitHandle ? highlight : handles[i]->getHandleColor() });
	}
	// Screen-facing outer circle framing the three axis rings — visual only
	// (there is no fourth handle behind it), always oriented at the camera.
	if (screenRingMesh) {
		iris::Mat4 t;
		t.translate(Gizmo::getTransform().column(3).toVector3D());
		if (!viewDir.isNull())
			t.rotate(iris::Quat::rotationTo(iris::Vec3(0, 0, 1), -viewDir.normalized()));
		t.scale(getGizmoScale() * handles[0]->handleScale);
		items.append({ screenRingMesh, t, QColor(205, 205, 205) });
	}
	return items;
}
