#include "meshpropertywidget.h"
#include "../filepickerwidget.h"
#include "../../irisgl/src/scenegraph/meshnode.h"

MeshPropertyWidget::MeshPropertyWidget()
{
    meshPicker = this->addFilePicker("Mesh Path");

    connect(meshPicker, SIGNAL(onPathChanged(QString)), SLOT(onMeshPathChanged(QString)));
}

MeshPropertyWidget::~MeshPropertyWidget()
{

}

void MeshPropertyWidget::onMeshPathChanged(const QString &path)
{
    // do any changes here...sceneNode is initialized...
    qDebug() << sceneNode->getName();
    qDebug() << path;
}

void MeshPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode) {
        this->sceneNode = sceneNode.staticCast<iris::SceneNode>();
    } else {
        this->sceneNode.clear();
    }
}
