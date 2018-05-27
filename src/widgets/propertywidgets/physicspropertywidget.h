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
    void onPhysicsTypeChanged(int);
    void onPhysicsEnabled(bool);
    void onVisibilityChanged(bool);
    void onMassChanged(float);
    void onBouncinessChanged(float);
    void onStaticTypeChecked(bool);

private:
    iris::SceneNodePtr sceneNode;
    SceneViewWidget *sceneView;

    CheckBoxWidget* isPhysicsObject;
    CheckBoxWidget* isStaticObject;
    CheckBoxWidget* isVisible;
    HFloatSliderWidget *massValue;
    HFloatSliderWidget *bouncinessValue;
    ComboBoxWidget *shapeSelector;
};

#endif // PHYSICSPROPERTYWIDGET_HPP