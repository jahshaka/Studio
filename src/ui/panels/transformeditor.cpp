/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/quat.h"
#include "irisgl/document/scenegraph/scalelock.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>

#include "services/extentmeasure.h"
#include <QPushButton>

#include "ui/panels/transformeditor.h"

#include "services/editgate.h"
#include "ui/controls/dragspinbox.h"
#include "commands/nodeeditcommand.h"

#include "irisgl/document/scenegraph/scenenode.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "commands/transformscenenodecommand.h"
#include "ui/style/stylesheet.h"

namespace {
// per-pixel scrub sensitivity
const double kPosScaleStepPerPx = 0.02;
const double kRotStepPerPx = 0.5; // degrees
const int kTitleWidth = 56;
// The lock's share of the fixed-width title cell: a 16 px button holding a
// 12 px icon, 3 px from the label. The label keeps the rest — "Scale" needs
// far less than the 56 px the widest row title does.
const int kLockButtonPx = 16;
const int kLockIconPx = 12;
const int kLockSpacing = 3;

/// The chain-link icon in its two states: full strength when the ratio is
/// locked, a third of it when it is not. ONE pixmap source (the app's
/// link-symbol.svg), so the two states can never drift apart, and the dim half
/// is composited here rather than shipped as a second file or asked for with a
/// stylesheet (raw sheets are forbidden outside src/ui/style).
QIcon lockIcon()
{
    const QIcon source(QStringLiteral(":/icons/link-symbol.svg"));
    const QSize size(kLockIconPx, kLockIconPx);
    QIcon icon;
    const QPixmap on = source.pixmap(size);
    icon.addPixmap(on, QIcon::Normal, QIcon::On);
    // A NULL source (an offscreen test target that carries no resources) stays
    // null rather than producing a 12x12 transparent square that looks like a
    // missing icon.
    if (on.isNull()) return icon;
    QPixmap off(on.size());
    off.setDevicePixelRatio(on.devicePixelRatio());
    off.fill(Qt::transparent);
    {
        QPainter p(&off);
        p.setOpacity(0.35);
        p.drawPixmap(0, 0, on);
    }
    icon.addPixmap(off, QIcon::Normal, QIcon::Off);
    return icon;
}
}

