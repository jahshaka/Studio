/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef HANDPROPERTYWIDGET_H
#define HANDPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>

#include "../../irisgl/src/irisglfwd.h"
#include "../accordianbladewidget.h"

class HandPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
	HandPropertyWidget();
    ~HandPropertyWidget();

    void setSceneNode(iris::SceneNodePtr sceneNode);

protected slots:
    void onPoseTypeChanged(const QString&);
	void onPoseFactorChanged(float value);

private:
    iris::GrabNodePtr grabNode;
    ComboBoxWidget* poseType;
	HFloatSliderWidget* poseFactor;
};

#endif // HANDPROPERTYWIDGET_H
