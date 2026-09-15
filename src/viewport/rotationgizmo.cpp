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
	case GizmoAxis::Screen:
		// The plane is the VIEW's, so it is not a constant in gizmo space;
		// `screenAxis` carries it instead (world space, refreshed with the
		// gizmo's frame). Grey, and wider than the axis rings.
		plane = iris::Vec3(0, 0, 1);
		ringRadius = GizmoMeshes::kScreenRingRadius;
		setHandleColor(QColor(205, 205, 205));
		break;
	default:
		break;
	}
}

// The frame a ring is drawn and picked in. An axis ring rides the gizmo's own
// frame; the screen ring is turned to face the camera — its +Z is the direction
// BACK to the eye — so that its circle is the circle on screen. `screenAxis` is
// written by refreshFrame(), so this frame is frozen during a drag too and the
// outer ring cannot slide under the cursor either.
iris::Mat4 RotationHandle::ringFrame() const
{
	iris::Mat4 t = gizmo->getTransform();
	if (axis != GizmoAxis::Screen) return t;
	iris::Mat4 facing;
	facing.setToIdentity();
	facing.translate(t.column(3).toVector3D());
	if (!screenAxis.isNull())
		facing.rotate(iris::Quat::rotationTo(iris::Vec3(0, 0, 1), -screenAxis.normalized()));
	return facing;
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
	case GizmoAxis::Screen: return QStringLiteral("screen");
	default: return QString();
	}
}

