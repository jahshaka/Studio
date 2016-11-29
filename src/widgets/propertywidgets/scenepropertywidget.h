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
}

class ScenePropertyWidget:public AccordianBladeWidget
{
    Q_OBJECT

public:
    ScenePropertyWidget(QWidget* parent = nullptr);

    void setScene(QSharedPointer<jah3d::Scene> sceneNode);

private:
    QSharedPointer<jah3d::Scene> sceneNode;
};

#endif // SCENEPROPERTYWIDGET_H
