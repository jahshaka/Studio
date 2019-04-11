/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDPROPERTYWIDGET_H
#define WORLDPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../../irisgl/src/irisglfwd.h"
#include "../accordianbladewidget.h"

namespace iris {
    class Scene;
    class SceneNode;
    class LightNode;
}

class Database;

/**
 * This widget displays the properties of the scene.
 */
class WorldPropertyWidget: public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldPropertyWidget();

    void setScene(QSharedPointer<iris::Scene> scene);
	void setDatabase(Database*);

protected slots:
    void onGravityChanged(float value);
    void onSkyChanged(int index);
    void onSkyColorChanged(QColor color);
    void onAmbientColorChanged(QColor color);

private:
    QSharedPointer<iris::Scene> scene;
    CheckBoxWidget *flipView;
    ColorValueWidget *skyColor;
    ColorValueWidget *ambientColor;
    HFloatSliderWidget *worldGravity;
	ComboBoxWidget *skySelector;

	Database *db;
};

#endif // WORLDPROPERTYWIDGET_H
