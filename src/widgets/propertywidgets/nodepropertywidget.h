#ifndef NODEPROPERTYWIDGET_H
#define NODEPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>

#include "../../irisgl/src/irisglfwd.h"
#include "../accordianbladewidget.h"

class CheckBoxProperty;

class NodePropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    NodePropertyWidget();
    ~NodePropertyWidget();

    void setSceneNode(iris::SceneNodePtr sceneNode);

protected slots:
    void onShadowEnabledChanged(bool val);

private:
    QSharedPointer<iris::SceneNode> sceneNode;

    HFloatSlider* slider;
    CheckBoxProperty* shadowCaster;
    CheckBoxProperty* shadowReceiver;
};

#endif // NODEPROPERTY_H
