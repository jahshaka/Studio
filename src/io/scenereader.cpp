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
#include "core/guidmanager.h"

#include "globals.h"
#include "constants.h"
#include "core/database/database.h"

#include "editor/editordata.h"
#include "materials/jahdefaultmaterial.h"

#include "irisgl/Graphics.h"
#include "irisgl/Animation.h"
#include "irisgl/SceneGraph.h"

#include "irisgl/src/graphics/postprocess.h"
#include "irisgl/src/graphics/postprocessmanager.h"

#include "../irisgl/src/postprocesses/bloompostprocess.h"
#include "../irisgl/src/postprocesses/coloroverlaypostprocess.h"
#include "../irisgl/src/postprocesses/greyscalepostprocess.h"
#include "../irisgl/src/postprocesses/materialpostprocess.h"
#include "../irisgl/src/postprocesses/radialblurpostprocess.h"
#include "../irisgl/src/postprocesses/ssaopostprocess.h"
#include "../irisgl/src/postprocesses/fxaapostprocess.h"

#include "irisgl/src/physics/physicsproperties.h"
#include "irisgl/src/physics/physicshelper.h"

#include "materialreader.hpp"
#include "../core/guidmanager.h"

iris::ScenePtr SceneReader::readScene(const QString &projectPath,
                                      const QByteArray &sceneBlob,
                                      iris::PostProcessManagerPtr postMan,
                                      EditorData **editorData)
{
    dir = projectPath;
	useAlternativeLocation = false;
    auto doc = QJsonDocument::fromJson(sceneBlob);
    auto projectObj = doc.object();

    auto scene = readScene(projectObj);

    if (editorData) *editorData = readEditorData(projectObj);
	if (!!postMan)
		readPostProcessData(projectObj, postMan);

    for (auto node : scene->rootNode->children) {
        node->applyDefaultPose();
    }

    return scene;
}

EditorData* SceneReader::readEditorData(QJsonObject& projectObj)
{
    if (projectObj["editor"].isNull()) return nullptr;

    auto editorObj = projectObj["editor"].toObject();

    // @todo: check if camera object is null
    auto camObj = editorObj["camera"].toObject();
    auto camera = iris::CameraNode::create();
    camera->angle = (float)camObj["angle"].toDouble(45.f);
    camera->nearClip = (float)camObj["nearClip"].toDouble(1.f);
    camera->farClip = (float)camObj["farClip"].toDouble(100.f);
    camera->setLocalPos(readVector3(camObj["pos"].toObject()));
    camera->setLocalRot(QQuaternion::fromEulerAngles(readVector3(camObj["rot"].toObject())));
	camera->setOrthagonalZoom((float)camObj["orthogonalSize"].toDouble(3.0f));
	iris::CameraProjection val = camObj["projectionMode"].toString().compare("orthogonal") == 0 ? iris::CameraProjection::Orthogonal : iris::CameraProjection::Perspective;
	camera->setProjection(val);

    auto editorData = new EditorData();
    editorData->editorCamera = camera;
    editorData->distFromPivot = (float)camObj["distanceFromPivot"].toDouble(5.0f);
    editorData->showLightWires = editorObj["showLightWires"].toBool();
	editorData->showDebugDrawFlags = editorObj["showDebugDrawFlags"].toBool();

    return editorData;
}

void SceneReader::readPostProcessData(QJsonObject &projectObj, iris::PostProcessManagerPtr postMan)
{
	/*
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
            if(name == "fxaa")
               process = iris::FxaaPostProcess::create();
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
	*/
}