TransformEditor::TransformEditor(QWidget* parent) :
    QWidget(parent)
{
    setObjectName("TransformEditor");
    // Classic's sheet (its axis identity is a coloured border-left per field);
    // under Qlementine the fields stay the style's own and carry the same
    // colours as a painted strip (DragSpinBox::setAxisColor, addRow).
    setStyleSheet(StyleSheet::TransformEditorPanel());

    auto grid = new QGridLayout(this);
    grid->setContentsMargins(14, 4, 14, 6);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(4);

    // three horizontal rows: title on the left, X/Y/Z side by side
    addRow(grid, 0, "Position", xpos, ypos, zpos, kPosScaleStepPerPx);
    addRow(grid, 1, "Rotation", xrot, yrot, zrot, kRotStepPerPx);
    // The Scale row carries the PRESERVE-RATIO LOCK in its title cell
    // (SCALE-LOCK-1).
    addRow(grid, 2, "Scale",    xscale, yscale, zscale, kPosScaleStepPerPx, true);

    // THE MEASUREMENT (services/extentmeasure.h): what this node actually MEASURES in
    // the scene, in metres. Read-only, and the only number on this panel that
    // is an answer rather than a control — position/rotation/scale describe
    // the transform, this describes the result, which is what "1 unit = 1
    // metre" is about. It is also how a user sees that an imported asset's
    // fit landed (a fitted character reads ~1.75 m tall here).
    sizeLabel = new QLabel(this);
    sizeLabel->setObjectName("sizeLabel");
    sizeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    {
        auto *caption = new QLabel("Size", this);
        caption->setFixedWidth(kTitleWidth);
        caption->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(caption, 3, 0);
        grid->addWidget(sizeLabel, 3, 1, 1, 3);
    }

    resetBtn = new QPushButton("Reset", this);
    resetBtn->setObjectName("resetBtn");
    grid->addWidget(resetBtn, 4, 1, 1, 3);

    adjustSize(); // AccordianBladeWidget sizes the blade from height()

    connect(xpos,   SIGNAL(valueChanged(double)),   SLOT(xPosChanged(double)));
    connect(ypos,   SIGNAL(valueChanged(double)),   SLOT(yPosChanged(double)));
    connect(zpos,   SIGNAL(valueChanged(double)),   SLOT(zPosChanged(double)));

    connect(xrot,   SIGNAL(valueChanged(double)),   SLOT(xRotChanged(double)));
    connect(yrot,   SIGNAL(valueChanged(double)),   SLOT(yRotChanged(double)));
    connect(zrot,   SIGNAL(valueChanged(double)),   SLOT(zRotChanged(double)));

    connect(xscale, SIGNAL(valueChanged(double)),   SLOT(xScaleChanged(double)));
    connect(yscale, SIGNAL(valueChanged(double)),   SLOT(yScaleChanged(double)));
    connect(zscale, SIGNAL(valueChanged(double)),   SLOT(zScaleChanged(double)));

    connect(resetBtn, SIGNAL(clicked(bool)),        SLOT(onResetBtnClicked()));

    if (scaleLockBtn)
        connect(scaleLockBtn, &QToolButton::toggled, this, &TransformEditor::onScaleLockToggled);

    for (auto box : { xpos, ypos, zpos, xrot, yrot, zrot, xscale, yscale, zscale }) {
        connect(box, &DragSpinBox::scrubStarted,  this, &TransformEditor::onScrubStarted);
        connect(box, &DragSpinBox::scrubFinished, this, &TransformEditor::onScrubFinished);
    }

    // SHIFT MEANS UNIFORM ON THE SCALE FIELDS (SCALE-LOCK-1), so it cannot also
    // mean the coarse x10 rate there — a modifier with two meanings in one
    // gesture is a modifier with none. Ctrl's fine x0.1 is untouched, and the
    // position/rotation fields keep both.
    for (auto box : { xscale, yscale, zscale })
        box->setShiftCoarseEnabled(false);
}

void TransformEditor::addRow(QGridLayout* grid, int row, const QString& title,
                             DragSpinBox*& x, DragSpinBox*& y, DragSpinBox*& z,
                             double perPixelStep, bool withLock)
{
    auto label = new QLabel(title, this);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (!withLock) {
        label->setFixedWidth(kTitleWidth);
        grid->addWidget(label, row, 0);
    } else {
        // THE TITLE CELL IS THE SAME WIDTH IT ALWAYS WAS (SCALE-LOCK-1, the
        // owner's shape: the lock sits to the RIGHT of the label and BEFORE
        // the fields). The icon is paid for out of the LABEL's width inside a
        // cell of exactly kTitleWidth, so column 0 does not grow, the three
        // stretch columns keep their share, and the Scale fields land pixel for
        // pixel where the Position fields are. Putting the button in a grid
        // column of its own would have taken that width off the fields.
        auto *cell = new QWidget(this);
        cell->setObjectName(QStringLiteral("scaleTitleCell"));
        cell->setFixedWidth(kTitleWidth);
        auto *cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(kLockSpacing);
        cellLayout->addWidget(label, 1);

        scaleLockBtn = new QToolButton(cell);
        scaleLockBtn->setObjectName(QStringLiteral("scaleLockBtn"));
        scaleLockBtn->setCheckable(true);
        scaleLockBtn->setAutoRaise(true);
        scaleLockBtn->setFocusPolicy(Qt::NoFocus);
        scaleLockBtn->setCursor(Qt::ArrowCursor);
        scaleLockBtn->setFixedSize(kLockButtonPx, kLockButtonPx);
        scaleLockBtn->setIconSize(QSize(kLockIconPx, kLockIconPx));
        // The app's own chain-link icon (app/icons.qrc) — the affordance
        // Unreal and Blender both use for "these numbers move together". No
        // stylesheet: the CHECKED state is the style's own (theme.no_raw_sheets),
        // and the OFF state is the same link drawn at a third of its opacity,
        // which is how the hierarchy's lock column reads too (a dim icon and a
        // filled one).
        scaleLockBtn->setIcon(lockIcon());
        scaleLockBtn->setToolTip(
            QObject::tr("Preserve the scale ratio: a change to one axis scales the other two by "
                        "the same ratio.\nShift-drag a scale field or a gizmo handle to do it "
                        "once without the lock."));
        cellLayout->addWidget(scaleLockBtn, 0);
        grid->addWidget(cell, row, 0);
    }

    const char* suffix = (row == 0) ? "pos" : (row == 1) ? "rot" : "scale";
    x = createField(QString("x") + suffix, perPixelStep);
    y = createField(QString("y") + suffix, perPixelStep);
    z = createField(QString("z") + suffix, perPixelStep);

    if (!StyleSheet::classicThemeActive()) {
        x->setAxisColor(QColor(0xc0, 0x39, 0x2b));
        y->setAxisColor(QColor(0x27, 0xae, 0x60));
        z->setAxisColor(QColor(0x29, 0x80, 0xb9));
    }

    grid->addWidget(x, row, 1);
    grid->addWidget(y, row, 2);
    grid->addWidget(z, row, 3);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);
}

