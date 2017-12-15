#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QTimer>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QVector3D>

#include <QWheelEvent>
#include <QKeyEvent>
#include <QKeyEvent>
#include <QFocusEvent>

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

    void update();
    void paintGL();
    void updateScene();
    void initializeGL();
    void resizeGL(int width, int height);

    void renderObject();
    void resetViewerCamera();
    void loadModel(QString str);

    void wheelEvent(QWheelEvent *event);
    void mousePressEvent(QMouseEvent *e);
    void mouseReleaseEvent(QMouseEvent *e);
    void mouseMoveEvent(QMouseEvent *e);

	void addMesh(const QString &path = "", bool ignore = false, QVector3D position = QVector3D());
	void addNodeToScene(QSharedPointer<iris::SceneNode> sceneNode, bool ignore);
	QImage takeScreenshot(int width, int height);

    float onProgress(float percentage) {
        emit progressChanged(percentage * 100);
        return percentage;
    }

signals:
    void progressChanged(int);

private:
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

    QVector3D localPos, lookAt;
};