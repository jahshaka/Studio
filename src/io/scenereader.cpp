/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QSharedPointer>
#include "assetiobase.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonValueRef>
#include <QJsonDocument>

#include "materialreader.hpp"
#include "scenereader.h"
#include "assetmanager.h"

#include "../constants.h"

#include "../editor/editordata.h"

#include "../irisgl/src/scenegraph/scene.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../irisgl/src/core/irisutils.h"
#include "../irisgl/src/scenegraph/meshnode.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include "../irisgl/src/scenegraph/viewernode.h"
#include "../irisgl/src/scenegraph/lightnode.h"
#include "../irisgl/src/scenegraph/particlesystemnode.h"
#include "../irisgl/src/materials/defaultmaterial.h"
#include "../irisgl/src/materials/custommaterial.h"
#include "../irisgl/src/core/property.h"
#include "../irisgl/src/graphics/texture2d.h"
#include "../irisgl/src/graphics/graphicshelper.h"
#include "../irisgl/src/graphics/mesh.h"
#include "../irisgl/src/animation/animation.h"
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/animation/keyframeset.h"
#include "../irisgl/src/animation/propertyanim.h"
#include "../irisgl/src/graphics/postprocess.h"
#include "../irisgl/src/graphics/postprocessmanager.h"

#include "../irisgl/src/postprocesses/bloompostprocess.h"
#include "../irisgl/src/postprocesses/coloroverlaypostprocess.h"
#include "../irisgl/src/postprocesses/greyscalepostprocess.h"
#include "../irisgl/src/postprocesses/materialpostprocess.h"
#include "../irisgl/src/postprocesses/radialblurpostprocess.h"
#include "../irisgl/src/postprocesses/ssaopostprocess.h"

#include "../constants.h"

iris::ScenePtr SceneReader::readScene(QString filePath,
                                      iris::PostProcessManagerPtr postMan,
                                      EditorData **editorData)
{
    dir = AssetIOBase::getDirFromFileName(filePath);
    QFile file(filePath);
    file.open(QIODevice::ReadOnly);

    auto data = file.readAll();
    auto doc = QJsonDocument::fromJson(data);

    auto projectObj = doc.object();
    auto scene = readScene(projectObj);
    if(editorData)
        *editorData = readEditorData(projectObj);

    readPostProcessData(projectObj, postMan);

    return scene;
}

iris::ScenePtr SceneReader::readScene(QString filePath,
                                      const QByteArray &sceneBlob,
                                      iris::PostProcessManagerPtr postMan,
                                      EditorData **editorData)
{
    dir = AssetIOBase::getDirFromFileName(filePath);
    auto doc = QJsonDocument::fromBinaryData(sceneBlob);
    auto projectObj = doc.object();

    auto scene = readScene(projectObj);

    if (editorData) *editorData = readEditorData(projectObj);
    readPostProcessData(projectObj, postMan);

    return scene;
}

EditorData* SceneReader::readEditorData(QJsonObject& projectObj)
{
    if(projectObj["editor"].isNull())
        return nullptr;

    auto editorObj = projectObj["editor"].toObject();

    // @todo: check if camera object is null
    auto camObj = editorObj["camera"].toObject();
    auto camera = iris::CameraNode::create();
    camera->angle = (float)camObj["angle"].toDouble(45.f);
    camera->nearClip = (float)camObj["nearClip"].toDouble(1.f);
    camera->farClip = (float)camObj["farClip"].toDouble(100.f);
    camera->setLocalPos(readVector3(camObj["pos"].toObject()));
    camera->setLocalRot(QQuaternion::fromEulerAngles(readVector3(camObj["rot"].toObject())));

    auto editorData = new EditorData();
    editorData->editorCamera = camera;
    editorData->distFromPivot = (float)camObj["distanceFromPivot"].toDouble(5.0f);


    return editorData;
}

