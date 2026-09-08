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

#include "irisgl/document/physics/avatarmovement.h"
#include "irisgl/document/animation/locomotion.h"
#include "io/materialreader.h"
#include "modules/materials/core/materialhelper.h"
#include "modules/materials/core/pieceemitter.h"
#include "io/scenereader.h"
#include "io/sceneformat.h"
#include "services/scenefolders.h"
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

#include "irisgl/document/materials/postprocess.h"
#include "irisgl/document/materials/postprocessmanager.h"

#include "irisgl/document/materials/postfx/fxaapostprocess.h"

#include "irisgl/document/physics/physicsproperties.h"
#include "irisgl/document/physics/physicshelper.h"
#include "irisgl/document/materials/pbrmaterial.h"

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

    // FORMAT VERSION (src/io/sceneformat.h). v2 is what this build writes; a v1
    // blob is read as a ONE-WAY CONVERSION — every difference between the two is
    // an absent key with a documented default, so nothing is lost, and the next
    // save writes v2. Announced rather than silent: "the file I opened is not
    // the file I write" is exactly the kind of thing a bug report needs to say.
    const int version = sceneformat::versionOf(projectObj);
    if (version < sceneformat::kVersion) {
        qInfo("SceneReader: converting a v%d scene to v%d on load (SPECS/SCENEGRAPH_SPEC.md "
              "§3 step 4 — no migration exists and none is needed; the next save writes v%d).",
              version, sceneformat::kVersion, sceneformat::kVersion);
    } else if (version > sceneformat::kVersion) {
        qWarning("SceneReader: this scene was written by a NEWER build (format v%d, this build "
                 "reads v%d). Unknown keys are ignored; saving will drop them.",
                 version, sceneformat::kVersion);
    }

    auto scene = readScene(projectObj);

    if (editorData) *editorData = readEditorData(projectObj);
    // OUTLINER FOLDERS (SCENEGRAPH_SPEC §6b): read from the EDITOR section,
    // never from the node blocks. Read UNCONDITIONALLY — a project opened
    // without an EditorData out-parameter (headless, thumbnails) still has to
    // come back with its folders, because the next save would otherwise write
    // an empty list over them.
    scenefolders::readEditorBlock(projectObj["editor"].toObject(), scene);
	if (!!postMan)
		readPostProcessData(projectObj, postMan);

    for (auto node : scene->rootNode->children()) {
        node->applyDefaultPose();
        // SCENE_STATIC, re-derived on load (SCENEGRAPH_SPEC §6): the hint is
        // not in the file format yet, so the reader applies the same policy the
        // Add/import paths do once the whole scene is built and at rest. A
        // loaded world's ground and props therefore cost the engine nothing per
        // frame until something moves them.
        node->applyStaticDefaults();
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
    // Default OFF since 2026-09-06 (scenes ship a tiled floor); a file that
    // recorded a choice keeps it — only the missing-key default changed.
    editorData->showGrid = editorObj["showGrid"].toBool(false);

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

    // THE PROJECT-FOLDER LAST RESORT, and the writer's matching branch is why
    // it has to exist. SceneWriter::assetGuidForTexturePath resolves a texture
    // path to a guid two ways: through the CAS, and — when the file is not a
    // store object — by looking the catalog up by FILE NAME within the project
    // (Database::fetchAssetGUIDByName). That second branch is still live and
    // still fires: MainWindow::createDefaultScene copies Tile.png straight into
    // the project folder and registers a bare catalog row, so the default
    // ground's texture is exactly such an asset — a guid the store knows
    // nothing about. Writer and reader have to agree, or a save/reopen ERASES
    // the texture and the floor comes back bare white (the "reopen lighting
    // blowout", which was never a lighting bug).
    //
    // MaterialReader::resolveTextureGuid has carried this branch since that
    // defect was fixed; THIS reader did not, and did not need to while the
    // default ground was a legacy material that went through the other reader.
    // HLMS_ADOPTION P4b made it a PbrMaterial, which lands here — so the same
    // asymmetry re-appeared, in the same place, with the same 65,65,65 ->
    // 255,255,255 signature. Last-resort and existence-checked, so nothing in
    // the pin world changes shape because of it.
    if (path.isEmpty() && !useAlternativeLocation && handle && project &&
        !project->getProjectFolder().isEmpty()) {
        const QString name = handle->fetchAsset(guid).name;
        if (!name.isEmpty()) {
            const QString candidate = QDir(project->getProjectFolder()).filePath(name);
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
	// Sun coupling (re-audit F5); absent in every document written before it,
	// which reads as "nothing is driven" — the default.
	scene->sunLightGuid = sceneObj["sunLight"].toString();
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
        // THE GI UPDATE BUDGET (FIX WAVE B1), with the legacy mapping: a
        // document written before the fix wave carries the giAutoRefresh bool
        // and nothing else, and false meant exactly what budget 0 means.
        scene->giUpdateBudget =
            sceneObj.contains("giUpdateBudget")
                ? qBound(0, sceneObj["giUpdateBudget"].toInt(1), 512)
                : (sceneObj["giAutoRefresh"].toBool(true) ? 1 : 0);
        if (sceneObj.contains("giPccGrid"))   // pre-hybrid documents keep the 3x2x3 default
            scene->giPccGrid = readVector3(sceneObj["giPccGrid"].toObject());
        // Probe-capture knobs (REFLECTIONS_ADOPTION_SPEC P3). Absent in every
        // document written before this phase; the toInt/toDouble defaults ARE
        // the constructor's, so an old scene reads exactly as it did.
        scene->giProbeHdr = qBound(-1, sceneObj["giProbeHdr"].toInt(-1), 1);
        scene->giProbeShadows = qBound(-1, sceneObj["giProbeShadows"].toInt(-1), 1);
        scene->giProbeOverlap =
            float(qBound(0.01, sceneObj["giProbeOverlap"].toDouble(1.25), 8.0));
        scene->giProbeSnapDeviation =
            float(qMax(0.0, sceneObj["giProbeSnapDeviation"].toDouble(0.05)));
        scene->giProbeSnapSidesMin =
            float(qMax(0.0, sceneObj["giProbeSnapSidesMin"].toDouble(0.25)));
        scene->giProbeSnapSidesMax =
            float(qMax(0.0, sceneObj["giProbeSnapSidesMax"].toDouble(0.25)));
        // Dynamic probes (P5a). Absent = 0 = the all-static grid every document
        // written before this phase was rendered with.
        scene->giRayMarchStepScale =
            float(qBound(1.0, sceneObj["giRayMarchStepScale"].toDouble(1.0), 8.0));
        // DDGI (GI_UNIFIED_SPEC.md §4 P1). Absent in every document written
        // before this phase, and the fallbacks ARE the constructor's values —
        // -1 (auto, which resolves OFF while there is no Rayon tier) is what
        // makes those documents render exactly as they always did.
        scene->giDdgi = qBound(-1, sceneObj["giDdgi"].toInt(-1), 1);
        scene->giDdgiIntensity =
            float(qBound(0.0, sceneObj["giDdgiIntensity"].toDouble(1.0), 64.0));
        // The ambient sky-visibility strength (the Rayon ambient fix). Absent
        // in every document written before it: the fallback 1.0 turns the fix
        // ON for them, deliberately — it corrects a term those documents were
        // MISSING, and the sealed-room invariance gate is what says that is
        // safe for the scenes it cannot change.
        scene->giDdgiAmbient =
            float(qBound(0.0, sceneObj["giDdgiAmbient"].toDouble(1.0), 8.0));
        // RAYON's quality tier (GI_UNIFIED_SPEC §2 / P2). Absent in every
        // document written before the unification — those are DERIVED from the
        // fields above, below, once the World Mode is known.
        if (sceneObj.contains("giTier")) {
            bool ok = false;
            const auto t = worldmodes::rayonTierFromName(sceneObj["giTier"].toString(), &ok);
            scene->giTier = ok ? int(t) : 3;
        }
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
    // The adaptation window. Absent = the engine's historical hard-coded pair,
    // so an older document grades exactly as it did.
    scene->exposureMin = float(qBound(-8.0, sceneObj["exposureMin"].toDouble(-2.5), 8.0));
    scene->exposureMax = float(qBound(double(scene->exposureMin),
                                      sceneObj["exposureMax"].toDouble(2.5), 8.0));
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

            // RAYON MIGRATION (GI_UNIFIED_SPEC §2 / P2). A document written
            // before the GI dial existed carries no `giTier`: derive which tier
            // its settings correspond to and pin whatever deviates, so the
            // scene renders IDENTICALLY and the panel still has an honest tier
            // to show. It runs HERE, after worldMode is known, because whether
            // the tier row needs a pin depends on what the scene's World Mode
            // would otherwise resolve it to.
            //
            // The branch above (a pre-World-Modes document) deliberately does
            // NOT derive: it applies the Epic tier wholesale, which is what it
            // has always done, and preserving its old GI settings as pins would
            // pin every row of every old document for ever.
            if (!sceneObj.contains("giTier")) worldmodes::deriveRayonFromDocument(scene);
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
        if (!childNode) continue;      // a retired node type — skipped, not attached
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

    // The active camera (CAMERAS_SPEC D6) is read AFTER the tree, because
    // Scene::setActiveCamera validates the guid against the cameras the walk
    // just registered. A guid that no longer resolves — a camera deleted by
    // hand out of the file, or a scene from a build where the guid meant
    // something else — falls back to the free viewer instead of arming play
    // with a camera that does not exist.
    scene->setActiveCamera(sceneObj["activeCamera"].toString());

    // The play mode (AVATAR_LOCOMOTION_SPEC §8.5). Tolerant by design: a key
    // that is absent (every scene older than Stage 3) or that names a mode this
    // build does not know leaves the default `explorer` — the behaviour those
    // scenes already had — rather than refusing to open the file.
    {
        iris::ScenePlayMode mode = iris::ScenePlayMode::Explorer;
        if (iris::playModeFromName(sceneObj["playMode"].toString(), mode))
            scene->setPlayMode(mode);
    }

    return scene;
}

iris::SceneNodePtr SceneReader::readFragment(const SceneFragment &fragment)
{
    if (fragment.isNull()) return iris::SceneNodePtr();

    QJsonObject nodeObj = fragment.node;      // readSceneNode takes a mutable ref
    auto node = readSceneNode(nodeObj);
    if (!node) return node;

    // The SESSION identities, replayed positionally over the same pre-order
    // walk that captured them (src/io/sceneformat.h explains why pre-order and
    // not a guid map). An empty list means "these are new nodes" — a paste —
    // and the ids the constructors minted are kept.
    if (!fragment.nodeIds.isEmpty()) {
        int i = 0;
        std::function<void(const iris::SceneNodePtr &)> replay =
            [&](const iris::SceneNodePtr &n) {
                if (i < fragment.nodeIds.size()) n->nodeId = fragment.nodeIds[i];
                ++i;
                const int kids = n->childCount();
                for (int c = 0; c < kids; ++c)
                    if (iris::SceneNode *child = n->childAt(c)) replay(child->sharedFromThis());
            };
        replay(node);
    }

    // The subtree is complete and at rest — the same two passes the reader runs
    // over a freshly loaded scene, for the same reason (the rest pose clip
    // translation reads, and the SCENE_STATIC classification). applyStaticDefaults
    // is deliberately NOT run here: the fragment is not attached to anything yet,
    // so rule 2 would refuse every node in it. Whoever attaches it runs the pass
    // (AddSceneNodeCommand::redo does).
    node->applyDefaultPose();
    return node;
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

    // A type this build retired (sceneformat::isRetiredNodeType documents the
    // contract). SKIP it — and its subtree with it — rather than substituting a
    // type it never was. Both child loops below tolerate the null this returns.
    if (sceneformat::isRetiredNodeType(nodeType)) {
        irisLog(QString("scene reader: skipping '%1', a '%2' node — that node type "
                        "no longer exists in this build")
                    .arg(nodeObj["name"].toString(QStringLiteral("<unnamed>")), nodeType));
        return sceneNode;
    }

    if (nodeType == "mesh") {
        sceneNode = createMesh(nodeObj).staticCast<iris::SceneNode>();
    } else if (nodeType == "light") {
        sceneNode = createLight(nodeObj).staticCast<iris::SceneNode>();
    } else if (nodeType == "particle system") {
        sceneNode = createParticleSystem(nodeObj).staticCast<iris::SceneNode>();
    } else if (nodeType == "decal") {
        sceneNode = createDecal(nodeObj).staticCast<iris::SceneNode>();
    } else if (nodeType == "camera") {
        sceneNode = createCamera(nodeObj).staticCast<iris::SceneNode>();
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
    // Absent = false, same as planarReflector: the writer only emits it when set.
    sceneNode->setGiBoundsExcluded(nodeObj["giBoundsExcluded"].toBool(false));
    // Shadow Caster: absent = TRUE (the document default) — the writer only
    // emits the key when the user turned casting off, so every scene written
    // before the key existed loads exactly as it did.
    sceneNode->setShadowCastingEnabled(nodeObj["castShadow"].toBool(true));
    // LIGHTING CHANNELS. ABSENT = ALL CHANNELS, which is what every scene
    // written before the key existed means and what "the feature is off"
    // means — so no old document changes appearance. Read through a double
    // (QJsonValue's only numeric type; it holds every uint32 exactly) and
    // masked back to 32 bits, so a hand-edited -1 also reads as "everything".
    sceneNode->setLightMask(static_cast<quint32>(
        static_cast<qlonglong>(nodeObj["lightMask"].toDouble(4294967295.0)) & 0xFFFFFFFFll));
    // SCENE_STATIC, the persisted USER OVERRIDE (format v2). Absent — every
    // node of every scene written before v2, and the overwhelming majority
    // after it — means "no opinion": the default policy decides, in the
    // applyStaticDefaults pass that runs once the whole tree is built.
    //
    // The RAW setter, not setStaticHint: this node's parent chain does not
    // exist yet (the reader builds children before it attaches the subtree), so
    // asking the graph to make it static now would be refused by rule 2 and log
    // a warning per node. Recording the intent here and letting the pass at the
    // end of the load act on it is the same thing, in the right order.
    if (nodeObj.contains(QLatin1String("static")))
        sceneNode->_setStaticOverride(nodeObj["static"].toBool()
                                          ? iris::StaticOverride::Static
                                          : iris::StaticOverride::Dynamic);
    // Socket attachment (CAMERAS_SPEC §5). The RAW setter: the owner is very
    // often read AFTER this node (a camera can precede the character it rides),
    // so nothing here can validate the guid. Scene::addNode registers whatever
    // this sets, and SocketResolver skips it silently if it never resolves.
    {
        const QJsonObject attachment = nodeObj["socketAttachment"].toObject();
        const QString owner = attachment["owner"].toString();
        const QString socket = attachment["socket"].toString();
        if (!owner.isEmpty() && !socket.isEmpty())
            sceneNode->setSocketAttachment(owner, socket);
    }

    // COLLISION CONTENT (AVATAR_LOCOMOTION_SPEC §6.3 option C). ABSENT means
    // "the type default", which the constructor already set — on for a mesh,
    // off for everything else — so every scene written before this flag existed
    // gets today's behaviour and only an explicit user override is read back.
    sceneNode->collisionEnabled = nodeObj["collision"].toBool(sceneNode->collisionEnabled);

    // THE AVATAR COMPONENT (AVATAR_LOCOMOTION_SPEC §6). Absent on all but an
    // avatar wrapper. Every knob falls back to the component's own default, so
    // a block written by an older build that lacked a key reads as that key's
    // default rather than as zero — the difference between "this avatar has no
    // jump" and "this file predates jumpReArm".
    if (nodeObj.contains(QLatin1String("avatar"))) {
        const QJsonObject a = nodeObj["avatar"].toObject();
        auto movement = iris::AvatarMovementPtr(new iris::AvatarMovement());
        iris::AvatarMovementParams p;
        p.walkSpeed = float(a["walkSpeed"].toDouble(p.walkSpeed));
        p.runSpeed = float(a["runSpeed"].toDouble(p.runSpeed));
        p.maxAcceleration = float(a["maxAcceleration"].toDouble(p.maxAcceleration));
        p.brakingDeceleration = float(a["brakingDeceleration"].toDouble(p.brakingDeceleration));
        p.groundFriction = float(a["groundFriction"].toDouble(p.groundFriction));
        p.jumpVelocity = float(a["jumpVelocity"].toDouble(p.jumpVelocity));
        p.jumpCount = a["jumpCount"].toInt(p.jumpCount);
        p.coyoteTime = float(a["coyoteTime"].toDouble(p.coyoteTime));
        p.jumpReArm = float(a["jumpReArm"].toDouble(p.jumpReArm));
        p.airControl = float(a["airControl"].toDouble(p.airControl));
        p.gravityScale = float(a["gravityScale"].toDouble(p.gravityScale));
        p.maxStepHeight = float(a["maxStepHeight"].toDouble(p.maxStepHeight));
        p.walkableFloorAngle = float(a["walkableFloorAngle"].toDouble(p.walkableFloorAngle));
        p.orientRotationToMovement = a["orientRotationToMovement"].toBool(p.orientRotationToMovement);
        p.rotationRate = float(a["rotationRate"].toDouble(p.rotationRate));
        p.capsuleAuto = a["capsuleAuto"].toBool(p.capsuleAuto);
        p.capsuleRadius = float(a["capsuleRadius"].toDouble(p.capsuleRadius));
        p.capsuleHeight = float(a["capsuleHeight"].toDouble(p.capsuleHeight));
        movement->setParams(p);
        sceneNode->setAvatarComponent(movement);
    }

    // THE LOCOMOTION STATE MACHINE (AVATAR_LOCOMOTION_SPEC §7). The roles a
    // file recorded are restored as MANUAL bindings: they were resolved once
    // and saved, so the auto-matcher must not clobber them the next time a clip
    // is loaded. A refused asset (a hand-edited file with a condition outside
    // the closed vocabulary) is REPORTED and the character falls back to the
    // generated default rather than loading with no state machine at all.
    if (nodeObj.contains(QLatin1String("locomotion"))) {
        const QJsonObject l = nodeObj["locomotion"].toObject();
        // The blend space's sample POSITIONS follow the movement knobs, so the
        // degradation path needs the knobs this very node just read (the avatar
        // block above runs first, always — it is written first too).
        const iris::AvatarMovementParams mp = sceneNode->avatar()
                                                  ? sceneNode->avatar()->params()
                                                  : iris::AvatarMovementParams();
        auto loco = iris::AvatarLocomotionPtr(new iris::AvatarLocomotion());
        iris::ClipRoles roles;
        const QJsonObject rolesObj = l["roles"].toObject();
        for (int i = 0; i < iris::kClipRoleCount; ++i) {
            const auto role = iris::ClipRole(i);
            const QString key = QLatin1String(iris::clipRoleName(role));
            if (rolesObj.contains(key)) roles.set(role, rolesObj[key].toString());
        }
        loco->markRolesFromFile(roles, l["default"].toBool(true));
        if (l.contains(QLatin1String("asset"))) {
            iris::LocomotionAsset asset;
            QString err;
            if (iris::locomotionAssetFromJson(l["asset"].toObject(), asset, &err)) {
                loco->setAssetPreservingDefaultFlag(asset, &err);
            } else {
                qWarning("SceneReader: node '%s' has an invalid locomotion asset (%s) — "
                         "falling back to the generated default",
                         qUtf8Printable(sceneNode->getName()), qUtf8Printable(err));
                loco->setDefaultAsset(roles, mp.walkSpeed, mp.runSpeed);
            }
        } else {
            loco->setDefaultAsset(roles, mp.walkSpeed, mp.runSpeed);
        }
        sceneNode->setLocomotionComponent(loco);
    }

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
        if (!childNode) continue;      // a retired node type — skipped, not attached
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

            auto skel = this->getSkeletalAnimation(skelAnim["source"].toString(),
                                                  skelAnim["name"].toString(),
                                                  skelAnim["guid"].toString());
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

    // ROTATION, in either spelling, told apart by the `scalar` key.
    //
    // Format v2 writes `rot` as the QUATERNION and writes nothing else (see the
    // note beside the writer: the euler detour is lossy and moved every rotated
    // node a little on every single open). `rotQuat` was the transitional key
    // v1 wrote alongside the euler `rot`, and is still read first because a
    // project saved by that build is what a developer's library is full of.
    //
    // A `rot` with no `scalar` is an EULER TRIPLE, which is what every library
    // Object asset blob in existence carries (they are node objects too, and
    // they were written years before this) — those keep loading unchanged.
    const auto readQuat = [](const QJsonObject &o) {
        return iris::Quat(float(o["scalar"].toDouble(1.0)),
                          float(o["x"].toDouble(0.0)),
                          float(o["y"].toDouble(0.0)),
                          float(o["z"].toDouble(0.0))).normalized();
    };
    const QJsonObject rotQuat = nodeObj["rotQuat"].toObject();
    const QJsonObject rot = nodeObj["rot"].toObject();
    if (!rotQuat.isEmpty()) {
        sceneNode->setLocalRot(readQuat(rotQuat));
    } else if (rot.contains(QLatin1String("scalar"))) {
        sceneNode->setLocalRot(readQuat(rot));
    } else if (!rot.isEmpty()) {
        sceneNode->setLocalRot(iris::Quat::fromEulerAngles(readVector3(rot)).normalized());
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

    // Sockets (CAMERAS_SPEC §5). Read with setSockets rather than addSocket:
    // addSocket VALIDATES against the rig, and a file must not silently drop a
    // socket because the mesh failed to load or because a re-import renamed a
    // bone. A socket naming a bone that is not there resolves to nothing (the
    // fail-soft rule), stays in the file, and starts working again the moment
    // the bone comes back.
    const QJsonArray socketArray = nodeObj["sockets"].toArray();
    if (!socketArray.isEmpty()) {
        QList<iris::Socket> sockets;
        for (const auto &raw : socketArray) {
            const QJsonObject socketObj = raw.toObject();
            iris::Socket socket;
            socket.name = socketObj["name"].toString();
            socket.boneName = socketObj["bone"].toString();
            if (socket.name.isEmpty()) continue;
            socket.position = readVector3(socketObj["position"].toObject());
            const QJsonObject rot = socketObj["rotation"].toObject();
            socket.rotation = iris::Quat(float(rot["scalar"].toDouble(1.0)),
                                         float(rot["x"].toDouble(0.0)),
                                         float(rot["y"].toDouble(0.0)),
                                         float(rot["z"].toDouble(0.0))).normalized();
            if (socketObj.contains("scale"))
                socket.scale = readVector3(socketObj["scale"].toObject());
            socket.builtIn = socketObj["builtIn"].toBool(false);
            sockets.append(socket);
        }
        meshNode->setSockets(sockets);
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
    // The default is the CONSTRUCTOR's (0.15 since LIGHTING_FIX fix 5), not the
    // old 1.0: a document written before softness was persisted never meant
    // "all penumbra", it simply had nothing to say.
    lightNode->spotCutOffSoftness = (float)nodeObj["spotCutOffSoftness"].toDouble(0.15f);
    lightNode->spotFalloff = (float)nodeObj["spotFalloff"].toDouble(1.0f);
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

iris::CameraNodePtr SceneReader::createCamera(QJsonObject& nodeObj)
{
    auto cameraNode = iris::CameraNode::create();

    // The projection block. Defaults are the CameraNode constructor's own, so a
    // file missing a key loads the same camera the Add menu makes.
    cameraNode->angle       = (float) nodeObj["angle"].toDouble(45.0);
    cameraNode->nearClip    = (float) nodeObj["nearClip"].toDouble(0.1);
    cameraNode->farClip     = (float) nodeObj["farClip"].toDouble(500.0);
    cameraNode->aspectRatio = (float) nodeObj["aspectRatio"].toDouble(1.0);
    cameraNode->setOrthagonalZoom((float) nodeObj["orthogonalSize"].toDouble(10.0));
    cameraNode->setProjection(nodeObj["projectionMode"].toString("perspective") == "orthogonal"
                                  ? iris::CameraProjection::Orthogonal
                                  : iris::CameraProjection::Perspective);

    // CAMERAS_SPEC §2. The sensor is set through the raw fields, not
    // setSensorSize: the angle above is the authored truth and must not be
    // re-derived from a focal length the file does not carry.
    const float sw = (float) nodeObj["sensorWidth"].toDouble(36.0);
    const float sh = (float) nodeObj["sensorHeight"].toDouble(24.0);
    if (sw > 0.0f) cameraNode->sensorWidth = sw;
    if (sh > 0.0f) cameraNode->sensorHeight = sh;
    cameraNode->authorMode = nodeObj["authorMode"].toString("degrees") == QLatin1String("mm")
                                 ? iris::CameraAuthorMode::Millimeters
                                 : iris::CameraAuthorMode::Degrees;
    // CAMERA_LENS_SPEC §3. Every key below defaults to the value the
    // constructor already set, so a file written before this phase existed
    // loads a camera that projects EXACTLY what it used to (vertical fit, no
    // squeeze, no shift). Set through the raw fields, not the setters, for the
    // same reason the sensor pair is: `angle` above is the authored truth.
    const QString sensorFit = nodeObj["sensorFit"].toString("vertical");
    cameraNode->sensorFit = sensorFit == QLatin1String("horizontal") ? iris::CameraSensorFit::Horizontal
                          : sensorFit == QLatin1String("auto")       ? iris::CameraSensorFit::Auto
                                                                     : iris::CameraSensorFit::Vertical;
    const float squeeze = (float) nodeObj["anamorphicSqueeze"].toDouble(1.0);
    if (squeeze > 0.0f) cameraNode->anamorphicSqueeze = squeeze;
    cameraNode->lensShiftX = qBound(-1.0f, (float) nodeObj["lensShiftX"].toDouble(0.0), 1.0f);
    cameraNode->lensShiftY = qBound(-1.0f, (float) nodeObj["lensShiftY"].toDouble(0.0), 1.0f);
    cameraNode->constrainAspect = nodeObj["constrainAspect"].toBool(false);
    cameraNode->dofEnabled      = nodeObj["dofEnabled"].toBool(false);
    const QString focusMode = nodeObj["focusMode"].toString("manual");
    cameraNode->focusMode = focusMode == QLatin1String("track") ? iris::CameraFocusMode::Track
                          : focusMode == QLatin1String("off")   ? iris::CameraFocusMode::Off
                                                                : iris::CameraFocusMode::Manual;
    cameraNode->focusDistance = std::max(0.0f, (float) nodeObj["focusDistance"].toDouble(10.0));
    cameraNode->focusTarget   = nodeObj["focusTarget"].toString();
    cameraNode->fStop         = std::max(0.0f, (float) nodeObj["fStop"].toDouble(2.8));
    // CAMERA_LENS_SPEC §3 P2, the focus block — absent keys keep the
    // constructor's defaults, which is what every pre-P2 file means.
    cameraNode->focusOffset         = (float) nodeObj["focusOffset"].toDouble(0.0);
    cameraNode->smoothFocus         = nodeObj["smoothFocus"].toBool(false);
    cameraNode->focusSmoothingSpeed = std::max(0.0f, (float) nodeObj["focusSmoothingSpeed"].toDouble(8.0));
    cameraNode->minFocusDistance    = std::max(0.0f, (float) nodeObj["minFocusDistance"].toDouble(0.1));
    cameraNode->bladeCount          = qBound(3, nodeObj["bladeCount"].toInt(5), 16);
    cameraNode->focusPlaneVisible   = nodeObj["focusPlaneVisible"].toBool(false);
    cameraNode->outputHeight  = qBound(1, nodeObj["outputHeight"].toInt(1080), 16384);
    cameraNode->bodyVisible   = nodeObj["bodyVisible"].toBool(true);
    // CAMERA_LENS_SPEC §4. Absent = "inherit", which is what every file written
    // before this phase existed means, and it is bit-for-bit the old behaviour.
    const QString exposureMode = nodeObj["exposureMode"].toString("inherit");
    cameraNode->exposureMode = exposureMode == QLatin1String("auto")   ? iris::CameraExposureMode::Auto
                             : exposureMode == QLatin1String("manual") ? iris::CameraExposureMode::Manual
                                                                       : iris::CameraExposureMode::Inherit;
    cameraNode->exposure    = (float) nodeObj["exposure"].toDouble(0.0);
    cameraNode->exposureMin = (float) nodeObj["exposureMin"].toDouble(-3.5);
    cameraNode->exposureMax = (float) nodeObj["exposureMax"].toDouble(3.5);
    if (cameraNode->exposureMax < cameraNode->exposureMin)
        std::swap(cameraNode->exposureMin, cameraNode->exposureMax);
    // CAMERA_LENS_SPEC §5. SANITISED, not trusted: a file may name a key this
    // build does not have (an older or newer Jahshaka, or a hand edit), and an
    // unknown override would otherwise sit in the document forever, be written
    // back out, and mean nothing. setPostOverride is the same door the verbs
    // and the panel use, so the file cannot express anything they cannot.
    {
        const QJsonObject overrides = nodeObj["postOverrides"].toObject();
        for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it)
            cameraNode->setPostOverride(it.key(), it.value().toVariant());
    }

    return cameraNode;
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
		// An enum row stores (and serializes) a plain int — see SceneWriter.
		case iris::PropertyType::List:
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

	restoreCustomPieces(mat, values);
	return mat;
}

/// GENERATED SHADER PIECES (HLMS_ADOPTION P5), on the way back in.
///
/// The scene stores the piece by FILE NAME (a hash of the file's own bytes) and
/// the guid of the shader asset it was generated from. Two ways home, in order:
///
///   1. the file is already in this user's piece cache — the ordinary case,
///      free, and the reason the name rather than a path is what gets written
///      (the cache lives somewhere different on every machine);
///   2. it is not, because this is another machine or a wiped cache — then the
///      SHADER ASSET is fetched by guid and re-emitted. The graph is the source
///      of truth and emission is deterministic, so the regenerated file has the
///      same name the scene asked for.
///
/// If neither works (no database, the asset is gone) the material simply loads
/// as its baked self: an animated surface renders its t=0 fold instead of
/// moving. That is a visible degradation, never a broken material.
void SceneReader::restoreCustomPieces(iris::PbrMaterialPtr mat, const QJsonObject& values)
{
	const QString pixelName  = values["customPiece"].toString();
	const QString vertexName = values["customPieceVertex"].toString();
	if (pixelName.isEmpty() && vertexName.isEmpty()) return;

	const QString dir = materials::PieceEmitter::cacheDir();
	auto resolve = [&dir](const QString& name) {
		if (name.isEmpty()) return QString();
		const QString path = dir + QLatin1Char('/') + name;
		return QFileInfo::exists(path) ? path : QString();
	};
	QString pixelPath = resolve(pixelName);
	QString vertexPath = resolve(vertexName);

	if ((pixelPath.isEmpty() && !pixelName.isEmpty()) ||
	    (vertexPath.isEmpty() && !vertexName.isEmpty())) {
		const QString graphGuid = values["customPieceGraph"].toString();
		if (!graphGuid.isEmpty() && handle) {
			MaterialReader reader;
			reader.setProject(project);
			const QJsonObject definition = reader.getShaderObjectFromId(graphGuid, handle);
			if (!definition.isEmpty()) {
				// createPbrMaterialFromDefinition re-emits and writes the piece
				// files as a side effect; we want the PATHS, not the material.
				if (auto regenerated = MaterialHelper::createPbrMaterialFromDefinition(definition)) {
					if (pixelPath.isEmpty()) pixelPath = resolve(pixelName);
					if (vertexPath.isEmpty()) vertexPath = resolve(vertexName);
				}
			}
		}
	}
	mat->setCustomPiecePixel(pixelPath);
	mat->setCustomPieceVertex(vertexPath);
}

iris::MaterialPtr SceneReader::readMaterial(QJsonObject& nodeObj)
{
	MaterialReader reader;
	reader.setProject(project);
	if (useAlternativeLocation) reader.setSource(TextureSource::GlobalAssets, assetDirectory);
    // A node with no material at all gets the default PbrMaterial — the one
    // material class there is (HLMS_ADOPTION P4b). It used to get an EMPTY
    // CustomMaterial: no shader, no properties, so every value the panel
    // showed and every value the mirror read was absent.
    if (nodeObj["material"].isNull()) return iris::PbrMaterial::create();

	auto mat = nodeObj["material"].toObject();

	// materialType selects which Material subclass to rebuild. Scenes written
	// before PBR existed have no such key, so absent means "custom" - but
	// parseMaterialTyped additionally routes graph-backed materials (their
	// shaderGuid resolves to a shadergraph definition) to the shader's baked
	// PbrMaterial (MATERIALS_EVALUATOR phase 5).
	const auto materialType = mat["materialType"].toString("custom");
	if (materialType == "pbr") return readPbrMaterial(mat);

	return reader.parseMaterialTyped(mat, handle, true);
   
// (A ~60-line commented-out copy of the reader's builtin-shader lookup lived
// here. It named a class that no longer exists — HLMS_ADOPTION P4b — and the
// live path above has done this job for a long time. Deleted rather than left
// as an archaeological hazard.)
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

iris::SkeletalAnimationPtr SceneReader::getSkeletalAnimation(QString filePath, QString animName,
                                                             const QString &assetGuid)
{
    auto relPath = filePath;
    // The GUID FIRST when the file carries one (F5). It is the only reference
    // that survives the store moving: a CAS object's file name is its sha256,
    // so neither the persisted relative path nor the name-based re-home below
    // can find it again, and the clip came back null — a character at bind
    // pose with nothing in the log.
    QString resolvedByGuid;
    if (!assetGuid.isEmpty()) resolvedByGuid = resolveAssetPath(assetGuid);
    if (!resolvedByGuid.isEmpty() && QFileInfo::exists(resolvedByGuid)) {
        extractAssetsFromAssimpScene(resolvedByGuid);
        auto byGuid = animations[resolvedByGuid];
        for (auto anim : byGuid) anim->source = relPath;
        if (byGuid.contains(animName)) return byGuid[animName];
        if (byGuid.size() == 1) return byGuid.first();
    }
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