DragSpinBox* TransformEditor::createField(const QString& objectName, double perPixelStep)
{
    auto box = new DragSpinBox(this);
    box->setObjectName(objectName);
    // 4 decimals, not 2: imported models routinely carry root scales like
    // 0.0143 (Sketchfab FBX->glTF conversions); a 2-decimal field cannot even
    // DISPLAY them without lying.
    box->setDecimals(4);
    box->setRange(-1024.0, 1024.0);
    box->setPerPixelStep(perPixelStep);
    box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return box;
}

void TransformEditor::onResetBtnClicked()
{
    // in the future, this should be the imported models defaults instead of assumed scene's
    if (auto sceneNode = editableNode()) {
        // THE NODE, NOT THE ROW CALLBACKS. Reset used to walk the nine
        // valueChanged slots with the values it wanted — which stopped meaning
        // anything for the rotation the moment those slots started reading the
        // FIELDS (they still held the old angles here, so Reset left the
        // rotation exactly as it was). What Reset means is one statement:
        auto scale = defaultStateNode->getLocalScale();
        sceneNode->setLocalPos(iris::Vec3(0, 0, 0));
        sceneNode->setLocalRot(iris::Quat::fromEulerAngles(iris::Vec3(0, 0, 0)));
        sceneNode->setLocalScale(scale);

        // Display-only: the unrounded values went onto the node above; the
        // spinboxes must not echo their ROUNDED copies back (see refreshUi).
        const QSignalBlocker b1(xpos), b2(ypos), b3(zpos);
        const QSignalBlocker b4(xrot), b5(yrot), b6(zrot);
        const QSignalBlocker b7(xscale), b8(yscale), b9(zscale);

        xpos->setValue(0);
        ypos->setValue(0);
        zpos->setValue(0);

        xrot->setValue(0);
        yrot->setValue(0);
        zrot->setValue(0);

        xscale->setValue(scale.x());
        yscale->setValue(scale.y());
        zscale->setValue(scale.z());
    }
}

void TransformEditor::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    this->sceneNode = defaultStateNode = sceneNode;
    // A NEW SELECTION ALWAYS GETS THE CANONICAL TRIPLE (round 2): the memo is
    // "this row built the rotation this node is holding", which is a statement
    // about one node and one gesture. Selecting another node — or the same one
    // again — is neither.
    rotationMemoValid = false;

    if (!!sceneNode) {
		refreshUi();
    }
}

