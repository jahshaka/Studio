#ifndef LIGHTPROPERTYWIDGET_H
#define LIGHTPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"

class ColorValueWidget;
namespace jah3d
{
    class SceneNode;
}


/**
 * This class displays properties of light nodes
 */
class LightPropertyWidget:public AccordianBladeWidget
{
    Q_OBJECT

public:
    LightPropertyWidget(QWidget* parent);

    ColorValueWidget* lightColor;
    HFloatSlider* radius;
    HFloatSlider* spotCutOff;
    //EnumPicker* lightTypePicker;


    void setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode);

private:
    QSharedPointer<jah3d::SceneNode> sceneNode;
};

#endif // LIGHTPROPERTYWIDGET_H