void SceneReader::readPostProcessData(QJsonObject &projectObj, iris::PostProcessManagerPtr postMan)
{

    if (projectObj.contains("postprocesses")) {
        auto processListObj = projectObj["postprocesses"].toArray();

        for (auto processVal : processListObj) {
            auto processObj = processVal.toObject();
            auto name = processObj["name"].toString("");

            iris::PostProcessPtr process;

            if(name == "bloom")
               process = iris::BloomPostProcess::create();
            if(name == "color_overlay")
               process = iris::ColorOverlayPostProcess::create();
            //if(name == "greyscale")
            //   process = iris::GreyscalePostProcess::create();
            if(name == "radial_blur")
               process = iris::RadialBlurPostProcess::create();
            if(name == "ssao")
               process = iris::SSAOPostProcess::create();
            //if(name == "material")
            //   process = iris::MaterialPostProcess::create();

            if (!!process) {
                auto propertyObj = processObj["properties"].toObject();
                auto props = process->getProperties();
                for ( auto prop : props) {

                    if (propertyObj.contains(prop->name)) {

                        prop->setValue(propertyObj[prop->name].toVariant());
                        process->setProperty(prop);
                    }
                }
            }

            postMan->addPostProcess(process);
        }
    }
}

iris::ScenePtr SceneReader::readScene(QJsonObject& projectObj)
{
    auto scene = iris::Scene::create();

    //scene already contains root node, so just add children
    auto sceneObj = projectObj["scene"].toObject();

    //read properties
    auto skyBox = sceneObj["skyBox"].toObject();

    auto front = !skyBox["front"].toString("").isEmpty()
            ? getAbsolutePath(skyBox["front"].toString())
            : QString();
    auto back = !skyBox["back"].toString("").isEmpty()
            ? getAbsolutePath(skyBox["back"].toString())
            : QString();
    auto top = !skyBox["top"].toString("").isEmpty()
            ? getAbsolutePath(skyBox["top"].toString())
            : QString();
    auto bottom = !skyBox["bottom"].toString("").isEmpty()
            ? getAbsolutePath(skyBox["bottom"].toString())
            : QString();
    auto left = !skyBox["left"].toString("").isEmpty()
            ? getAbsolutePath(skyBox["left"].toString())
            : QString();
    auto right = !skyBox["right"].toString("").isEmpty()
            ? getAbsolutePath(skyBox["right"].toString())
            : QString();

    QString sides[6] = {front, back, top, bottom, left, right};

    QImage *info;
    bool useTex = false;
    for (int i = 0; i < 6; i++) {
        if (!sides[i].isEmpty()) {
            info = new QImage(sides[i]);
            useTex = true;
            break;
        }
    }

    if (useTex) {
        scene->skyBoxTextures[0] = front;
        scene->skyBoxTextures[1] = back;
        scene->skyBoxTextures[2] = top;
        scene->skyBoxTextures[3] = bottom;
        scene->skyBoxTextures[4] = left;
        scene->skyBoxTextures[5] = right;
        scene->setSkyTexture(iris::Texture2D::createCubeMap(front, back, top, bottom, left, right, info));
    }

    scene->setSkyColor(this->readColor(sceneObj["skyColor"].toObject()));
    scene->setAmbientColor(this->readColor(sceneObj["ambientColor"].toObject()));

    scene->fogColor = this->readColor(sceneObj["fogColor"].toObject());
    scene->fogStart = sceneObj["fogStart"].toDouble(100);
    scene->fogEnd = sceneObj["fogEnd"].toDouble(120);
    scene->fogEnabled = sceneObj["fogEnabled"].toBool(true);
    scene->shadowEnabled = sceneObj["shadowEnabled"].toBool(true);

    auto rootNode = sceneObj["rootNode"].toObject();
    QJsonArray children = rootNode["children"].toArray();

    for(auto childObj:children)
    {
        auto sceneNodeObj = childObj.toObject();
        auto childNode = readSceneNode(sceneNodeObj);
        scene->getRootNode()->addChild(childNode);
    }

    return scene;
}

/**
 * Creates scene node from json data
 * @param nodeObj
 * @return
 */
