#ifndef MATERIALPROPERTYWIDGET_H
#define MATERIALPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"

namespace iris
{
    class SceneNode;
    class MeshNode;
    class DefaultMaterial;
}

/**
 *  Displays properties for materials
 *  right now it is made specifically for the iris::DefaulMaterial class
 *
 */
class MaterialPropertyWidget:public AccordianBladeWidget
{
    Q_OBJECT

public:
    MaterialPropertyWidget(QWidget* parent=nullptr);

    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);
    QSharedPointer<iris::MeshNode> meshNode;
    QSharedPointer<iris::DefaultMaterial> material;

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

    void onTextureScaleChanged(float scale);

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

    HFloatSlider* textureScale;

};

#endif // MATERIALPROPERTYWIDGET_H
