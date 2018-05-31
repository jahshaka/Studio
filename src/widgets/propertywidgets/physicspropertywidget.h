#ifndef PHYSICSPROPERTYWIDGET_H
#define PHYSICSPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>

#include "irisgl/src/irisglfwd.h"
#include "../accordianbladewidget.h"

class SceneViewWidget;
class btRigidBody;

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
    void onPhysicsShapeChanged(int);
    void onVisibilityChanged(bool);
    void onMassChanged(float);
    void onMarginChanged(float);
    void onBouncinessChanged(float);

private:
    btRigidBody *currentBody;
    iris::SceneNodePtr sceneNode;
    SceneViewWidget *sceneView;

    CheckBoxWidget* isVisible;
    HFloatSliderWidget *massValue;
    HFloatSliderWidget *bouncinessValue;
    HFloatSliderWidget *marginValue;
    ComboBoxWidget *physicsTypeSelector;
    ComboBoxWidget *physicsShapeSelector;

    QMap<int, QString> physicsTypes;
    QMap<int, QString> physicsShapes;
};

#endif // PHYSICSPROPERTYWIDGET_HPP