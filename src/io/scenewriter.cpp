/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "io/scenewriter.h"

#include <Qt>
#include <QVector>

#include <QQuaternion>
#include <QVector3D>
#include <QDir>
#include <QFile>

#include "irisgl/document/assets/mesh.h"
#include "irisgl/import/model.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/texture.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/assets/shader.h"
#include "irisgl/document/materials/renderstates.h"
#include "irisgl/document/materials/rasterizerstate.h"
#include "irisgl/import/graphicshelper.h"
#include "irisgl/core/viewport.h"
#include "irisgl/document/animation/animableproperty.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/propertyanim.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/viewernode.h"

#include "irisgl/document/materials/postprocess.h"
#include "irisgl/document/materials/postprocessmanager.h"

#include "io/assetiobase.h"
#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "viewport/editordata.h"

Database *SceneWriter::handle = 0;
Project *SceneWriter::projectHandle = nullptr;

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

    return QJsonDocument(projectObj).toJson();
}

void SceneWriter::writeScene(QJsonObject& projectObj, iris::ScenePtr scene)
{
    QJsonObject sceneObj;

	QJsonObject skyDefs;
	QMapIterator<QString, QJsonObject> it(scene->skyData);
	while (it.hasNext()) {
		it.next();
		skyDefs.insert(it.key(), it.value());
	}

	sceneObj["skyType"] = static_cast<int>(scene->skyType);
    sceneObj["skyGuid"] = scene->skyGuid;
    sceneObj["skyData"] = skyDefs;
	sceneObj["ambientMusicGuid"] = scene->ambientMusicGuid;
	sceneObj["ambientMusicVolume"] = scene->ambientMusicVolume;
    sceneObj["gravity"] = scene->gravity;
    sceneObj["ambientColor"] = jsonColor(scene->ambientColor);

    sceneObj["fogColor"] = jsonColor(scene->fogColor);
    sceneObj["fogStart"] = scene->fogStart;
    sceneObj["fogEnd"] = scene->fogEnd;
    sceneObj["fogEnabled"] = scene->fogEnabled;
    sceneObj["shadowEnabled"] = scene->shadowEnabled;

    // Anti-aliasing: the REQUESTED MSAA sample count (1 = off). The achieved
    // count is a per-machine runtime fact and is never serialized.
    sceneObj["antiAliasing"] = scene->antiAliasing;

    // Global illumination (world panel). Mode/quality are written as stable
    // strings — the enum ints must stay free to be reordered.
    static const char *giModeNames[] = { "off", "instant_radiosity", "vct", "vct_pcc_hybrid" };
    static const char *giQualityNames[] = { "low", "medium", "high" };
    sceneObj["giMode"] = giModeNames[qBound(0, static_cast<int>(scene->giMode), 3)];
    sceneObj["giQuality"] = giQualityNames[qBound(0, static_cast<int>(scene->giQuality), 2)];
    sceneObj["giBoundsMin"] = jsonVector3(scene->giBoundsMin);
    sceneObj["giBoundsMax"] = jsonVector3(scene->giBoundsMax);
    sceneObj["giLight"] = scene->giLightGuid;
    sceneObj["giNumBounces"] = scene->giNumBounces;
    sceneObj["giAutoRefresh"] = scene->giAutoRefresh;
    sceneObj["giPccGrid"] = jsonVector3(scene->giPccGrid);
	
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

void SceneWriter::writeEditorData(QJsonObject& projectObj, EditorData* editorData)
{
    QJsonObject editorObj;
    editorObj["showLightWires"] = editorData->showLightWires;
	editorObj["showDebugDrawFlags"] = editorData->showDebugDrawFlags;
    editorObj["showGrid"] = editorData->showGrid;

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
	sceneNodeObj["guid"] = sceneNode->getGUID();
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

    writeAnimationData(sceneNodeObj, sceneNode);

	sceneNodeObj["physicsObject"] = sceneNode->isPhysicsBody;

	if (sceneNode->isPhysicsBody) {
		QJsonObject physicsProperties;

		physicsProperties.insert("centerOfMass", jsonVector3(sceneNode->physicsProperty.centerOfMass));
		physicsProperties.insert("pivot", jsonVector3(sceneNode->physicsProperty.pivotPoint));
		physicsProperties.insert("static", sceneNode->physicsProperty.isStatic);
		physicsProperties.insert("collisionMargin", sceneNode->physicsProperty.objectCollisionMargin);
		physicsProperties.insert("damping", sceneNode->physicsProperty.objectDamping);
		physicsProperties.insert("mass", sceneNode->physicsProperty.objectMass);
		physicsProperties.insert("friction", sceneNode->physicsProperty.objectFriction);
		physicsProperties.insert("bounciness", sceneNode->physicsProperty.objectRestitution);
		physicsProperties.insert("shape", static_cast<int>(sceneNode->physicsProperty.shape));
		physicsProperties.insert("type", static_cast<int>(sceneNode->physicsProperty.type));

		QJsonArray constraintProperties;

		for (const auto &constraint : sceneNode->physicsProperty.constraints) {
			QJsonObject constraintProp;
			constraintProp.insert("constraintFrom", constraint.constraintFrom);
			constraintProp.insert("constraintTo", constraint.constraintTo);
			constraintProp.insert("constraintType", static_cast<int>(constraint.constraintType));

			constraintProperties.append(constraintProp);
		}

		physicsProperties["constraints"] = constraintProperties;

		sceneNodeObj["physicsProperties"] = physicsProperties;
	}

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
            // Never persist an ABSOLUTE source (GLB importer fix phase 1):
            // import sets SkeletalAnimation::source to the model's absolute
            // path, which breaks the {source, name} reference on any other
            // machine (or after the project moves). Store it relative to the
            // project dir — SceneReader::getSkeletalAnimation resolves
            // relative sources via getAbsolutePath and re-relativizes after
            // load, so saved scenes converge on the stable relative form.
            QString source = skelAnim->source;
            if (!source.isEmpty() && QFileInfo(source).isAbsolute())
                source = dir.relativeFilePath(source);
            skelObj["source"] = source;
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
    writeSceneNodeMaterial(matObj, meshNode->getMaterial(), relative);
	sceneNodeObject["material"] = matObj;
	//auto matDef = meshNode->getMaterial().staticCast<iris::CustomMaterial>()->materialDefinitions;
	//qDebug() << QJsonDocument(matDef).toJson(QJsonDocument::Indented);
	//sceneNodeObject["material"] = meshNode->getMaterial().staticCast<iris::CustomMaterial>()->materialDefinitions;
}

void SceneWriter::writeViewerData(QJsonObject& sceneNodeObject,iris::ViewerNodePtr viewerNode)
{
    sceneNodeObject.insert("viewScale", viewerNode->getViewScale());
	sceneNodeObject.insert("visible", viewerNode->isVisible());
	sceneNodeObject.insert("activeCharacterController", viewerNode->isActiveCharacterController());
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
    // An emitter can have no texture (cleared in the property panel): write an
    // empty guid instead of dereferencing null (audit defect #6).
    sceneNodeObject["texture"]              = node->texture
        ? handle->fetchAssetGUIDByName(QFileInfo(node->texture->getSource()).fileName(), projectHandle->getProjectGuid())
        : QString();
}


void SceneWriter::writeSceneNodeMaterial(QJsonObject& matObj, iris::MaterialPtr mat, bool relative)
{
	if (!mat) return;

	// materialType discriminates which Material subclass to rebuild on load.
	// Absent in scenes written before PBR existed; the reader treats a missing
	// key as "custom", so older scenes load unchanged.
	auto customMat = mat.dynamicCast<iris::CustomMaterial>();
	if (!!customMat) {
		matObj["materialType"] = "custom";
		matObj["name"]         = customMat->getName();
		matObj["shaderGuid"]   = customMat->getGuid();
	}
	else if (!!mat.dynamicCast<iris::PbrMaterial>()) {
		matObj["materialType"] = "pbr";
	}
	else {
		// Some other Material subclass - record the parameters, but there is no
		// type tag that would let the reader rebuild it. Better to say so than
		// to write a tag that lies.
		matObj["materialType"] = "unknown";
	}

	matObj["version"] = 2;

	QJsonObject valuesObj;
    for (auto prop : mat->properties) {
        if (prop->type == iris::PropertyType::Bool) {
			valuesObj[prop->name] = prop->getValue().toBool();
        }

        if (prop->type == iris::PropertyType::Float) {
			valuesObj[prop->name] = prop->getValue().toFloat();
        }

        // PbrMaterial's alphaMode is an IntProperty; without this case it would
        // silently vanish from the saved scene.
        if (prop->type == iris::PropertyType::Int) {
			valuesObj[prop->name] = prop->getValue().toInt();
        }

        if (prop->type == iris::PropertyType::Color) {
			valuesObj[prop->name] = prop->getValue().value<QColor>().name();
        }

        if (prop->type == iris::PropertyType::Texture) {
			//matObj[prop->name] = relative ? getRelativePath(prop->getValue().toString()) : QFileInfo(prop->getValue().toString()).fileName();
			auto id = relative
				? handle->fetchAssetGUIDByName(QFileInfo(prop->getValue().toString()).fileName(), projectHandle->getProjectGuid())
				: getRelativePath(prop->getValue().toString());
			// A texture that is not a database asset (no GUID) would otherwise be
			// written as an empty string and lost; fall back to a relative path,
			// which the reader also resolves.
			if (relative && id.isEmpty() && !prop->getValue().toString().isEmpty())
				id = getRelativePath(prop->getValue().toString());
			valuesObj[prop->name] = id;
        }

		if (prop->type == iris::PropertyType::Vec2) {
			valuesObj[prop->name] = jsonVector2(prop->getValue().value<QVector2D>());
		}

		if (prop->type == iris::PropertyType::Vec3) {
			valuesObj[prop->name] = jsonVector3(prop->getValue().value<QVector3D>());
		}

		if (prop->type == iris::PropertyType::Vec4) {
			valuesObj[prop->name] = jsonVector4(prop->getValue().value<QVector4D>());
		}

		// add vector properties
    }

	matObj["values"] = valuesObj;
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

QJsonObject SceneWriter::jsonVector2(QVector2D vec)
{
    QJsonObject obj;
    obj["x"] = vec.x();
    obj["y"] = vec.y();

    return obj;
}

QJsonObject SceneWriter::jsonVector3(QVector3D vec)
{
	QJsonObject obj;
	obj["x"] = vec.x();
	obj["y"] = vec.y();
	obj["z"] = vec.z();

	return obj;
}

QJsonObject SceneWriter::jsonVector4(QVector4D vec)
{
	QJsonObject obj;
	obj["x"] = vec.x();
	obj["y"] = vec.y();
	obj["z"] = vec.z();
	obj["w"] = vec.w();

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
    case iris::ShadowMapType::VerySoft:
        return "verysoft";
    }

    return "none";
}

void SceneWriter::writeLightData(QJsonObject& sceneNodeObject,iris::LightNodePtr lightNode)
{
    sceneNodeObject["lightType"] = getLightNodeTypeName(lightNode->lightType);
    sceneNodeObject["intensity"] = lightNode->intensity;
    sceneNodeObject["distance"] = lightNode->distance;
    sceneNodeObject["spotCutOff"] = lightNode->spotCutOff;
    sceneNodeObject["spotCutOffSoftness"] = lightNode->spotCutOffSoftness;
    sceneNodeObject["rectWidth"] = lightNode->rectWidth;
    sceneNodeObject["rectHeight"] = lightNode->rectHeight;
    sceneNodeObject["doubleSided"] = lightNode->doubleSided;
    sceneNodeObject["accurate"] = lightNode->accurate;
	sceneNodeObject["color"] = jsonColor(lightNode->color);

	sceneNodeObject["shadowAlpha"] = lightNode->shadowAlpha;
	sceneNodeObject["shadowColor"] = jsonColor(lightNode->shadowColor);

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
        case iris::LightType::Area:
            return "area";
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
