/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "nodepropertywidget.h"

#include "../checkboxwidget.h"
#include "../hfloatsliderwidget.h"
#include "../comboboxwidget.h"

#include "../../irisgl/src/scenegraph/meshnode.h"
#include "../../irisgl/src/scenegraph/particlesystemnode.h"

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

    connect(shadowCaster,   SIGNAL(valueChanged(bool)),
            this,           SLOT(onShadowEnabledChanged(bool)));

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
        shadowCaster->setValue(this->sceneNode->getShadowEnabled());
    } else {
        this->sceneNode.clear();
    }
}

void NodePropertyWidget::onShadowEnabledChanged(bool val)
{
    if (!!this->sceneNode) {
        this->sceneNode->setShadowEnabled(val);
    }
}

void NodePropertyWidget::drawTypeChanged(const QString& text)
{

}
