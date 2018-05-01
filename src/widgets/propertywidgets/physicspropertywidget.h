#ifndef PHYSICSPROPERTYWIDGET_H
#define PHYSICSPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>

#include "irisgl/src/irisglfwd.h"
#include "../accordianbladewidget.h"

class SceneViewWidget;

class PhysicsPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    PhysicsPropertyWidget();
    ~PhysicsPropertyWidget();

    void setSceneNode(iris::SceneNodePtr sceneNode);
    void setSceneView(SceneViewWidget *sceneView);

protected slots:
    void onPhysicsTypeChanged(const QString&);
    void onPhysicsEnabled(bool);
    void onStaticTypeChecked(bool);

private:
    QSharedPointer<iris::MeshNode> meshNode;
    SceneViewWidget *sceneView;

    CheckBoxWidget* isPhysicsObject;
    CheckBoxWidget* isStaticObject;
    ComboBoxWidget *shapeSelector;
};

#endif // PHYSICSPROPERTYWIDGET_HPP