iris::SceneNodePtr SceneReader::readSceneNode(QJsonObject& nodeObj)
{
    iris::SceneNodePtr sceneNode;

    QString nodeType = nodeObj["type"].toString("empty");
    if (nodeType == "mesh") {
        sceneNode = createMesh(nodeObj).staticCast<iris::SceneNode>();
    } else if (nodeType == "light") {
        sceneNode = createLight(nodeObj).staticCast<iris::SceneNode>();
    } else if (nodeType == "viewer") {
        sceneNode = createViewer(nodeObj).staticCast<iris::SceneNode>();
    } else if (nodeType == "particle system") {
        sceneNode = createParticleSystem(nodeObj).staticCast<iris::SceneNode>();
    } else {
        sceneNode = iris::SceneNode::create();
    }

    //read transform
    readSceneNodeTransform(nodeObj,sceneNode);

    readAnimationData(nodeObj,sceneNode);

    //read name
    sceneNode->name = nodeObj["name"].toString("");
    sceneNode->setAttached(nodeObj["attached"].toBool());

    QJsonArray children = nodeObj["children"].toArray();
    for(auto childObj:children)
    {
        auto sceneNodeObj = childObj.toObject();
        auto childNode = readSceneNode(sceneNodeObj);
        sceneNode->addChild(childNode, false);
    }

    return sceneNode;
}


void SceneReader::readAnimationData(QJsonObject& nodeObj,iris::SceneNodePtr sceneNode)
{
    auto animList = nodeObj["animations"].toArray();
    auto activeAnimIndex = nodeObj["activeAnimation"].toInt(-1);

    for (auto animVal : animList) {
        auto animObj = animVal.toObject();

        auto name = animObj["name"].toString();
        auto animation = iris::Animation::create(name);
        animation->setLength(animObj["length"].toDouble());
        animation->setLooping(animObj["loop"].toBool());

        auto propList = animObj["properties"].toArray();
        for (auto propVal : propList) {
            auto propObj = propVal.toObject();
            auto name = propObj["name"].toString();
            auto type = propObj["type"].toString();

            iris::PropertyAnim* propAnim;
            if(type=="float")
                propAnim = new iris::FloatPropertyAnim();
            else if(type=="vector3")
                propAnim = new iris::Vector3DPropertyAnim();
            else
                propAnim = new iris::ColorPropertyAnim();

            propAnim->setName(name);

            auto keyFrameList = propObj["keyFrames"].toArray();
            int index = 0;
            for (auto keyFrameVal : keyFrameList) {
                auto keyFrameObj = keyFrameVal.toObject();
                auto name = keyFrameObj["name"].toString();

                auto keyList = keyFrameObj["keys"].toArray();
                auto keyFrame = propAnim->getKeyFrame(index);

                for (auto keyVal : keyList) {
                    auto keyObj = keyVal.toObject();
                    auto time = keyObj["time"].toDouble();
                    auto val = keyObj["value"].toDouble();
                    auto key = keyFrame->addKey(val, time);

                    key->leftSlope = keyObj["leftSlope"].toDouble();
                    key->rightSlope = keyObj["rightSlope"].toDouble();

                    key->leftTangent = getTangentTypeFromName(keyObj["leftTangent"].toString());
                    key->rightTangent = getTangentTypeFromName(keyObj["rightTangent"].toString());

                    key->handleMode = getHandleModeFromName(keyObj["handleMode"].toString());
                }


                index++;
            }

            animation->addPropertyAnim(propAnim);
        }

        if (animObj.contains("skeletalAnimation")) {
            auto skelAnim = animObj["skeletalAnimation"].toObject();

            auto skel = this->getSkeletalAnimation(skelAnim["source"].toString(), skelAnim["name"].toString());
            animation->setSkeletalAnimation(skel);
        }

        sceneNode->addAnimation(animation);
        //if (animation->getName() == activeAnim)
        //    sceneNode->setAnimation(animation);
    }
    if (activeAnimIndex != -1) {
        sceneNode->setAnimation(sceneNode->getAnimations()[activeAnimIndex]);
    }
}

/**
 * Reads pos, rot and scale properties from json object
 * if scale isnt available then it's set to (1,1,1) by default
 * @param nodeObj
 * @param sceneNode
 */
void SceneReader::readSceneNodeTransform(QJsonObject& nodeObj,iris::SceneNodePtr sceneNode)
{
    auto pos = nodeObj["pos"].toObject();
    if(!pos.isEmpty())
    {
        sceneNode->setLocalPos(readVector3(pos));
    }

    auto rot = nodeObj["rot"].toObject();
    if(!rot.isEmpty())
    {
        //the rotation is stored as euler angles
        sceneNode->setLocalRot(QQuaternion::fromEulerAngles(readVector3(rot)));
    }

    auto scale = nodeObj["scale"].toObject();
    if(!scale.isEmpty())
    {
        sceneNode->setLocalScale(readVector3(scale));
    }
    else
    {
        sceneNode->setLocalScale(QVector3D(1,1,1));
    }
}

