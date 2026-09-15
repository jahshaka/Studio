/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/quat.h"
#include <QGridLayout>
#include <QLabel>

#include "services/fitsize.h"
#include <QPushButton>

#include "ui/panels/transformeditor.h"

#include "services/editgate.h"
#include "ui/controls/dragspinbox.h"

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
    addRow(grid, 2, "Scale",    xscale, yscale, zscale, kPosScaleStepPerPx);

    // FIT TO SIZE (services/fitsize.h): what this node actually MEASURES in
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

    for (auto box : { xpos, ypos, zpos, xrot, yrot, zrot, xscale, yscale, zscale }) {
        connect(box, &DragSpinBox::scrubStarted,  this, &TransformEditor::onScrubStarted);
        connect(box, &DragSpinBox::scrubFinished, this, &TransformEditor::onScrubFinished);
    }
}

void TransformEditor::addRow(QGridLayout* grid, int row, const QString& title,
                             DragSpinBox*& x, DragSpinBox*& y, DragSpinBox*& z,
                             double perPixelStep)
{
    auto label = new QLabel(title, this);
    label->setFixedWidth(kTitleWidth);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(label, row, 0);

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

		// The measured world size of this node's subtree, in metres.
		const fitsize::Extent extent = fitsize::measureNode(sceneNode);
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

void TransformEditor::xPosChanged(double value)
{
    if (auto sceneNode = editableNode()) {
        auto pos = sceneNode->getLocalPos();
        pos.setX(value);
        sceneNode->setLocalPos(pos);
    }
}

void TransformEditor::yPosChanged(double value)
{
    if (auto sceneNode = editableNode()) {
        auto pos = sceneNode->getLocalPos();
        pos.setY(value);
        sceneNode->setLocalPos(pos);
    }
}

void TransformEditor::zPosChanged(double value)
{
    if (auto sceneNode = editableNode()) {
        auto pos = sceneNode->getLocalPos();
        pos.setZ(value);
        sceneNode->setLocalPos(pos);
    }
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

void TransformEditor::xRotChanged(double) { applyRotationFromFields(); }

void TransformEditor::yRotChanged(double) { applyRotationFromFields(); }

void TransformEditor::zRotChanged(double) { applyRotationFromFields(); }

/**
 * scale change callbacks
 */
void TransformEditor::xScaleChanged(double value)
{
    if (auto sceneNode = editableNode()) {
        auto scale = sceneNode->getLocalScale();
        scale.setX(value);
        sceneNode->setLocalScale(scale);
    }
}

void TransformEditor::yScaleChanged(double value)
{
    if (auto sceneNode = editableNode()) {
        auto scale = sceneNode->getLocalScale();
        scale.setY(value);
        sceneNode->setLocalScale(scale);
    }
}

void TransformEditor::zScaleChanged(double value)
{
    if (auto sceneNode = editableNode()) {
        auto scale = sceneNode->getLocalScale();
        scale.setZ(value);
        sceneNode->setLocalScale(scale);
    }
}
