#ifndef MATERIALPROPERTYWIDGET_H
#define MATERIALPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"

namespace jah3d
{
    class SceneNode;
}

class MaterialPropertyWidget:public AccordianBladeWidget
{
    Q_OBJECT

public:
    MaterialPropertyWidget(QWidget* parent=nullptr);

    void setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode);
    QSharedPointer<jah3d::SceneNode> sceneNode;

};

#endif // MATERIALPROPERTYWIDGET_H
