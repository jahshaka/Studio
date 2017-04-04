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

#include "../editor/editordata.h"

#include "../irisgl/src/core/scene.h"
#include "../irisgl/src/core/scenenode.h"
#include "../irisgl/src/core/irisutils.h"
#include "../irisgl/src/scenegraph/meshnode.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include "../irisgl/src/scenegraph/viewernode.h"
#include "../irisgl/src/scenegraph/lightnode.h"
#include "../irisgl/src/scenegraph/particlesystemnode.h"
#include "../irisgl/src/materials/defaultmaterial.h"
#include "../irisgl/src/materials/custommaterial.h"
#include "../irisgl/src/graphics/texture2d.h"
#include "../irisgl/src/graphics/graphicshelper.h"
#include "../irisgl/src/animation/animation.h"
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/animation/keyframeset.h"

iris::ScenePtr SceneReader::readScene(QString filePath, EditorData** editorData)
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
    camera->pos = readVector3(camObj["pos"].toObject());
    camera->rot = QQuaternion::fromEulerAngles(readVector3(camObj["rot"].toObject()));

    auto editorData = new EditorData();
    editorData->editorCamera = camera;
    editorData->distFromPivot = (float)camObj["distanceFromPivot"].toDouble(5.0f);


    return editorData;
}

iris::ScenePtr SceneReader::readScene(QJsonObject& projectObj)
{
    auto scene = iris::Scene::create();

    //scene already contains root node, so just add children
    auto sceneObj = projectObj["scene"].toObject();

    //read properties
    auto skyTexPath = sceneObj["skyTexture"].toString("");
    if(!skyTexPath.isEmpty())
    {
        auto fInfo = QFileInfo(skyTexPath);
        auto path = fInfo.path();
        auto ext = fInfo.suffix();

        auto x1 = path + "/left." + ext;
        auto x2 = path + "/right." + ext;
        auto y1 = path + "/top." + ext;
        auto y2 = path + "/bottom." + ext;
        auto z1 = path + "/front." + ext;
        auto z2 = path + "/back." + ext;

//        skyTexPath = this->getAbsolutePath(skyTexPath);
//        scene->setSkyTexture(iris::Texture2D::load(skyTexPath,false));
        scene->setSkyTexture(iris::Texture2D::createCubeMap(x1, x2, y1, y2, z1, z2));
        scene->setSkyTextureSource(z1);
    }
    scene->setSkyColor(this->readColor(sceneObj["skyColor"].toObject()));
    scene->setAmbientColor(this->readColor(sceneObj["ambientColor"].toObject()));

    scene->fogColor = this->readColor(sceneObj["fogColor"].toObject());
    scene->fogStart = sceneObj["fogStart"].toDouble(100);
    scene->fogEnd = sceneObj["fogEnd"].toDouble(120);
    scene->fogEnabled = sceneObj["fogEnabled"].toBool(true);

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
    auto animObj = nodeObj["animation"].toObject();
    if(animObj.isEmpty())
        return;

    auto framesArray = animObj["frames"].toArray();
    if(framesArray.isEmpty())
        return;

    auto animation = iris::Animation::create();

    for(auto frameValue:framesArray)
    {
        auto frameObj = frameValue.toObject();

        auto frame = new iris::FloatKeyFrame();
        frame->name = frameObj["name"].toString("Frame");


        auto keysObj = frameObj["keys"].toArray();
        for(auto keyValue:keysObj)
        {
            auto keyObj = keyValue.toObject();
            float time = keyObj["time"].toDouble(0);
            float value = keyObj["value"].toDouble(0);
            frame->addKey(value,time);
        }
        animation->keyFrameSet->keyFrames.insert(frame->name,frame);
        animation->length = animObj["length"].toDouble(1);
        animation->loop = animObj["loop"].toBool(false);
    }

    sceneNode->animation = animation;
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
        sceneNode->pos = readVector3(pos);
    }

    auto rot = nodeObj["rot"].toObject();
    if(!rot.isEmpty())
    {
        //the rotation is stored as euler angles
        sceneNode->rot = QQuaternion::fromEulerAngles(readVector3(rot));
    }

    auto scale = nodeObj["scale"].toObject();
    if(!scale.isEmpty())
    {
        sceneNode->scale = readVector3(scale);
    }
    else
    {
        sceneNode->scale = QVector3D(1,1,1);
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
    auto pickable = nodeObj["pickable"].toBool();

    if (!source.isEmpty()) {
        auto mesh = getMesh(getAbsolutePath(source), meshIndex);
        meshNode->setMesh(mesh);
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
    if(lightNode->lightType == iris::LightType::Directional)
        lightNode->icon = iris::Texture2D::load(IrisUtils::getAbsoluteAssetPath("app/icons/light.png"));
    else
        lightNode->icon = iris::Texture2D::load(IrisUtils::getAbsoluteAssetPath("app/icons/bulb.png"));
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

/**
 * Extracts material from node's json object.
 * Creates default material if one isnt defined in nodeObj
 * @param nodeObj
 * @return
 */
iris::MaterialPtr SceneReader::readMaterial(QJsonObject& nodeObj)
{
    if (nodeObj["material"].isNull()) {
        return iris::CustomMaterial::create();
    }

    auto mat = nodeObj["material"].toObject();

    MaterialReader *materialReader = new MaterialReader();
    QString path =  "app/shader_defs/" + mat["name"].toString() + ".json";

    materialReader->readJahShader(IrisUtils::getAbsoluteAssetPath(path));

    auto m = iris::CustomMaterial::create();
    m->generate(materialReader->getParsedShader());

    int cCtr = 0;
    for (auto s : m->colorUniforms) {
        QColor col;
        col.setNamedColor(mat[s.name].toString());
        m->updateColorAndUniform(cCtr, col);
        cCtr++;
    }

    int tCtr = 0;
    for (auto s : m->textureUniforms) {
        auto tex = mat[s.name].toString();
        if (!tex.isEmpty()) {
            tex = getAbsolutePath(mat[s.name].toString());
            // qDebug() << "TEX " << tex;
        }
        m->updateTextureAndToggleUniform(tCtr, tex);
        tCtr++;
    }

    int sCtr = 0;
    for (auto s : m->sliderUniforms) {
        m->updateFloatAndUniform(sCtr, mat[s.name].toDouble());
        sCtr++;
    }

    return m;
}

/**
 * Returns mesh from mesh file at index
 * if the mesh doesnt exist, nullptr is returned
 * @param filePath
 * @param index
 * @return
 */
iris::Mesh* SceneReader::getMesh(QString filePath,int index)
{
    // if the mesh is already in the hashmap then it was already loaded, just return the indexed mesh
    if (meshes.contains(filePath)) {
        auto meshList = meshes[filePath];
        if (index < meshList.size()) return meshList[index];
        // maybe the mesh was modified after the file was saved
        return nullptr;
    }

    else {
        auto meshList = iris::GraphicsHelper::loadAllMeshesFromFile(filePath);
        meshes.insert(filePath,meshList);
        if (index < meshList.size()) return meshList[index];
        // maybe the mesh was modified after the file was saved
        return nullptr;
    }
}