bool RotationHandle::screenDistance(const QPointF& cursor, float& distancePx, float& facing) const
{
	distancePx = -1.0f;
	facing = 0.0f;
	const GizmoPickView &view = gizmo->pickView();
	if (!view.isValid()) return false;

	const float radius = ringRadius * handleScale * gizmo->getGizmoScale();
	if (!(radius > 0.0f)) return false;

	// THE RING AS IT IS DRAWN. gizmomeshes::rotationRing builds the unit circle
	// in the plane spanned by (U, V) of the axis frame, i.e. the plane whose
	// normal is this handle's `plane`; drawItems renders it under the gizmo's
	// transform scaled by handleScale * gizmoScale. Both are read from the same
	// two calls here, so the circle measured IS the circle on screen.
	const iris::Mat4 t = ringFrame();
	const iris::Vec3 centre = t.column(3).toVector3D();
	iris::Vec3 u, v, n;
	switch (axis) {
	case GizmoAxis::X: n = iris::Vec3(1, 0, 0); u = iris::Vec3(0, 1, 0); v = iris::Vec3(0, 0, 1); break;
	case GizmoAxis::Y: n = iris::Vec3(0, 1, 0); u = iris::Vec3(0, 0, 1); v = iris::Vec3(1, 0, 0); break;
	// Z and Screen are both the XY circle of their own frame — which for the
	// screen ring is the camera-facing one ringFrame() just built.
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
	// THE SCREEN RING'S ANGLE IS A SCREEN ANGLE (GIZMO-1 item 2). There is no
	// 3D construction to condition badly: the ring is a circle on the display,
	// and the quantity the drag differences is the cursor's angle around its
	// centre. The sign makes it the MATHS convention (counter-clockwise
	// positive) even though Qt's pixel y points down, and it is negated so that
	// drag()'s `startAngle - hitAngle` comes out as the counter-clockwise turn
	// the cursor made — which about an axis pointing back at the eye is exactly
	// the turn the user watched.
	if (axis == GizmoAxis::Screen) {
		const iris::Vec3 centre = gizmo->getTransform().column(3).toVector3D();
		QPointF centrePx, cursorPx;
		if (!gizmo->projectToPixel(centre, centrePx)) return false;
		if (!gizmo->rayPixel(rayPos, rayDir, centre, cursorPx)) return false;
		const double dx = cursorPx.x() - centrePx.x();
		const double dy = -(cursorPx.y() - centrePx.y());
		if (dx * dx + dy * dy < 1e-6) return false;       // dead on the centre
		angle = -float(qRadiansToDegrees(std::atan2(dy, dx)));
		return true;
	}

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
	handles[kScreenHandle] = new RotationHandle(this, GizmoAxis::Screen);

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
	handleMeshes.append(GizmoMeshes::screenRing());

	// THE DRAG MARKER (GIZMO-2 item 3): built once beside the rings, drawn only
	// while a ring is being dragged. The screen ring gets a hub and no arrow —
	// its axis is the direction the camera looks, so an arrow along it would
	// project to a point.
	for (int i = 0; i < 4; ++i) {
		hubMeshes.append(GizmoMeshes::dragHub(handles[i]->axis));
		arrowMeshes.append(handles[i]->axis == GizmoAxis::Screen
		                       ? iris::MeshPtr()
		                       : GizmoMeshes::dragArrow(handles[i]->axis));
	}
}

bool RotationGizmo::isDragging()
{
	return dragging;
}

void RotationGizmo::startDragging(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir)
{
	dragging = false;                 // so the hit test below still refreshes
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
	// RE-ORIENT AT RELEASE (§345): the frozen frame is dropped here, so the
	// rings snap to the node's new rotation the moment the button comes up.
	dragging = false;
	draggedHandle = nullptr;

	// undo-redo
	createUndoAction();
	// AFTER the undo action, never before: createUndoAction puts the node back
	// to where the drag started and re-applies the new transform through the
	// command stack, so the node's FINAL rotation only exists once it returns.
	refreshFrame();
}

void RotationGizmo::drag(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir)
{
	//qDebug() << "dragging";
	if (draggedHandle == nullptr) {
		//dragging = false;
		return;
	}
	//qDebug()<<"sliding";
	float hitAngle = 0.0f;
	// No answer this frame (the cursor dead on the centre pixel, a projection
	// failure, the camera flown behind the gizmo mid-drag): hold still rather
	// than feed an unset angle into the node — the plane drag's grazing guard.
	if (!draggedHandle->getHitAngle(rayPos, rayDir, hitAngle)) return;

	// move node along line
	// do snapping here as well
	auto diff = startAngle - hitAngle;
	auto mods = QApplication::keyboardModifiers();
	if (mods.testFlag(Qt::ControlModifier)) {
		diff = Gizmo::snap(diff, SnapSettings::rotateSize());
	}

	// THE SCREEN RING (GIZMO-1 item 2) turns the node about the direction the
	// camera looks, and that is the SAME rotation in either transform space: a
	// turn about a world axis does not care which frame the OTHER handles are
	// expressed in. The axis is brought into the node's PARENT frame, which is
	// the frame setLocalRot writes in, so a rotated parent cannot skew it.
	if (draggedHandle->axis == GizmoAxis::Screen) {
		iris::Vec3 axis = -draggedHandle->screenAxis;       // points back at the eye
		if (axis.isNull()) return;
		if (auto parent = selectedNode->getParent())
			axis = parent->getGlobalRotation().normalized().conjugated().rotatedVector(axis);
		const iris::Quat rot = iris::Quat::fromAxisAndAngle(axis.normalized(), diff);
		selectedNode->setLocalRot((rot * nodeStartRot).normalized());
		applyGroupDelta();
		if (services && services->sceneEdit) services->sceneEdit->notifyTransformChanged();
		return;
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
		default:
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
	refreshFrame();

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
	// THE OUTER RING LOSES EVERY TIE (GIZMO-1 item 2). It frames the three axis
	// rings and crosses them wherever one is seen edge-on, and the axis ring is
	// what the user is reaching for there — so it only wins when it is CLEARLY
	// the nearer circle, by more than the tie band.
	{
		float d = -1.0f, facing = 0.0f;
		// ...and the tie band only applies against an axis ring that is itself
		// inside the tolerance: an axis ring at 8 px must not veto the outer
		// ring at 6.5 px and then fail the 7 px test itself, leaving a 2 px
		// sliver where nothing picks (second reader, GIZMO-1).
		if (handles[kScreenHandle]->screenDistance(cursor, d, facing) &&
		    (nearest == nullptr || nearestDist > kRingPickTolerancePx ||
		     d < nearestDist - kRingPickTiePx)) {
			nearest = handles[kScreenHandle];
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
	refreshFrame();
	QPointF cursor;
	if (!rayPixel(rayPos, rayDir, trans.column(3).toVector3D(), cursor)) return nullptr;
	float distancePx = -1.0f;
	auto* handle = ringAtPixel(cursor, distancePx);
	if (handle) handle->getHitAngle(rayPos, rayDir, hitAngle);
	return handle;
}

// THE ONE FRAME EVERYTHING READS. Drawing and picking both go through
// getTransform(), and getTransform() answers `trans` — which refreshFrame()
// refuses to move while a drag is running. That is the whole of the freeze:
// there is no second path that could read the live node mid-drag (there were
// two before §345 — drawItems called Gizmo::getTransform() directly, and both
// pick entry points re-read it on every mouse move).
void RotationGizmo::refreshFrame()
{
	if (dragging) return;
	trans = Gizmo::getTransform();
	// The screen ring's own axis comes from the camera the gizmo is picked
	// through — the SAME camera drawItems is handed a view direction for — and
	// it freezes with everything else, so the outer ring cannot slide under the
	// cursor mid-drag either.
	const GizmoPickView &view = pickView();
	if (view.isValid())
		handles[kScreenHandle]->screenAxis =
			view.camera->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1)).normalized();
}

iris::Mat4 RotationGizmo::getTransform()
{
	return trans;
}

void RotationGizmo::setTransformSpace(GizmoTransformSpace transformSpace)
{
	this->transformSpace = transformSpace;
	refreshFrame();
}

void RotationGizmo::setSelectedNode(iris::SceneNodePtr node)
{
	selectedNode = node;
	refreshFrame();
}

QVector<GizmoDrawItem> RotationGizmo::drawItems(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir)
{
	QVector<GizmoDrawItem> items;
	if (!selectedNode) return items;
	refreshFrame();
	// The screen ring's axis normally comes from the pick view (refreshFrame).
	// A caller that draws without ever picking — an overlay test, a stand-in
	// viewport — still hands a view direction here, so take it when there is no
	// pick view to read. Never while dragging: the frame is frozen.
	if (!dragging && !viewDir.isNull() && !pickView().isValid())
		handles[kScreenHandle]->screenAxis = viewDir.normalized();

	const QColor highlight(255, 255, 0);
	// THE DRAWN FRAME, per handle: the gizmo's own for an axis ring, the
	// camera-facing one for the screen ring — the same frames picking measures.
	const auto itemFor = [&](int i, const QColor &colour) {
		iris::Mat4 t = handles[i]->ringFrame();
		t.scale(getGizmoScale() * handles[i]->handleScale);
		return GizmoDrawItem{ handleMeshes[i], t, colour };
	};
	if (dragging) {
		// WHAT A TURN LOOKS LIKE WHILE IT IS HAPPENING (GIZMO-2 item 3, owner
		// §366/§371 — Unreal's shape): the ring being dragged, highlighted as
		// before, plus a small disc at the centre and a line with an arrowhead
		// running out along that ring's AXIS to the ring's radius, in the
		// ring's own colour (X red, Y green, Z blue, the screen ring grey) so
		// the axis the object is turning about is readable at a glance. All
		// three ride `trans` — the frame frozen at startDragging — through
		// ringFrame(), so the marker cannot slide under the cursor either, and
		// all of it is gone at release.
		for (int i = 0; i < 4; i++) {
			if (handles[i] != draggedHandle) continue;
			items.append(itemFor(i, highlight));
			const QColor axisColour = handles[i]->getHandleColor();
			iris::Mat4 t = handles[i]->ringFrame();
			t.scale(getGizmoScale() * handles[i]->handleScale);
			if (!hubMeshes[i].isNull()) items.append({ hubMeshes[i], t, axisColour });
			if (!arrowMeshes[i].isNull()) items.append({ arrowMeshes[i], t, axisColour });
		}
		return items;
	}
	float hitAngle = 0.0f;
	auto hitHandle = getHitHandle(rayPos, rayDir, hitAngle);
	// The three axis rings first, then the outer screen ring ON TOP of them —
	// it is the handle that frames the others, and it is the one a tie goes
	// against, so drawing it last is what makes the picture agree with the pick.
	for (int i = 0; i < 4; i++)
		items.append(itemFor(i, handles[i] == hitHandle ? highlight : handles[i]->getHandleColor()));
	return items;
}
