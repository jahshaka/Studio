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

#include "../irisgl/src/irisgl.h"
#include "../irisgl/src/scenegraph/scene.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../irisgl/src/scenegraph/meshnode.h"
#include "../irisgl/src/scenegraph/lightnode.h"
#include "../irisgl/src/scenegraph/viewernode.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include "../irisgl/src/scenegraph/particlesystemnode.h"
#include "../irisgl/src/materials/custommaterial.h"
#include "../irisgl/src/core/property.h"
#include "../irisgl/src/animation/animation.h"
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/animation/keyframeset.h"
#include "../irisgl/src/animation/propertyanim.h"
#include "../irisgl/src/animation/skeletalanimation.h"
#include "../irisgl/src/graphics/postprocess.h"
#include "../irisgl/src/graphics/postprocessmanager.h"

#include "scenewriter.h"
#include "assetiobase.h"
#include "../constants.h"
#include "../core/database/database.h"

Database *SceneWriter::handle = 0;

void SceneWriter::writeScene(QString filePath,
                             iris::ScenePtr scene,
                             iris::PostProcessManagerPtr postMan,
                             EditorData* editorData)
{
    dir = AssetIOBase::getDirFromFileName(filePath);
    QFile file(filePath);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);

    QJsonObject projectObj;
    projectObj["version"] = Constants::CONTENT_VERSION;
    writeScene(projectObj, scene);
    if(editorData != nullptr)
        writeEditorData(projectObj, editorData);

    if (!!postMan) {
        writePostProcessData(projectObj, postMan);
    }

    QJsonDocument saveDoc(projectObj);
    file.write(saveDoc.toJson());
    file.close();
}

QByteArray SceneWriter::getSceneObject(QString projectPath,
                                       iris::ScenePtr scene,
                                       iris::PostProcessManagerPtr postMan,
                                       EditorData *editorData)
{
    dir = projectPath;
    QJsonObject projectObj;
    projectObj["version"] = Constants::CONTENT_VERSION;

    writeScene(projectObj, scene);

    if (editorData != nullptr) {
        writeEditorData(projectObj, editorData);
    }

    if (!!postMan) {
        writePostProcessData(projectObj, postMan);
    }

    //qDebug() << projectObj;

    return QJsonDocument(projectObj).toBinaryData();
}

void SceneWriter::writeScene(QJsonObject& projectObj, iris::ScenePtr scene)
{
    QJsonObject sceneObj;

    //scene properties

    QJsonObject skyTexture;
    skyTexture["front"] = getRelativePath(scene->skyBoxTextures[0]);
    skyTexture["back"] = getRelativePath(scene->skyBoxTextures[1]);
    skyTexture["top"] = getRelativePath(scene->skyBoxTextures[2]);
    skyTexture["bottom"] = getRelativePath(scene->skyBoxTextures[3]);
    skyTexture["left"] = getRelativePath(scene->skyBoxTextures[4]);
    skyTexture["right"] = getRelativePath(scene->skyBoxTextures[5]);

//    if (!!scene->skyTexture) {
//        sceneObj["skyTexture"] = getRelativePath(scene->skyTexture->getSource());//);
//    } else {
//        sceneObj["skyTexture"] = "";
//    }

    sceneObj["skyBox"] = skyTexture;
    sceneObj["gravity"] = scene->gravity;
    sceneObj["skyColor"] = jsonColor(scene->skyColor);
    sceneObj["ambientColor"] = jsonColor(scene->ambientColor);

    sceneObj["fogColor"] = jsonColor(scene->fogColor);
    sceneObj["fogStart"] = scene->fogStart;
    sceneObj["fogEnd"] = scene->fogEnd;
    sceneObj["fogEnabled"] = scene->fogEnabled;
    sceneObj["shadowEnabled"] = scene->shadowEnabled;
	


    QJsonObject rootNodeObj;
    writeSceneNode(rootNodeObj,scene->getRootNode());
    sceneObj["rootNode"] = rootNodeObj;

    projectObj["scene"] = sceneObj;
}

