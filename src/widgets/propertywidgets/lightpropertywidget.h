#ifndef LIGHTPROPERTYWIDGET_H
#define LIGHTPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"

class ColorValueWidget;
class ColorPickerWidget;
namespace jah3d
{
    class SceneNode;
    class LightNode;
}


/**
 * This class displays properties of light nodes
 */
class LightPropertyWidget:public AccordianBladeWidget
{
    Q_OBJECT

public:
    LightPropertyWidget(QWidget* parent=nullptr);

    ColorValueWidget* lightColor;
    HFloatSlider* radius;
    HFloatSlider* spotCutOff;
    HFloatSlider* intensity;
    //EnumPicker* lightTypePicker;


    void setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode);
protected slots:
    void lightColorChanged(QColor color);

private:
    //QSharedPointer<jah3d::SceneNode> sceneNode;
    QSharedPointer<jah3d::LightNode> lightNode;
};

#endif // LIGHTPROPERTYWIDGET_H
