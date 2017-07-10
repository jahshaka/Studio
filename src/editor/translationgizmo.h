/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TRANSLATIONGIZMO_H
#define TRANSLATIONGIZMO_H

#include <QUndoStack>

#include "gizmoinstance.h"
#include "gizmohandle.h"
#include "../irisgl/src/scenegraph/scene.h"
#include "../irisgl/src/graphics/graphicshelper.h"
#include "../uimanager.h"
#include "../commands/transfrormscenenodecommand.h"


class TranslationGizmo : public GizmoInstance
{
private:

    GizmoHandle* handles[3];
    GizmoHandle* gizmo;
    GizmoHandle* activeHandle;

    QOpenGLShaderProgram* handleShader;
    float scale;
    const float gizmoSize = .038f;

public:

    QSharedPointer<iris::SceneNode> getRootNode() {
        return this->POINTER->getRootNode();
    }

    TranslationGizmo(const QSharedPointer<iris::CameraNode>& camera) {
        gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
        this->camera = camera;
        scale = 1.f;
        POINTER = iris::Scene::create();

        // todo load one obj
        const QString objPath = "app/models/axis_x.obj";
        // having to cast to int kinda defeats the point of nice short enums huh...
        handles[(int) AxisHandle::X] = new GizmoHandle("app/models/axis_x.obj", "axis__x");
        handles[(int) AxisHandle::X]->setHandleColor(QColor(231, 76, 60));
        POINTER->rootNode->addChild(handles[(int) AxisHandle::X]->gizmoHandle);

        handles[(int) AxisHandle::Y] = new GizmoHandle("app/models/axis_y.obj", "axis__y");
        handles[(int) AxisHandle::Y]->setHandleColor(QColor(46, 224, 113));
        POINTER->rootNode->addChild(handles[(int) AxisHandle::Y]->gizmoHandle);

        handles[(int) AxisHandle::Z] = new GizmoHandle("app/models/axis_z.obj", "axis__z");
        handles[(int) AxisHandle::Z]->setHandleColor(QColor(37, 118, 235));
        POINTER->rootNode->addChild(handles[(int) AxisHandle::Z]->gizmoHandle);
    }

    void setPlaneOrientation(const QString& axis) {
        if (axis == "axis__x")      translatePlaneNormal = QVector3D(.0f, 1.f, .0f);
        else if (axis == "axis__y") translatePlaneNormal = QVector3D(.0f, .0f, 1.f);
        else if (axis == "axis__z") translatePlaneNormal = QVector3D(.0f, 1.f, .0f);

//        if (transformOrientation == "Local") {
//            translatePlaneNormal =
//                    lastSelectedNode->rot.rotatedVector(translatePlaneNormal).normalized();
//        }
    }

    QString getTransformOrientation() {
        return transformOrientation;
    }

    void setTransformOrientation(const QString& type) {
        transformOrientation = type;
    }

    void updateTransforms(const QVector3D& pos) {
        scale = gizmoSize * ((pos - lastSelectedNode->getLocalPos()).length() / qTan(45.0f / 2.0f));

        for (int i = 0; i < 3; i++) {
            handles[i]->gizmoHandle->setLocalPos(lastSelectedNode->getGlobalPosition());
//            if (transformOrientation == "Local") {
//                handles[i]->gizmoHandle->rot = lastSelectedNode->rot;
//            } else {
//                handles[i]->gizmoHandle->rot = QQuaternion();
//            }
            handles[i]->gizmoHandle->setLocalScale(QVector3D(scale, scale, scale));
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
            QVector3D Offset = Point - finalHitPoint;

            // standard - move to set plane orientation func
            if (currentNode->getName() == "axis__x") {
                Offset = QVector3D(Offset.x(), 0, 0);
            } else if (currentNode->getName() == "axis__y") {
                Offset = QVector3D(0, Offset.y(), 0);
            } else if (currentNode->getName() == "axis__z") {
                Offset = QVector3D(0, 0, Offset.z());
            }

            currentNode->setLocalPos( currentNode->getLocalPos() + Offset);
            lastSelectedNode->setLocalPos( lastSelectedNode->getLocalPos() + Offset);

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

    void render(QMatrix4x4& viewMatrix, QMatrix4x4& projMatrix) {
        handleShader->bind();

        if (!!this->currentNode) {
            for (int i = 0; i < 3; i++) {
                handles[i]->gizmoHandle->setLocalPos(currentNode->getLocalPos());
            }
        } else {
            for (int i = 0; i < 3; i++) {
                handles[i]->gizmoHandle->setLocalPos(lastSelectedNode->getGlobalPosition());
            }
        }

        POINTER->update(0);

        gl->glDisable(GL_CULL_FACE);
        gl->glClear(GL_DEPTH_BUFFER_BIT);

        for (int i = 0; i < 3; i++) {
            QMatrix4x4 widgetPos;
            widgetPos.setToIdentity();

            widgetPos.translate(handles[i]->gizmoHandle->getLocalPos());
//            widgetPos.rotate(handles[i]->gizmoHandle->rot);
            widgetPos.scale(scale);

            handleShader->setUniformValue("u_worldMatrix", widgetPos);
            handleShader->setUniformValue("u_viewMatrix", viewMatrix);
            handleShader->setUniformValue("u_projMatrix", projMatrix);

            handleShader->setUniformValue("showHalf", false);
            handleShader->setUniformValue("color", handles[i]->getHandleColor());
            handles[i]->gizmoHandle->getMesh()->draw(gl, handleShader);
        }

        gl->glEnable(GL_CULL_FACE);
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

        //add command here
        if (!!lastSelectedNode) {
            auto endTransform = lastSelectedNode->getLocalTransform();
            // reset the transform because the command will re-apply the transform
            lastSelectedNode->setLocalTransform(hitTransform);
            UiManager::undoStack->push(new TransformSceneNodeCommand(lastSelectedNode, endTransform));
        }
    }

    void isGizmoHit(const iris::CameraNodePtr& camera, const QPointF& pos, const QVector3D& rayDir) {
        camera->updateCameraMatrices();

        auto segStart = camera->getLocalPos();
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
        hitTransform = lastSelectedNode->getLocalTransform();
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

#endif // TRANSLATIONGIZMO_H
