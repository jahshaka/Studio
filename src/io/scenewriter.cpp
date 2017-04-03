/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <Qt>
#include <qvector.h>

#include <QQuaternion>
#include <QVector3D>
#include <QDir>
#include <QFile>

#include "../editor/editordata.h"

#include "../irisgl/src/core/scene.h"
#include "../irisgl/src/core/scenenode.h"
#include "../irisgl/src/scenegraph/meshnode.h"
#include "../irisgl/src/scenegraph/lightnode.h"
#include "../irisgl/src/scenegraph/viewernode.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include "../irisgl/src/scenegraph/particlesystemnode.h"
#include "../irisgl/src/materials/custommaterial.h"
#include "../irisgl/src/animation/animation.h"
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/animation/keyframeset.h"

#include "scenewriter.h"
#include "assetiobase.h"


void SceneWriter::writeScene(QString filePath,iris::ScenePtr scene,EditorData* editorData)
{
    dir = AssetIOBase::getDirFromFileName(filePath);
    QFile file(filePath);
    file.open(QIODevice::WriteOnly|QIODevice::Truncate);

    QJsonObject projectObj;
    projectObj["version"] = "0.1";
    writeScene(projectObj, scene);
    if(editorData != nullptr)
        writeEditorData(projectObj, editorData);

    QJsonDocument saveDoc(projectObj);
    file.write(saveDoc.toJson());
    file.close();
}

void SceneWriter::writeScene(QJsonObject& projectObj,iris::ScenePtr scene)
{
    QJsonObject sceneObj;

    //scene properties
    if (!!scene->skyTexture) {
        sceneObj["skyTexture"] = /*this->getRelativePath(*/scene->skyTexture->getSource();//);
    } else {
        sceneObj["skyTexture"] = "";
    }

    sceneObj["skyColor"] = jsonColor(scene->skyColor);
    sceneObj["ambientColor"] = jsonColor(scene->ambientColor);

    sceneObj["fogColor"] = jsonColor(scene->fogColor);
    sceneObj["fogStart"] = scene->fogStart;
    sceneObj["fogEnd"] = scene->fogEnd;
    sceneObj["fogEnabled"] = scene->fogEnabled;


    QJsonObject rootNodeObj;
    writeSceneNode(rootNodeObj,scene->getRootNode());
    sceneObj["rootNode"] = rootNodeObj;

    projectObj["scene"] = sceneObj;
}

void SceneWriter::writeEditorData(QJsonObject& projectObj,EditorData* editorData)
{
    QJsonObject editorObj;

    QJsonObject cameraObj;
    auto cam = editorData->editorCamera;
    cameraObj["angle"] = cam->angle;
    cameraObj["nearClip"] = cam->nearClip;
    cameraObj["farClip"] = cam->farClip;
    cameraObj["pos"] = jsonVector3(editorData->editorCamera->pos);
    cameraObj["rot"] = jsonVector3(editorData->editorCamera->rot.toEulerAngles());

    editorObj["camera"] = cameraObj;
    projectObj["editor"] = editorObj;
}

void SceneWriter::writeSceneNode(QJsonObject& sceneNodeObj,iris::SceneNodePtr sceneNode)
{
    sceneNodeObj["name"] = sceneNode->getName();
    sceneNodeObj["type"] = getSceneNodeTypeName(sceneNode->sceneNodeType);

    sceneNodeObj["pos"] = jsonVector3(sceneNode->pos);
    auto rot = sceneNode->rot.toEulerAngles();
    sceneNodeObj["rot"] = jsonVector3(rot);
    sceneNodeObj["scale"] = jsonVector3(sceneNode->scale);


    //todo: write data specific to node type
    switch (sceneNode->sceneNodeType) {
        case iris::SceneNodeType::Mesh:
            writeMeshData(sceneNodeObj, sceneNode.staticCast<iris::MeshNode>());
        break;
        case iris::SceneNodeType::Light:
            writeLightData(sceneNodeObj, sceneNode.staticCast<iris::LightNode>());
        break;
        case iris::SceneNodeType::Viewer:
            writeViewerData(sceneNodeObj, sceneNode.staticCast<iris::ViewerNode>());
        break;
        case iris::SceneNodeType::ParticleSystem:
            writeParticleData(sceneNodeObj, sceneNode.staticCast<iris::ParticleSystemNode>());
        break;
        default: break;
    }

    writeAnimationData(sceneNodeObj,sceneNode);

    QJsonArray childrenArray;
    for (auto childNode : sceneNode->children) {
        QJsonObject childNodeObj;
        writeSceneNode(childNodeObj, childNode);
        childrenArray.append(childNodeObj);
    }

    sceneNodeObj["children"] = childrenArray;
}

