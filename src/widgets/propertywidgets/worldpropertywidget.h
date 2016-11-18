#ifndef SCENEPROPERTYWIDGET_H
#define SCENEPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"

class ColorValueWidget;
class ColorPickerWidget;
namespace jah3d
{
    class Scene;
    class SceneNode;
    class LightNode;
}

class WorldPropertyWidget:public AccordianBladeWidget
{
public:
    WorldPropertyWidget();
    QSharedPointer<jah3d::Scene> scene;

    void setScene(QSharedPointer<jah3d::Scene> scene);

protected slots:
    void onSkyTextureSet(QString texPath);

private:
    TexturePicker* skyTexture;



};

#endif // SCENEPROPERTYWIDGET_H
