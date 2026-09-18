/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef GIZMOHANDLE_H
#define GIZMOHANDLE_H

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QtMath>
#include <QColor>
#include <QList>
#include <QPointF>
#include <QString>
#include <Qt>
#include <QVector>
#include "irisgl/irisglfwd.h"


enum class GizmoAxis
{

	Center,
	X,
	Y,
	Z,
	/// THE VIEW DIRECTION (GIZMO-1 item 2, owner report §345). The rotation
	/// gizmo's outer grey ring is a real handle: it turns the node about the
	/// axis the camera looks along, and its angle is simply the cursor's angle
	/// around the circle on screen — well conditioned everywhere, because it
	/// is a 2D quantity.
	Screen,
	/// THE TRANSLATE GIZMO'S PLANE HANDLES (GIZMO-1 item 3, owner report §346:
	/// "it helps with the spatial connection for the user"). Each is a small
	/// square drawn IN the plane its two axes span, and dragging it slides the
	/// node across that plane.
	XYPlane,
	YZPlane,
	XZPlane
};

enum class AxisHandle
{
	Center = 0,
    X,
    Y,
    Z
};

enum class GizmoTransformMode
{
    Translate,
    Rotate,
    Scale
};

enum class GizmoTransformSpace
{
    Local,
    Global
};

enum class GizmoTransformAxis
{
    NONE,
	Center,
    X,
    Y,
    Z,
    XY,
    XZ,
    YZ
};

enum class GizmoPivot
{
    CENTER,
    OBJECT_PIVOT
};

class GizmoHandle
{
private:

    QColor      handleColor;
    QString     handleName;
    iris::Vec3   handlePosition;
    iris::Vec3   handleScale;
    iris::Quat handleRotation;

public:

    GizmoHandle()
    {

    }

    void setHandleColor(const QColor& color) {
        this->handleColor = color;
    }

    QColor getHandleColor() const {
        return this->handleColor;
    }

    void setHandleScale(const iris::Vec3& scale) {
        this->handleScale = scale;
    }

    iris::Vec3 getHandleScale() const {
        return this->handleScale;
    }

    void setHandleName(const QString& name) {
        this->handleName = name;
    }

    QString getHandleName() const {
        return this->handleName;
    }
};

/// What a gizmo would draw this frame, as data: a mesh, its world transform and a
/// flat colour. The engine viewport
/// turns these into on-top overlay items (VIEWPORT_MIGRATION_PLAN.md step 8).
struct GizmoDrawItem
{
    iris::MeshPtr mesh;
    iris::Mat4 transform;
    QColor colour;
};

struct StudioServices;

/// The calibration constant of Gizmo::updateSize (see the long note there).
/// gizmoScale = k * distance * tan(EFFECTIVE fov/2) keeps the gizmo the same
/// fraction of the frame at every angle of view, every window shape and every
/// distance; k is fixed at the value that reproduces the pre-2026-09-07 look at
/// the default 45-degree camera EXACTLY. The fov must be the camera's
/// effectiveFovDegrees() — the angle RENDERED — and never the authored `angle`
/// (the 2026-09-08 "the gizmo is huge in the Showroom" report).
constexpr float kGizmoScreenFraction = 4.33f;

/// THE EYE THE VR GIZMO IS SIZED FOR, vertical degrees (VR_INPUT_SPEC §5.2,
/// the owner's decision 5: "fixed angular size").
///
/// A FIXED number, deliberately. The runtime's real per-eye fov is not reported
/// by the engine today (`VrStatus` carries ipd, eyeWidth/eyeHeight and
/// `asymmetricFov`, but no angle), and a gizmo whose apparent size changed with
/// the headset would not be a FIXED angular size in any case. 90 degrees is the
/// spec's own nominal; it is an ARGUMENT everywhere below so that the day a
/// session reports its fov, exactly one call site changes.
constexpr float kVrNominalEyeFovDegrees = 90.0f;