iris::ScenePtr SceneReader::readScene(QJsonObject& projectObj)
{
    auto scene = iris::Scene::create();

    //scene already contains root node, so just add children
    auto sceneObj = projectObj["scene"].toObject();
	scene->skyGuid = sceneObj["skyGuid"].toString();
	scene->ambientMusicGuid = sceneObj["ambientMusicGuid"].toString();
	auto volume = sceneObj["ambientMusicVolume"].toDouble(50);
	scene->setAmbientMusicVolume(volume);
	auto asset = handle->fetchAsset(scene->ambientMusicGuid);
	if (!asset.name.isEmpty()) {
		QString fullPathToAudio = IrisUtils::join(Globals::project->getProjectFolder(), asset.name);
		scene->setAmbientMusic(fullPathToAudio);
		scene->startPlayingAmbientMusic();
	}

	scene->skyType = static_cast<iris::SkyType>(sceneObj["skyType"].toInt());
    scene->setAmbientColor(this->readColor(sceneObj["ambientColor"].toObject()));

	QJsonObject skyDataDef = sceneObj["skyData"].toObject();
	for (const auto &key : skyDataDef.keys()) {
		scene->skyData.insert(key, skyDataDef.value(key).toObject());
	}

	switch (scene->skyType) {
        case iris::SkyType::SINGLE_COLOR: {
			scene->skyColor = readColor(scene->skyData.value("SingleColor").value("skyColor").toObject());
			break;
		}

		case iris::SkyType::REALISTIC: {
			auto realisticDefinition = scene->skyData.value("Realistic");

			scene->skyRealistic.luminance		= realisticDefinition["luminance"].toDouble(1.f);
			scene->skyRealistic.reileigh		= realisticDefinition["reileigh"].toDouble(2.5f);
			scene->skyRealistic.mieCoefficient	= realisticDefinition["mieCoefficient"].toDouble(.053f);
			scene->skyRealistic.mieDirectionalG = realisticDefinition["mieDirectionalG"].toDouble(.75f);
			scene->skyRealistic.turbidity		= realisticDefinition["turbidity"].toDouble(.32f);
			scene->skyRealistic.sunPosX			= realisticDefinition["sunPosX"].toDouble(10.f);
			scene->skyRealistic.sunPosY			= realisticDefinition["sunPosY"].toDouble(7.f);
			scene->skyRealistic.sunPosZ			= realisticDefinition["sunPosZ"].toDouble(10.f);
			break;
		}

		case iris::SkyType::EQUIRECTANGULAR: {
			QString textureGuid = scene->skyData.value("Equirectangular").value("equiSkyGuid").toString();
			auto image = IrisUtils::join(Globals::project->getProjectFolder(), handle->fetchAsset(textureGuid).name);
			if (QFileInfo(image).isFile()) scene->setSkyTexture(iris::Texture2D::load(image, false));
			break;
		}

        case iris::SkyType::CUBEMAP: {
			auto cubeDefs = scene->skyData.value("Cubemap");
			auto front = handle->fetchAsset(cubeDefs["front"].toString()).name;
			auto back = handle->fetchAsset(cubeDefs["back"].toString()).name;
			auto left = handle->fetchAsset(cubeDefs["left"].toString()).name;
			auto right = handle->fetchAsset(cubeDefs["right"].toString()).name;
			auto top = handle->fetchAsset(cubeDefs["top"].toString()).name;
			auto bottom = handle->fetchAsset(cubeDefs["bottom"].toString()).name;

			QVector<QString> sides = { front, back, top, bottom, left, right };
			for (int i = 0; i < sides.count(); ++i) {
				QString path = IrisUtils::join(Globals::project->getProjectFolder(), sides[i]);
				sides[i] = QFileInfo(path).isFile() ? path : QString();
			}

			// We need at least one valid image to get some metadata from
			QImage* info;
			bool useTex = false;
			for (const auto image : sides) {
				if (!image.isEmpty()) {
					info = new QImage(image);
					useTex = true;
					break;
				}
			}

			if (useTex) {
				scene->setSkyTexture(iris::Texture2D::createCubeMap(sides[0], sides[1], sides[2], sides[3], sides[4], sides[5], info));
			}

			break;
		}

        case iris::SkyType::GRADIENT: {
			auto gradientDefinition = scene->skyData.value("Gradient");

			scene->gradientTop = SceneReader::readColor(gradientDefinition.value("gradientTop").toObject());
			scene->gradientMid = SceneReader::readColor(gradientDefinition.value("gradientMid").toObject());
			scene->gradientBot = SceneReader::readColor(gradientDefinition.value("gradientBot").toObject());
			scene->gradientOffset = gradientDefinition.value("gradientOffset").toDouble();
			break;
		}

        case iris::SkyType::MATERIAL: {
			scene->skyType = iris::SkyType::MATERIAL;
			break;
		}

        default: break;
    }

    scene->switchSkyTexture(scene->skyType);

    scene->fogColor = this->readColor(sceneObj["fogColor"].toObject());
    scene->fogStart = sceneObj["fogStart"].toDouble(100);
    scene->fogEnd = sceneObj["fogEnd"].toDouble(120);
    scene->fogEnabled = sceneObj["fogEnabled"].toBool(true);
    scene->shadowEnabled = sceneObj["shadowEnabled"].toBool(true);
	scene->setWorldGravity(sceneObj["gravity"].toDouble(Constants::GRAVITY));

    auto rootNode = sceneObj["rootNode"].toObject();
    QJsonArray children = rootNode["children"].toArray();

    for (const auto childObj : children) {
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
    } else if (nodeType == "grab") {
		sceneNode = createGrab(nodeObj).staticCast<iris::SceneNode>();
	}
	else {
        sceneNode = iris::SceneNode::create();
    }

    //read transform
    readSceneNodeTransform(nodeObj,sceneNode);

    readAnimationData(nodeObj,sceneNode);

    //read name
    sceneNode->name = nodeObj["name"].toString("");
	sceneNode->setGUID(nodeObj["guid"].toString(GUIDManager::generateGUID()));
    sceneNode->setAttached(nodeObj["attached"].toBool());
    sceneNode->setPickable(nodeObj["pickable"].toBool(true));

	sceneNode->isPhysicsBody = nodeObj["physicsObject"].toBool();

	if (sceneNode->isPhysicsBody) {
		QJsonObject physicsDef = nodeObj["physicsProperties"].toObject();
		sceneNode->physicsProperty.centerOfMass = readVector3(physicsDef["centerOfMass"].toObject());
		sceneNode->physicsProperty.isStatic = physicsDef["static"].toBool();
		sceneNode->physicsProperty.objectCollisionMargin = physicsDef["collisionMargin"].toDouble();
		sceneNode->physicsProperty.objectDamping = physicsDef["damping"].toDouble();
		sceneNode->physicsProperty.objectMass = physicsDef["mass"].toDouble();
		sceneNode->physicsProperty.objectFriction = physicsDef["friction"].toDouble(.5f);
		sceneNode->physicsProperty.objectRestitution = physicsDef["bounciness"].toDouble();
		sceneNode->physicsProperty.pivotPoint = readVector3(physicsDef["pivot"].toObject());
		sceneNode->physicsProperty.shape = static_cast<iris::PhysicsCollisionShape>(physicsDef["shape"].toInt());
		sceneNode->physicsProperty.type = static_cast<iris::PhysicsType>(physicsDef["type"].toInt());

		QJsonArray constraints = physicsDef["constraints"].toArray();
		for (const auto &constraint : constraints) {
			QJsonObject constraintObject = constraint.toObject();

			iris::ConstraintProperty constraintProp;
			constraintProp.constraintFrom = constraintObject.value("constraintFrom").toString();
			constraintProp.constraintTo = constraintObject.value("constraintTo").toString();
			constraintProp.constraintType = static_cast<iris::PhysicsConstraintType>(constraintObject.value("constraintType").toInt());

			sceneNode->physicsProperty.constraints.append(constraintProp);
		}
	}

    QJsonArray children = nodeObj["children"].toArray();
    for (auto childObj : children) {
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
    if (!pos.isEmpty()) sceneNode->setLocalPos(readVector3(pos));

    auto rot = nodeObj["rot"].toObject();
    if (!rot.isEmpty()) {
        //the rotation is stored as euler angles
        sceneNode->setLocalRot(QQuaternion::fromEulerAngles(readVector3(rot)).normalized());
    }

    auto scale = nodeObj["scale"].toObject();
    if (!scale.isEmpty()) {
        sceneNode->setLocalScale(readVector3(scale));
    } else {
        sceneNode->setLocalScale(QVector3D(1, 1, 1));
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

	auto asset = handle->fetchAsset(nodeObj["mesh"].toString(""));

    QString source = nodeObj["mesh"].toString("");
	// Keep a special reference to embedded asset primitives for now
	if (!source.startsWith(":")) {
        source = IrisUtils::join(assetDirectory, asset.name);
	}

    int meshIndex = nodeObj["meshIndex"].toInt(0);
    QString meshGUID = nodeObj["guid"].toString();

    if (!source.isEmpty()) {
        auto mesh = getMesh(source, meshIndex);

        if (source.startsWith(":")) {
            meshNode->setMesh(source);
			meshNode->meshPath = source;
        } else {
            meshNode->setMesh(mesh);
			meshNode->meshPath = nodeObj["mesh"].toString();
        }

        meshNode->setGUID(meshGUID);
		meshNode->setVisible(nodeObj["visible"].toBool(true));
        meshNode->meshIndex = meshIndex;
    }

    auto material = readMaterial(nodeObj);
    meshNode->setMaterial(material);

    QString faceCullingMode = nodeObj["faceCullingMode"].toString("back");

    if (faceCullingMode == "back") {
        meshNode->setFaceCullingMode(iris::FaceCullingMode::Back);
    } else if (faceCullingMode == "front") {
        meshNode->setFaceCullingMode(iris::FaceCullingMode::Front);
    } else if (faceCullingMode == "material") {
        meshNode->setFaceCullingMode(iris::FaceCullingMode::DefinedInMaterial);
    } else {
        meshNode->setFaceCullingMode(iris::FaceCullingMode::None);
    }

    meshNode->applyDefaultPose();

    return meshNode;
}

iris::ShadowMapType evalShadowMapType(QString shadowType)
{
    if (shadowType=="hard")
        return iris::ShadowMapType::Hard;
    if (shadowType=="soft")
        return iris::ShadowMapType::Soft;
    if (shadowType=="verysoft")
        return iris::ShadowMapType::VerySoft;

    return iris::ShadowMapType::None;
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
	lightNode->setVisible(nodeObj["visible"].toBool(true));

	lightNode->shadowAlpha = (float)nodeObj["shadowAlpha"].toDouble(1.0f);
	lightNode->shadowColor = readColor(nodeObj["shadowColor"].toObject());

    //shadow data
    auto shadowMap = lightNode->shadowMap;
    shadowMap->bias = (float)nodeObj["shadowBias"].toDouble(0.0015f);
    // ensure shadow map size isnt too big ro too small
    auto res = qBound(512, nodeObj["shadowSize"].toInt(1024), 4096);
    shadowMap->setResolution(res);
    shadowMap->shadowType = evalShadowMapType(nodeObj["shadowType"].toString());

    //TODO: move this to the sceneview widget or somewhere more appropriate
    if (lightNode->lightType == iris::LightType::Directional) {
        lightNode->icon = iris::Texture2D::load(":/icons/light.png");
    } else {
        lightNode->icon = iris::Texture2D::load(":/icons/bulb.png");
    }

    lightNode->iconSize = 0.5f;

    return lightNode;
}

iris::ViewerNodePtr SceneReader::createViewer(QJsonObject& nodeObj)
{
    auto viewerNode = iris::ViewerNode::create();
    viewerNode->setViewScale((float)nodeObj["viewScale"].toDouble(1.0f));
	viewerNode->setVisible(nodeObj["visible"].toBool(true));
	viewerNode->setActiveCharacterController(nodeObj["activeCharacterController"].toBool(false));

    return viewerNode;
}

iris::ParticleSystemNodePtr SceneReader::createParticleSystem(QJsonObject& nodeObj)
{
    auto particleNode = iris::ParticleSystemNode::create();

    particleNode->setGUID(nodeObj["guid"].toString());
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

    QString textureStr = QDir(assetDirectory).filePath(handle->fetchAsset(nodeObj["texture"].toString()).name);

    particleNode->setTexture(iris::Texture2D::load(getAbsolutePath(textureStr)));
	particleNode->setVisible(nodeObj["visible"].toBool(true));

    return particleNode;
}

iris::GrabNodePtr SceneReader::createGrab(QJsonObject & nodeObj)
{
	iris::HandPoseType poseType;
	auto poseName = nodeObj["poseType"].toString();
	if (poseName == "grab")
		poseType = iris::HandPoseType::Grab;
	else if (poseName == "pinch")
		poseType = iris::HandPoseType::Pinch;

	auto grabNode = iris::GrabNode::create();
	grabNode->setGUID(nodeObj["guid"].toString());
	grabNode->setPose(poseType);
	grabNode->poseFactor = (float)nodeObj["poseFactor"].toDouble();

	return grabNode;
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
	MaterialReader reader;
	if (useAlternativeLocation) reader.setSource(TextureSource::GlobalAssets, assetDirectory);
    if (nodeObj["material"].isNull()) return iris::CustomMaterial::create();

	auto mat = nodeObj["material"].toObject();
	return reader.parseMaterial(mat, handle, true);
   
/*
	auto m = iris::CustomMaterial::create();
    auto shaderGuid = mat["guid"].toString();

    m->setName(mat["name"].toString());
    m->setGuid(shaderGuid);

    QFileInfo shaderFile;

    // Note that this runs after asset accumulation, hence why we can get custom shaders used
    QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
    while (it.hasNext()) {
        it.next();
        if (it.key() == shaderGuid) {
            shaderFile = QFileInfo(IrisUtils::getAbsoluteAssetPath(it.value()));
            break;
        }
    }

    if (shaderFile.exists()) {
        m->generate(shaderFile.absoluteFilePath());
    }
    else {
        if (useAlternativeLocation) {
            auto shader = handle->fetchAssetData(shaderGuid);
            QJsonObject shaderDefinition = QJsonDocument::fromBinaryData(shader).object();

            if (!shaderDefinition.isEmpty()) {
                auto vAsset = handle->fetchAsset(shaderDefinition["vertex_shader"].toString());
                auto fAsset = handle->fetchAsset(shaderDefinition["fragment_shader"].toString());

                if (!vAsset.name.isEmpty()) shaderDefinition["vertex_shader"] = QDir(assetDirectory).filePath(vAsset.name);
                if (!fAsset.name.isEmpty()) shaderDefinition["fragment_shader"] = QDir(assetDirectory).filePath(fAsset.name);
                
                m->generate(shaderDefinition);
            }
        }
        else {
            for (auto asset : AssetManager::getAssets()) {
                if (asset->type == ModelTypes::Shader) {
                    if (asset->assetGuid == m->getGuid()) {
                        auto def = asset->getValue().toJsonObject();
                        auto vertexShader = def["vertex_shader"].toString();
                        auto fragmentShader = def["fragment_shader"].toString();
                        for (auto asset : AssetManager::getAssets()) {
                            if (asset->type == ModelTypes::File) {
                                if (vertexShader == asset->assetGuid) vertexShader = asset->path;
                                if (fragmentShader == asset->assetGuid) fragmentShader = asset->path;
                            }
                        }
                        def["vertex_shader"] = vertexShader;
                        def["fragment_shader"] = fragmentShader;

                        m->generate(def);
                    }
                }
            }
        }
    }

    for (auto prop : m->properties) {
        if (mat.contains(prop->name)) {
            if (prop->type == iris::PropertyType::Texture) {
                QString textureStr = !mat[prop->name].toString().isEmpty()
                        ? QDir(assetDirectory).filePath(handle->fetchAsset(mat[prop->name].toString()).name)
                        : QString();

                m->setValue(prop->name, textureStr);
            } else {
                m->setValue(prop->name, mat[prop->name].toVariant());
            }
        }
    }

    return m;
	*/
}

void SceneReader::extractAssetsFromAssimpScene(QString filePath)
{
    if (!assimpScenes.contains(filePath)) {
        QList<iris::MeshPtr> meshList;
        QMap<QString, iris::SkeletalAnimationPtr> animationss;
        
        if (useAlternativeLocation) {
		    iris::GraphicsHelper::loadAllMeshesAndAnimationsFromFile(filePath, meshList, animationss);
        }
        else {
            iris::GraphicsHelper::loadAllMeshesAndAnimationsFromStore<Asset*>(AssetManager::getAssets(),
                filePath,
                meshList,
                animationss);
        }

        meshes.insert(filePath, meshList);
        assimpScenes.insert(filePath);
        animations.insert(filePath, animationss);
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
