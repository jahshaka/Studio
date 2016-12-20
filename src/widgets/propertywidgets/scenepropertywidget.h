#ifndef SCENEPROPERTYWIDGET_H
#define SCENEPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"

class ColorValueWidget;
class ColorPickerWidget;
namespace iris
{
    class Scene;
}

class ScenePropertyWidget:public AccordianBladeWidget
{
    Q_OBJECT

public:
    ScenePropertyWidget(QWidget* parent = nullptr);

    void setScene(QSharedPointer<iris::Scene> sceneNode);

private:
    QSharedPointer<iris::Scene> sceneNode;
};

#endif // SCENEPROPERTYWIDGET_H
