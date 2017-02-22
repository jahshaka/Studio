#include "editorvrcontroller.h"
#include "../irisgl/src/graphics/renderitem.h"
#include "../irisgl/src/graphics/material.h"
#include "../irisgl/src/graphics/mesh.h"
#include "../irisgl/src/materials/defaultmaterial.h"
#include "../irisgl/src/vr/vrdevice.h"
#include "../irisgl/src/vr/vrmanager.h"
#include "../irisgl/src/core/scene.h"
#include "../irisgl/src/core/scenenode.h"
#include "../irisgl/src/core/irisutils.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include "../core/keyboardstate.h"


EditorVrController::EditorVrController()
{
    //auto cube = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/primitives/cube.obj"));
    auto leftHandModel = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/models/external_controller01_left.obj"));

    auto mat = iris::DefaultMaterial::create();
    mat->setDiffuseTexture(
                iris::Texture2D::load(
                    IrisUtils::getAbsoluteAssetPath("app/content/models/external_controller01_col.png")));

    leftHandRenderItem = new iris::RenderItem();
    leftHandRenderItem->type = iris::RenderItemType::Mesh;
    leftHandRenderItem->material = mat;
    leftHandRenderItem->mesh = leftHandModel;

    auto rightHandModel = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/models/external_controller01_right.obj"));
    rightHandRenderItem = new iris::RenderItem();
    rightHandRenderItem->type = iris::RenderItemType::Mesh;
    rightHandRenderItem->material = mat;
    rightHandRenderItem->mesh = rightHandModel;
}

void EditorVrController::setScene(iris::ScenePtr scene)
{
    this->scene = scene;
}

void EditorVrController::update(float dt)
{
    vrDevice = iris::VrManager::getDefaultDevice();
    const float linearSpeed = 1.4f;

    // keyboard movement
    const QVector3D upVector(0, 1, 0);
    auto rot = vrDevice->getHeadRotation();
    auto forwardVector = rot.rotatedVector(QVector3D(0, 0, -1));
    auto x = QVector3D::crossProduct(forwardVector,upVector).normalized();
    auto z = QVector3D::crossProduct(upVector,x).normalized();

    // left
    if(KeyboardState::isKeyDown(Qt::Key_Left) ||KeyboardState::isKeyDown(Qt::Key_A) )
        camera->pos -= x * linearSpeed;

    // right
    if(KeyboardState::isKeyDown(Qt::Key_Right) ||KeyboardState::isKeyDown(Qt::Key_D) )
        camera->pos += x * linearSpeed;

    // up
    if(KeyboardState::isKeyDown(Qt::Key_Up) ||KeyboardState::isKeyDown(Qt::Key_W) )
        camera->pos += z * linearSpeed;

    // down
    if(KeyboardState::isKeyDown(Qt::Key_Down) ||KeyboardState::isKeyDown(Qt::Key_S) )
        camera->pos -= z * linearSpeed;

    // touch controls
    auto dir = vrDevice->getTouchController(0)->GetThumbstick();
    //qDebug() << dir;
    camera->pos += x * dir.x();
    camera->pos += z * dir.y();
    camera->rot = QQuaternion();
    camera->update(0);


    // Submit items to renderer
    auto device = iris::VrManager::getDefaultDevice();

    QMatrix4x4 world;
    world.setToIdentity();
    world.translate(device->getHandPosition(0));
    world.rotate(device->getHandRotation(0));
    world.scale(0.55f);
    leftHandRenderItem->worldMatrix = camera->globalTransform * world;


    world.setToIdentity();
    world.translate(device->getHandPosition(1));
    world.rotate(device->getHandRotation(1));
    world.scale(0.55f);
    rightHandRenderItem->worldMatrix = camera->globalTransform * world;

    scene->geometryRenderList.append(leftHandRenderItem);
    scene->geometryRenderList.append(rightHandRenderItem);
}


