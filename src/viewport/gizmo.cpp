/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/vec.h"
#include "viewport/gizmo.h"

#include "irisgl/irisgl.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "commands/transformscenenodecommand.h"
#include <QUndoStack>


Gizmo::Gizmo()
{
	transformSpace = GizmoTransformSpace::Local;
	gizmoScale = 1.0f;
}

// Screen-constant gizmo sizing.
//
// THE CONTRACT (owner requirement, 2026-09-08): the gizmo is the SAME SIZE ON
// SCREEN in every scene, on every window shape, and it never changes size when
// the camera dollies in or out — Unreal's and Maya's behaviour. Everything
// below exists to make that one sentence true.
//
// The maths: a world segment of length L, perpendicular to the view, at
// distance d, covers
//
//     L / (2 * d * tan(vfov / 2))
//
// of the frame's height. Making that a CONSTANT means making L proportional to
// d * tan(vfov/2), which is exactly what this function computes — the k below
// is the constant of proportionality, so the fraction it produces is
// independent of BOTH the distance and the angle of view. tests/gizmo's
// gizmo.screen_size suite measures the projected fraction across distances
// 2/5/20, angles 30/45/75 and aspects 16:9 and 2.4:1: invariant to the last
// digit across distance, and identical across window shapes at a matched
// rendered angle (the rest is perspective, and is bounded there).
//
// THREE BUGS IN ONE LINE, all now fixed:
//
//  1. DEGREES INTO qTan (2016-2026-09-07). `camera->angle` is the vertical
//     angle of view in DEGREES (cameranode.h: "Degrees are always used
//     internally"), and qTan takes RADIANS. At the default 45 the expression
//     evaluated tan(22.5 rad) = +0.557 — a positive number by pure luck, which
//     is why the gizmo looked roughly right forever. At fov 75 it evaluates
//     tan(37.5 rad) = -0.199: gizmoScale goes NEGATIVE, every handle transform
//     is mirrored (the visual flip pushTransform launders into a 180-degree
//     rotation) and every hit radius — `gizmo->getGizmoScale() * handleScale`
//     in the three gizmos' isHit — goes negative too, so no arrow can be picked
//     at all.
//  2. IT DIVIDED. Screen-constant sizing means the gizmo's world size must GROW
//     with the angle of view: the same on-screen fraction of a wider frustum is
//     more world units. `d / tan(fov/2)` shrinks it instead.
//  3. IT READ THE AUTHORED ANGLE, NOT THE RENDERED ONE (the 2026-09-08 half of
//     the owner report: "the gizmo is huge on the four silver balls"). A FREE
//     camera on a window wider than its framing aspect is DRAWN at a narrowed
//     vertical angle (freecamerapolicy.h), so `camera->angle` is not the
//     frustum on screen: in the Grand Showroom's 75-degree camera the picture
//     was 59 degrees tall on a 2.4:1 window while the gizmo sized itself for
//     75, i.e. 1.35x too big — and the same mismatch made the gizmo's world
//     size disagree with the pick rays cast through the rendered frustum.
//     `effectiveFovDegrees()` IS the rendered angle (it is what
//     updateCameraMatrices projects with), so the two can no longer diverge.
//     Never read `camera->angle` here again.
//
// The k: world size = distance * tan(half angle) * k, with k chosen so the
// default 45-degree camera keeps EXACTLY the pre-2026-09-07 look: the old
// expression gave 1/tan(22.5 rad) = 1.7935 * d, and 4.33 * tan(22.5 deg) =
// 4.33 * 0.41421 = 1.7935. So nothing moves at 45 and every other fov becomes
// correct instead of arbitrary.
void Gizmo::updateSize(iris::CameraNodePtr camera)
{
	if (!!selectedNode) {
		if (camera->getProjection() == iris::CameraProjection::Perspective) {
			float distToCam = (selectedNode->getGlobalPosition() - camera->getGlobalPosition()).length();
			// THE ANGLE THAT IS ACTUALLY RENDERED, never the authored one
			// (bug 3 above). With no framing hold, and on any window at or
			// below the hold aspect, this IS `camera->angle`, bit for bit.
			//
			// Guard the ends of the range the way Ogre does (setFOVy is clamped
			// to (0, 180) upstream): a zero or 180-degree angle has no tangent.
			const float fovDeg = qBound(1.0f, camera->effectiveFovDegrees(), 179.0f);
			gizmoScale = kGizmoScreenFraction * distToCam
			           * qTan(qDegreesToRadians(fovDeg * 0.5f));
		}
		else {
			// ORTHOGRAPHIC: there is no distance and no angle — the frame's
			// height IS 2 * orthoSize at every depth, so a fixed multiple of it
			// is already screen-constant. Unchanged since 2016 on purpose.
			gizmoScale = camera->orthoSize * 5.0;
		}
	}
}

