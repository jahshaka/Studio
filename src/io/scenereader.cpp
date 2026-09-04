/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QSharedPointer>
#include "io/assetiobase.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonValueRef>
#include <QJsonDocument>

#include "io/materialreader.h"
#include "io/scenereader.h"
#include "io/assetmanager.h"
#include "data/guidmanager.h"

#include "data/constants.h"
#include "data/database/database.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/lightbindings.h"
#include "services/loadtimeline.h"
#include "services/meshbakestore.h"

#include <functional>
#include <QSqlDatabase>

#include "viewport/editordata.h"

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
#include "irisgl/document/scenegraph/scene.h"
#include "services/worldmodes.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/decalnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/viewernode.h"

#include "irisgl/document/materials/postprocess.h"
#include "irisgl/document/materials/postprocessmanager.h"

#include "irisgl/document/materials/postfx/fxaapostprocess.h"

#include "irisgl/document/physics/physicsproperties.h"
#include "irisgl/document/physics/physicshelper.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/materials/custommaterial.h"

#include "io/materialreader.h"
#include "data/guidmanager.h"

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
    // rotQuat first (the lossless spelling), euler for anything written before
    // it existed — see readSceneNodeTransform.
    const QJsonObject camRotQuat = camObj["rotQuat"].toObject();
    if (!camRotQuat.isEmpty())
        camera->setLocalRot(iris::Quat(float(camRotQuat["scalar"].toDouble(1.0)),
                                        float(camRotQuat["x"].toDouble(0.0)),
                                        float(camRotQuat["y"].toDouble(0.0)),
                                        float(camRotQuat["z"].toDouble(0.0))).normalized());
    else
        camera->setLocalRot(iris::Quat::fromEulerAngles(readVector3(camObj["rot"].toObject())));
	camera->setOrthagonalZoom((float)camObj["orthogonalSize"].toDouble(3.0f));
	iris::CameraProjection val = camObj["projectionMode"].toString().compare("orthogonal") == 0 ? iris::CameraProjection::Orthogonal : iris::CameraProjection::Perspective;
	camera->setProjection(val);

    auto editorData = new EditorData();
    editorData->editorCamera = camera;
    editorData->distFromPivot = (float)camObj["distanceFromPivot"].toDouble(5.0f);
    // Light wires default ON (owner 2026-08-31): scenes saved before the flag
    // existed read back true; an explicitly saved false is honored.
    editorData->showLightWires = editorObj["showLightWires"].toBool(true);
	editorData->showDebugDrawFlags = editorObj["showDebugDrawFlags"].toBool();
    // Grid defaults ON: scenes saved before the grid existed read back true.
    editorData->showGrid = editorObj["showGrid"].toBool(true);

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

QString SceneReader::resolveAssetPath(const QString &guid)
{
    if (guid.isEmpty()) return QString();
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();

    QString path;
    if (!useAlternativeLocation && project && !project->getProjectGuid().isEmpty())
        path = AssetCas::resolvePinned(conn, root, project->getProjectGuid(), guid);
    else
        path = AssetCas::resolveSource(conn, root, guid);

    if (path.isEmpty() && useAlternativeLocation && handle) {
        // Preview loads may pass an explicit directory (a store view).
        const QString name = handle->fetchAsset(guid).name;
        if (!name.isEmpty()) {
            const QString candidate = QDir(assetDirectory).filePath(name);
            if (QFileInfo::exists(candidate)) path = candidate;
        }
    }
    return path;
}