void TransformEditor::refreshUi()
{
	// ui might have a null node
	if (!!sceneNode) {
		// Populating the fields FROM the document must never write back INTO
		// the document. Without the blockers, QDoubleSpinBox::setValue rounds
		// to the field's decimals, and a changed (= rounded) value fires
		// valueChanged straight into set{Pos,Rot,Scale} — selecting a freshly
		// imported model silently stamped the rounded transform onto the node
		// (the double-import "root scale 0.0143 became 0.01" corruption: only
		// the FIRST selection in a panel's life changed the spinbox value, so
		// only the first import was hit). Euler round-trips through the
		// rotation fields corrupted rotations the same way.
		const QSignalBlocker b1(xpos), b2(ypos), b3(zpos);
		const QSignalBlocker b4(xrot), b5(yrot), b6(zrot);
		const QSignalBlocker b7(xscale), b8(yscale), b9(zscale);

		auto pos = sceneNode->getLocalPos();
		xpos->setValue(pos.x());
		ypos->setValue(pos.y());
		zpos->setValue(pos.z());

		// THE ROTATION FIELDS ONLY MOVE WHEN THE ROTATION DOES (lane SPACE-2,
		// the other half of the owner's "I can't drag the Rotation box").
		//
		// A quaternion has many euler triples, and toEulerAngles() returns the
		// canonical one — which at gimbal lock is NOT the triple the user is
		// typing or dragging. Anything that refreshes this panel (the gizmo's
		// transformRefreshRequested, a selection re-bind) therefore rewrote the
		// rotation row with an equivalent-but-different triple, and the field
		// under the user's mouse snapped back: measured on the rig at pitch
		// -90, dragging Z to 20 left the panel reading (-90, 20, 0) with Z at
		// zero again, which is exactly what "the box does not drag" looks like.
		//
		// THE TEST IS IDENTITY, NOT NEARNESS (round 2). The first cut compared
		// the row's quaternion to the node's with a tolerance, and a tolerance
		// on a dot product is a DEAD BAND: 1e-4 of |dot| is 1.62 degrees, so a
		// scripted `node.transform(id, {rotation: {y: 1}})` left the panel
		// reading zero, a gizmo nudge moved the row in 1.6-degree steps, and
		// selecting a node within 1.6 degrees of the last one showed the OLD
		// triple. The row is allowed to differ from the canonical decomposition
		// for exactly one reason — the document is holding the rotation THIS
		// ROW built — so that is the question asked, bit for bit, against the
		// quaternion the row last wrote (read back from the node, so a
		// document-side normalisation cannot make it a near-miss). Anything
		// else moved the rotation, and the row follows it.
		const iris::Quat current = sceneNode->getLocalRot();
		const bool rowBuiltThis = rotationMemoValid
			&& current.x() == rotationMemo.x() && current.y() == rotationMemo.y()
			&& current.z() == rotationMemo.z() && current.scalar() == rotationMemo.scalar();
		if (!rowBuiltThis) {
			auto rot = current.toEulerAngles();
			xrot->setValue(rot.x());
			yrot->setValue(rot.y());
			zrot->setValue(rot.z());
		}

		auto scale = sceneNode->getLocalScale();
		xscale->setValue(scale.x());
		yscale->setValue(scale.y());
		zscale->setValue(scale.z());

		// The lock reads the DOCUMENT, like every other control here — so an
		// undo, a script, a paste or a new selection puts the right icon up.
		refreshLockButton();

		// The measured world size of this node's subtree, in metres.
		const extent::Extent extent = extent::measureNode(sceneNode);
		sizeLabel->setText(extent.valid
		                       ? QStringLiteral("%1 \u00d7 %2 \u00d7 %3 m")
		                             .arg(extent.x, 0, 'g', 3)
		                             .arg(extent.y, 0, 'g', 3)
		                             .arg(extent.z, 0, 'g', 3)
		                       : QStringLiteral("\u2014"));
	}
	else if (sizeLabel) {
		sizeLabel->setText(QString());
	}
}

