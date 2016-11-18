#ifndef MATERIALPROPERTYWIDGET_H
#define MATERIALPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"

namespace jah3d
{
    class SceneNode;
    class MeshNode;
}

/**
 *  Displays properties for materials
 *  right now it is made specifically for the jah3d::DefaulMaterial class
 *
 */
class MaterialPropertyWidget:public AccordianBladeWidget
{
    Q_OBJECT

public:
    MaterialPropertyWidget(QWidget* parent=nullptr);

    void setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode);
    QSharedPointer<jah3d::MeshNode> meshNode;

protected slots:
    void onDiffuseColorChanged(QColor color);
    void onSpecularColorChanged(QColor color);

private:
    ColorValueWidget* ambientColor;

    ColorValueWidget* diffuseColor;
    TexturePicker* diffuseTexture;

    ColorValueWidget* specularColor;
    TexturePicker* specularTexture;
    HFloatSlider* shininess;

    TexturePicker* normalTexture;
    HFloatSlider* normalIntensity;

    TexturePicker* reflectionTexture;
    HFloatSlider* reflectionInfluence;

};

#endif // MATERIALPROPERTYWIDGET_H
