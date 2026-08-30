#ifndef HEADLESSEDITORVIEWPORT_H
#define HEADLESSEDITORVIEWPORT_H

// HeadlessEditorViewport — the document-only stand-in used when no engine view
// can exist (--headless script runs, --dump-api-docs, offscreen platform).
//
// Before step 14 this role was played by an unrealized legacy SceneViewWidget;
// with the legacy GL viewport deleted, this class carries exactly the document
// surface those runs exercise: a scene, an editor camera, editor data. Every
// rendering-flavoured call is a no-op. Never shown, never renders.
#include <QWidget>
#include "viewport/ieditorviewport.h"
#include "viewport/editordata.h"
#include "irisgl/document/scenegraph/cameranode.h"

class HeadlessEditorViewport : public IEditorViewport
{
public:
    HeadlessEditorViewport(QWidget *parent = nullptr)
    {
        mWidget = new QWidget(parent);
        mEvents = new EditorViewportEvents(mWidget);
        resetEditorCam();
    }

    QWidget *asWidget() override { return mWidget; }
    EditorViewportEvents *events() override { return mEvents; }

    void setMainWindow(MainWindow *) override {}
    void setDatabase(Database *) override {}

    // ---- document ----
    void setScene(iris::ScenePtr scene) override
    {
        mScene = scene;
        if (mScene && mEditorCam) mScene->setCamera(mEditorCam);
    }
    iris::ScenePtr getScene() override { return mScene; }
    void setSelectedNode(iris::SceneNodePtr) override {}
    void clearSelectedNode() override {}
    void focusOnNode(iris::SceneNodePtr) override {}

    // ---- editor camera ----
    iris::CameraNodePtr editorCamera() override { return mEditorCam; }
    void setEditorCamera(iris::CameraNodePtr camera) override { if (camera) mEditorCam = camera; }
    void resetEditorCam() override
    {
        mEditorCam = iris::CameraNode::create();
        mEditorCam->setLocalPos(QVector3D(0, 5, 14));
        mEditorCam->lookAt(QVector3D(0, 0, 0));
        mEditorCam->angle = 45.0f;
        mEditorCam->nearClip = 0.1f;
        mEditorCam->farClip = 1000.0f;
    }
    void setFreeCameraMode() override {}
    void setArcBallCameraMode() override {}
    void setEditorData(EditorData *data) override
    {
        mEditorData = data;
        if (data && data->editorCamera) mEditorCam = data->editorCamera;
        if (mScene && mEditorCam) mScene->setCamera(mEditorCam);
    }
    EditorData *getEditorData() override
    {
        if (!mEditorData) mEditorData = new EditorData();
        mEditorData->editorCamera = mEditorCam;
        mEditorData->showLightWires = false;
        mEditorData->showDebugDrawFlags = false;
        return mEditorData;
    }

    // ---- modes ----
    void setWindowSpace(WindowSpaces) override {}
    void setSceneMode(SceneMode) override {}
    void enterEditorMode() override {}
    void enterPlayerMode() override {}

    // ---- gizmos ----
    void setGizmoLoc() override {}
    void setGizmoRot() override {}
    void setGizmoScale() override {}
    void setGizmoTransformToLocal() override {}
    void setGizmoTransformToGlobal() override {}

    // ---- play / physics ----
    void startPlayingScene() override {}
    void pausePlayingScene() override {}
    void stopPlayingScene() override {}
    void startPhysicsSimulation() override {}
    void restartPhysicsSimulation() override {}
    void stopPhysicsSimulation() override {}

    // ---- overlays / output ----
    bool getShowLightWires() const override { return false; }
    void setShowLightWires(bool) override {}
    bool getShowDebugDrawFlags() const override { return false; }
    void setShowDebugDrawFlags(bool) override {}
    void setShowFps(bool) override {}
    void setShowPerspeciveLabel(bool) override {}
    QImage takeScreenshot(int, int) override { return QImage(); }
    QImage takeScreenshot(QSize) override { return QImage(); }

    // ---- lifecycle ----
    void begin() override {}
    void end() override {}
    bool isInitialized() override { return true; }
    void cleanup() override { mScene.clear(); }

private:
    QWidget *mWidget = nullptr;
    EditorViewportEvents *mEvents = nullptr;
    iris::ScenePtr mScene;
    iris::CameraNodePtr mEditorCam;
    EditorData *mEditorData = nullptr;
};

#endif // HEADLESSEDITORVIEWPORT_H
