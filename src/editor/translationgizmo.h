#ifndef TRANSLATIONGIZMO_H
#define TRANSLATIONGIZMO_H

#include "gizmoinstance.h"
#include "gizmohandle.h"

class TranslationGizmo : public GizmoInstance
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

    TranslationGizmo() {

        POINTER = iris::Scene::create();

        // todo load one obj
        const QString objPath = "app/models/axis_x.obj";
        handles[AxisHandle::X] = new GizmoHandle(objPath, "axis__x");
        handles[AxisHandle::X]->setHandleColor(QColor(255, 0, 0, 96));
        POINTER->rootNode->addChild(handles[AxisHandle::X]->gizmoHandle);

        handles[AxisHandle::Y] = new GizmoHandle("app/models/axis_y.obj", "axis__y");
        handles[AxisHandle::Y]->setHandleColor(QColor(0, 255, 0, 96));
        POINTER->rootNode->addChild(handles[AxisHandle::X]->gizmoHandle);

        handles[AxisHandle::Z] = new GizmoHandle("app/models/axis_z.obj", "axis__z");
        handles[AxisHandle::Z]->setHandleColor(QColor(0, 0, 255, 96));
        POINTER->rootNode->addChild(handles[AxisHandle::Z]->gizmoHandle);
    }

    void setPlaneNormal() {

    }

    ~TranslationGizmo() {
        // pass
    }

    void update(QVector3D pos, QVector3D r) {
        QVector3D ray = (r * 512 - pos).normalized();
        float nDotR = -QVector3D::dotProduct(translatePlaneNormal, ray);

        if (nDotR != 0.0f) {
            float distance = (QVector3D::dotProduct(
                                  translatePlaneNormal,
                                  pos) + translatePlaneD) / nDotR;
            QVector3D Point = ray * distance + pos;
            QVector3D Offset = Point - finalHitPoint;

            // standard - move to set plane orientation func
            if (currentNode->getName() == "axis__x") {
                Offset = QVector3D(Offset.x(), 0, 0);
            } else if (currentNode->getName() == "axis__y") {
                Offset = QVector3D(0, Offset.y(), 0);
            } else if (currentNode->getName() == "axis__z") {
                Offset = QVector3D(0, 0, Offset.z());
            }

            currentNode->pos += Offset;
            lastSelectedNode->pos += Offset;

            finalHitPoint = Point;
        }
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
        if (!!this->currentNode &&
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

#endif // TRANSLATIONGIZMO_H