/// WHICH UNIT A PICK IS MEASURED IN (VR_INPUT_SPEC §5.2, phase 4b stage 2).
///
/// Pixel is the desk: a cursor, a camera and a widget, and the rotation rings
/// and plane handles measured on screen (smoke S15, GIZMO-1 item 3). Ray is the
/// headset: no cursor, no camera and no widget — the pointer is a world-space
/// ray and the same questions are asked as ANGLES at its origin (gizmoray.h).
/// The drag maths is not in this enum and never will be: both paths hand the
/// same ray to the same constraint arithmetic.
enum class GizmoPickSpace
{
    Pixel,
    Ray
};

/// WHAT THE WEARER IS POINTING WITH, and where they are looking from — the VR
/// half of GizmoPickView. Armed by the VR interaction every frame a session
/// drives this gizmo (VrInteraction::step), cleared when it stops.
///
/// `eye` is the head in world space, or — with no session at all, which is how
/// every gesture is gated headlessly — the aiming hand itself. It is what the
/// SIZE rule measures from, and it is why an armed VR pick takes the gizmo's
/// size away from the desktop camera (see Gizmo::updateSize).
struct GizmoVrPick
{
    bool valid = false;
    iris::Vec3 eye;
    float fovDegrees = kVrNominalEyeFovDegrees;
    iris::Vec3 rayPos, rayDir, viewDir;
};

/// WHAT THE VIEWPORT IS SHOWING, in the units a mouse event speaks (smoke
/// S15). A gizmo that picks in PIXELS — which the rotation gizmo now does,
/// because a ring seen edge-on has no 3D annulus left to hit — needs the
/// camera it is drawn through and the size of the widget it is drawn in.
/// `width`/`height` are LOGICAL (device-independent) pixels, i.e. exactly
/// `QWidget::width()`/`height()` and exactly the units `QMouseEvent::position`
/// carries, so a pixel TOLERANCE expressed in them is the same physical size
/// on a HiDPI screen as on a plain one; `devicePixelRatio` is carried for the
/// callers that need the physical count (never for the tolerance).
struct GizmoPickView
{
    iris::CameraNodePtr camera;
    float width = 0.0f;
    float height = 0.0f;
    float devicePixelRatio = 1.0f;
    bool isValid() const { return !camera.isNull() && width >= 2.0f && height >= 2.0f; }
};

class Gizmo
{
protected:
	/// Undo pushes and transform-refresh notifications go through the
	/// services (Phase 4: was UiManager's statics). Nullable in tests.
	StudioServices* services = nullptr;

	iris::SceneNodePtr selectedNode;
	GizmoTransformSpace transformSpace;
	float gizmoScale;

	iris::Vec3 oldPos, oldScale;
	iris::Quat oldRot;

	/// The view the gizmo is currently drawn (and picked) in. Invalid until a
	/// viewport sets it — a gizmo with no pick view picks nothing, which is
	/// what a document-only stand-in wants.
	GizmoPickView pickViewData;

	/// THE WEARER'S POINTER, when one is driving this gizmo (stage 2). While it
	/// is valid the gizmo is sized for the HEAD and drawn with the controller's
	/// own highlight; the desk keeps picking in pixels, because its own presses
	/// run outside a RayPickScope.
	GizmoVrPick vrPickData;
	/// The unit THIS call measures in. Pixel unless a caller says otherwise —
	/// the desktop's presses, drags and screenshots are untouched by stage 2.
	GizmoPickSpace pickSpaceNow = GizmoPickSpace::Pixel;
	/// A GESTURE THAT LOST ITS INPUT IS NOT A TRANSFORM (VR_INPUT_SPEC §5.4:
	/// focus loss cancels). Set for the length of cancelDragging() and read by
	/// createUndoAction, which then puts everything back and pushes NOTHING —
	/// an empty command on the stack would eat the user's next Ctrl+Z (the
	/// lesson the group path records below).
	bool cancelPending = false;