float Gizmo::getGizmoScale()
{
	return gizmoScale;
}

// ---- PIXEL-SPACE PICKING (smoke S15) ---------------------------------------
//
// The camera's matrices are brought in step with the widget size HERE, the way
// ScenePicker::screenSegment does it before every pick ray: the projection a
// handle is measured through and the frustum a ray is unprojected from are then
// the same matrices by construction, and a picture-vs-pick disagreement (the
// class of bug the 2026-09-08 fov work chased) cannot come back through this
// door.
void Gizmo::setPickView(const iris::CameraNodePtr &camera, float width, float height,
                        float devicePixelRatio)
{
	pickViewData.camera = camera;
	pickViewData.width = width;
	pickViewData.height = height;
	pickViewData.devicePixelRatio = devicePixelRatio > 0.0f ? devicePixelRatio : 1.0f;
	if (!pickViewData.isValid()) return;
	camera->setAspectRatio(width / height);
	camera->updateCameraMatrices();
}

bool Gizmo::projectToPixel(const iris::Vec3 &world, QPointF &pixel) const
{
	if (!pickViewData.isValid()) return false;
	const iris::CameraNodePtr &cam = pickViewData.camera;
	const iris::Vec4 clip = (cam->projMatrix * cam->viewMatrix) *
	                        iris::Vec4(world.x(), world.y(), world.z(), 1.0f);
	if (clip.w() <= 1e-6f) return false;             // behind the eye
	const float ndcX = clip.x() / clip.w();
	const float ndcY = clip.y() / clip.w();
	// NDC y points UP, Qt's pixel y points DOWN.
	pixel = QPointF(double((ndcX * 0.5f + 0.5f) * pickViewData.width),
	                double((1.0f - (ndcY * 0.5f + 0.5f)) * pickViewData.height));
	return true;
}

bool Gizmo::rayPixel(const iris::Vec3 &rayPos, const iris::Vec3 &rayDir,
                     const iris::Vec3 &reference, QPointF &pixel) const
{
	if (!pickViewData.isValid()) return false;
	iris::Vec3 dir = rayDir;
	if (dir.lengthSquared() < 1e-12f) return false;
	dir.normalize();
	float along = iris::Vec3::dotProduct(reference - rayPos, dir);
	if (!(along > 1e-3f)) along = 1.0f;   // the reference is behind the ray's start
	return projectToPixel(rayPos + dir * along, pixel);
}

void Gizmo::setTransformSpace(GizmoTransformSpace transformSpace)
{
	this->transformSpace = transformSpace;
}

void Gizmo::setSelectedNode(iris::SceneNodePtr node)
{
	selectedNode = node;
}

void Gizmo::clearSelectedNode()
{
	selectedNode.clear();
}

void Gizmo::setGroup(const QList<iris::SceneNodePtr> &nodes)
{
	group.clear();
	for (const auto &n : nodes) if (n) group.append(n);
	if (group.size() < 2) group.clear();      // a group of one is not a group
}

void Gizmo::setInitialTransform()
{
	oldPos = selectedNode->getLocalPos();
	oldRot = selectedNode->getLocalRot();
	oldScale = selectedNode->getLocalScale();
	captureGroupStart();
}

void Gizmo::captureGroupStart()
{
	groupStart.clear();
	if (group.isEmpty() || !selectedNode) return;
	pivotStartPos   = selectedNode->getGlobalPosition();
	pivotStartRot   = selectedNode->getGlobalRotation().normalized();
	pivotStartScale = selectedNode->getLocalScale();
	for (const auto &node : group) {
		if (!node) continue;
		MemberStart m;
		m.node       = node;
		m.globalPos  = node->getGlobalPosition();
		m.globalRot  = node->getGlobalRotation().normalized();
		m.localPos   = node->getLocalPos();
		m.localRot   = node->getLocalRot();
		m.localScale = node->getLocalScale();
		groupStart.append(m);
	}
}