/**
 * Creates mesh using scene node data
 * @param nodeObj
 * @return
 */
iris::MeshNodePtr SceneReader::createMesh(QJsonObject& nodeObj)
{
    auto meshNode = iris::MeshNode::create();

    auto source = nodeObj["mesh"].toString("");
    auto meshIndex = nodeObj["meshIndex"].toInt(0);
    auto pickable = nodeObj["pickable"].toBool(true);

    if (!source.isEmpty()) {
        auto mesh = getMesh(getAbsolutePath(source), meshIndex);
        if (source.startsWith(":")) {
            meshNode->setMesh(source);
        } else {
            meshNode->setMesh(mesh);
        }
        meshNode->setPickable(pickable);
        meshNode->meshPath = source;
        meshNode->meshIndex = meshIndex;
    }

    auto material = readMaterial(nodeObj);
    meshNode->setMaterial(material);

    auto faceCullingMode = nodeObj["faceCullingMode"].toString("back");

    if (faceCullingMode == "back") {
        meshNode->setFaceCullingMode(iris::FaceCullingMode::Back);
    } else if (faceCullingMode == "front") {
        meshNode->setFaceCullingMode(iris::FaceCullingMode::Front);
    } else if (faceCullingMode == "frontandback") {
        meshNode->setFaceCullingMode(iris::FaceCullingMode::FrontAndBack);
    } else {
        meshNode->setFaceCullingMode(iris::FaceCullingMode::None);
    }

    meshNode->applyDefaultPose();

    return meshNode;
}

/**
 * Creates light from light node data
 * @param nodeObj
 * @return
 */
iris::LightNodePtr SceneReader::createLight(QJsonObject& nodeObj)
{
    auto lightNode = iris::LightNode::create();

    lightNode->setLightType(getLightTypeFromName(nodeObj["lightType"].toString()));
    lightNode->intensity = (float)nodeObj["intensity"].toDouble(1.0f);
    lightNode->distance = (float)nodeObj["distance"].toDouble(1.0f);
    lightNode->spotCutOff = (float)nodeObj["spotCutOff"].toDouble(30.0f);
    lightNode->color = readColor(nodeObj["color"].toObject());

    //TODO: move this to the sceneview widget or somewhere more appropriate
    if (lightNode->lightType == iris::LightType::Directional) {
        lightNode->icon = iris::Texture2D::load(":/app/icons/light.png");
    } else {
        lightNode->icon = iris::Texture2D::load(":/app/icons/bulb.png");
    }

    lightNode->iconSize = 0.5f;

    return lightNode;
}

iris::ViewerNodePtr SceneReader::createViewer(QJsonObject& nodeObj)
{
    auto viewerNode = iris::ViewerNode::create();
    viewerNode->setViewScale((float)nodeObj["viewScale"].toDouble(1.0f));

    return viewerNode;
}

iris::ParticleSystemNodePtr SceneReader::createParticleSystem(QJsonObject& nodeObj)
{
    auto particleNode = iris::ParticleSystemNode::create();

    particleNode->setPPS((float) nodeObj["particlesPerSecond"].toDouble(1.0f));
    particleNode->setParticleScale((float) nodeObj["particleScale"].toDouble(1.0f));
    particleNode->setDissipation(nodeObj["dissipate"].toBool());
    particleNode->setDissipationInv(nodeObj["dissipateInv"].toBool());
    particleNode->setRandomRotation(nodeObj["randomRotation"].toBool());
    particleNode->setGravity((float) nodeObj["gravityComplement"].toDouble(1.0f));
    particleNode->setBlendMode(nodeObj["blendMode"].toBool());
    particleNode->setLife((float) nodeObj["lifeLength"].toDouble(1.0f));
    particleNode->setName(nodeObj["name"].toString());
    particleNode->setSpeed((float) nodeObj["speed"].toDouble(1.0f));
    particleNode->setTexture(iris::Texture2D::load(getAbsolutePath(nodeObj["texture"].toString())));

    return particleNode;
}