void SceneWriter::writePostProcessData(QJsonObject &projectObj, iris::PostProcessManagerPtr postMan)
{
    QJsonArray processesObj;

    for(auto process : postMan->getPostProcesses()) {
        QJsonObject processObj;

        processObj["name"] = process->getName();

        QJsonObject props;

        for ( auto prop : process->getProperties()) {
            props.insert(prop->name, QJsonValue::fromVariant(prop->getValue()));
        }

        processObj["properties"] = props;

        processesObj.append(processObj);
    }

    projectObj["postprocesses"] = processesObj;
}

void SceneWriter::writeEditorData(QJsonObject& projectObj,EditorData* editorData)
{
    QJsonObject editorObj;
    editorObj["showLightWires"] = editorData->showLightWires;

    QJsonObject cameraObj;
    auto cam = editorData->editorCamera;
    cameraObj["angle"] = cam->angle;
    cameraObj["nearClip"] = cam->nearClip;
    cameraObj["farClip"] = cam->farClip;
    cameraObj["pos"] = jsonVector3(editorData->editorCamera->getLocalPos());
    cameraObj["rot"] = jsonVector3(editorData->editorCamera->getLocalRot().toEulerAngles());
	cameraObj["orthogonalSize"] = cam->orthoSize;
	cameraObj["projectionMode"] = cam->projMode == iris::CameraProjection::Perspective ? "perspective" : "orthogonal";

    editorObj["camera"] = cameraObj;
    projectObj["editor"] = editorObj;
}

void SceneWriter::writeSceneNode(QJsonObject& sceneNodeObj, iris::SceneNodePtr sceneNode, bool relative)
{
    sceneNodeObj["name"] = sceneNode->getName();
    sceneNodeObj["attached"] = sceneNode->isAttached();
    sceneNodeObj["type"] = getSceneNodeTypeName(sceneNode->sceneNodeType);
    sceneNodeObj["pickable"] = sceneNode->isPickable();
    sceneNodeObj["pos"] = jsonVector3(sceneNode->getLocalPos());
    auto rot = sceneNode->getLocalRot().normalized().toEulerAngles();
    sceneNodeObj["rot"] = jsonVector3(rot);
    sceneNodeObj["scale"] = jsonVector3(sceneNode->getLocalScale());
	sceneNodeObj["visible"] = sceneNode->isVisible();

    //todo: write data specific to node type
    switch (sceneNode->sceneNodeType) {
        case iris::SceneNodeType::Mesh:
            writeMeshData(sceneNodeObj, sceneNode.staticCast<iris::MeshNode>(), relative);
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
        writeSceneNode(childNodeObj, childNode, relative);
        childrenArray.append(childNodeObj);
    }

    sceneNodeObj["children"] = childrenArray;
}

void SceneWriter::writeAnimationData(QJsonObject& sceneNodeObj,iris::SceneNodePtr sceneNode)
{
    auto activeAnim = sceneNode->getAnimation();

    auto animations = sceneNode->getAnimations();
    QJsonArray animListObj;

    if (!!activeAnim)
        sceneNodeObj["activeAnimation"] = animations.indexOf(activeAnim);
    else
        sceneNodeObj["activeAnimation"] = -1;
        //sceneNodeObj["activeAnimation"] = activeAnim->getName();


    // todo: add all animations
    for (auto anim : animations) {
        QJsonObject animObj;
        animObj["name"] = anim->getName();
        animObj["length"] = anim->getLength();
        animObj["loop"] = anim->getLooping();

        auto animPropListObj = QJsonArray();

        for (auto propName: anim->properties.keys()) {
            auto prop = anim->properties[propName];
            auto propObj = QJsonObject();
            propObj["name"] = propName;

            auto keyFrames = prop->getKeyFrames();
            if(keyFrames.size()==1)
                propObj["type"] = "float";
            if(keyFrames.size()==3)
                propObj["type"] = "vector3";
            if(keyFrames.size()==4)
                propObj["type"] = "color";

            QJsonArray keyFrameList;
            for (auto animInfo : keyFrames) {
                auto keyFrameObj = QJsonObject();

                auto keyFrame = animInfo.keyFrame;
                keyFrameObj["name"] = animInfo.name;

                auto keysListObj = QJsonArray();
                for (auto key : keyFrame->keys) {
                    auto keyObj = QJsonObject();
                    keyObj["time"] = key->time;
                    keyObj["value"] = key->value;

                    keyObj["leftSlope"] = key->leftSlope;
                    keyObj["rightSlope"] = key->rightSlope;

                    keyObj["leftTangentType"] = getKeyTangentTypeName(key->leftTangent);
                    keyObj["rightTangentType"] = getKeyTangentTypeName(key->rightTangent);

                    keyObj["handleMode"] = getKeyHandleModeName(key->handleMode);

                    keysListObj.append(keyObj);
                }
                keyFrameObj["keys"] = keysListObj;
                keyFrameList.append(keyFrameObj);
            }
            propObj["keyFrames"] = keyFrameList;

            animPropListObj.append(propObj);
        }

        animObj["properties"] = animPropListObj;

        if (anim->hasSkeletalAnimation()) {
            auto skelAnim = anim->getSkeletalAnimation();
            QJsonObject skelObj;
            skelObj["source"] = skelAnim->source;
            skelObj["name"] = skelAnim->name;
            animObj["skeletalAnimation"] = skelObj;
        }

        animListObj.append(animObj);
    }

    sceneNodeObj["animations"] = animListObj;
}

