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

#include "../editor/editorcameracontroller.h"
#include "../editor/cameracontrollerbase.h"
#include "../editor/orbitalcameracontroller.h"

#include "dialogs/progressdialog.h"

class AssetViewer : public QOpenGLWidget, protected QOpenGLFunctions_3_2_Core, iris::IModelReadProgress
{
    Q_OBJECT

public:
    AssetViewer(QWidget *parent = Q_NULLPTR);

    iris::SceneSource *ssource;

    void update();
    void paintGL();
    void updateScene();
    void initializeGL();
    void resizeGL(int width, int height);

    void renderObject();
    void resetViewerCamera();
	void resetViewerCameraAfter();
    void loadModel(QString str, bool firstAdd = true, bool cache = false, bool firstLoad = true);

    void wheelEvent(QWheelEvent *event);
    void mousePressEvent(QMouseEvent *e);
    void mouseReleaseEvent(QMouseEvent *e);
    void mouseMoveEvent(QMouseEvent *e);

	void addMesh(const QString &path = QString(), bool firstAdd = true, bool cache = false, QVector3D position = QVector3D());
	void addNodeToScene(QSharedPointer<iris::SceneNode> sceneNode, QString guid = "", bool viewed = false, bool cache = false);
	QImage takeScreenshot(int width, int height);

    float onProgress(float percentage) {
        emit progressChanged(percentage * 100);
        return percentage;
    }

	void createMaterial(QJsonObject &matObj, iris::CustomMaterialPtr mat);
	void setMaterial(const QJsonObject &matObj) {
		assetMaterial = matObj;
	}

	QJsonObject getMaterial() {
		return assetMaterial;
	}

    void clearScene() {
		if (scene->rootNode->hasChildren()) {
			for (auto child : scene->rootNode->children) {
				// clear the scene of anything that is not a light for the next asset
				if (child->sceneNodeType != iris::SceneNodeType::Light) {
					child->removeFromParent();
				}
			}
		}
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

    float getBoundingRadius(iris::SceneNodePtr node);
    void getBoundingSpheres(iris::SceneNodePtr node, QList<iris::BoundingSphere>& spheres);

    QVector3D localPos, localRot, lookAt;
	int distanceFromPivot;
};