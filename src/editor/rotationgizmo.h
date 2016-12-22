#ifndef ROTATIONGIZMO_H
#define ROTATIONGIZMO_H

#include "gizmoinstance.h"
#include "gizmohandle.h"

class RotationGizmo : public GizmoInstance
{
private:

    GizmoHandle* handles[3];
    GizmoHandle* gizmo;
    GizmoHandle* activeHandle;

    QOpenGLShaderProgram* handleShader;

public:

    QSharedPointer<iris::Scene> POINTER;
    QSharedPointer<iris::SceneNode> getRootNode() {
        return this->POINTER->getRootNode();
    }

    RotationGizmo() {

        POINTER = iris::Scene::create();

        handles[(int) AxisHandle::X] = new GizmoHandle("app/models/rot_x.obj", "axis__x");
        handles[(int) AxisHandle::X]->setHandleColor(QColor(231, 76, 60));
        POINTER->rootNode->addChild(handles[(int) AxisHandle::X]->gizmoHandle);

        handles[(int) AxisHandle::Y] = new GizmoHandle("app/models/rot_y.obj", "axis__y");
        handles[(int) AxisHandle::Y]->setHandleColor(QColor(46, 224, 113));
        POINTER->rootNode->addChild(handles[(int) AxisHandle::Y]->gizmoHandle);

        handles[(int) AxisHandle::Z] = new GizmoHandle("app/models/rot_z.obj", "axis__z");
        handles[(int) AxisHandle::Z]->setHandleColor(QColor(37, 118, 235));
        POINTER->rootNode->addChild(handles[(int) AxisHandle::Z]->gizmoHandle);
    }

    void setPlaneOrientation(const QString& axis) {
        if (axis == "axis__y") translatePlaneNormal = QVector3D(.0f, .0f, 1.f);
        else translatePlaneNormal = QVector3D(.0f, 1.f, .0f);
    }

    ~RotationGizmo() {
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

            auto prevHit = (finalHitPoint - currentNode->pos).normalized();
            auto curHit = (Point - currentNode->pos).normalized();

            if (currentNode->getName() == "axis__x") {
                auto prevAngle = qAtan2(-prevHit.z(), prevHit.x());
                auto curAngle = qAtan2(-curHit.z(), curHit.x());

                auto angleDiff = curAngle - prevAngle;

                currentNode->rot =
                     QQuaternion::fromEulerAngles(qRadiansToDegrees(angleDiff), 0, 0) * currentNode->rot;
            }

            if (currentNode->getName() == "axis__y") {
                auto prevAngle = qAtan2(-prevHit.x(), prevHit.y());
                auto curAngle = qAtan2(-curHit.x(), curHit.y());

                auto angleDiff = curAngle - prevAngle;

                currentNode->rot =
                     QQuaternion::fromEulerAngles(0, 0, qRadiansToDegrees(angleDiff)) * currentNode->rot;
            }

            if (currentNode->getName() == "axis__z") {
                auto prevAngle = qAtan2(-prevHit.z(), prevHit.x());
                auto curAngle = qAtan2(-curHit.z(), curHit.x());

                auto angleDiff = curAngle - prevAngle;

                currentNode->rot =
                     QQuaternion::fromEulerAngles(0, qRadiansToDegrees(angleDiff), 0) * currentNode->rot;
            }

            lastSelectedNode->rot = currentNode->rot;

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
            widgetPos.translate(currentNode->pos);
//            widgetPos.rotate(currentNode->rot);
            for (int i = 0; i < 3; i++) {
                handles[i]->gizmoHandle->pos = currentNode->pos;
                handles[i]->gizmoHandle->rot = currentNode->rot;
            }
        } else if (!!lastSelectedNode) {
            widgetPos.translate(lastSelectedNode->pos);
//            widgetPos.rotate(lastSelectedNode->rot);
            for (int i = 0; i < 3; i++) {
                handles[i]->gizmoHandle->pos = lastSelectedNode->pos;
                handles[i]->gizmoHandle->rot = lastSelectedNode->rot;
            }
        }

//        if (!!this->currentNode && this->currentNode->getName() == "axis__x") {
//            handles[(int) AxisHandle::X]->gizmoHandle->rot = this->currentNode->rot;
//        } else {
//            handles[(int) AxisHandle::X]->gizmoHandle->rot = this->lastSelectedNode->rot;
//        }

//        if (!!this->currentNode && this->currentNode->getName() == "axis__y") {
//            handles[(int) AxisHandle::Y]->gizmoHandle->rot = this->currentNode->rot;
//        } else {
//            handles[(int) AxisHandle::Y]->gizmoHandle->rot = this->lastSelectedNode->rot;
//        }

//        if (!!this->currentNode && this->currentNode->getName() == "axis__z") {
//            handles[(int) AxisHandle::Z]->gizmoHandle->rot = this->currentNode->rot;
//        } else {
//            handles[(int) AxisHandle::Z]->gizmoHandle->rot = this->lastSelectedNode->rot;
//        }

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

    void onMousePress(QVector3D pos, QVector3D r) {
        translatePlaneD = -QVector3D::dotProduct(translatePlaneNormal, finalHitPoint);

        QVector3D ray = (r - pos).normalized();
        float nDotR = -QVector3D::dotProduct(translatePlaneNormal, ray);

        if (nDotR != 0.0f) {
            float distance = (QVector3D::dotProduct(translatePlaneNormal, pos) + translatePlaneD) / nDotR;
            finalHitPoint = ray * distance + pos; // initial hit
        }
    }

    bool onMouseMove(int x, int y) {

    }

    bool onMouseRelease(int x, int y) {

    }
};

#endif // ROTATIONGIZMO_H
