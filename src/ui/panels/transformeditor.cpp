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
#include <QPushButton>

#include "ui/panels/transformeditor.h"
#include "ui/controls/dragspinbox.h"

#include "irisgl/document/scenegraph/scenenode.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "commands/transformscenenodecommand.h"

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
    setStyleSheet(
        "QWidget#TransformEditor { border: none; }"
        "QLabel { color: #DEDEDE; background: transparent; }"
        "QDoubleSpinBox {"
        "    border-radius: 1px; padding: 3px; background: #292929; color: #DEDEDE;"
        "    selection-background-color: #3498db;"
        "}"
        // axis identity moved from the old X/Y/Z chips to a colored edge per field
        "DragSpinBox#xpos, DragSpinBox#xrot, DragSpinBox#xscale { border-left: 3px solid #c0392b; }"
        "DragSpinBox#ypos, DragSpinBox#yrot, DragSpinBox#yscale { border-left: 3px solid #27ae60; }"
        "DragSpinBox#zpos, DragSpinBox#zrot, DragSpinBox#zscale { border-left: 3px solid #2980b9; }"
        "QPushButton#resetBtn { background-color: #333; color: #DEDEDE; border: 0;"
        "                       padding: 4px 16px; border-radius: 1px; }"
        "QPushButton#resetBtn:hover { background-color: #555; }"
        "QPushButton#resetBtn:pressed { background-color: #444; }");

    auto grid = new QGridLayout(this);
    grid->setContentsMargins(14, 4, 14, 6);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(4);

    // three horizontal rows: title on the left, X/Y/Z side by side
    addRow(grid, 0, "Position", xpos, ypos, zpos, kPosScaleStepPerPx);
    addRow(grid, 1, "Rotation", xrot, yrot, zrot, kRotStepPerPx);
    addRow(grid, 2, "Scale",    xscale, yscale, zscale, kPosScaleStepPerPx);

    resetBtn = new QPushButton("Reset", this);
    resetBtn->setObjectName("resetBtn");
    grid->addWidget(resetBtn, 3, 1, 1, 3);

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
    if (!!sceneNode) {
        xPosChanged(0);
        yPosChanged(0);
        zPosChanged(0);

        xRotChanged(0);
        yRotChanged(0);
        zRotChanged(0);

        auto scale = defaultStateNode->getLocalScale();
        xScaleChanged(scale.x());
        yScaleChanged(scale.y());
        zScaleChanged(scale.z());

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

		auto rot = sceneNode->getLocalRot().toEulerAngles();
		xrot->setValue(rot.x());
		yrot->setValue(rot.y());
		zrot->setValue(rot.z());

		auto scale = sceneNode->getLocalScale();
		xscale->setValue(scale.x());
		yscale->setValue(scale.y());
		zscale->setValue(scale.z());
	}
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
    if (!!sceneNode) {
        auto pos = sceneNode->getLocalPos();
        pos.setX(value);
        sceneNode->setLocalPos(pos);
    }
}

void TransformEditor::yPosChanged(double value)
{
    if (!!sceneNode) {
        auto pos = sceneNode->getLocalPos();
        pos.setY(value);
        sceneNode->setLocalPos(pos);
    }
}

void TransformEditor::zPosChanged(double value)
{
    if (!!sceneNode) {
        auto pos = sceneNode->getLocalPos();
        pos.setZ(value);
        sceneNode->setLocalPos(pos);
    }
}

/**
 * rotation change callbacks
 */
void TransformEditor::xRotChanged(double value)
{
    if (!!sceneNode) {
        auto rot = sceneNode->getLocalRot().toEulerAngles();
        rot.setX(value);
        sceneNode->setLocalRot(iris::Quat::fromEulerAngles(rot));
    }
}

void TransformEditor::yRotChanged(double value)
{
    if (!!sceneNode) {
        auto rot = sceneNode->getLocalRot().toEulerAngles();
        rot.setY(value);
        sceneNode->setLocalRot(iris::Quat::fromEulerAngles(rot));
    }
}

void TransformEditor::zRotChanged(double value)
{
    if (!!sceneNode) {
        auto rot = sceneNode->getLocalRot().toEulerAngles();
        rot.setZ(value);
        sceneNode->setLocalRot(iris::Quat::fromEulerAngles(rot));
    }
}

/**
 * scale change callbacks
 */
void TransformEditor::xScaleChanged(double value)
{
    if (!!sceneNode) {
        auto scale = sceneNode->getLocalScale();
        scale.setX(value);
        sceneNode->setLocalScale(scale);
    }
}

void TransformEditor::yScaleChanged(double value)
{
    if (!!sceneNode) {
        auto scale = sceneNode->getLocalScale();
        scale.setY(value);
        sceneNode->setLocalScale(scale);
    }
}

void TransformEditor::zScaleChanged(double value)
{
    if (!!sceneNode) {
        auto scale = sceneNode->getLocalScale();
        scale.setZ(value);
        sceneNode->setLocalScale(scale);
    }
}
