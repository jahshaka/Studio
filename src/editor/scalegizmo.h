#ifndef SCALEGIZMO_H
#define SCALEGIZMO_H

#include "gizmoinstance.h"
#include "gizmohandle.h"

class ScaleGizmo : public GizmoInstance
{
private:

    GizmoHandle* handles[3];
    GizmoHandle* gizmo;
    GizmoHandle* activeHandle;

    QOpenGLShaderProgram* handleShader;

public:
//    QSharedPointer<iris::Scene> POINTER;
//    QSharedPointer<iris::SceneNode> lastSelectedNode;
//    QSharedPointer<iris::SceneNode> currentNode;
//    QVector3D finalHitPoint;
//    QVector3D translatePlaneNormal;
//    float translatePlaneD;

    QSharedPointer<iris::Scene> POINTER;
    QSharedPointer<iris::SceneNode> getRootNode() {
        return this->POINTER->getRootNode();
    }

    ScaleGizmo() {

        POINTER = iris::Scene::create();

        const QString objPath = "app/models/scale_x.obj";
        handles[HANDLE_XAXIS] = new GizmoHandle(objPath, "axis__x");
        handles[HANDLE_XAXIS]->setHandleColor(QColor(255, 0, 0, 96));
        POINTER->rootNode->addChild(handles[HANDLE_XAXIS]->gizmoHandle);

        handles[HANDLE_YAXIS] = new GizmoHandle("app/models/scale_y.obj", "axis__y");
        handles[HANDLE_YAXIS]->setHandleColor(QColor(0, 255, 0, 96));
        POINTER->rootNode->addChild(handles[HANDLE_YAXIS]->gizmoHandle);

        handles[HANDLE_ZAXIS] = new GizmoHandle("app/models/scale_z.obj", "axis__z");
        handles[HANDLE_ZAXIS]->setHandleColor(QColor(0, 0, 255, 96));
        POINTER->rootNode->addChild(handles[HANDLE_ZAXIS]->gizmoHandle);
    }

    void setPlaneNormal() {

    }

    ~ScaleGizmo() {
        // pass
    }

    void update() {
//        for (int i = 0; i < 3; i++) {
//            handles[i]->localTransform.setToIdentity();
//            handles[i]->localTransform.translate(handles[i]->getHandlePosition());
//            handles[i]->localTransform.rotate(handles[i]->getHandleRotation());

//            handles[i]->globalTransform = handles[i]->localTransform;
//        }
    }

    void createHandleShader() {
        QOpenGLShader *vshader = new QOpenGLShader(QOpenGLShader::Vertex);
        vshader->compileSourceFile("app/shaders/gizmo.vert");
        QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment);
        fshader->compileSourceFile("app/shaders/gizmo.frag");

        handleShader = new QOpenGLShaderProgram;
        handleShader->addShader(vshader);
        handleShader->addShader(fshader);

        handleShader->link();
    }

    void render(QOpenGLFunctions_3_2_Core* gl, QMatrix4x4& viewMatrix, QMatrix4x4& projMatrix) {
        handleShader->bind();

        QMatrix4x4 widgetPos;
        widgetPos.setToIdentity();

        // wtf...
        if (!!this->currentNode && !!this->lastSelectedNode &&
                (this->currentNode->getName() == "axis__x" ||
                 this->currentNode->getName() == "axis__y" ||
                 this->currentNode->getName() == "axis__z"))
        {
            widgetPos.translate(this->currentNode->pos);
            for (int i = 0; i < 3; i++) {
                handles[i]->gizmoHandle->pos = this->currentNode->pos;
            }
        } else if (!!this->lastSelectedNode) {
            widgetPos.translate(this->lastSelectedNode->pos);
            for (int i = 0; i < 3; i++) {
                handles[i]->gizmoHandle->pos = this->lastSelectedNode->pos;
            }
        }

        POINTER->update(0);

        gl->glDisable(GL_CULL_FACE);
        gl->glClear(GL_DEPTH_BUFFER_BIT);

        for (int i = 0; i < 3; i++) {
//            widgetPos.translate(handles[i]->getHandlePosition());
//            widgetPos.rotate(handles[i]->getHandleRotation());

            handleShader->setUniformValue("u_worldMatrix", widgetPos);
            handleShader->setUniformValue("u_viewMatrix", viewMatrix);
            handleShader->setUniformValue("u_projMatrix", projMatrix);

            handleShader->setUniformValue("color", handles[i]->getHandleColor());
            handles[i]->gizmoHandle->getMesh()->draw(gl, handleShader);
        }

        gl->glEnable(GL_CULL_FACE);
    }

    bool onHandleSelected(QString name, int x, int y) {

    }

    bool onMousePress(int x, int y) {

    }

    bool onMouseMove(int x, int y) {

    }

    bool onMouseRelease(int x, int y) {

    }
};

#endif // SCALEGIZMO_H