iris::LightType SceneReader::getLightTypeFromName(QString lightType)
{
    if (lightType == "point")       return iris::LightType::Point;
    if (lightType == "directional") return iris::LightType::Directional;
    if (lightType == "spot")        return iris::LightType::Spot;

    return iris::LightType::Point;
}

iris::TangentType SceneReader::getTangentTypeFromName(QString tangentType)
{
    if (tangentType=="free")
        return iris::TangentType::Free;
    else if (tangentType=="linear")
        return iris::TangentType::Linear;
    else if (tangentType=="constant")
        return iris::TangentType::Constant;

    return iris::TangentType::Free;
}

iris::HandleMode SceneReader::getHandleModeFromName(QString handleMode)
{
    if (handleMode=="joined")
        return iris::HandleMode::Joined;
    else if (handleMode=="broken")
        return iris::HandleMode::Broken;

    return iris::HandleMode::Joined;
}

/**
 * Extracts material from node's json object.
 * Creates default material if one isnt defined in nodeObj
 * @param nodeObj
 * @return
 */
iris::MaterialPtr SceneReader::readMaterial(QJsonObject& nodeObj)
{
    if (nodeObj["material"].isNull()) return iris::CustomMaterial::create();

    auto mat = nodeObj["material"].toObject();
    auto m = iris::CustomMaterial::create();
    auto shaderName = Constants::SHADER_DEFS + mat["name"].toString() + ".shader";
    auto shaderFile = QFileInfo(IrisUtils::getAbsoluteAssetPath(shaderName));
    m->setName(mat["name"].toString());


    if (shaderFile.exists()) {
        m->generate(shaderFile.absoluteFilePath());
    } else {
        for (auto asset : AssetManager::assets) {
            if (asset->type == AssetType::Shader) {
                if (asset->fileName == mat["name"].toString() + ".shader") {
                    //qDebug() << asset->path;
                    m->generate(asset->path, true);
                }
            }
        }
    }

    for (auto prop : m->properties) {
        if (mat.contains(prop->name)) {
            if (prop->type == iris::PropertyType::Texture) {
                auto textureStr = !mat[prop->name].toString().isEmpty()
                                  ? getAbsolutePath(mat[prop->name].toString())
                                  : QString();

                m->setValue(prop->name, textureStr);
            } else {
                m->setValue(prop->name, mat[prop->name].toVariant());
            }
        }
    }

    return m;
}

void SceneReader::extractAssetsFromAssimpScene(QString filePath)
{
    if (!assimpScenes.contains(filePath)) {
        QList<iris::MeshPtr> meshList;
        QMap<QString, iris::SkeletalAnimationPtr> animationss;
        iris::GraphicsHelper::loadAllMeshesAndAnimationsFromStore<Asset*>(AssetManager::assets,
                                                                          filePath,
                                                                          meshList,
                                                                          animationss);

        meshes.insert(filePath,meshList);
        assimpScenes.insert(filePath);
        animations.insert(filePath, animationss);

//        auto relPath = QDir(Globals::project->folderPath).relativeFilePath(filename);
//        for(auto anim : node->getAnimations()) {
//            if (!!anim->skeletalAnimation)
//                anim->skeletalAnimation->source = relPath;
//        }
    }
}

/**
 * Returns mesh from mesh file at index
 * if the mesh doesnt exist, nullptr is returned
 * @param filePath
 * @param index
 * @return
 */
iris::MeshPtr SceneReader::getMesh(QString filePath, int index)
{
    extractAssetsFromAssimpScene(filePath);

    // if the mesh is already in the hashmap then it was already loaded, just return the indexed mesh=
    auto meshList = meshes[filePath];
    if (index < meshList.size()) return meshList[index];

    // maybe the mesh was modified after the file was saved
    return iris::MeshPtr();
}

iris::SkeletalAnimationPtr SceneReader::getSkeletalAnimation(QString filePath, QString animName)
{
    auto relPath = filePath;
    filePath = this->getAbsolutePath(filePath);
    extractAssetsFromAssimpScene(filePath);

    auto animMap = animations[filePath];

    //reset relative paths for animations since they have the absolute path
    for(auto anim : animMap)
        anim->source = relPath;

    if (animMap.contains(animName)) return animMap[animName];

    return iris::SkeletalAnimationPtr();
}