// THE NODE THIS PANEL MAY WRITE RIGHT NOW (owner, ledger §423;
// services/editgate.h). Null while a script run owns the document, which makes
// every slot below inert in exactly the way an empty selection already does —
// and it is asked here rather than at the undo push because these rows write
// the node live through a scrub and record the step only when it ends.
QSharedPointer<iris::SceneNode> TransformEditor::editableNode() const
{
    if (editgate::refuse()) return QSharedPointer<iris::SceneNode>();
    return sceneNode;
}

void TransformEditor::onScrubStarted()
{
    if (!!sceneNode) {
        scrubStartPos = sceneNode->getLocalPos();
        scrubStartRot = sceneNode->getLocalRot();
        scrubStartScale = sceneNode->getLocalScale();
    }
}

void TransformEditor::onScrubFinished(bool cancelled)
{
    if (!sceneNode) return;

    if (cancelled) {
        // DragSpinBox restored its own value; restore the node to match
        sceneNode->setLocalPos(scrubStartPos);
        sceneNode->setLocalRot(scrubStartRot);
        sceneNode->setLocalScale(scrubStartScale);
        refreshUi();
        return;
    }

    auto newPos = sceneNode->getLocalPos();
    auto newRot = sceneNode->getLocalRot();
    auto newScale = sceneNode->getLocalScale();

    if (newPos == scrubStartPos && newRot == scrubStartRot && newScale == scrubStartScale)
        return;

    // same pattern as Gizmo::createUndoAction — rewind to the drag-start
    // transform, then push; the command's redo() applies the new transform
    sceneNode->setLocalPos(scrubStartPos);
    sceneNode->setLocalRot(scrubStartRot);
    sceneNode->setLocalScale(scrubStartScale);
    if (services && services->undo)
        services->undo->push(new TransformSceneNodeCommand(sceneNode, newPos, newRot, newScale));
}

// A TYPED COMMIT IS ONE UNDO STEP (SCALE-LOCK-1 round 2; debt L6's last corner).
//
// These nine slots write the document LIVE, which is right — the viewport is
// the feedback — and until now only a SCRUB was ever recorded: DragSpinBox
// brackets a drag with scrubStarted/scrubFinished and onScrubFinished pushes the
// one command. A value the user TYPED (or stepped with an arrow key) reached the
// document with nothing on the stack at all, so Ctrl+Z after typing into this
// panel undid whatever came BEFORE it. The lock made that sharp enough to fix:
// one typed zero on a locked node writes all three channels.
//
// So every slot routes through here. `box` is the field the value came from;
// while it is SCRUBBING this records nothing (the gesture's own single step is
// onScrubFinished's job, and a step per tick is exactly what that avoids).
// Otherwise: snapshot, let the write run, and if the transform actually moved,
// rewind and push the same command the scrub pushes — the rewind is not
// ceremony, it is what lets the command capture the pre-edit SCENE_STATIC
// classification before its own redo() demotes the subtree (the shape
// onScrubFinished and Gizmo::createUndoAction both use).
void TransformEditor::writeTransform(DragSpinBox *box,
                                     const std::function<void(const iris::SceneNodePtr &)> &write)
{
    const QSharedPointer<iris::SceneNode> node = editableNode();
    if (!node) return;                     // empty selection, or a script owns the document

    const bool typed = !(box && box->isScrubbing());
    iris::Vec3 oldPos, oldScale;
    iris::Quat oldRot;
    if (typed) {
        oldPos = node->getLocalPos();
        oldRot = node->getLocalRot();
        oldScale = node->getLocalScale();
    }

    write(node);

    if (!typed) return;
    if (!services || !services->undo) return;   // nothing to record it; leave the write standing

    const iris::Vec3 newPos = node->getLocalPos();
    const iris::Quat newRot = node->getLocalRot();
    const iris::Vec3 newScale = node->getLocalScale();
    if (newPos == oldPos && newRot == oldRot && newScale == oldScale)
        return;                            // the same value again is not an edit

    node->setLocalPos(oldPos);
    node->setLocalRot(oldRot);
    node->setLocalScale(oldScale);
    services->undo->push(new TransformSceneNodeCommand(node, newPos, newRot, newScale));
}

