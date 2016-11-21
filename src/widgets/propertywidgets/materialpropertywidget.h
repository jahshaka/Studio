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
    void onAmbientColorChanged(QColor color);

    void onDiffuseColorChanged(QColor color);
    void onDiffuseTextureChanged(QString texture);

    void onSpecularColorChanged(QColor color);
    void onSpecularTextureChanged(QString texture);
    void onShininessChanged(float shininess);

    void onNormalTextureChanged(QString texture);
    void onNormalIntensityChanged(float intensity);

    void onReflectionTextureChanged(QString texture);
    void onReflectionInfluenceChanged(float intensity);

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
