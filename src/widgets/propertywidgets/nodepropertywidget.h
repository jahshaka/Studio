/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef NODEPROPERTYWIDGET_H
#define NODEPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>

#include "../../irisgl/src/irisglfwd.h"
#include "../accordianbladewidget.h"

class NodePropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    NodePropertyWidget();
    ~NodePropertyWidget();

    void setSceneNode(iris::SceneNodePtr sceneNode);

protected slots:
    void onShadowEnabledChanged(bool val);
    void drawTypeChanged(const QString&);

private:
    QSharedPointer<iris::SceneNode> sceneNode;

    ComboBoxWidget* drawType;
    TextInputWidget* uuid;
    CheckBoxWidget* shadowCaster;
    CheckBoxWidget* shadowReceiver;
};

#endif // NODEPROPERTY_H