	// ---- GROUP TRANSFORM (EDITOR_MULTISELECT_SPEC §2.4) -------------------
	//
	// The gizmo still belongs to the PRIMARY: it sits at its pivot, hit-tests
	// against it and sizes itself from it, and the three subclasses go on
	// writing exactly that one node. The group is applied AFTER them, as a
	// DELTA read back off the primary — translate is the primary's snapped
	// move applied to everyone, rotate and scale are the primary's delta about
	// the primary's own pivot. That way snapping, axis constraints and the
	// handle maths stay in one place and there is no second implementation of
	// them to drift.
	struct MemberStart
	{
		iris::SceneNodePtr node;
		iris::Vec3 globalPos;
		iris::Quat globalRot;
		iris::Vec3 localPos, localScale;
		iris::Quat localRot;
	};
	/// The D5-reduced selection (the primary included when it is in it).
	/// Empty = single-node drag, i.e. exactly the pre-multiselect behaviour.
	QList<iris::SceneNodePtr> group;
	QVector<MemberStart> groupStart;
	iris::Vec3 pivotStartPos, pivotStartScale;
	iris::Quat pivotStartRot;
	/// The modifiers the current gesture is driven with (setDragModifiers).
	Qt::KeyboardModifiers dragModifiers = Qt::NoModifier;

	/// Snapshots every member's start transform (called by setInitialTransform,
	/// which every subclass's startDragging already calls).
	void captureGroupStart();

	/// THE RAY A DRAW SHOULD USE. The desktop hands its mouse ray to
	/// drawItems(); while a VR pick is armed the wearer's aim replaces it, so
	/// the highlight under the controller is the highlight everybody sees —
	/// there is one gizmo and one picture of it.
	void resolvePickRay(iris::Vec3 &rayPos, iris::Vec3 &rayDir, iris::Vec3 &viewDir) const;

public:
	/// Applies the primary's delta to the rest of the group. Called at the end
	/// of each subclass's drag() — and PUBLIC because it is the whole of the
	/// group-transform maths, which gizmo.group_transform drives directly
	/// (a synthetic ray that hits a handle is a test of the handle geometry,
	/// not of this).
	void applyGroupDelta();

	Gizmo();
	void setServices(StudioServices* s) { services = s; }
	/// THE DESKTOP'S SIZE RULE — and it STANDS DOWN while a VR pick is armed
	/// (see setVrPick): one gizmo cannot be two sizes at once, and while a
	/// wearer is driving it the wearer's constant angular size is the one that
	/// matters. The desk goes on drawing and dragging the same object, which is
	/// what keeps its picture and its pick in agreement.
	virtual void updateSize(iris::CameraNodePtr camera);
	/// THE VR SIZE RULE (VR_INPUT_SPEC §5.2): a CONSTANT ANGULAR size,
	/// `kGizmoScreenFraction * distance(eye, pivot) * tan(fov/2)` —
	/// gizmoray::vrGizmoScale, which is the desktop expression evaluated at the
	/// eye instead of at a document camera.
	void updateSizeForVr(const iris::Vec3 &eye,
	                     float fovDegrees = kVrNominalEyeFovDegrees);
	float getGizmoScale();

	// ---- THE WEARER'S POINTER (VR_INPUT_SPEC §5.2, stage 2) --------------
	/// Arms (or, with `valid` false, disarms) the VR pick: the size rule, the
	/// ray the gizmo highlights itself from, and the view direction its
	/// camera-facing rules use. Sizing happens HERE, so a caller that arms it
	/// every frame needs no second call.
	void setVrPick(const GizmoVrPick &pick);
	const GizmoVrPick &vrPick() const { return vrPickData; }
	bool vrPickArmed() const { return vrPickData.valid; }

