/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QTimer>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QVector3D>
#include <QJsonObject>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QJsonObject>
#include <QJsonDocument>

#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/graphics/mesh.h"
#include "irisgl/src/graphics/rendertarget.h"
#include "irisgl/src/graphics/forwardrenderer.h"
#include "irisgl/src/graphics/texture2d.h"
#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/scenegraph/lightnode.h"
#include "irisgl/src/scenegraph/cameranode.h"
#include "irisgl/src/scenegraph/meshnode.h"
#include "irisgl/src/graphics/viewport.h"
#include "irisgl/src/materials/custommaterial.h"
#include "irisgl/src/animation/animation.h"
#include "irisglfwd.h"

#include "core/database/database.h"
#include "../editor/editorcameracontroller.h"
#include "../editor/cameracontrollerbase.h"
#include "../editor/orbitalcameracontroller.h"

#include "dialogs/progressdialog.h"

#include "irisgl/src/graphics/shadowmap.h"

class AssetViewer : public QOpenGLWidget, protected QOpenGLFunctions_3_2_Core, iris::IModelReadProgress
{
    Q_OBJECT

public:
    AssetViewer(QWidget *parent = Q_NULLPTR);

    iris::SceneSource *ssource;

    void setDatabase(Database *db) { this->db = db;  }
    void update();
    void paintGL();
    void updateScene();
    void initializeGL();
    void resizeGL(int width, int height);

    void updateNodeMaterialValues(iris::SceneNodePtr &node, QJsonObject definition);

    void renderObject();
    void resetViewerCamera();
	void resetViewerCameraAfter();
    void loadJafMaterial(QString guid, bool firstAdd = true, bool cache = false, bool firstLoad = true);
    void loadJafParticleSystem(QString guid, bool firstAdd = true, bool cache = false, bool firstLoad = true);
    void loadJafShader(QString guid, QMap<QString, QString> &outGuids, bool firstAdd = true, bool cache = false, bool firstLoad = true);
    void loadJafModel(QString str, QString guid, bool firstAdd = true, bool cache = false, bool firstLoad = true);
    void loadModel(QString str, bool firstAdd = true, bool cache = false, bool firstLoad = true);

    void wheelEvent(QWheelEvent *event);
    void mousePressEvent(QMouseEvent *e);
    void mouseReleaseEvent(QMouseEvent *e);
    void mouseMoveEvent(QMouseEvent *e);

	void addJafMaterial(const QString &guid, bool firstAdd = true, bool cache = false, QVector3D position = QVector3D());
	void addJafShader(const QString &guid, QMap<QString, QString> &outGuids, bool firstAdd = true, bool cache = false, QVector3D position = QVector3D());
	void addJafMesh(const QString &path, const QString &guid, bool firstAdd = true, bool cache = false, QVector3D position = QVector3D());
	void addMesh(const QString &path = QString(), bool firstAdd = true, bool cache = false, QVector3D position = QVector3D());
	void addNodeToScene(QSharedPointer<iris::SceneNode> sceneNode, QString guid = "", bool viewed = false, bool cache = false, bool isOnGround = true);
	QImage takeScreenshot(int width, int height);

    float onProgress(float percentage) {
        emit progressChanged(percentage * 100);
        return percentage;
    }

	void changeBackdrop(unsigned int id);

	void createMaterial(QJsonObject &matObj, iris::CustomMaterialPtr mat);
	void setMaterial(const QJsonObject &matObj) {
		assetMaterial = matObj;
	}

	QJsonObject getMaterial() {
		return assetMaterial;
	}

	void clearScene() {
		if (scene->rootNode->hasChildren()) {
			auto lastNode = scene->rootNode->children.last();
			if (!lastNode->isBuiltIn) {
				lastNode->removeFromParent();
			}
		}
	}

    iris::ShadowMapType evalShadowMapType(QString shadowType)
    {
        if (shadowType == "hard")
            return iris::ShadowMapType::Hard;
        if (shadowType == "soft")
            return iris::ShadowMapType::Soft;
        if (shadowType == "softer")
            return iris::ShadowMapType::Softer;

        return iris::ShadowMapType::None;
    }

    iris::LightType getLightTypeFromName(QString lightType)
    {
        if (lightType == "point")       return iris::LightType::Point;
        if (lightType == "directional") return iris::LightType::Directional;
        if (lightType == "spot")        return iris::LightType::Spot;

        return iris::LightType::Point;
    }

    iris::LightNodePtr createLight(QJsonObject& nodeObj)
    {
        auto lightNode = iris::LightNode::create();

        lightNode->setLightType(getLightTypeFromName(nodeObj["lightType"].toString()));
        lightNode->intensity = (float) nodeObj["intensity"].toDouble(1.0f);
        lightNode->distance = (float) nodeObj["distance"].toDouble(1.0f);
        lightNode->spotCutOff = (float) nodeObj["spotCutOff"].toDouble(30.0f);
        lightNode->color = IrisUtils::readColor(nodeObj["color"].toObject());
        lightNode->setVisible(nodeObj["visible"].toBool(true));

        //shadow data
        auto shadowMap = lightNode->shadowMap;
        shadowMap->bias = (float) nodeObj["shadowBias"].toDouble(0.0015f);
        // ensure shadow map size isnt too big ro too small
        auto res = qBound(512, nodeObj["shadowSize"].toInt(1024), 4096);
        shadowMap->setResolution(res);
        shadowMap->shadowType = evalShadowMapType(nodeObj["shadowType"].toString());

        //TODO: move this to the sceneview widget or somewhere more appropriate
        if (lightNode->lightType == iris::LightType::Directional) {
            lightNode->icon = iris::Texture2D::load(":/icons/light.png");
        }
        else {
            lightNode->icon = iris::Texture2D::load(":/icons/bulb.png");
        }

        lightNode->iconSize = 0.5f;

        return lightNode;
    }

	void orientCamera(QVector3D pos, QVector3D localRot, int distanceFromPivot) {
		this->localPos = pos;
		this->distanceFromPivot = distanceFromPivot;
		this->localRot = localRot;

		resetViewerCameraAfter();
	}

	void cacheCurrentModel(QString guid);

	QJsonObject getSceneProperties();

	QMap<QString, iris::SceneNodePtr> cachedAssets;

signals:
    void progressChanged(int);

private:

	QJsonObject assetMaterial;
    ProgressDialog * pdialog;
    QOpenGLFunctions_3_2_Core *gl;
    iris::ForwardRendererPtr renderer;
    iris::ScenePtr scene;
    iris::SceneNodePtr sceneNode;
    iris::CameraNodePtr camera;
    iris::CustomMaterialPtr material;

    EditorCameraController* defaultCam;
    CameraControllerBase* camController;
    OrbitalCameraController* orbitalCam;
    QPointF prevMousePos;

    QElapsedTimer elapsedTimer;
    QTimer* timer;

    iris::Viewport* viewport;
    bool render;

	iris::RenderTargetPtr previewRT;
	iris::Texture2DPtr screenshotTex;

    Database *db;

    float getBoundingRadius(iris::SceneNodePtr node);
    void getBoundingSpheres(iris::SceneNodePtr node, QList<iris::BoundingSphere>& spheres);
	iris::AABB getNodeBoundingBox(iris::SceneNodePtr node);

    QVector3D localPos, localRot, lookAt;
	int distanceFromPivot;

    QString lastNode;
};
