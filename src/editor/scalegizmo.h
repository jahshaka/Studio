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

    ScaleGizmo(const QSharedPointer<iris::CameraNode>& camera) {

        this->camera = camera;
        POINTER = iris::Scene::create();

        handles[(int) AxisHandle::X] = new GizmoHandle("app/models/scale_x.obj", "axis__x");
        handles[(int) AxisHandle::X]->setHandleColor(QColor(231, 76, 60));
        POINTER->rootNode->addChild(handles[(int) AxisHandle::X]->gizmoHandle);

        handles[(int) AxisHandle::Y] = new GizmoHandle("app/models/scale_y.obj", "axis__y");
        handles[(int) AxisHandle::Y]->setHandleColor(QColor(46, 224, 113));
        POINTER->rootNode->addChild(handles[(int) AxisHandle::Y]->gizmoHandle);

        handles[(int) AxisHandle::Z] = new GizmoHandle("app/models/scale_z.obj", "axis__z");
        handles[(int) AxisHandle::Z]->setHandleColor(QColor(37, 118, 235));
        POINTER->rootNode->addChild(handles[(int) AxisHandle::Z]->gizmoHandle);
    }

    void setPlaneOrientation(const QString& axis) {
        if (axis == "axis__x")      translatePlaneNormal = QVector3D(.0f, 1.f, .0f);
        else if (axis == "axis__y") translatePlaneNormal = QVector3D(.0f, .0f, 1.f);
        else if (axis == "axis__z") translatePlaneNormal = QVector3D(.0f, 1.f, .0f);
    }

    ~ScaleGizmo() {
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

            // scale the handles but not the box part
//            currentNode->pos += Offset;
            lastSelectedNode->scale += Offset;

            finalHitPoint = Point;
        }

        if (lastHitAxis == "axis__x") {
            handles[(int) AxisHandle::X]->setHandleColor(QColor(245, 245, 0, 200));
        }

        if (lastHitAxis == "axis__y") {
            handles[(int) AxisHandle::Y]->setHandleColor(QColor(245, 245, 0, 200));
        }

        if (lastHitAxis == "axis__z") {
            handles[(int) AxisHandle::Z]->setHandleColor(QColor(245, 245, 0, 200));
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

    void onMousePress(QVector3D pos, QVector3D r) {
        translatePlaneD = -QVector3D::dotProduct(translatePlaneNormal, finalHitPoint);

        QVector3D ray = (r - pos).normalized();
        float nDotR = -QVector3D::dotProduct(translatePlaneNormal, ray);

        if (nDotR != 0.0f) {
            float distance = (QVector3D::dotProduct(translatePlaneNormal, pos) + translatePlaneD) / nDotR;
            finalHitPoint = ray * distance + pos; // initial hit
        }
    }

    void isHandleHit() {
        if (!!hitNode && hitNode->getName() == "axis__x") {
            handles[(int) AxisHandle::X]->setHandleColor(QColor(245, 245, 0, 200));
        } else {
            handles[(int) AxisHandle::X]->setHandleColor(QColor(231, 76, 60));
        }

        if (!!hitNode && hitNode->getName() == "axis__y") {
            handles[(int) AxisHandle::Y]->setHandleColor(QColor(245, 245, 0, 200));
        } else {
            handles[(int) AxisHandle::Y]->setHandleColor(QColor(46, 224, 113));
        }

        if (!!hitNode && hitNode->getName() == "axis__z") {
            handles[(int) AxisHandle::Z]->setHandleColor(QColor(245, 245, 0, 200));
        } else {
            handles[(int) AxisHandle::Z]->setHandleColor(QColor(37, 118, 235));
        }
    }

    void onMouseRelease() {
        handles[(int) AxisHandle::X]->setHandleColor(QColor(231, 76, 60));
        handles[(int) AxisHandle::Y]->setHandleColor(QColor(46, 224, 113));
        handles[(int) AxisHandle::Z]->setHandleColor(QColor(37, 118, 235));
    }

    void isGizmoHit(const iris::CameraNodePtr& camera, const QPointF& pos, const QVector3D& rayDir) {
        camera->updateCameraMatrices();

        auto segStart = camera->pos;
        auto segEnd = segStart + rayDir * 512;

        QList<PickingResult> hitList;
        doMeshPicking(POINTER->getRootNode(), segStart, segEnd, hitList);

        if (hitList.size() == 0) {
            hitNode = iris::SceneNodePtr();
            return;
        }

        qSort(hitList.begin(), hitList.end(), [](const PickingResult& a, const PickingResult& b) {
            return a.distanceFromCameraSqrd > b.distanceFromCameraSqrd;
        });

        hitNode = hitList.last().hitNode;
        lastHitAxis = hitNode->getName();
    }

    void doMeshPicking(const QSharedPointer<iris::SceneNode>& sceneNode,
                       const QVector3D& segStart,
                       const QVector3D& segEnd,
                       QList<PickingResult>& hitList)
    {
        if (sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh)
        {
            auto meshNode = sceneNode.staticCast<iris::MeshNode>();
            auto triMesh = meshNode->getMesh()->getTriMesh();

            // transform segment to local space
            auto invTransform = meshNode->globalTransform.inverted();
            auto a = invTransform * segStart;
            auto b = invTransform * segEnd;

            QList<iris::TriangleIntersectionResult> results;
            if (int resultCount = triMesh->getSegmentIntersections(a, b, results)) {
                for (auto triResult : results) {
                    // convert hit to world space
                    auto hitPoint = meshNode->globalTransform * triResult.hitPoint;

                    PickingResult pick;
                    pick.hitNode = sceneNode;
                    pick.hitPoint = hitPoint;
                    pick.distanceFromCameraSqrd = (hitPoint - camera->getGlobalPosition()).lengthSquared();

                    hitList.append(pick);
                }
            }
        }

        for (auto child : sceneNode->children) {
            doMeshPicking(child, segStart, segEnd, hitList);
        }
    }
};

#endif // SCALEGIZMO_H