void TransformEditor::xPosChanged(double value)
{
    writeTransform(xpos, [value](const iris::SceneNodePtr &node) {
        auto pos = node->getLocalPos();
        pos.setX(float(value));
        node->setLocalPos(pos);
    });
}

void TransformEditor::yPosChanged(double value)
{
    writeTransform(ypos, [value](const iris::SceneNodePtr &node) {
        auto pos = node->getLocalPos();
        pos.setY(float(value));
        node->setLocalPos(pos);
    });
}

void TransformEditor::zPosChanged(double value)
{
    writeTransform(zpos, [value](const iris::SceneNodePtr &node) {
        auto pos = node->getLocalPos();
        pos.setZ(float(value));
        node->setLocalPos(pos);
    });
}

/**
 * rotation change callbacks
 *
 * THE THREE FIELDS ARE THE ROTATION (owner report, 2026-09-14: "I can click and
 * drag the Position and Scale boxes, but I can't click and drag the Rotation
 * box").
 *
 * Each of these used to re-derive the euler triple FROM THE NODE'S QUATERNION
 * on every tick, change the one component it owns and write it back. A
 * quaternion has no memory of which triple produced it, and the decomposition
 * is only unique away from gimbal lock — so for any node whose pitch is at or
 * near +/-90 degrees (every flat plane, image plane and decal, and the many
 * imported models that arrive rotated -90 on X) the triple that came back was
 * not the one on screen, and the user's edit landed somewhere else entirely.
 * Measured on the rig at rotation (-90, 0, 0): a 20-degree drag of the Z field
 * moved the node's Y by 90 and left Z at 0 — the field snapped back and the
 * panel looked dead. Past +/-90 the same thing reverses the drag (89 + 20
 * landed on 71).
 *
 * So the panel's own three fields are the source of truth while the user is
 * editing: whatever they read is the rotation the document gets. refreshUi
 * puts the document's own decomposition back into them whenever the selection
 * or the node changes, which is the only place a canonical triple belongs.
 */
void TransformEditor::applyRotationFromFields()
{
    const QSharedPointer<iris::SceneNode> sceneNode = editableNode();
    if (!sceneNode) return;
    sceneNode->setLocalRot(iris::Quat::fromEulerAngles(
        iris::Vec3(float(xrot->value()), float(yrot->value()), float(zrot->value()))));
    // READ BACK, don't remember what we sent: this is the value refreshUi
    // compares against, and it has to be what the DOCUMENT holds.
    rotationMemo = sceneNode->getLocalRot();
    rotationMemoValid = true;
}

void TransformEditor::xRotChanged(double)
{
    writeTransform(xrot, [this](const iris::SceneNodePtr &) { applyRotationFromFields(); });
}

void TransformEditor::yRotChanged(double)
{
    writeTransform(yrot, [this](const iris::SceneNodePtr &) { applyRotationFromFields(); });
}

void TransformEditor::zRotChanged(double)
{
    writeTransform(zrot, [this](const iris::SceneNodePtr &) { applyRotationFromFields(); });
}

/**
 * scale change callbacks
 *
 * ONE CHANNEL AT A TIME, THROUGH THE DOCUMENT'S OWN RULE (SCALE-LOCK-1):
 * iris::scalelock::apply is what node.transform's one-channel write and the
 * gizmo's axis handles compute with too, so "scale X to 2" means the same thing
 * from a field, a script and a handle. The panel's only additions are the two
 * things a panel knows and the document does not: whether SHIFT is held for
 * this gesture, and that the other two fields have to show what the ratio did
 * to them.
 */