void Gizmo::applyGroupDelta()
{
	if (groupStart.isEmpty() || !selectedNode) return;

	// The delta the PRIMARY just took, in global terms.
	const iris::Vec3 deltaPos = selectedNode->getGlobalPosition() - pivotStartPos;
	const iris::Quat deltaRot =
		(selectedNode->getGlobalRotation().normalized() * pivotStartRot.conjugated()).normalized();
	// SCALE stays a LOCAL ratio: there is no setGlobalScale, and a member's
	// parent scale is constant through the drag (D5 guarantees no ancestor of a
	// member is in the group), so the local ratio IS the global one.
	const iris::Vec3 s0 = pivotStartScale;
	const iris::Vec3 scaleRatio(
		qFuzzyIsNull(s0.x()) ? 1.0f : selectedNode->getLocalScale().x() / s0.x(),
		qFuzzyIsNull(s0.y()) ? 1.0f : selectedNode->getLocalScale().y() / s0.y(),
		qFuzzyIsNull(s0.z()) ? 1.0f : selectedNode->getLocalScale().z() / s0.z());

	for (const MemberStart &m : groupStart) {
		if (!m.node || m.node.data() == selectedNode.data()) continue;   // the subclass wrote the primary

		// pos = pivot + ΔR · (s ∘ (start − pivot)) + Δpos
		//   translate: ΔR = I, s = 1  -> start + Δpos
		//   rotate:    Δpos = 0, s = 1 -> orbit around the primary's pivot
		//   scale:     Δpos = 0, ΔR = I -> scale about the primary's pivot
		iris::Vec3 offset = m.globalPos - pivotStartPos;
		offset = iris::Vec3(offset.x() * scaleRatio.x(),
		                    offset.y() * scaleRatio.y(),
		                    offset.z() * scaleRatio.z());
		offset = deltaRot.rotatedVector(offset);
		m.node->setGlobalPos(pivotStartPos + offset + deltaPos);
		m.node->setGlobalRot((deltaRot * m.globalRot).normalized());
		m.node->setLocalScale(iris::Vec3(m.localScale.x() * scaleRatio.x(),
		                                 m.localScale.y() * scaleRatio.y(),
		                                 m.localScale.z() * scaleRatio.z()));
	}
}

void Gizmo::createUndoAction()
{
	// ONE UNDO STEP for the whole group (EDITOR_MULTISELECT_SPEC §2.4): every
	// member is put back to where the drag started and re-applied through its
	// own TransformSceneNodeCommand, all inside one macro. N = 1 keeps today's
	// stack shape exactly — a single command, no macro.
	if (!groupStart.isEmpty()) {
		QUndoStack *stack = (services && services->undo) ? services->undo->stack() : nullptr;
		struct Applied { iris::SceneNodePtr node; iris::Vec3 pos, scale; iris::Quat rot; };
		QVector<Applied> applied;
		for (const MemberStart &m : groupStart) {
			if (!m.node) continue;
			applied.append({ m.node, m.node->getLocalPos(), m.node->getLocalScale(),
			                 m.node->getLocalRot() });
			m.node->setLocalPos(m.localPos);
			m.node->setLocalRot(m.localRot);
			m.node->setLocalScale(m.localScale);
		}
		if (services && services->undo) {
			const bool macro = applied.size() > 1 && stack;
			if (macro) stack->beginMacro(QObject::tr("Transform %1 objects").arg(applied.size()));
			for (const Applied &a : applied)
				services->undo->push(new TransformSceneNodeCommand(a.node, a.pos, a.rot, a.scale));
			if (macro) stack->endMacro();
		}
		groupStart.clear();
		return;
	}

	auto newPos = selectedNode->getLocalPos();
	auto newRot = selectedNode->getLocalRot();
	auto newScale = selectedNode->getLocalScale();

	selectedNode->setLocalPos(oldPos);
	selectedNode->setLocalRot(oldRot);
	selectedNode->setLocalScale(oldScale);
	if (services && services->undo)
		services->undo->push(new TransformSceneNodeCommand(selectedNode, newPos, newRot, newScale));
}

iris::Vec3 Gizmo::snap(iris::Vec3 pos, float gridSize)
{
	return iris::Vec3(Gizmo::snap(pos.x(), gridSize),
					 Gizmo::snap(pos.y(), gridSize),
					 Gizmo::snap(pos.z(), gridSize));
}

float Gizmo::snap(float value, float gridSize)
{
	return qFloor(value / gridSize)*gridSize;
}

// returns transform of the gizmo, not the scene node
// the transform is calculated based on the transform's space (local or global)
iris::Mat4 Gizmo::getTransform()
{
	if (!selectedNode) {
		iris::Mat4 mat;
		mat.setToIdentity();
		return mat;
	}

	if (transformSpace == GizmoTransformSpace::Global) {
		iris::Mat4 trans;
		trans.setToIdentity();
		trans.translate(selectedNode->getGlobalPosition());
		return trans;
	}
	else {
		//todo: remove scale
		iris::Mat4 trans;
		trans.setToIdentity();
		trans.translate(selectedNode->getGlobalPosition());
		trans.rotate(selectedNode->getGlobalRotation().normalized());
		return trans;
	}
}

bool Gizmo::isHit(iris::Vec3 rayPos, iris::Vec3 rayDir)
{
	return false;
}