void SceneWriter::writeMeshData(QJsonObject& sceneNodeObject, iris::MeshNodePtr meshNode, bool relative)
{
    // It's a safe assumption that the filename is safe to use here in queries if need be
	sceneNodeObject["mesh"]          = meshNode->meshPath;
	sceneNodeObject["guid"]          = meshNode->getGUID();
    sceneNodeObject["meshIndex"]     = meshNode->meshIndex;
    sceneNodeObject["physicsObject"] = meshNode->isPhysicsBody;

    if (meshNode->isPhysicsBody) {
        QJsonObject physicsProperties;

        physicsProperties.insert("centerOfMass", jsonVector3(meshNode->physicsProperty.centerOfMass));
        physicsProperties.insert("pivot", jsonVector3(meshNode->physicsProperty.pivotPoint));
        physicsProperties.insert("static", meshNode->physicsProperty.isStatic);
        physicsProperties.insert("collisionMargin", meshNode->physicsProperty.objectCollisionMargin);
        physicsProperties.insert("damping", meshNode->physicsProperty.objectDamping);
        physicsProperties.insert("mass", meshNode->physicsProperty.objectMass);
        physicsProperties.insert("bounciness", meshNode->physicsProperty.objectRestitution);
        physicsProperties.insert("shape", static_cast<int>(meshNode->physicsProperty.shape));
        physicsProperties.insert("type", static_cast<int>(meshNode->physicsProperty.type));

        QJsonArray constraintProperties;
        
        for (const auto &constraint : meshNode->physicsProperty.constraints) {
            QJsonObject constraintProp;
            constraintProp.insert("constraintFrom", constraint.constraintFrom);
            constraintProp.insert("constraintTo", constraint.constraintTo);
            constraintProp.insert("constraintType", static_cast<int>(constraint.constraintType));

            constraintProperties.append(constraintProp);
        }

        physicsProperties["constraints"] = constraintProperties;

        sceneNodeObject["physicsProperties"] = physicsProperties;
    }

    auto cullMode = meshNode->getFaceCullingMode();
    switch (cullMode) {
        case iris::FaceCullingMode::Back:
            sceneNodeObject["faceCullingMode"] = "back";
            break;
        case iris::FaceCullingMode::Front:
            sceneNodeObject["faceCullingMode"] = "front";
            break;
        case iris::FaceCullingMode::DefinedInMaterial:
            sceneNodeObject["faceCullingMode"] = "material";
            break;
        case iris::FaceCullingMode::None:
            sceneNodeObject["faceCullingMode"] = "none";
            break;
        default: break;
    }

    // todo: check if material actually exists
    QJsonObject matObj;
    writeSceneNodeMaterial(matObj, meshNode->getMaterial().staticCast<iris::CustomMaterial>(), relative);
    sceneNodeObject["material"] = matObj;
}

