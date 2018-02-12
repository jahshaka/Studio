#ifndef TRANSFRORMSCENENODECOMMAND_H
#define TRANSFRORMSCENENODECOMMAND_H

#include <QUndoCommand>
#include <QMatrix4x4>
#include "../irisgl/src/irisglfwd.h"

class TransformSceneNodeCommand : public QUndoCommand
{
    //QMatrix4x4 oldTransform;
    //QMatrix4x4 newTransform;
	QVector3D oldPos, oldScale;
	QQuaternion oldRot;

	QVector3D newPos, newScale;
	QQuaternion newRot;

    iris::SceneNodePtr sceneNode;
public:

    TransformSceneNodeCommand(iris::SceneNodePtr node, QMatrix4x4 localTransform);
	TransformSceneNodeCommand(iris::SceneNodePtr node, QVector3D pos, QQuaternion rot, QVector3D scale);
	TransformSceneNodeCommand(iris::SceneNodePtr node,
							  QVector3D oldPos, QQuaternion oldRot, QVector3D oldScale,
							  QVector3D newPos, QQuaternion newRot, QVector3D newScale);
    void undo() override;
    void redo() override;
};

#endif // TRANSFRORMSCENENODECOMMAND_H
