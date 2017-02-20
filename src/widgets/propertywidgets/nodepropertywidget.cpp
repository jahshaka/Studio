#include "nodepropertywidget.h"

#include "../checkboxproperty.h"
#include "../hfloatslider.h"

#include "../../irisgl/src/scenegraph/meshnode.h"
#include "../../irisgl/src/scenegraph/particlesystemnode.h"

NodePropertyWidget::NodePropertyWidget()
{
//    slider = this->addFloatValueSlider("aaaaaaaaaa", 1, 10);
    shadowCaster = this->addCheckBox("Cast Shadows", true);
    shadowReceiver = this->addCheckBox("Receive Shadows", true);

//    slider->setDisabled(true);
    shadowReceiver->setDisabled(true);

    connect(shadowCaster, SIGNAL(valueChanged(bool)), SLOT(onShadowEnabledChanged(bool)));
}

NodePropertyWidget::~NodePropertyWidget()
{

}

void NodePropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode) {
        this->sceneNode = sceneNode.staticCast<iris::SceneNode>();
        shadowCaster->setValue(this->sceneNode->getShadowEnabled());
    } else {
        this->sceneNode.clear();
    }
}

void NodePropertyWidget::onShadowEnabledChanged(bool val)
{
    if (!!this->sceneNode) {
        this->sceneNode->setShadowEnabled(val);
    }
}
