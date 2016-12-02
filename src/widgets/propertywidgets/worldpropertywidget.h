#ifndef SCENEPROPERTYWIDGET_H
#define SCENEPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"

class ColorValueWidget;
class ColorPickerWidget;
class TexturePicker;

namespace jah3d
{
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

    void setScene(QSharedPointer<jah3d::Scene> scene);

protected slots:
    void onSkyTextureChanged(QString texPath);
    void onFogColorChanged(QColor color);
    void onFogStartChanged(float val);
    void onFogEndChanged(float val);

private:
    QSharedPointer<jah3d::Scene> scene;
    TexturePicker* skyTexture;

    //fog properties
    HFloatSlider* fogStart;
    HFloatSlider* fogEnd;
    ColorValueWidget* fogColor;

};

#endif // SCENEPROPERTYWIDGET_H