	/// THE UNIT THIS CALL MEASURES IN. Callers do not set it by hand: the VR
	/// interaction wraps its hit tests and drags in a RayPickScope, and
	/// everything else is the desk's pixels.
	GizmoPickSpace pickSpace() const { return pickSpaceNow; }
	bool rayPicking() const { return pickSpaceNow == GizmoPickSpace::Ray; }
	/// THE DIRECTION A CAMERA-FACING RULE IS JUDGED ALONG IN VR: from the
	/// wearer's EYE TO THE GIZMO — the radial direction — and not the eye's
	/// forward, which is what the desktop uses. Null (and false) when no VR
	/// pick is armed.
	///
	/// WHY THEY DIFFER, and why this one is right for a headset. Both rules cut
	/// every ring with ONE plane through the gizmo's centre (Blender's "three
	/// arcs of a single sphere"), so the picture reads the same; what changes
	/// is the plane's normal. On a monitor the two are nearly the same
	/// direction — a 45-degree frustum puts everything within 22 degrees of the
	/// view axis — so Blender's choice of the view axis costs nothing there. A
	/// wearer's field of view is 90 degrees and more, and their hand points
	/// where their head is not: judging a gizmo 40 degrees off the gaze by the
	/// gaze's own direction cuts its rings as if it were somewhere else, and
	/// the half the wearer is looking straight at stops being a handle. The
	/// direction to the eye is the same quantity evaluated at the gizmo, and it
	/// is exact at every angle.
	bool vrLookDirection(const iris::Vec3 &gizmoPosition, iris::Vec3 &look) const;

	/// A pixel tolerance of the nominal frame, as an angle at the armed eye
	/// (gizmoray::toleranceRadians) — the one conversion the two pick paths
	/// share, so the desktop's tuned constants are the VR constants.
	float rayTolerance(float pixels) const;

	/// MEASURE IN ANGLES FOR THE LENGTH OF THIS SCOPE. RAII because a gesture
	/// that threw or returned early with the gizmo left in Ray space would
	/// silently change what the next mouse press picks.
	class RayPickScope
	{
	public:
		explicit RayPickScope(Gizmo *gizmo)
		    : target(gizmo), previous(gizmo ? gizmo->pickSpaceNow : GizmoPickSpace::Pixel)
		{
			if (target) target->pickSpaceNow = GizmoPickSpace::Ray;
		}
		~RayPickScope() { if (target) target->pickSpaceNow = previous; }
		RayPickScope(const RayPickScope &) = delete;
		RayPickScope &operator=(const RayPickScope &) = delete;
	private:
		Gizmo *target;
		GizmoPickSpace previous;
	};

	// THE SNAP MODIFIER IN THE HEADSET is `menu` held (owner answer 10), and it
	// reaches the drags by the SAME door the desk's Ctrl does: the host pushes
	// setDragModifiers(Qt::ControlModifier) while `menu` is down and NoModifier
	// otherwise (SCALE-LOCK-1's one-source rule; the VR-GIZMO-1 merge folded the
	// lane's separate snapHeld() into it).

	/// PUT EVERYTHING BACK AND RECORD NOTHING (VR_INPUT_SPEC §5.4). The drag
	/// ends exactly as endDragging() ends it — the subclass's own bookkeeping,
	/// the frozen frame released — but createUndoAction rewinds instead of
	/// pushing. Safe when no drag is running.
	void cancelDragging();

	/// WHICH HANDLE A RAY IS ON, by the name the verb surface uses ("x",
	/// "xy", "screen", "center", ...), or empty. The three gizmos answer it
	/// from their own hit test, so `vr.gizmo()` and a suite can name what the
	/// wearer is pointing at without synthesizing a press.
	virtual QString handleNameAt(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);