QStringList SceneReader::collectMeshSources(const QJsonObject &projectObj)
{
    // The prewarm PLAN. Walks the blob's node tree for "mesh" nodes and
    // resolves each source guid exactly as createMesh() will, so the worker
    // parses the same files the reader is about to ask for — no more, no
    // fewer. Pure JSON + the CAS resolution (which is DB work, hence UI
    // thread); no parsing happens here.
    QStringList out;
    std::function<void(const QJsonObject &)> walk = [&](const QJsonObject &nodeObj) {
        if (nodeObj["type"].toString() == QLatin1String("mesh")) {
            const QString source = nodeObj["mesh"].toString();
            if (!source.isEmpty() && !source.startsWith(':')) {
                const QString path = resolveAssetPath(source);
                if (!path.isEmpty() && !out.contains(path)) out.append(path);
            }
        }
        const QJsonArray children = nodeObj["children"].toArray();
        for (const auto &child : children) walk(child.toObject());
    };
    const QJsonObject sceneObj = projectObj["scene"].toObject();
    const QJsonArray roots = sceneObj["rootNode"].toObject()["children"].toArray();
    for (const auto &child : roots) walk(child.toObject());
    return out;
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
	const QString ambientMusicPath = resolveAssetPath(scene->ambientMusicGuid);
	if (!ambientMusicPath.isEmpty()) {
		scene->setAmbientMusic(ambientMusicPath);
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

			// Per-key defaults are the *model's* working values (VISUAL_PARITY
			// item 1), matching iris::Scene's constructor: a key missing from an
			// older document lands on something the Preetham bake can use rather
			// than the legacy panel's degenerate corner.
			{
				const iris::SkyRealistic d = iris::SkyRealistic::defaults();
				scene->skyRealistic.luminance		= realisticDefinition["luminance"].toDouble(d.luminance);
				scene->skyRealistic.reileigh		= realisticDefinition["reileigh"].toDouble(d.reileigh);
				scene->skyRealistic.mieCoefficient	= realisticDefinition["mieCoefficient"].toDouble(d.mieCoefficient);
				scene->skyRealistic.mieDirectionalG = realisticDefinition["mieDirectionalG"].toDouble(d.mieDirectionalG);
				scene->skyRealistic.turbidity		= realisticDefinition["turbidity"].toDouble(d.turbidity);
				scene->skyRealistic.sunPosX			= realisticDefinition["sunPosX"].toDouble(d.sunPosX);
				scene->skyRealistic.sunPosY			= realisticDefinition["sunPosY"].toDouble(d.sunPosY);
				scene->skyRealistic.sunPosZ			= realisticDefinition["sunPosZ"].toDouble(d.sunPosZ);
			}
			break;
		}

		case iris::SkyType::EQUIRECTANGULAR: {
			QString textureGuid = scene->skyData.value("Equirectangular").value("equiSkyGuid").toString();
			auto image = resolveAssetPath(textureGuid);
			if (QFileInfo(image).isFile()) scene->setSkyTexture(iris::Texture2D::load(image, false));
			break;
		}

        case iris::SkyType::CUBEMAP: {
			auto cubeDefs = scene->skyData.value("Cubemap");
			auto front = resolveAssetPath(cubeDefs["front"].toString());
			auto back = resolveAssetPath(cubeDefs["back"].toString());
			auto left = resolveAssetPath(cubeDefs["left"].toString());
			auto right = resolveAssetPath(cubeDefs["right"].toString());
			auto top = resolveAssetPath(cubeDefs["top"].toString());
			auto bottom = resolveAssetPath(cubeDefs["bottom"].toString());

			QVector<QString> sides = { front, back, top, bottom, left, right };
			for (int i = 0; i < sides.count(); ++i) {
				sides[i] = QFileInfo(sides[i]).isFile() ? sides[i] : QString();
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
			// The Material sky type was removed from the UI (it never worked, even
			// in the legacy renderer). Old scenes fall back to a single-colour sky.
			scene->skyType = iris::SkyType::SINGLE_COLOR;
			if (scene->skyData.contains("SingleColor"))
				scene->skyColor = readColor(scene->skyData.value("SingleColor").value("skyColor").toObject());
			break;
		}

        default: break;
    }


    scene->fogColor = this->readColor(sceneObj["fogColor"].toObject());
    scene->fogStart = sceneObj["fogStart"].toDouble(100);
    scene->fogEnd = sceneObj["fogEnd"].toDouble(120);
    scene->fogEnabled = sceneObj["fogEnabled"].toBool(true);
    // Fog became EXPONENTIAL. No migration pass exists and none is needed: a scene
    // written before the change has no fogDensity key, and its old linear pair is
    // exactly what the default derives from.
    scene->fogDensity = sceneObj["fogDensity"].toDouble(
        double(iris::Scene::fogDensityFromLinear(scene->fogStart, scene->fogEnd)));
    scene->fogHeightDensity = sceneObj["fogHeightDensity"].toDouble(0.0);
    scene->fogHeightFalloff = sceneObj["fogHeightFalloff"].toDouble(0.1);
    scene->fogHeightLevel = sceneObj["fogHeightLevel"].toDouble(0.0);
    scene->fogBreakMinBrightness = sceneObj["fogBreakMinBrightness"].toDouble(0.25);
    scene->fogBreakFalloff = sceneObj["fogBreakFalloff"].toDouble(0.1);

    // Global illumination: absent (older scenes) or unknown values mean OFF.
    {
        const QString giMode = sceneObj["giMode"].toString("off");
        if (giMode == "instant_radiosity") scene->giMode = iris::GiMode::INSTANT_RADIOSITY;
        else if (giMode == "vct") scene->giMode = iris::GiMode::VCT;
        else if (giMode == "vct_pcc_hybrid") scene->giMode = iris::GiMode::VCT_PCC_HYBRID;
        else scene->giMode = iris::GiMode::OFF;
        const QString giQuality = sceneObj["giQuality"].toString("medium");
        if (giQuality == "low") scene->giQuality = iris::GiQuality::LOW;
        else if (giQuality == "high") scene->giQuality = iris::GiQuality::HIGH;
        else scene->giQuality = iris::GiQuality::MEDIUM;
        scene->giBoundsMin = readVector3(sceneObj["giBoundsMin"].toObject());
        scene->giBoundsMax = readVector3(sceneObj["giBoundsMax"].toObject());
        scene->giLightGuid = sceneObj["giLight"].toString();
        scene->giNumBounces = qBound(1, sceneObj["giNumBounces"].toInt(1), 4);
        scene->giAutoRefresh = sceneObj["giAutoRefresh"].toBool(true);
        if (sceneObj.contains("giPccGrid"))   // pre-hybrid documents keep the 3x2x3 default
            scene->giPccGrid = readVector3(sceneObj["giPccGrid"].toObject());
    }
    scene->shadowEnabled = sceneObj["shadowEnabled"].toBool(true);
    // Anti-aliasing: absent (older scenes) means off (1 sample); anything odd
    // is rounded down to the nearest supported step (1/2/4/8).
    {
        const int aa = sceneObj["antiAliasing"].toInt(1);
        scene->antiAliasing = aa >= 8 ? 8 : aa >= 4 ? 4 : aa >= 2 ? 2 : 1;
    }
    // Shadow-map resolution: absent or <= 0 means Auto (derive from the lights);
    // anything else is clamped to the engine's own [256, 8192] window.
    {
        const int sr = sceneObj["shadowResolution"].toInt(0);
        scene->shadowResolution = sr <= 0 ? 0 : qBound(256, sr, 8192);
    }
    // Shadow FILTER quality: absent means Auto (-1); otherwise 0/1/2.
    {
        const int sf = sceneObj["shadowFilterTier"].toInt(-1);
        scene->shadowFilterTier = (sf >= 0 && sf <= 2) ? sf : -1;
        // Absent in every scene written before the ParticleFX2 adoption: 1 = real time.
        scene->particleTimeScale = std::max(0.0, sceneObj["particleTimeScale"].toDouble(1.0));
    }
    // Post-processing chain (POST_CHAIN_SPEC §§3-7). Absent = off, which is what
    // every document written before the chain existed means.
    scene->hdrEnabled = sceneObj["hdrEnabled"].toBool(false);
    scene->exposure = float(sceneObj["exposure"].toDouble(0.0));
    scene->bloomEnabled = sceneObj["bloomEnabled"].toBool(false);
    scene->bloomThreshold = float(sceneObj["bloomThreshold"].toDouble(5.0));
    scene->ssaoEnabled = sceneObj["ssaoEnabled"].toBool(false);
    scene->ssaoScale = float(qBound(0.25, sceneObj["ssaoScale"].toDouble(1.0), 1.0));
    scene->ssaoPower = float(qBound(0.1, sceneObj["ssaoPower"].toDouble(1.5), 8.0));
    scene->ssaoRadius = float(qBound(0.05, sceneObj["ssaoRadius"].toDouble(2.0), 64.0));
    scene->smaaPreset = qBound(-1, sceneObj["smaaPreset"].toInt(-1), 3);
    scene->ssrMode = qBound(0, sceneObj["ssrMode"].toInt(0), 2);
    scene->refractionsMode = qBound(0, sceneObj["refractionsMode"].toInt(0), 2);

    // Planar reflections: absent means "follow the world mode" on all three
    // (-1 / 0 / -1), which is what every document written before this feature
    // says by omission. Explicit values are clamped to what the engine accepts.
    {
        const int pb = sceneObj["planarReflectionBudget"].toInt(-1);
        scene->planarReflectionBudget = pb < 0 ? -1 : qBound(0, pb, 8);
        const int pres = sceneObj["planarReflectionResolution"].toInt(0);
        scene->planarReflectionResolution = pres <= 0 ? 0 : qBound(256, pres, 2048);
        const int ps = sceneObj["planarReflectionShadows"].toInt(-1);
        scene->planarReflectionShadows = (ps == 0 || ps == 1) ? ps : -1;
    }
    // World Mode (POST_CHAIN_SPEC §9). Absent reads as "custom": the fields
    // above ARE the truth for a document written before modes existed, and for
    // one the user never put on a tier. (§12 decision 8 proposed reading absent
    // as Epic; that would silently switch VCT GI, 4x MSAA and a 4096 shadow
    // atlas on for every existing scene — left to the owner.)
    {
        const QString m = sceneObj["worldMode"].toString().trimmed().toLower();
        scene->worldOverrides = sceneObj["worldOverrides"].toObject();
        if (m.isEmpty()) {
            // A document written before World Modes existed — the shipped sample
            // scenes, and nothing else. §12 decision 8: it reads as EPIC, and the
            // tier is applied so the write-through invariant holds (a backing
            // field is always the resolved value). Its own settings are NOT
            // preserved as overrides: that would pin every row of every old
            // document for ever and make the mode meaningless.
            worldmodes::setMode(scene, worldmodes::Mode::Epic);
        } else {
            bool ok = false;
            const auto mode = worldmodes::modeFromName(m, &ok);
            scene->worldMode = ok ? int(mode) : int(worldmodes::Mode::Custom);
            // The fields above were just read from the document and ARE the
            // resolved values; no tier is re-applied, so a pinned row and a
            // hand-edited field both survive a round trip untouched.
        }
    }
    // Realistic-sky bake width: 256 (absent/older scenes), 512 or 1024.
    {
        const int sb = sceneObj["skyBakeResolution"].toInt(256);
        scene->skyBakeResolution = sb >= 1024 ? 1024 : sb >= 512 ? 512 : 256;
    }
    scene->ambientFromSky = sceneObj["ambientFromSky"].toBool(true);
	scene->setWorldGravity(sceneObj["gravity"].toDouble(Constants::GRAVITY));

    auto rootNode = sceneObj["rootNode"].toObject();

    // The World node's OWN identity. The writer has always serialized the root
    // like any other node (guid, name, animations, transform); the reader read
    // only its children and kept the brand-new root iris::Scene::create() had
    // just made — so every open MINTED A NEW GUID for World and dropped its
    // animation list, and every save therefore wrote a different blob for an
    // untouched scene (found by the reopen-fidelity blob diff, 2026-09-04).
    // Only the identity half is adopted here: the root's transform stays the
    // identity the Scene ctor gives it, which is what every scene in existence
    // has and what the whole scene graph is expressed relative to.
    {
        const QString rootGuid = rootNode["guid"].toString();
        if (!rootGuid.isEmpty()) scene->getRootNode()->setGUID(rootGuid);
        const QString rootName = rootNode["name"].toString();
        if (!rootName.isEmpty()) scene->getRootNode()->setName(rootName);
        readAnimationData(rootNode, scene->getRootNode());
    }

    QJsonArray children = rootNode["children"].toArray();

    for (const auto childObj : children) {
        auto sceneNodeObj = childObj.toObject();
        auto childNode = readSceneNode(sceneNodeObj);
        // keepTransform = FALSE, exactly like the nested children below: the
        // node's local TRS is what was just read, and it is already the local
        // transform this parent wants.
        //
        // THE DEFECT THIS FIXES (found by the clean-start sample audit,
        // 2026-09-04): addChild's default is keepTransform=TRUE, which makes
        // SceneNode::insertChild recompose the child's local TRS out of
        // parentGlobal^-1 * childGlobal — and it extracts the rotation with
        // QQuaternion::fromRotationMatrix(diff.normalMatrix()). normalMatrix
        // is the inverse-transpose, i.e. R*S^-1, NOT R, so the extracted
        // rotation is only correct when the scale is exactly 1. Every
        // top-level node in every saved scene therefore came back ROTATED on
        // open, by an amount that grows with how far its scale is from 1:
        // measured on Showroom, a 0.16/0.75/0.16 wall panel moved 0.66 deg and
        // a 1.5-scaled torus 10 deg PER OPEN, and since closing a project
        // autosaves, the wrong value is persisted and the error compounds
        // every single time the scene is opened. (Nested children never had
        // this: line ~565 already passes false.)
        //
        // insertChild's decomposition is wrong on its own account — a real
        // reparent of a scaled node loses the same way — but that is
        // irisgl-side and out of this lane's scope; it is reported, not fixed
        // here. This line is the reader's half and it is the one the shipped
        // samples hit.
        scene->getRootNode()->addChild(childNode, false);
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
    } else if (nodeType == "decal") {
        sceneNode = createDecal(nodeObj).staticCast<iris::SceneNode>();
    } else {
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
    // Absent = false: the writer only emits the key when the flag is on.
    sceneNode->setPlanarReflector(nodeObj["planarReflector"].toBool(false));
    // Shadow Caster: absent = TRUE (the document default) — the writer only
    // emits the key when the user turned casting off, so every scene written
    // before the key existed loads exactly as it did.
    sceneNode->setShadowCastingEnabled(nodeObj["castShadow"].toBool(true));

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
                // BOUNDS-CHECKED: the index comes straight from the file (the
                // length of its keyFrames array), and the channel accessors
                // answer null past the end of a track — a blob with four
                // entries on a vector3 property used to dereference it.
                if (!keyFrame) { index++; continue; }

                for (auto keyVal : keyList) {
                    auto keyObj = keyVal.toObject();
                    auto time = keyObj["time"].toDouble();
                    auto val = keyObj["value"].toDouble();
                    auto key = keyFrame->addKey(val, time);

                    key->leftSlope = keyObj["leftSlope"].toDouble();
                    key->rightSlope = keyObj["rightSlope"].toDouble();

                    // SPELLING MISMATCH, fixed 2026-09-04: SceneWriter has
                    // always written "leftTangentType"/"rightTangentType" and
                    // this read "leftTangent"/"rightTangent", so BOTH tangent
                    // types were silently reset to the default on every single
                    // reload — a curve authored Linear or Constant came back
                    // Free. The writer's spelling wins going forward; the short
                    // one is still accepted because a file may carry it (the
                    // reader is the only thing that ever named it).
                    const auto tangentName = [&keyObj](const char *preferred, const char *legacy) {
                        return keyObj.contains(QLatin1String(preferred))
                                   ? keyObj[QLatin1String(preferred)].toString()
                                   : keyObj[QLatin1String(legacy)].toString();
                    };
                    key->leftTangent = getTangentTypeFromName(
                        tangentName("leftTangentType", "leftTangent"));
                    key->rightTangent = getTangentTypeFromName(
                        tangentName("rightTangentType", "rightTangent"));

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
    // BOUNDS-CHECKED. This was a bare operator[] on the index the file happens
    // to carry: a blob whose activeAnimation points past its (possibly empty)
    // animation list indexed a QList out of range — undefined behaviour driven
    // straight from a document field. Out of range now means "no active
    // animation", the same thing -1 means.
    const auto &anims = sceneNode->getAnimations();
    if (activeAnimIndex >= 0 && activeAnimIndex < anims.size()) {
        sceneNode->setAnimation(anims[activeAnimIndex]);
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

    // Rotation. "rotQuat" is the node's stored quaternion, written since
    // 2026-09-04 alongside the historical euler triple, and it is preferred
    // because it is the only one that ROUND-TRIPS: quaternion -> euler ->
    // quaternion is not a fixed point in float, so reading the euler back moved
    // every rotated node a little on every single open (see the note beside the
    // writer). Absent = a scene written before the key, or by an older build:
    // read the euler exactly as before.
    const QJsonObject rotQuat = nodeObj["rotQuat"].toObject();
    if (!rotQuat.isEmpty()) {
        sceneNode->setLocalRot(iris::Quat(float(rotQuat["scalar"].toDouble(1.0)),
                                           float(rotQuat["x"].toDouble(0.0)),
                                           float(rotQuat["y"].toDouble(0.0)),
                                           float(rotQuat["z"].toDouble(0.0))).normalized());
    } else {
        auto rot = nodeObj["rot"].toObject();
        if (!rot.isEmpty()) {
            //the rotation is stored as euler angles
            sceneNode->setLocalRot(iris::Quat::fromEulerAngles(readVector3(rot)).normalized());
        }
    }

    auto scale = nodeObj["scale"].toObject();
    if (!scale.isEmpty()) {
        sceneNode->setLocalScale(readVector3(scale));
    } else {
        sceneNode->setLocalScale(iris::Vec3(1, 1, 1));
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

	// Without a database handle the mesh GUID cannot be resolved to a file
	// name; the node still loads for the ":"-prefixed built-in primitives.
    QString source = nodeObj["mesh"].toString("");
	// Keep a special reference to embedded asset primitives for now
	if (!source.startsWith(":")) {
        source = resolveAssetPath(source);
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
    // Serializer gap fixed (WEB_EXPORT_AUDIT §1): the mirror consumes softness
    // but it was never persisted. Default matches the LightNode constructor.
    lightNode->spotCutOffSoftness = (float)nodeObj["spotCutOffSoftness"].toDouble(1.0f);
    lightNode->rectWidth = (float)nodeObj["rectWidth"].toDouble(1.0f);
    lightNode->rectHeight = (float)nodeObj["rectHeight"].toDouble(1.0f);
    lightNode->doubleSided = nodeObj["doubleSided"].toBool(false);
    lightNode->accurate = nodeObj["accurate"].toBool(false);
    // Asset bindings: the guid is what was written; the path and the profile's
    // photometric scale are runtime state, re-derived from the store on every
    // load exactly like the sky's texture (the renderer opens FILES, and the
    // library owns where they live).
    lightNode->iesProfileGuid = nodeObj["iesProfile"].toString();
    lightNode->iesProfilePath = resolveAssetPath(lightNode->iesProfileGuid);
    lightNode->iesNormalisation =
        LightBindings::normalisationFor(lightNode->iesProfileGuid, handle);
    lightNode->lightTextureGuid = nodeObj["lightTexture"].toString();
    lightNode->lightTexturePath = resolveAssetPath(lightNode->lightTextureGuid);
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
        lightNode->icon = iris::Texture2D::load(":/icons/light.png");      // the sun glyph
    } else if (lightNode->lightType == iris::LightType::Spot) {
        lightNode->icon = iris::Texture2D::load(":/icons/spotlight.png");
    } else if (lightNode->lightType == iris::LightType::Area) {
        // No bundled glyph: SceneMirror::syncLightIcon draws a procedural
        // rounded-rect billboard for area lights when icon is unset.
    } else {
        lightNode->icon = iris::Texture2D::load(":/icons/bulb.png");
    }

    lightNode->iconSize = 0.5f;

    return lightNode;
}

iris::DecalNodePtr SceneReader::createDecal(QJsonObject& nodeObj)
{
    auto decalNode = iris::DecalNode::create();

    decalNode->textureGuid  = nodeObj["decalTexture"].toString();
    decalNode->normalGuid   = nodeObj["decalNormal"].toString();
    decalNode->emissiveGuid = nodeObj["decalEmissive"].toString();
    decalNode->width  = (float) nodeObj["width"].toDouble(1.0);
    decalNode->height = (float) nodeObj["height"].toDouble(1.0);
    decalNode->depth  = (float) nodeObj["depth"].toDouble(0.5);
    decalNode->metalness = (float) nodeObj["metalness"].toDouble(0.0);
    decalNode->roughness = (float) nodeObj["roughness"].toDouble(1.0);
    decalNode->ignoreAlphaDiffuse = nodeObj["ignoreAlphaDiffuse"].toBool(false);
    decalNode->setVisible(nodeObj["visible"].toBool(true));

    // Bytes: pin-first through the CAS, exactly like material maps. A guid that
    // no longer resolves leaves the path empty — the node still loads, draws its
    // wire box and projects nothing, rather than failing the whole scene load.
    decalNode->resolvedTexturePath  = resolveAssetPath(decalNode->textureGuid);
    decalNode->resolvedNormalPath   = resolveAssetPath(decalNode->normalGuid);
    decalNode->resolvedEmissivePath = resolveAssetPath(decalNode->emissiveGuid);

    return decalNode;
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

    // ---- ParticleFX2 keys (PARTICLES_FX2_SPEC §5) --------------------------
    // ALL OPTIONAL, all defaulted to the legacy behaviour: a scene written
    // before the adoption reads back with exactly the emitter it had. There is
    // no migration and none is needed — this ships as a new app, and the ten
    // legacy keys above map 1:1 onto the engine's emitter and affectors.
    //
    // The three "random" spreads (speedError/lifeError/scaleError) were edited
    // by the panel and never written for ten years (audit defect #7). They are
    // written now, absolute rather than fractional, and absent means 0.
    particleNode->speedError = (float) nodeObj["speedError"].toDouble(0.0);
    particleNode->lifeError  = (float) nodeObj["lifeError"].toDouble(0.0);
    particleNode->scaleError = (float) nodeObj["scaleError"].toDouble(0.0);
    particleNode->maxParticles = nodeObj["maxParticles"].toInt(0);

    particleNode->shape = iris::ParticleSystemNode::shapeFromName(
        nodeObj["shape"].toString("point"));
    particleNode->orientation = iris::ParticleSystemNode::orientationFromName(
        nodeObj["orientation"].toString("billboard"));
    particleNode->preset = iris::ParticleSystemNode::presetFromName(
        nodeObj["preset"].toString("custom"));
    particleNode->coneAngle        = (float) nodeObj["coneAngle"].toDouble(0.0);
    particleNode->turbulence       = (float) nodeObj["turbulence"].toDouble(0.0);
    particleNode->rotationSpeedMin = (float) nodeObj["rotationSpeedMin"].toDouble(0.0);
    particleNode->rotationSpeedMax = (float) nodeObj["rotationSpeedMax"].toDouble(0.0);
    particleNode->burstDuration    = (float) nodeObj["burstDuration"].toDouble(0.0);
    particleNode->burstRepeatDelay = (float) nodeObj["burstRepeatDelay"].toDouble(0.0);
    particleNode->startDelay       = (float) nodeObj["startDelay"].toDouble(0.0);
    particleNode->alphaHash        = nodeObj["alphaHash"].toBool(true);
    if (nodeObj.contains("extents"))
        particleNode->extents = readVector3(nodeObj["extents"].toObject());
    if (nodeObj.contains("innerExtents"))
        particleNode->innerExtents = readVector3(nodeObj["innerExtents"].toObject());
    if (nodeObj.contains("wind"))
        particleNode->wind = readVector3(nodeObj["wind"].toObject());
    if (nodeObj.contains("emitColourStart"))
        particleNode->emitColourStart = readColor(nodeObj["emitColourStart"].toObject());
    if (nodeObj.contains("emitColourEnd"))
        particleNode->emitColourEnd = readColor(nodeObj["emitColourEnd"].toObject());

    particleNode->colourKeys.clear();
    for (const QJsonValue &v : nodeObj["colourKeys"].toArray()) {
        const QJsonObject o = v.toObject();
        iris::ParticleColourKey k;
        k.time = (float) o["time"].toDouble(0.0);
        k.r = (float) o["r"].toDouble(1.0); k.g = (float) o["g"].toDouble(1.0);
        k.b = (float) o["b"].toDouble(1.0); k.a = (float) o["a"].toDouble(1.0);
        particleNode->colourKeys.append(k);
    }
    particleNode->scaleKeys.clear();
    for (const QJsonValue &v : nodeObj["scaleKeys"].toArray()) {
        const QJsonObject o = v.toObject();
        iris::ParticleScaleKey k;
        k.time  = (float) o["time"].toDouble(0.0);
        k.scale = (float) o["scale"].toDouble(1.0);
        particleNode->scaleKeys.append(k);
    }

    if (handle) {
        const QString texturePath = resolveAssetPath(nodeObj["texture"].toString());
        if (!texturePath.isEmpty())
            particleNode->setTexture(iris::Texture2D::load(texturePath));
    }
	particleNode->setVisible(nodeObj["visible"].toBool(true));

    return particleNode;
}


iris::LightType SceneReader::getLightTypeFromName(QString lightType)
{
    if (lightType == "point")       return iris::LightType::Point;
    if (lightType == "directional") return iris::LightType::Directional;
    if (lightType == "spot")        return iris::LightType::Spot;
    if (lightType == "area")        return iris::LightType::Area;

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
iris::MaterialPtr SceneReader::readPbrMaterial(const QJsonObject& matObj)
{
	auto mat    = iris::PbrMaterial::create();
	auto values = matObj["values"].toObject();

	// Drive the material through setValue so the field the shader reads and the
	// Property object the panel shows are both updated from one place.
	for (auto prop : mat->properties) {
		if (!values.contains(prop->name)) continue;
		const auto val = values.value(prop->name);

		switch (prop->type) {
		case iris::PropertyType::Float:
			mat->setValue(prop->name, static_cast<float>(val.toDouble()));
			break;
		case iris::PropertyType::Int:
			mat->setValue(prop->name, val.toInt());
			break;
		case iris::PropertyType::Color:
			mat->setValue(prop->name, QColor(val.toString()));
			break;
		case iris::PropertyType::Bool:
			mat->setValue(prop->name, val.toBool());
			break;
		case iris::PropertyType::Texture: {
			// SceneWriter stores a texture as the asset GUID when saving against
			// the project database (relative == true), or as a scene-relative
			// path otherwise. Resolve the GUID the way MaterialReader::parseMaterial
			// does (asset name joined onto the project folder / asset directory),
			// and fall back to treating the value as a path relative to the scene
			// file. An empty result clears the map.
			const QString stored = val.toString();
			QString path;
			if (!stored.isEmpty()) {
				path = resolveAssetPath(stored);
				if (path.isEmpty()) path = getAbsolutePath(stored);
			}
			mat->setValue(prop->name, path);
			break;
		}
		default:
			break;
		}
	}

	return mat;
}

iris::MaterialPtr SceneReader::readMaterial(QJsonObject& nodeObj)
{
	MaterialReader reader;
	reader.setProject(project);
	if (useAlternativeLocation) reader.setSource(TextureSource::GlobalAssets, assetDirectory);
    if (nodeObj["material"].isNull()) return iris::CustomMaterial::create();

	auto mat = nodeObj["material"].toObject();

	// materialType selects which Material subclass to rebuild. Scenes written
	// before PBR existed have no such key, so absent means "custom" - but
	// parseMaterialTyped additionally routes graph-backed materials (their
	// shaderGuid resolves to a shadergraph definition) to the shader's baked
	// PbrMaterial (MATERIALS_EVALUATOR phase 5).
	const auto materialType = mat["materialType"].toString("custom");
	if (materialType == "pbr") return readPbrMaterial(mat);

	return reader.parseMaterialTyped(mat, handle, true);
   
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
            auto shader = handle ? handle->fetchAssetData(shaderGuid) : QByteArray();
            QJsonObject shaderDefinition = QJsonDocument::fromBinaryData(shader).object();

            if (!shaderDefinition.isEmpty()) {
                auto vAsset = handle->fetchAsset(shaderDefinition["vertex_shader"].toString());
                auto fAsset = handle->fetchAsset(shaderDefinition["fragment_shader"].toString());

                const QString vPath = resolveAssetPath(shaderDefinition["vertex_shader"].toString());
                const QString fPath = resolveAssetPath(shaderDefinition["fragment_shader"].toString());
                if (!vPath.isEmpty()) shaderDefinition["vertex_shader"] = vPath;
                else if (!vAsset.name.isEmpty()) shaderDefinition["vertex_shader"] = QDir(assetDirectory).filePath(vAsset.name);
                if (!fPath.isEmpty()) shaderDefinition["fragment_shader"] = fPath;
                else if (!fAsset.name.isEmpty()) shaderDefinition["fragment_shader"] = QDir(assetDirectory).filePath(fAsset.name);
                
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
                QString textureStr = (handle && !mat[prop->name].toString().isEmpty())
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

        // THE BAKE (MESH_BAKE_SPEC phase 1) — the parse, already paid at
        // import. On the threaded open the worker has read it; on the
        // synchronous one we resolve it here (a catalog query, UI thread).
        // Either way the model is SHARED with the session registration, so a
        // world's geometry is deserialized exactly once per open.
        // The counter spans the RESOLVE AND THE READ, not just the copy out of
        // an already-loaded model: a ledger that timed the memcpy and not the
        // file would report 0 ms and prove nothing. A miss is counted too —
        // it is one catalog query, and it is honest to see it.
        LoadTimeline::Accumulate bakeAttempt(QStringLiteral("bake:sceneReader"));
        iris::BakedModelPtr baked = prewarm ? prewarm->baked(filePath) : iris::BakedModelPtr();
        if (!baked) baked = MeshBakeStore::load(filePath);
        if (baked) {
            meshList = baked->meshes;
            animationss = baked->animations;
            for (auto &anim : animationss) anim->source = filePath;
            meshes.insert(filePath, meshList);
            assimpScenes.insert(filePath);
            animations.insert(filePath, animationss);
            return;
        }
        bakeAttempt.stop();   // a miss must not bank the parse below

        // The threaded open parses these on a worker BEFORE the reader runs
        // (irisgl/import/meshprewarm.h): consume that and this whole stage is
        // a copy out of an aiScene instead of an assimp parse.
        if (prewarm) {
            if (const aiScene *ready = prewarm->scene(filePath)) {
                LoadTimeline::Accumulate hit(QStringLiteral("prewarm:sceneReaderHit"));
                meshList = iris::GraphicsHelper::loadAllMeshesFromAssimpScene(ready);
                animationss = iris::Mesh::extractAnimations(ready, filePath);
                meshes.insert(filePath, meshList);
                assimpScenes.insert(filePath);
                animations.insert(filePath, animationss);
                return;
            }
        }

        // ONE assimp parse per distinct file per open — and it IS a parse:
        // the pipeline removed the up-front preloader, and nothing caches a
        // baked form, so every open re-parses every model from the store
        // (the recorded import-time-bake debt; measured by this counter).
        LoadTimeline::Accumulate parse(QStringLiteral("assimp:sceneReader"));
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
    // Pin world: project folders no longer hold asset files, so a persisted
    // scene-relative source usually resolves to nothing. Re-home it through
    // the catalog: the mesh asset row with the source's file name, resolved
    // pin-first (same bytes the mesh itself loads from).
    if ((filePath.isEmpty() || !QFileInfo::exists(filePath)) && handle && project) {
        const QString byName = handle->fetchAssetGUIDByName(
            QFileInfo(relPath).fileName(), project->getProjectGuid());
        if (!byName.isEmpty()) {
            const QString resolved = resolveAssetPath(byName);
            if (!resolved.isEmpty()) filePath = resolved;
        }
    }
    extractAssetsFromAssimpScene(filePath);

    auto animMap = animations[filePath];

    //reset relative paths for animations since they have the absolute path
    for(auto anim : animMap)
        anim->source = relPath;

    if (animMap.contains(animName)) return animMap[animName];

    // Name miss with exactly one clip in the source: take it. Heals scenes
    // saved before clip names were fixed (extraction used to collapse a clip
    // named after its first channel to "", a name that no longer exists in
    // the re-extracted map). With several clips there is no safe guess —
    // warn instead of silently dropping the animation.
    if (animMap.size() == 1) return animMap.first();
    if (!animMap.isEmpty())
        qWarning() << "getSkeletalAnimation: no clip named" << animName
                   << "in" << relPath << "- clips:" << animMap.keys();

    return iris::SkeletalAnimationPtr();
}
