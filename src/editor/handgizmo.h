#ifndef HAND_GIZMO_H
#define HAND_GIZMO_H

#include <QVector3D>
#include <irisgl/Graphics.h>
#include <irisgl/SceneGraph.h>
#include <irisgl/Content.h>
#include "widgets/sceneviewwidget.h"

enum class HandGesture
{
	Grab,
	Pinch
};

class HandGizmoHandler
{
public:
	HandGesture gesture;
	float gestureFactor;

	iris::ModelPtr handModel;
	iris::MaterialPtr handMaterial;
	QMatrix4x4 handScale;

	HandGizmoHandler();

	void loadAssets(const iris::ContentManagerPtr& content);

	bool doHandPicking(const iris::ScenePtr& scene,
		const QVector3D& segStart,
		const QVector3D& segEnd,
		QList<PickingResult>& hitList);

	void submitHandToScene(const iris::ScenePtr& scene);
	void submitHandToScene(const iris::ScenePtr& scene, const iris::GrabNodePtr& grabNode, const QMatrix4x4& transform);
};


#endif // HAND_GIZMO_H