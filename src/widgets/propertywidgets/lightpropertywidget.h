#ifndef LIGHTPROPERTYWIDGET_H
#define LIGHTPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"

class ColorValueWidget;
class ColorPickerWidget;
namespace iris
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

    /**
     * Sets the active sceneNode. If the sceneNode is a LightNode it is casted
     * to a LightNode and stored in lightNode, otherwise lightNode is reset to null
     * @param sceneNode
     */
    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);

protected slots:

    /**
     * Sets the light's color
     * @param color
     */
    void lightColorChanged(QColor color);

    /**
     * Sets the light's intensity
     * @param intensity
     */
    void lightIntensityChanged(float intensity);

    /**
     * Sets the light's radius
     * @param radius
     */
    void lightRadiusChanged(float radius);

    /**
     * Sets the light's spot cutoff angle. Only valid for spotlights.
     * @param spotCutOff
     */
    void lightSpotCutoffChanged(float spotCutOff);

private:

    QSharedPointer<iris::LightNode> lightNode;

    ColorValueWidget* lightColor;
    HFloatSlider* radius;
    HFloatSlider* spotCutOff;
    HFloatSlider* intensity;
    //EnumPicker* lightTypePicker;
};

#endif // LIGHTPROPERTYWIDGET_H
