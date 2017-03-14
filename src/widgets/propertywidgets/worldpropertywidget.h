/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEPROPERTYWIDGET_H
#define SCENEPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../irisglfwd.h"
#include "../graphics/texture2d.h"
#include "../accordianbladewidget.h"

namespace iris {
    class Scene;
    class SceneNode;
    class LightNode;
}

/**
 * This widget displays the properties of the scene.
 */
class WorldPropertyWidget: public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldPropertyWidget();

    void setScene(QSharedPointer<iris::Scene> scene);
    void setupViewSelector();

protected slots:
    void onSkyTextureChanged(QString texPath);
    void onSkyColorChanged(QColor color);
    void onAmbientColorChanged(QColor color);
    void viewTextureSlotChanged(const QString &text);

private:
    QSharedPointer<iris::Scene> scene;
    ComboBoxWidget *viewSelector;
    CheckBoxWidget *flipView;
    TexturePickerWidget *skyTexture;
    ColorValueWidget *skyColor;
    ColorValueWidget *ambientColor;

    QString skyBoxTextures[6];
};

#endif // SCENEPROPERTYWIDGET_H
