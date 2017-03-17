/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ROTATIONGIZMO_H
#define ROTATIONGIZMO_H

#include "gizmoinstance.h"
#include "gizmohandle.h"
#include "../irisgl/src/graphics/graphicshelper.h"

class RotationGizmo : public GizmoInstance
{
private:

    GizmoHandle* handles[3];
    GizmoHandle* gizmo;
    GizmoHandle* activeHandle;

    QOpenGLShaderProgram* handleShader;
    float scale;
    const float gizmoSize = .0175f;

public:
    QSharedPointer<iris::SceneNode> getRootNode() {
        return this->POINTER->getRootNode();
    }

    RotationGizmo(const QSharedPointer<iris::CameraNode>& camera) {
        gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
        this->camera = camera;
        POINTER = iris::Scene::create();

        handles[(int) AxisHandle::X] = new GizmoHandle("app/models/rot_x.obj", "axis__x");
        handles[(int) AxisHandle::X]->setHandleColor(QColor(231, 76, 60));
        POINTER->rootNode->addChild(handles[(int) AxisHandle::X]->gizmoHandle);

        handles[(int) AxisHandle::Y] = new GizmoHandle("app/models/rot_y.obj", "axis__y");
        handles[(int) AxisHandle::Y]->setHandleColor(QColor(37, 118, 235));
        POINTER->rootNode->addChild(handles[(int) AxisHandle::Y]->gizmoHandle);

        handles[(int) AxisHandle::Z] = new GizmoHandle("app/models/rot_z.obj", "axis__z");
        handles[(int) AxisHandle::Z]->setHandleColor(QColor(46, 224, 113));
        POINTER->rootNode->addChild(handles[(int) AxisHandle::Z]->gizmoHandle);
    }

    void setPlaneOrientation(const QString& axis) {
        if (axis == "axis__x")      translatePlaneNormal = QVector3D(1.f, .0f, 0.f);
        else if (axis == "axis__y") translatePlaneNormal = QVector3D(.0f, .0f, 1.f);
        else if (axis == "axis__z") translatePlaneNormal = QVector3D(.0f, 1.f, 0.f);

        if (transformOrientation == "Local") {
            translatePlaneNormal =
                    lastSelectedNode->rot.rotatedVector(translatePlaneNormal).normalized();
        }
    }

    void updateTransforms(const QVector3D& pos) {
        scale = gizmoSize * ((pos - lastSelectedNode->pos).length() / qTan(45.0f / 2.0f));

        for (int i = 0; i < 3; i++) {
            if (transformOrientation == "Local") {
                handles[i]->gizmoHandle->rot = lastSelectedNode->rot;
            } else {
                handles[i]->gizmoHandle->rot = QQuaternion();
            }

            handles[i]->gizmoHandle->pos = lastSelectedNode->getGlobalPosition();
            handles[i]->gizmoHandle->scale = QVector3D(scale, scale, scale);
        }
    }