void TransformEditor::scaleChannelChanged(int axis, DragSpinBox* box, double value)
{
    writeTransform(box, [this, axis, box, value](const iris::SceneNodePtr &sceneNode) {
        // SHIFT = UNIFORM FOR THIS GESTURE ONLY, and it is read from the gesture
        // rather than from the keyboard: the modifiers ride the mouse events
        // driving the scrub, so pressing Shift halfway through a drag turns the
        // rest of that drag uniform and releasing it hands the rest back to the one
        // axis. A TYPED value is not a gesture — it carries no modifier and obeys
        // the lock alone (holding Shift while typing digits is not a thing anyone
        // means).
        const bool shiftHeld =
            box && box->isScrubbing() && box->scrubModifiers().testFlag(Qt::ShiftModifier);

        const bool uniform = shiftHeld || sceneNode->getScaleLock();

        // THE RATIO IS MEASURED FROM THE SCALE THE GESTURE STARTED AT, exactly as
        // the scale gizmo measures it from the scale it captured at press
        // (scalegizmo.cpp) — so the two surfaces agree, a long drag cannot
        // accumulate per-tick rounding in the two channels it is scaling, and a
        // modifier tapped and released mid-drag leaves NO residue: the other two
        // channels come back to precisely where they were. A TYPED value is not a
        // gesture and has no start, so its base is the value on the node.
        const iris::Vec3 base = (box && box->isScrubbing()) ? scrubStartScale
                                                            : sceneNode->getLocalScale();
        sceneNode->setLocalScale(iris::scalelock::apply(base, axis, float(value), uniform));
        if (uniform) refreshScaleFields();     // the other two moved with it
    });
}

void TransformEditor::refreshScaleFields()
{
    if (!sceneNode) return;
    // Display-only, blocked: the values on the node are the unrounded ones and
    // a 4-decimal echo back into setLocalScale would round them (the same trap
    // refreshUi documents at length).
    const QSignalBlocker b1(xscale), b2(yscale), b3(zscale);
    const auto scale = sceneNode->getLocalScale();
    xscale->setValue(scale.x());
    yscale->setValue(scale.y());
    zscale->setValue(scale.z());
}

void TransformEditor::xScaleChanged(double value) { scaleChannelChanged(0, xscale, value); }

void TransformEditor::yScaleChanged(double value) { scaleChannelChanged(1, yscale, value); }

void TransformEditor::zScaleChanged(double value) { scaleChannelChanged(2, zscale, value); }

// THE LOCK ITSELF (SCALE-LOCK-1). One undo step, in the shape every other
// row's one-shot edit has: applied first, recorded after, with the edit gate
// asked before either — a click while a script owns the document puts the
// button back instead of writing (services/editgate.h; the same refusal
// editableNode() gives the value rows, asked here because this control does not
// go through them).
//
// It records through NodeEditCommand directly rather than through
// panelundo::pushEdit: two suites outside tests/ui compile this panel for its
// transform rows alone (ui.material_panel, importer.glb), and the helper would
// drag the whole property-row spine into both for one button.
void TransformEditor::onScaleLockToggled(bool locked)
{
    if (!sceneNode || refreshingLock) return;
    if (sceneNode->getScaleLock() == locked) return;
    if (editgate::refuse()) { refreshLockButton(); return; }

    const iris::SceneNodePtr node = sceneNode;
    node->setScaleLock(locked);
    if (services && services->undo)
        services->undo->push(new NodeEditCommand(
            locked ? QObject::tr("lock scale ratio") : QObject::tr("unlock scale ratio"),
            [node, locked]() { node->setScaleLock(locked); },
            [node, locked]() { node->setScaleLock(!locked); }));
    // An undo of that step (or a refusal) may have put the flag back: the
    // button shows the DOCUMENT, never its own click.
    refreshLockButton();
}

void TransformEditor::refreshLockButton()
{
    if (!scaleLockBtn) return;
    const bool locked = !!sceneNode && sceneNode->getScaleLock();
    if (scaleLockBtn->isChecked() == locked) return;
    refreshingLock = true;
    scaleLockBtn->setChecked(locked);
    refreshingLock = false;
}
