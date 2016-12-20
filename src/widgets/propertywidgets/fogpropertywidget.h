#ifndef FOGPROPERTYWIDGET_H
#define FOGPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"

class ColorValueWidget;
class ColorPickerWidget;
class TexturePicker;

namespace iris
{
    class Scene;
    class SceneNode;
    class LightNode;
}

/**
 * This widget displays the properties of the scene.
 */
class FogPropertyWidget: public AccordianBladeWidget
{
    Q_OBJECT

public:
    FogPropertyWidget();

    void setScene(QSharedPointer<iris::Scene> scene);

protected slots:
    void onFogColorChanged(QColor color);
    void onFogStartChanged(float val);
    void onFogEndChanged(float val);
    void onFogEnabledChanged(bool val);

private:
    QSharedPointer<iris::Scene> scene;

    //fog properties
    CheckBoxProperty* fogEnabled;
    HFloatSlider* fogStart;
    HFloatSlider* fogEnd;
    ColorValueWidget* fogColor;

};

#endif // FOGPROPERTYWIDGET_H