void SceneWriter::writeViewerData(QJsonObject& sceneNodeObject,iris::ViewerNodePtr viewerNode)
{
    sceneNodeObject["viewScale"] = viewerNode->getViewScale();
	sceneNodeObject["visible"] = viewerNode->isVisible();
}

void SceneWriter::writeParticleData(QJsonObject& sceneNodeObject, iris::ParticleSystemNodePtr node)
{
    sceneNodeObject["guid"]                 = node->getGUID();
    sceneNodeObject["particlesPerSecond"]   = node->particlesPerSecond;
    sceneNodeObject["particleScale"]        = node->particleScale;
    sceneNodeObject["dissipate"]            = node->dissipate;
    sceneNodeObject["dissipateInv"]         = node->dissipateInv;
    sceneNodeObject["gravityComplement"]    = node->gravityComplement;
    sceneNodeObject["randomRotation"]       = node->randomRotation;
    sceneNodeObject["blendMode"]            = node->useAdditive;
    sceneNodeObject["lifeLength"]           = node->lifeLength;
    sceneNodeObject["speed"]                = node->speed;
	sceneNodeObject["visible"]				= node->isVisible();
    sceneNodeObject["texture"]              = handle->fetchAssetGUIDByName(QFileInfo(node->texture->getSource()).fileName());
}

void SceneWriter::writeSceneNodeMaterial(QJsonObject& matObj, iris::CustomMaterialPtr mat, bool relative)
{
    matObj["name"] = mat->getName();
    matObj["guid"] = mat->getGuid();

    for (auto prop : mat->properties) {
        if (prop->type == iris::PropertyType::Bool) {
            matObj[prop->name] = prop->getValue().toBool();
        }

        if (prop->type == iris::PropertyType::Float) {
            matObj[prop->name] = prop->getValue().toFloat();
        }

        if (prop->type == iris::PropertyType::Color) {
            matObj[prop->name] = prop->getValue().value<QColor>().name();
        }

        if (prop->type == iris::PropertyType::Texture) {
			//matObj[prop->name] = relative ? getRelativePath(prop->getValue().toString()) : QFileInfo(prop->getValue().toString()).fileName();
			matObj[prop->name] = relative
									? handle->fetchAssetGUIDByName(QFileInfo(prop->getValue().toString()).fileName())
									: getRelativePath(prop->getValue().toString());
        }
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

QString evalShadowTypeName(iris::ShadowMapType shadowType)
{
    switch(shadowType){
    case iris::ShadowMapType::None:
        return "none";
    case iris::ShadowMapType::Hard:
        return "hard";
    case iris::ShadowMapType::Soft:
        return "soft";
    case iris::ShadowMapType::Softer:
        return "softer";
    }
}

void SceneWriter::writeLightData(QJsonObject& sceneNodeObject,iris::LightNodePtr lightNode)
{
    sceneNodeObject["lightType"] = getLightNodeTypeName(lightNode->lightType);
    sceneNodeObject["intensity"] = lightNode->intensity;
    sceneNodeObject["distance"] = lightNode->distance;
    sceneNodeObject["spotCutOff"] = lightNode->spotCutOff;
    sceneNodeObject["color"] = jsonColor(lightNode->color);

    //shadow data
    auto shadowMap = lightNode->shadowMap;
    sceneNodeObject["shadowType"] = evalShadowTypeName(shadowMap->shadowType);
    sceneNodeObject["shadowSize"] = shadowMap->resolution;
    sceneNodeObject["shadowBias"] = shadowMap->bias;
	sceneNodeObject["visible"] = lightNode->isVisible();
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

QString SceneWriter::getKeyTangentTypeName(iris::TangentType tangentType)
{
    switch (tangentType) {
    case iris::TangentType::Free:
        return "free";
    case iris::TangentType::Linear:
        return "linear";
    case iris::TangentType::Constant:
        return "constant";
    default:
        return "free";
    }
}

QString SceneWriter::getKeyHandleModeName(iris::HandleMode handleMode)
{
    switch (handleMode) {
    case iris::HandleMode::Joined:
        return "joined";
    case iris::HandleMode::Broken:
        return "broken";
    default:
        return "joined";
    }
}