	// ---- PIXEL-SPACE PICKING (smoke S15) ---------------------------------
	//
	// The viewport hands the gizmo the view it is drawn through before every
	// pick ray it casts (EngineSceneViewport::mouseRay), so hit-testing can
	// ask "how far is the cursor from this handle ON SCREEN" instead of
	// intersecting handle geometry that may be edge-on to the camera.
	//
	// setPickView also brings the camera's own matrices in step with that
	// widget size (aspect + updateCameraMatrices, exactly as
	// ScenePicker::screenSegment does before building a ray), so projecting
	// through them and unprojecting a ray through them cannot disagree.
	void setPickView(const iris::CameraNodePtr &camera, float width, float height,
	                 float devicePixelRatio = 1.0f);
	const GizmoPickView &pickView() const { return pickViewData; }
	/// World point -> viewport pixel, Qt's top-left origin. False when there
	/// is no pick view or the point is behind the eye.
	bool projectToPixel(const iris::Vec3 &world, QPointF &pixel) const;
	/// THE PIXEL A PICK RAY CAME FROM. Every point of a pick ray projects to
	/// the same pixel (they are collinear with the eye), so this projects one
	/// of them; `reference` — typically the gizmo's own centre — only chooses
	/// a numerically comfortable one, in front of the eye.
	bool rayPixel(const iris::Vec3 &rayPos, const iris::Vec3 &rayDir,
	              const iris::Vec3 &reference, QPointF &pixel) const;

	virtual void setTransformSpace(GizmoTransformSpace transformSpace);
	/// The space this gizmo drags in. Read by the viewport for
	/// editor.gizmoSpace and by the toolbar, which used to guess.
	GizmoTransformSpace getTransformSpace() const { return transformSpace; }
	virtual void setSelectedNode(iris::SceneNodePtr node);
	void clearSelectedNode();
	/// IS THERE ANYTHING TO DRAW A GIZMO ON? The viewport already asks this
	/// question of its own selection before drawing or picking (mSelectedNode);
	/// the VR pointer asks it here, because a gizmo with no node still answers
	/// its handle hit tests — against an IDENTITY transform at the world
	/// origin, with whatever scale was last set — and would otherwise report a
	/// handle under a ray that points at nothing at all.
	bool hasSelectedNode() const { return !selectedNode.isNull(); }
	/// The nodes a drag moves TOGETHER (EDITOR_MULTISELECT_SPEC §2.4) — the
	/// D5-reduced selection set. An empty or single-entry list restores the
	/// single-node behaviour exactly.
	void setGroup(const QList<iris::SceneNodePtr> &nodes);
	int groupSize() const { return group.size(); }

	// undo-redo
	void setInitialTransform();
	void createUndoAction();

	static iris::Vec3 snap(iris::Vec3 pos, float gridSize);
	static float snap(float value, float gridSize);

	// returns transform of the gizmo, not the scene node
	// the transform is calculated based on the transform's space (local or global)
	virtual iris::Mat4 getTransform();
	virtual bool isHit(iris::Vec3 rayPos, iris::Vec3 rayDir);

	// ---- THE GESTURE'S MODIFIERS (SCALE-LOCK-1) --------------------------
	//
	// The viewport hands the gizmo the modifiers carried by the very mouse
	// event that is driving the drag, on the press and on every move, for the
	// same reason DragSpinBox reads them off its own events: a key pressed or
	// released mid-drag then takes effect for the remainder of the gesture, and
	// a test can drive a modified drag without a keyboard (QApplication::
	// keyboardModifiers() reads the real one and cannot be synthesised).
	// Today only the scale gizmo reads it — Shift = scale all three axes by
	// this drag's ratio.
	void setDragModifiers(Qt::KeyboardModifiers mods) { dragModifiers = mods; }
	Qt::KeyboardModifiers currentDragModifiers() const { return dragModifiers; }

	virtual bool isDragging() = 0;
	virtual void startDragging(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir) = 0;
	virtual void endDragging() = 0;
	virtual void drag(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir) = 0;

	/// Renderer-independent description of render(). Empty when nothing is selected.
	virtual QVector<GizmoDrawItem> drawItems(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir) = 0;
};

#endif // GIZMOHANDLE_H