void SceneWriter::writeAnimationData(QJsonObject& sceneNodeObj,iris::SceneNodePtr sceneNode)
{
    auto anim = sceneNode->animation;
    if(!anim)
        return;

    QJsonObject animObj;
    animObj["name"] = anim->name;
    animObj["length"] = anim->length;
    animObj["loop"] = anim->loop;

    QJsonArray frames;
    for(auto frameName:anim->keyFrameSet->keyFrames.keys())
    {
        auto frame = anim->keyFrameSet->keyFrames[frameName];

        QJsonObject frameObj;
        QJsonArray keys;
        for(auto key:frame->keys)
        {
            QJsonObject keyObj;
            keyObj["time"] = key->time;
            keyObj["value"] = key->value;

            keys.append(keyObj);
        }

        frameObj["keys"] = keys;
        frameObj["name"] = frameName;
        frames.append(frameObj);
    }

    animObj["frames"] = frames;

    sceneNodeObj["animation"] = animObj;
}

void SceneWriter::writeMeshData(QJsonObject& sceneNodeObject, iris::MeshNodePtr meshNode)
{
    // TODO: handle generated meshes properly
    // ???? sure...
    sceneNodeObject["mesh"] = getRelativePath(meshNode->meshPath);
    sceneNodeObject["meshIndex"] = meshNode->meshIndex;
    sceneNodeObject["pickable"] = meshNode->pickable;

    auto cullMode = meshNode->getFaceCullingMode();
    switch (cullMode) {
    case iris::FaceCullingMode::Back:
        sceneNodeObject["faceCullingMode"] = "back";
        break;
    case iris::FaceCullingMode::Front:
        sceneNodeObject["faceCullingMode"] = "front";
        break;
    case iris::FaceCullingMode::FrontAndBack:
        sceneNodeObject["faceCullingMode"] = "frontandback";
        break;
    case iris::FaceCullingMode::None:
        sceneNodeObject["faceCullingMode"] = "none";
        break;
    }

    // todo: check if material actually exists
    auto mat = meshNode->getMaterial().staticCast<iris::CustomMaterial>();
    QJsonObject matObj;
    writeSceneNodeMaterial(matObj, mat);
    sceneNodeObject["material"] = matObj;
}

void SceneWriter::writeViewerData(QJsonObject& sceneNodeObject,iris::ViewerNodePtr viewerNode)
{
    sceneNodeObject["viewScale"] = viewerNode->getViewScale();
}

void SceneWriter::writeParticleData(QJsonObject& sceneNodeObject, iris::ParticleSystemNodePtr node)
{
    sceneNodeObject["particlesPerSecond"]   = node->particlesPerSecond;
    sceneNodeObject["particleScale"]        = node->particleScale;
    sceneNodeObject["dissipate"]            = node->dissipate;
    sceneNodeObject["dissipateInv"]         = node->dissipateInv;
    sceneNodeObject["gravityComplement"]    = node->gravityComplement;
    sceneNodeObject["randomRotation"]       = node->randomRotation;
    sceneNodeObject["blendMode"]            = node->useAdditive;
    sceneNodeObject["lifeLength"]           = node->lifeLength;
    sceneNodeObject["speed"]                = node->speed;
    sceneNodeObject["texture"]              = node->texture->getSource();
}

void SceneWriter::writeSceneNodeMaterial(QJsonObject& matObj, iris::CustomMaterialPtr mat)
{
    matObj["name"] = mat->getMaterialName();

    for (auto s : mat->colorUniforms) {
        matObj[s.name] = s.value.name();
    }

    // TODO - nick can you fix these path path things... too many. idk
    for (auto s : mat->textureUniforms) {
        matObj[s.name] = getRelativePath(s.value);
    }

    for (auto s : mat->sliderUniforms) {
        matObj[s.name] = s.value;
    }
}

QJsonObject SceneWriter::jsonColor(QColor color)
{
    QJsonObject colObj;
    colObj["r"] = color.red();
    colObj["g"] = color.green();
    colObj["b"] = color.blue();
    colObj["a"] = color.alpha();

    return colObj;
}

QJsonObject SceneWriter::jsonVector3(QVector3D vec)
{
    QJsonObject obj;
    obj["x"] = vec.x();
    obj["y"] = vec.y();
    obj["z"] = vec.z();

    return obj;
}

void SceneWriter::writeLightData(QJsonObject& sceneNodeObject,iris::LightNodePtr lightNode)
{
    sceneNodeObject["lightType"] = getLightNodeTypeName(lightNode->lightType);
    sceneNodeObject["intensity"] = lightNode->intensity;
    sceneNodeObject["distance"] = lightNode->distance;
    sceneNodeObject["spotCutOff"] = lightNode->spotCutOff;
    sceneNodeObject["color"] = jsonColor(lightNode->color);
}

QString SceneWriter::getSceneNodeTypeName(iris::SceneNodeType nodeType)
{
    switch (nodeType) {
        case iris::SceneNodeType::Empty:
            return "empty";
        case iris::SceneNodeType::Light:
            return "light";
        case iris::SceneNodeType::Mesh:
            return "mesh";
        case iris::SceneNodeType::Viewer:
            return "viewer";
        case iris::SceneNodeType::ParticleSystem:
            return "particle system";
        default:
            return "empty";
    }
}

QString SceneWriter::getLightNodeTypeName(iris::LightType lightType)
{
    switch (lightType) {
        case iris::LightType::Point:
            return "point";
        case iris::LightType::Directional:
            return "directional";
        case iris::LightType::Spot:
            return "spot";
        default:
            return "none";
    }
}