    void update(QVector3D pos, QVector3D r) {
        QVector3D ray = (r * 1024 - pos).normalized();
        float nDotR = -QVector3D::dotProduct(translatePlaneNormal, ray);

        if (nDotR != 0.0f) {
            float distance = (QVector3D::dotProduct(
                                  translatePlaneNormal,
                                  pos) + translatePlaneD) / nDotR;
            QVector3D Point = ray * distance + pos;

            auto prevHit = (finalHitPoint - currentNode->pos).normalized();
            auto curHit = (Point - currentNode->pos).normalized();

            QQuaternion rot;

            if (currentNode->getName() == "axis__x") {
                auto prevAngle = qAtan2(-prevHit.y(), prevHit.z());
                auto curAngle = qAtan2(-curHit.y(), curHit.z());

                auto angleDiff = curAngle - prevAngle;

                rot = QQuaternion::fromEulerAngles(qRadiansToDegrees(angleDiff), 0, 0);
            }

            if (currentNode->getName() == "axis__y") {
                auto prevAngle = qAtan2(-prevHit.x(), prevHit.y());
                auto curAngle = qAtan2(-curHit.x(), curHit.y());

                auto angleDiff = curAngle - prevAngle;

                rot = QQuaternion::fromEulerAngles(0, 0, qRadiansToDegrees(angleDiff));
            }

            if (currentNode->getName() == "axis__z") {
                auto prevAngle = qAtan2(-prevHit.z(), prevHit.x());
                auto curAngle = qAtan2(-curHit.z(), curHit.x());

                auto angleDiff = curAngle - prevAngle;

                rot = QQuaternion::fromEulerAngles(0, qRadiansToDegrees(angleDiff), 0);
            }

            // if we're behind the normal for the axis, change the direction of the rotation
            if (nDotR < 0) {
                rot = -rot;
            }

            // local orientation
            if (transformOrientation == "Local") {
                lastSelectedNode->rot = lastSelectedNode->rot * rot;

                for (int i = 0; i < 3; i++) {
                    handles[i]->gizmoHandle->rot = lastSelectedNode->rot;
                }
            } else {
                lastSelectedNode->rot = rot * lastSelectedNode->rot;
            }

            lastSelectedNode->rot.normalize();

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
        handleShader = iris::GraphicsHelper::loadShader(
                    IrisUtils::getAbsoluteAssetPath("app/shaders/gizmo.vert"),
                    IrisUtils::getAbsoluteAssetPath("app/shaders/gizmo.frag")
        );

        handleShader->link();
    }

    QString getTransformOrientation() {
        return transformOrientation;
    }

    void setTransformOrientation(const QString& type) {
        transformOrientation = type;
    }

    void render(QMatrix4x4& viewMatrix, QMatrix4x4& projMatrix) {
        handleShader->bind();

        if (!!this->currentNode) {
            for (int i = 0; i < 3; i++) {
                handles[i]->gizmoHandle->pos = currentNode->pos;
            }
        } else {
            for (int i = 0; i < 3; i++) {
                handles[i]->gizmoHandle->pos = lastSelectedNode->pos;
            }
        }

        POINTER->update(0);

        // draw this gizmo over every other object in the scene
        gl->glDisable(GL_CULL_FACE);
        gl->glEnable(GL_BLEND);
        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl->glClear(GL_DEPTH_BUFFER_BIT);

        // every axis moves independently
        for (int i = 0; i < 3; i++) {
            QMatrix4x4 widgetPos;
            widgetPos.setToIdentity();

            widgetPos.translate(handles[i]->gizmoHandle->pos);
            widgetPos.rotate(handles[i]->gizmoHandle->rot);
            widgetPos.scale(scale);

            handleShader->setUniformValue("u_worldMatrix", widgetPos);
            handleShader->setUniformValue("u_viewMatrix", viewMatrix);
            handleShader->setUniformValue("u_projMatrix", projMatrix);

            handleShader->setUniformValue("showHalf", true);
            handleShader->setUniformValue("color", handles[i]->getHandleColor());
            handles[i]->gizmoHandle->getMesh()->draw(gl, handleShader);
        }

        gl->glDisable(GL_BLEND);
        gl->glEnable(GL_CULL_FACE);

        handleShader->release();
    }

    void onMousePress(QVector3D pos, QVector3D r) {
        if (!lastSelectedNode)
            return;

        translatePlaneD = -QVector3D::dotProduct(translatePlaneNormal, lastSelectedNode->getGlobalPosition());

        QVector3D ray = (r - pos).normalized();
        float nDotR = -QVector3D::dotProduct(translatePlaneNormal, ray);

        if (nDotR != 0.0f) {
            float distance = (QVector3D::dotProduct(translatePlaneNormal, pos) + translatePlaneD) / nDotR;
            finalHitPoint = ray * distance + pos;
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
        auto segEnd = segStart + rayDir * 1024;

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

#endif // ROTATIONGIZMO_H
