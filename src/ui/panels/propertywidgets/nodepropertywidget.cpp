/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/nodepropertywidget.h"

#include "ui/controls/checkboxwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/comboboxwidget.h"

#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"

NodePropertyWidget::NodePropertyWidget()
{
    drawType = this->addComboBox("Draw type");
    drawType->addItem("Textured");
    drawType->addItem("Shaded");
    drawType->addItem("Wireframe");

    uuid = this->addTextInput("UUID");
    shadowCaster = this->addCheckBox("Cast Shadows", true);
    shadowReceiver = this->addCheckBox("Receive Shadows", true);

    shadowReceiver->setDisabled(true);

    // REFLECTIONS_ADOPTION_SPEC.md P1a.2. The renderer fits the lit volume and
    // the reflection-probe region to the scene's geometry; this is the one
    // deterministic override for when that guess is wrong. The object still
    // bounces light — it just stops deciding WHERE the lighting happens, which
    // is what a 200-unit ground plane under a 2-unit scene otherwise does.
    giBoundsExcluded = this->addCheckBox("Exclude From GI Bounds", false);

    connect(shadowCaster,   SIGNAL(valueChanged(bool)),
            this,           SLOT(onShadowEnabledChanged(bool)));

    connect(giBoundsExcluded, SIGNAL(valueChanged(bool)),
            this,             SLOT(onGiBoundsExcludedChanged(bool)));

    connect(drawType,       SIGNAL(currentTextChanged(QString)),
            this,           SLOT(drawTypeChanged(QString)));
}

NodePropertyWidget::~NodePropertyWidget()
{

}

void NodePropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode) {
        this->sceneNode = sceneNode.staticCast<iris::SceneNode>();
        shadowCaster->setValue(this->sceneNode->getShadowCastingEnabled());
        giBoundsExcluded->setValue(this->sceneNode->getGiBoundsExcluded());
    } else {
        this->sceneNode.clear();
    }
}

void NodePropertyWidget::onShadowEnabledChanged(bool val)
{
    if (!!this->sceneNode) {
        this->sceneNode->setShadowCastingEnabled(val);
    }
}

void NodePropertyWidget::onGiBoundsExcludedChanged(bool val)
{
    if (!!this->sceneNode) {
        this->sceneNode->setGiBoundsExcluded(val);
    }
}

void NodePropertyWidget::drawTypeChanged(const QString& text)
{

}
