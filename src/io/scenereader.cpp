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
#include "irisgl/document/scenegraph/looks.h"
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
#include "data/guidmanager.h"

#include "data/constants.h"
#include "data/database/database.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/lightbindings.h"
#include "services/loadtimeline.h"
#include "services/meshbakestore.h"
#include "services/primitiveassets.h"

#include <functional>
#include <QSqlDatabase>

#include "viewport/editordata.h"

#include "irisgl/document/assets/mesh.h"
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
#include "services/vrworld.h"
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

// THE READER-DEFAULTS LAW (render audit I-5, lane READER-DEFAULTS-1) — ONE
// DEFINITION OF EVERY DEFAULT, AND IT IS THE CONSTRUCTOR'S.
//
// Every read in this file DEFAULT-CONSTRUCTS AND OVERWRITES PRESENT KEYS: the
// document object is made first, so it already holds the constructor's value,
// and the fallback handed to `toDouble`/`toInt`/`toBool` is that value —
// `nodeObj["x"].toDouble(node->x)`, never a literal. A literal here is a SECOND
// definition of a default, and two definitions of one number drift; when they
// drift, "a scene that was never saved" and "a scene whose file does not carry
// the key" become two different scenes, silently, for ever.
//
// It has cost real pictures four times: SUN1 found the shadow type reading None
// against ShadowMap's Soft; the darkness A/B of 2026-09-13 found `exposure`
// reading 0.0 against the scene's 0.6 and five shipped samples rendering at
// half brightness because of it; and this lane found nineteen more, of which the
// loudest were a light's RADIUS (1 against 10), a light's shadow resolution
// (1024 against 2048), the explorer camera's FAR CLIP (100 against 500) and six
// particle-emitter fields that turned every pre-key emitter into a slow, dark,
// non-dissipating trickle. `document.reader_defaults` is the guard: it writes
// each node type, strips every optional key from the JSON, reads it back and
// requires a node identical to a freshly constructed one.
//
// Where a key is an ENUM spelled as a string, the read is guarded by
// `contains()` instead, for the same reason: the spelling of the default would
// otherwise be a second copy of it.
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

    // ONE SOURCE FOR EVERY DEFAULT (the reader-defaults law, I-5; see the note
    // above readScene): the objects below are BORN carrying the constructor's
    // values and this function overwrites only the keys the FILE carries. A
    // literal here would be a second definition of a default and the two drift
    // — which is exactly how every project written before the clip-plane keys
    // existed came back with a 1 m near and a 100 m FAR clip against
    // CameraNode's own 0.1 / 500, so the explorer camera could not see past
    // 100 m in a scene whose ground is 4 km wide.
    auto editorData = new EditorData();

    // @todo: check if camera object is null
    auto camObj = editorObj.value("camera").toObject();
    auto camera = iris::CameraNode::create();
    camera->angle = (float)camObj.value("angle").toDouble(camera->angle);
    camera->nearClip = (float)camObj.value("nearClip").toDouble(camera->nearClip);
    camera->farClip = (float)camObj.value("farClip").toDouble(camera->farClip);
    camera->setLocalPos(readVector3(camObj.value("pos").toObject()));
    // rotQuat first (the lossless spelling), euler for anything written before
    // it existed — see readSceneNodeTransform.
    const QJsonObject camRotQuat = camObj.value("rotQuat").toObject();
    if (!camRotQuat.isEmpty())
        camera->setLocalRot(iris::Quat(float(camRotQuat.value("scalar").toDouble(1.0)),
                                        float(camRotQuat.value("x").toDouble(0.0)),
                                        float(camRotQuat.value("y").toDouble(0.0)),
                                        float(camRotQuat.value("z").toDouble(0.0))).normalized());
    else
        camera->setLocalRot(iris::Quat::fromEulerAngles(readVector3(camObj.value("rot").toObject())));
	camera->setOrthagonalZoom((float)camObj.value("orthogonalSize").toDouble(camera->orthoSize));
	iris::CameraProjection val = camObj.value("projectionMode").toString().compare("orthogonal") == 0 ? iris::CameraProjection::Orthogonal : iris::CameraProjection::Perspective;
	camera->setProjection(val);

    editorData->editorCamera = camera;
    // (`distanceFromPivot` was READ here into a field nothing consumed and no
    // writer ever wrote — the orbit distance lives in the viewport's own
    // per-view state. Deleted with EditorData::distFromPivot, CRUD law.)
    // Light wires default ON (owner 2026-08-31): scenes saved before the flag
    // existed read back true; an explicitly saved false is honored.
    editorData->showLightWires = editorObj.value("showLightWires").toBool(editorData->showLightWires);
	editorData->showDebugDrawFlags = editorObj.value("showDebugDrawFlags").toBool(editorData->showDebugDrawFlags);
    // The grid defaults OFF since 2026-09-06 (scenes ship a tiled floor); a
    // file that recorded a choice keeps it — and the default itself is
    // EditorData's, which is the one a brand-new scene is born with
    // (ui.grid_default asserts the two agree).
    editorData->showGrid = editorObj.value("showGrid").toBool(editorData->showGrid);

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

/// THE GLB TEXTURE-LOSS REPAIR (2026-09-09), reader half.
///
/// Between 2026-09-03 and this fix, SceneWriter stored the wrong guid in every
/// texture slot of an imported model: the writer recovered the guid from the
/// resolved store path, and the tie between the .glb Object's role='texture'
/// row and the member Texture's role='source' row over the SAME oid was broken
/// by insertion order, which the Object won (AssetCas::guidForStorePath now
/// spells the order out). The reader then resolved that guid the other way —
/// source-role first — and got the .glb itself, so `baseColorMap` pointed at a
/// model file and every imported model, Mixamo avatars included, reopened
/// untextured.
///
/// Scenes saved in that week are already on disk, and no user-data migration
/// framework exists (the app ships new). So this is a TOLERANT READER: a
/// texture slot naming a non-Texture asset is resolved to the object's texture
/// member that the slot must have meant, one log line each, and the count is
/// what makes the project dirty so the next save writes it correctly. Nothing
/// is written to the catalog here.
QString SceneReader::repairTextureSlot(const QString &stored, const QString &slotName)
{
    if (stored.isEmpty()) return stored;
    const QString repaired = AssetCas::textureGuidForSlot(QSqlDatabase::database(),
                                                          stored, slotName);
    if (repaired.isEmpty()) return stored;
    ++repairedSlots;
    irisLog(QString("scene reader: %1 named the object '%2' instead of a texture "
                    "(the 2026-09-03 save defect) - repaired to '%3'")
                .arg(slotName, stored, repaired));
    return repaired;
}

QString SceneReader::resolveAssetPath(const QString &guid)
{
    if (guid.isEmpty()) return QString();
    // A project load resolves through the project's PIN (resolvePinned falls
    // back to the library source itself); a library-source read (a store
    // preview, a library object dropped into a scene) through the library
    // source. THAT IS ALL (plan item 15c).
    //
    // Two by-NAME fallbacks followed until then: `assetDirectory + row name`
    // for a library-source read and `projectFolder + row name` for a project
    // load. The second existed for exactly one asset — the default ground's
    // Tile.png, which MainWindow::createDefaultScene copied into the project
    // folder under a bare catalog row, and which the writer re-found by name
    // on save: the two had to agree or the floor reopened bare white (the
    // "reopen lighting blowout"). The first had lost its last real directory
    // when the retired legacy store view went; every remaining caller passed
    // the project folder, i.e. the same fallback through another door. The
    // tile is a pinned store object now, like every texture, and all of it
    // went — the folder the first one joined against included.
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    if (!useAlternativeLocation && project && !project->getProjectGuid().isEmpty())
        return AssetCas::resolvePinned(conn, root, project->getProjectGuid(), guid);
    return AssetCas::resolveSource(conn, root, guid);
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
    const QJsonArray roots = sceneObj.value("rootNode").toObject()["children"].toArray();
    for (const auto &child : roots) walk(child.toObject());
    return out;
}

iris::ScenePtr SceneReader::readScene(QJsonObject& projectObj)
{
    auto scene = iris::Scene::create();

    //scene already contains root node, so just add children
    // CONST, and read through value() throughout: QJsonObject::operator[] on a
    // NON-const object INSERTS a null value for a key that is not there, so a
    // plain `sceneObj["x"]` read makes every later contains("x") in this
    // function true (the giUpdateBudget / giPccGrid / giTier branches below all
    // ask). const makes that impossible rather than merely unfashionable.
    const QJsonObject sceneObj = projectObj["scene"].toObject();
	scene->skyGuid = sceneObj.value("skyGuid").toString();
	// THE SUN PIN (SUN_AND_LIGHT_DEFAULTS Q1): which directional light is the
	// sun. Empty = automatic. The `skyDrivesSun` key beside it is GONE (D15):
	// the sky follows the sun, never the other way round, so an old file's
	// steering switch is simply not read and its sky takes its sun from the
	// same directional light the pin names.
	scene->sunLightGuid = sceneObj.value("sunLight").toString();
	// THE SUN DISC (SKY_LIGHT_SPEC §3): visible by default, excluded from probe
	// captures by default, drawn at iris::kDefaultSunDiscSize degrees across.
	// Absent keys read as those defaults, so a file written before the disc
	// existed gets the shipped behaviour — and the SIZE default is the same
	// named constant the field's initialiser uses, so the two cannot drift
	// apart (the reader-defaults trap this file's header records).
	scene->sunDiscVisible = sceneObj.value("sunDiscVisible").toBool(scene->sunDiscVisible);
	scene->sunDiscInProbes = sceneObj.value("sunDiscInProbes").toBool(scene->sunDiscInProbes);
	scene->sunDiscSize = float(qBound(double(iris::kMinSunDiscSize),
	                                  sceneObj.value("sunDiscSize")
	                                      .toDouble(double(scene->sunDiscSize)),
	                                  double(iris::kMaxSunDiscSize)));
	// HARDWARE RAY TRACING (ledger §425). Tolerant, like the play mode: a key
	// that is absent (every scene written before the row existed) or that names
	// a state this build does not know leaves the CONSTRUCTOR's default — which
	// is Auto, spelled exactly once, in the field's initialiser (the
	// reader-defaults trap this file's header records).
	{
		iris::RayTracingMode rt = iris::RayTracingMode::Auto;
		if (iris::rayTracingModeFromName(sceneObj.value("rayTracing").toString(), rt))
			scene->rayTracing = rt;
	}
	// THE PROJECT'S VR SETTINGS (lane VR-WORLD-1), GENERATED FROM THE TABLE
	// (services/vrworld.h) like the writer: one list of keys, one set of
	// clamps, and the reader-defaults law inside that one function — an absent
	// key, a number that is not one, or a mode name this build does not know
	// leaves the CONSTRUCTOR's value standing.
	vrworld::read(scene, sceneObj);
	// THE CLOUD LAYER (CLOUDS-2D-1). An absent block — every scene written
	// before the layer existed, and every scene that never turned it on — is
	// the constructor's layer, OFF; CloudLayer::fromJson gives each absent key
	// inside a present block the constructor's value too (the reader-defaults
	// law). The weather map is resolved here, like the sky's own image.
	scene->clouds = iris::CloudLayer::fromJson(sceneObj.value("clouds").toObject());
	if (!scene->clouds.weatherMapGuid.isEmpty()) {
		const QString weather = resolveAssetPath(scene->clouds.weatherMapGuid);
		if (QFileInfo(weather).isFile())
			scene->cloudWeatherMap = iris::Texture2D::load(weather, false);
	}
	scene->ambientMusicGuid = sceneObj.value("ambientMusicGuid").toString();
	auto volume = sceneObj.value("ambientMusicVolume").toDouble(scene->ambientMusicVolume);
	scene->setAmbientMusicVolume(volume);
	const QString ambientMusicPath = resolveAssetPath(scene->ambientMusicGuid);
	if (!ambientMusicPath.isEmpty()) {
		scene->setAmbientMusic(ambientMusicPath);
		scene->startPlayingAmbientMusic();
	}

	// THE READER-DEFAULTS LAW, no-argument form (READER-DEFAULTS-2): a bare
	// toInt() is an implicit 0 — it agreed with SkyType::SingleColor only by
	// the order of the enum, which is exactly the coincidence the law exists
	// to stop relying on.
	scene->skyType = static_cast<iris::SkyType>(
	    sceneObj.value("skyType").toInt(static_cast<int>(scene->skyType)));
	QJsonObject skyDataDef = sceneObj.value("skyData").toObject();
	for (const auto &key : skyDataDef.keys()) {
		scene->skyData.insert(key, skyDataDef.value(key).toObject());
	}

	switch (scene->skyType) {
        case iris::SkyType::SINGLE_COLOR: {
			scene->skyColor = readColor(scene->skyData.value("SingleColor").value("skyColor").toObject());
			break;
		}

		case iris::SkyType::REALISTIC: {
			// ONE READER (SKY-WRITE-1). The per-key defaults, the sunHaze
			// reader-defaults note and the colour fallback all live in
			// `iris::Scene::skyRealisticFromJson` now — this file, the sky
			// ASSET reader and the panel's bind each carried their own copy of
			// them. Written through `setSkyRealistic`, so the typed fields and
			// the JSON block the file just supplied cannot end up disagreeing.
			//
			// The sky's own sunPosX/Y/Z are GONE (D15) and so are the five
			// Preetham dials (SKY-GPU: the CPU bake they described does not
			// exist — the sky is the engine's own analytic model). An old
			// file's keys are simply not read; it opens at the defaults. No
			// migration exists.
			scene->setSkyRealistic(
				iris::Scene::skyRealisticFromJson(scene->skyData.value("Realistic")));
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
			scene->gradientOffset =
			    gradientDefinition.value("gradientOffset").toDouble(scene->gradientOffset);
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


    // readColor returns an INVALID QColor for an absent object, and an invalid
    // colour is not the constructor's (250,250,250) — so an absent key keeps
    // what the Scene was born with instead of blanking it.
    {
        const QColor fogColor = this->readColor(sceneObj.value("fogColor").toObject());
        if (fogColor.isValid()) scene->fogColor = fogColor;
    }
    scene->fogEnabled = sceneObj.value("fogEnabled").toBool(scene->fogEnabled);
    // Fog became EXPONENTIAL, and THE LINEAR PAIR IS GONE FROM THE DOCUMENT
    // (CRUD law, render audit I-6): `fogStart`/`fogEnd` were two fields nothing
    // rendered — one disabled panel row said so in its own label — whose last
    // remaining job was to derive a density for a scene written before
    // `fogDensity` existed. That job is done HERE, from the file's own keys, and
    // the two numbers are then forgotten: read, used, never stored and never
    // written again. 100 and 180 appear in this one expression because they are
    // the shape of the fog those old files had, not a default of anything.
    if (sceneObj.contains("fogDensity"))
        scene->fogDensity = sceneObj.value("fogDensity").toDouble(scene->fogDensity);
    else if (sceneObj.contains("fogStart") || sceneObj.contains("fogEnd"))
        scene->fogDensity = iris::Scene::fogDensityFromLinear(
            float(sceneObj.value("fogStart").toDouble(100.0)),
            float(sceneObj.value("fogEnd").toDouble(180.0)));
    scene->fogHeightDensity = sceneObj.value("fogHeightDensity").toDouble(scene->fogHeightDensity);
    scene->fogAtmosphere = sceneObj.value("fogAtmosphere").toBool(scene->fogAtmosphere);
    scene->fogHeightFalloff = sceneObj.value("fogHeightFalloff").toDouble(scene->fogHeightFalloff);
    scene->fogHeightLevel = sceneObj.value("fogHeightLevel").toDouble(scene->fogHeightLevel);
    scene->fogBreakMinBrightness =
        sceneObj.value("fogBreakMinBrightness").toDouble(scene->fogBreakMinBrightness);
    scene->fogBreakFalloff = sceneObj.value("fogBreakFalloff").toDouble(scene->fogBreakFalloff);

    // Global illumination: absent (older scenes) or unknown values mean OFF.
    {
        // ABSENT LEAVES THE CONSTRUCTOR'S VALUE (the reader-defaults law): the
        // key is only mapped when the file carries one, so "off" and "medium"
        // are not repeated here as second definitions of Scene's own defaults.
        // An unknown spelling still reads OFF / MEDIUM, which is what a corrupt
        // or newer-build document deserves.
        // "instant_radiosity" IS READ, and reads as VCT (PHOTON_SPEC E2 (4)).
        // The technique is deleted; the scenes that named it are not, and what
        // its tier means now IS the voxel arm — so an old document opens lit
        // rather than dark. It is never written again.
        if (sceneObj.contains("giMode")) {
            const QString giMode = sceneObj.value("giMode").toString();
            if (giMode == "vct" || giMode == "instant_radiosity") scene->giMode = iris::GiMode::VCT;
            else if (giMode == "vct_pcc_hybrid") scene->giMode = iris::GiMode::VCT_PCC_HYBRID;
            else scene->giMode = iris::GiMode::OFF;
        }
        if (sceneObj.contains("giQuality")) {
            const QString giQuality = sceneObj.value("giQuality").toString();
            if (giQuality == "low") scene->giQuality = iris::GiQuality::LOW;
            else if (giQuality == "high") scene->giQuality = iris::GiQuality::HIGH;
            else scene->giQuality = iris::GiQuality::MEDIUM;
        }
        // THE LIT VOLUME IS THE RENDERER'S (owner decision D8, 2026-09-13). The
        // reader no longer looks at `giBoundsMin`/`giBoundsMax`/`giAutoBoundsMax`:
        // a scene that pinned a volume OPENS UNPINNED, with the automatic fit,
        // which is the only behaviour left. Deliberately no tolerance and no
        // migration — there is nothing the pin could be migrated to.
        scene->giNumBounces = qBound(1, sceneObj.value("giNumBounces").toInt(scene->giNumBounces), 4);
        // THE GI UPDATE BUDGET (FIX WAVE B1), with the legacy mapping: a
        // document written before the fix wave carries the giAutoRefresh bool
        // and nothing else, and false meant exactly what budget 0 means.
        scene->giUpdateBudget =
            sceneObj.contains("giUpdateBudget")
                ? qBound(0, sceneObj.value("giUpdateBudget").toInt(scene->giUpdateBudget), 512)
                : (sceneObj.value("giAutoRefresh").toBool(true) ? 1 : 0);
        // (`giDynamicProbes` — Photon Epic's old fifth column — is READ BY
        // NOTHING. The feature is deleted, R2 2026-09-12: a moving object is not
        // in a probe capture at all any more, so there is nothing to reserve
        // captures for. An old file may carry the key; it is simply ignored,
        // and the pin it may have left in worldOverrides is dropped below.)
        if (sceneObj.contains("giPccGrid"))   // pre-hybrid documents keep the 3x2x3 default
            scene->giPccGrid = readVector3(sceneObj.value("giPccGrid").toObject());
        // Probe-capture knobs (REFLECTIONS_ADOPTION_SPEC P3). Absent in every
        // document written before this phase; the toInt/toDouble defaults ARE
        // the constructor's, so an old scene reads exactly as it did.
        scene->giProbeCaptureSize =
            qBound(0, sceneObj.value("giProbeCaptureSize").toInt(scene->giProbeCaptureSize), 1024);
        scene->giProbeHdr = qBound(-1, sceneObj.value("giProbeHdr").toInt(scene->giProbeHdr), 1);
        scene->giProbeShadows =
            qBound(-1, sceneObj.value("giProbeShadows").toInt(scene->giProbeShadows), 1);
        scene->giProbeOverlap = float(
            qBound(0.01, sceneObj.value("giProbeOverlap").toDouble(scene->giProbeOverlap), 8.0));
        scene->giProbeSnapDeviation = float(qMax(
            0.0, sceneObj.value("giProbeSnapDeviation").toDouble(scene->giProbeSnapDeviation)));
        scene->giProbeSnapSidesMin = float(qMax(
            0.0, sceneObj.value("giProbeSnapSidesMin").toDouble(scene->giProbeSnapSidesMin)));
        scene->giProbeSnapSidesMax = float(qMax(
            0.0, sceneObj.value("giProbeSnapSidesMax").toDouble(scene->giProbeSnapSidesMax)));
        scene->giRayMarchStepScale = float(qBound(
            1.0, sceneObj.value("giRayMarchStepScale").toDouble(scene->giRayMarchStepScale), 8.0));
        // PHOTON cascades (SPECS/PHOTON_SPEC.md P0). Absent in every document
        // written before the flag existed, and the default is the arm those
        // documents were authored against — there is nothing to migrate.
        // THE KEY IS ABSENT in every document written before the column existed
        // and was a BOOL for the first day of P0's flag; -1 was written for one
        // afternoon by a tri-state that is gone (scene.h says why). All four
        // spellings read here; only "absent" is left unresolved, and the block
        // after worldMode is read resolves it through the tier.
        {
            const QJsonValue casc = sceneObj.value("giCascades");
            scene->giCascades = casc.isUndefined() || casc.isNull() ? -1
                              : casc.isBool()                      ? (casc.toBool() ? 1 : 0)
                              : casc.toInt(-1) < 0                 ? -1
                              : (casc.toInt(0) != 0 ? 1 : 0);
        }
        scene->giCascadeInstanceCap = qBound(
            0, sceneObj.value("giCascadeInstanceCap").toInt(scene->giCascadeInstanceCap), 1 << 20);
        // THE SURFACE CACHE's three rows. Absent in every file written before
        // SURFACE-CACHE-1b, and the constructor's value is the answer then —
        // which is OFF, the shipped arm exactly (READER-DEFAULTS-1's rule: the
        // absent-key fallback and the constructor default must agree, and here
        // they are the same expression).
        scene->giCards = qBound(-1, sceneObj.value("giCards").toInt(scene->giCards), 1);
        scene->giCardBudgetTexels = qBound(
            0, sceneObj.value("giCardBudgetTexels").toInt(scene->giCardBudgetTexels), 1 << 26);
        scene->giCardRadius = float(qBound(
            0.0, sceneObj.value("giCardRadius").toDouble(double(scene->giCardRadius)), 100000.0));
        scene->giDragMoverChannel = qBound(
            0, sceneObj.value("giDragMoverChannel").toInt(scene->giDragMoverChannel), 1);
        scene->giCascadeSet.clear();
        for (const QJsonValue &v : sceneObj.value("giCascadeSet").toArray()) {
            const QJsonArray row = v.toArray();
            if (row.size() < 3) continue;
            scene->giCascadeSet.append(iris::Vec3(float(row.at(0).toDouble()),
                                                  float(row.at(1).toDouble()),
                                                  float(row.at(2).toDouble())));
        }
        // DDGI (GI_UNIFIED_SPEC.md §4 P1). Absent in every document written
        // before this phase, and the fallbacks ARE the constructor's values —
        // -1 (auto, which resolves OFF while there is no Photon tier) is what
        // makes those documents render exactly as they always did.
        scene->giDdgi = qBound(-1, sceneObj.value("giDdgi").toInt(scene->giDdgi), 1);
        // THE SCREEN-PROBE GATHER's row, with the absent-key fallback reading the
        // value the CONSTRUCTOR left (-1, auto) — the reader-defaults law
        // (READER-DEFAULTS-1): a fallback that disagrees with the ctor is how the
        // five Medium samples once shipped at exposure 0.
        scene->giGather = qBound(-1, sceneObj.value("giGather").toInt(scene->giGather), 1);
        // (A "giDdgiSource" key written before 2026-09-17 is IGNORED: the
        // irradiance field's rasterised probe source was deleted with the lane
        // FIELD-RASTER-CRUD, and the voxel source is the only one there is.)
        scene->giDdgiIntensity = float(
            qBound(0.0, sceneObj.value("giDdgiIntensity").toDouble(scene->giDdgiIntensity), 64.0));
        // PHOTON's quality tier (GI_UNIFIED_SPEC §2 / P2). Absent in every
        // document written before the unification — those are DERIVED from the
        // fields above, below, once the World Mode is known.
        if (sceneObj.contains("giTier")) {
            bool ok = false;
            const auto t = worldmodes::photonTierFromName(sceneObj.value("giTier").toString(), &ok);
            scene->giTier = ok ? int(t) : scene->giTier;
        }
    }
    scene->shadowEnabled = sceneObj.value("shadowEnabled").toBool(scene->shadowEnabled);
    // Anti-aliasing: absent (a document written before the key existed) reads
    // the CONSTRUCTOR's default — the fallback IS scene->antiAliasing rather
    // than a second literal, so the reader-defaults trap (an absent-key
    // fallback drifting away from the ctor, which is how five samples once
    // shipped at exposure 0) cannot happen to this field again. Anything odd
    // is rounded down to the nearest supported step (1/2/4/8).
    {
        const int aa = sceneObj.value("antiAliasing").toInt(scene->antiAliasing);
        scene->antiAliasing = aa >= 8 ? 8 : aa >= 4 ? 4 : aa >= 2 ? 2 : 1;
    }
    // Shadow-map resolution: absent or <= 0 means Auto (derive from the lights);
    // anything else is clamped to the engine's own [256, 8192] window.
    {
        const int sr = sceneObj.value("shadowResolution").toInt(scene->shadowResolution);
        scene->shadowResolution = sr <= 0 ? 0 : qBound(256, sr, 8192);
    }
    // Shadow-map BUDGET: absent (every scene written before shadow tooling)
    // means Auto, i.e. follow the World Mode tier.
    {
        const int sb = sceneObj.value("shadowMapBudget").toInt(scene->shadowMapBudget);
        scene->shadowMapBudget = sb <= 0 ? 0 : qBound(2, sb, 16);
    }
    // Shadow FILTER quality: absent means Auto (-1); otherwise 0/1/2.
    {
        const int sf = sceneObj.value("shadowFilterTier").toInt(scene->shadowFilterTier);
        scene->shadowFilterTier = (sf >= 0 && sf <= 2) ? sf : -1;
        // Absent in every scene written before the ParticleFX2 adoption: 1 = real time.
        scene->particleTimeScale =
            std::max(0.0, sceneObj.value("particleTimeScale").toDouble(scene->particleTimeScale));
    }
    // Post-processing chain (POST_CHAIN_SPEC §§3-7). Absent = off, which is what
    // every document written before the chain existed means.
    scene->hdrEnabled = sceneObj.value("hdrEnabled").toBool(scene->hdrEnabled);
    // EVERY FALLBACK IN THIS BLOCK IS THE CONSTRUCTOR'S VALUE, verbatim
    // (irisgl/document/scenegraph/scene.cpp) — a key a file does not carry must
    // read as what a scene that was never saved holds, or "absent" and "fresh"
    // mean two different scenes. This block DEFAULT-CONSTRUCTS AND OVERWRITES
    // PRESENT KEYS rather than repeating literals, which is the reader-defaults
    // law as code (`exposure` disagreed for a week and every document without
    // the key opened two stops dark; SUN1's shadow key was the same defect).
    //
    // THE EXPOSURE KEYS CHANGED NAME AND UNIT (EXPOSURE-1, 2026-09-17): the
    // document stores STOPS now, on the same axis a camera uses, with a MODE.
    // `exposureEv`/`exposureMinEv`/`exposureMaxEv` are the new keys precisely
    // so an old file's chain-unit `exposure` cannot be read as stops and
    // silently regrade a scene by a factor: an old file carries none of them,
    // reads the constructor — Manual at the derived default — and is developed
    // by physics instead of by a meter. No migration, by the CRUD law.
    {
        bool ok = false;
        const iris::ExposureMode m = iris::exposureModeFromName(
            sceneObj.value("exposureMode").toString().toLatin1().constData(), &ok);
        if (ok) scene->exposureMode = m;
        if (sceneObj.contains("exposureEv"))
            scene->exposure = float(qBound(-16.0, sceneObj.value("exposureEv")
                                              .toDouble(double(scene->exposure)), 16.0));
        if (sceneObj.contains("exposureMinEv"))
            scene->exposureMin =
                float(qBound(-16.0, sceneObj.value("exposureMinEv")
                                        .toDouble(double(scene->exposureMin)), 16.0));
        if (sceneObj.contains("exposureMaxEv"))
            scene->exposureMax =
                float(qBound(-16.0, sceneObj.value("exposureMaxEv")
                                        .toDouble(double(scene->exposureMax)), 16.0));
        if (scene->exposureMax < scene->exposureMin)
            std::swap(scene->exposureMin, scene->exposureMax);
        // TOLD ONCE, NOT MIGRATED (lead review item 4). A file with the retired
        // chain-unit keys and none of the new ones opened at the constructor's
        // grade — which is a decision, not an accident, and the one thing a
        // user cannot deduce from the picture. The scene carries the fact and
        // the issue bar says it; nothing converts anything.
        scene->legacyExposureKeyIgnored =
            (sceneObj.contains("exposure") || sceneObj.contains("exposureMin") ||
             sceneObj.contains("exposureMax")) &&
            !sceneObj.contains("exposureEv") && !sceneObj.contains("exposureMinEv") &&
            !sceneObj.contains("exposureMaxEv") && !sceneObj.contains("exposureMode");
        // THE METER (EXPOSURE-2). An absent key is the constructor's default
        // (centre-weighted, 10/90) — a file written before the meter existed
        // was metered by a whole-frame mean, and there is no honest conversion
        // from "no choice recorded" to one, so it opens at the default like
        // everything else the CRUD law leaves unmigrated.
        bool meterOk = false;
        const iris::ExposureMetering meter = iris::exposureMeteringFromName(
            sceneObj.value("exposureMetering").toString().toLatin1().constData(), &meterOk);
        if (meterOk) scene->exposureMetering = meter;
        if (sceneObj.contains("exposureMeterLow"))
            scene->exposureMeterLowPercent =
                float(qBound(0.0, sceneObj.value("exposureMeterLow")
                                      .toDouble(double(scene->exposureMeterLowPercent)), 100.0));
        if (sceneObj.contains("exposureMeterHigh"))
            scene->exposureMeterHighPercent =
                float(qBound(0.0, sceneObj.value("exposureMeterHigh")
                                      .toDouble(double(scene->exposureMeterHighPercent)), 100.0));
        if (scene->exposureMeterHighPercent < scene->exposureMeterLowPercent)
            std::swap(scene->exposureMeterLowPercent, scene->exposureMeterHighPercent);
    }
    scene->bloomEnabled = sceneObj.value("bloomEnabled").toBool(scene->bloomEnabled);
    scene->bloomThreshold = float(sceneObj.value("bloomThreshold").toDouble(scene->bloomThreshold));
    scene->bloomKnee = float(sceneObj.value("bloomKnee").toDouble(scene->bloomKnee));
    // THE READER-DEFAULTS LAW (document.reader_defaults): the fallback is the
    // CONSTRUCTOR'S value, so a file written before the Bloom Amount existed
    // opens at 1x — the picture it was authored with — rather than at zero.
    // Clamped on the way in like the ssao rows below: a hand-edited 5.0 would
    // otherwise be 5 in the document, 2.00 in the panel and 2x in the picture
    // (the engine and the shader clamp) — one number, three answers.
    scene->bloomAmount = float(qBound(0.0, sceneObj.value("bloomAmount").toDouble(scene->bloomAmount), 2.0));
    scene->ssaoEnabled = sceneObj.value("ssaoEnabled").toBool(scene->ssaoEnabled);
    scene->ssaoScale = float(qBound(0.25, sceneObj.value("ssaoScale").toDouble(scene->ssaoScale), 1.0));
    scene->ssaoPower = float(qBound(0.1, sceneObj.value("ssaoPower").toDouble(scene->ssaoPower), 8.0));
    scene->ssaoRadius =
        float(qBound(0.05, sceneObj.value("ssaoRadius").toDouble(scene->ssaoRadius), 64.0));
    scene->smaaPreset = qBound(-1, sceneObj.value("smaaPreset").toInt(scene->smaaPreset), 3);
    scene->ssrMode = qBound(0, sceneObj.value("ssrMode").toInt(scene->ssrMode), 2);
    scene->ssrMarch = qBound(0, sceneObj.value("ssrMarch").toInt(scene->ssrMarch), 2);
    // BOTH SPELLINGS, absent = 40 — the helper carries the reasoning and the
    // tolerance for the old `rayReflectRoughness` key (sceneformat.h).
    scene->reflectionRoughnessCutoff = sceneformat::readReflectionRoughnessCutoff(sceneObj);
    scene->refractionsMode = qBound(0, sceneObj.value("refractionsMode").toInt(scene->refractionsMode), 2);
    // Distortion: absent = AUTO, which is what a document written before the
    // feature existed means (it holds no distortion material, so auto costs it
    // nothing and a user who adds one later sees it work).
    scene->distortionMode = qBound(0, sceneObj.value("distortionMode").toInt(scene->distortionMode), 2);
    scene->distortionStrength = float(
        qBound(0.0, sceneObj.value("distortionStrength").toDouble(scene->distortionStrength), 8.0));
    // The Player's floor (PLAYER-FLOOR-1). Absent = the constructor's false,
    // which is what every document written before the setting existed means:
    // it played on its floor.
    scene->playerHidesFloor =
        sceneObj.value("playerHidesFloor").toBool(scene->playerHidesFloor);
    // The looks stack. Absent = empty = the renderer's behaviour before this
    // feature existed, byte for byte. Everything a file can get wrong — an
    // unknown look id (a document from a newer build), the same look twice, a
    // parameter out of range or missing — is handled in ONE place, the
    // document's own validator, which every write path also goes through.
    scene->looks = iris::normalizeLookStack(sceneObj.value("looks").toArray());

    // Planar reflections: absent means "follow the world mode" on all three
    // (-1 / 0 / -1), which is what every document written before this feature
    // says by omission. Explicit values are clamped to what the engine accepts.
    {
        const int pb = sceneObj.value("planarReflectionBudget").toInt(scene->planarReflectionBudget);
        scene->planarReflectionBudget = pb < 0 ? -1 : qBound(0, pb, 8);
        const int pres =
            sceneObj.value("planarReflectionResolution").toInt(scene->planarReflectionResolution);
        scene->planarReflectionResolution = pres <= 0 ? 0 : qBound(256, pres, 2048);
        const int ps = sceneObj.value("planarReflectionShadows").toInt(scene->planarReflectionShadows);
        scene->planarReflectionShadows = (ps == 0 || ps == 1) ? ps : -1;
    }
    // World Mode (POST_CHAIN_SPEC §9). Absent reads as "custom": the fields
    // above ARE the truth for a document written before modes existed, and for
    // one the user never put on a tier. (§12 decision 8 proposed reading absent
    // as Epic; that would silently switch VCT GI, 4x MSAA and a 4096 shadow
    // atlas on for every existing scene — left to the owner.)
    {
        const QString m = sceneObj.value("worldMode").toString().trimmed().toLower();
        scene->worldOverrides = sceneObj.value("worldOverrides").toObject();
        // A PIN OF A ROW THAT NO LONGER EXISTS is dropped on the way in (CRUD).
        // `giDynamicProbes` was Photon's fifth column until R2 deleted the
        // feature; a scene the user had pinned it on (the shipped Showroom was
        // one) would otherwise carry the dead key through every save for ever,
        // and the World panel would have nothing to show for it.
        scene->worldOverrides.remove(QStringLiteral("giDynamicProbes"));
        // THE PRODUCT RENAME (owner 2026-09-13): the GI dial's row was `rayon`
        // and is `photon`. Nothing else about the row changed, so a pin written
        // under the old spelling is carried over rather than dropped — the ONE
        // thing tolerated here, read-only and never written back. No shipped
        // sample carries it; a scene the owner pinned the dial on does.
        if (scene->worldOverrides.contains(QStringLiteral("rayon"))) {
            const QJsonValue pin = scene->worldOverrides.value(QStringLiteral("rayon"));
            scene->worldOverrides.remove(QStringLiteral("rayon"));
            if (!scene->worldOverrides.contains(worldmodes::photonRowId()))
                scene->worldOverrides.insert(worldmodes::photonRowId(), pin);
        }
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

            // PHOTON MIGRATION (GI_UNIFIED_SPEC §2 / P2). A document written
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
            if (!sceneObj.contains("giTier")) worldmodes::derivePhotonFromDocument(scene);
            // A TIER COLUMN THAT DID NOT EXIST WHEN THIS FILE WAS WRITTEN
            // FOLLOWS THE TIER (PHOTON_SPEC §7 E2 (6), the cascade chain).
            // Without this every document written before the column — which is
            // every document that exists, the eight shipped samples included —
            // would come up with the chain OFF and, worse, would read as
            // "Custom" for ever, because `photonDeviations` compares the stored
            // value against the tier's and would find a deviation nobody
            // authored. It is keyed on the KEY BEING ABSENT and not on its
            // value, so it can never re-normalise a scene that made a choice
            // (the re-application trap the deleted comment below records), and
            // it writes no pin.
            if (scene->giCascades < 0)
                scene->giCascades = worldmodes::photonCascades(worldmodes::photonTier(scene));
            // (THE TIER TABLE'S OPTION-(b) BUMP LIVED HERE and is DELETED,
            // 2026-09-12.) It re-applied the tier to any document that carried
            // a `giTier` but no `giDynamicProbes` — the one-day P2 table's
            // shape — and it keyed on the PRESENCE of a key that this build no
            // longer writes, so from the moment R2 deleted the column every
            // file the app saves would have taken it on every open, silently
            // re-normalising unpinned deviations for ever (code review
            // 2026-09-12, item 3). No migrations are owed (CRUD law): a
            // document's own rows are what it renders, and a sample that comes
            // up at the wrong tier is re-authored, never patched by the reader.
        }
    }
	scene->setWorldGravity(sceneObj.value("gravity").toDouble(scene->gravity));

    auto rootNode = sceneObj.value("rootNode").toObject();

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
    scene->setActiveCamera(sceneObj.value("activeCamera").toString());

    // The play mode (AVATAR_LOCOMOTION_SPEC §8.5). Tolerant by design: a key
    // that is absent (every scene older than Stage 3) or that names a mode this
    // build does not know leaves the default `explorer` — the behaviour those
    // scenes already had — rather than refusing to open the file.
    {
        iris::ScenePlayMode mode = iris::ScenePlayMode::Explorer;
        if (iris::playModeFromName(sceneObj.value("playMode").toString(), mode))
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
    // A NODE WITHOUT A GUID IS UNADDRESSABLE — no script verb, no MCP tool, no
    // per-node DB row and no undo command can name it — so one is MINTED here
    // rather than carried through as "".
    //
    // The default argument to QJsonValue::toString() only substitutes when the
    // value is not a STRING at all, so a file carrying `"guid": ""` (the
    // shipped World Background sample's first dragon did, found 2026-09-09 by
    // scene.find returning an empty id) sailed straight past it. Minting is the
    // only repair that can work: the identity was never written down.
    const QString storedGuid = nodeObj["guid"].toString();
    sceneNode->setGUID(storedGuid.isEmpty() ? GUIDManager::generateGUID() : storedGuid);
    // EVERY FALLBACK BELOW IS THE NODE'S OWN CURRENT VALUE, i.e. the
    // constructor's (the reader-defaults law, I-5): absent and fresh must mean
    // the same node.
    sceneNode->setAttached(nodeObj["attached"].toBool(sceneNode->isAttached()));
    sceneNode->setPickable(nodeObj["pickable"].toBool(sceneNode->isPickable()));
    sceneNode->setScaleLock(nodeObj["scaleLock"].toBool(sceneNode->getScaleLock()));
    // Absent = the ctor's false: the writer only emits the key when the flag is on.
    sceneNode->setPlanarReflector(
        nodeObj["planarReflector"].toBool(sceneNode->getPlanarReflector()));
    // Absent = the ctor's false, same as planarReflector: the writer only emits it when set.
    sceneNode->setGiBoundsExcluded(
        nodeObj["giBoundsExcluded"].toBool(sceneNode->getGiBoundsExcluded()));
    // Shadow Caster: absent = the document default (TRUE) — the writer only
    // emits the key when the user turned casting off, so every scene written
    // before the key existed loads exactly as it did.
    sceneNode->setShadowCastingEnabled(
        nodeObj["castShadow"].toBool(sceneNode->getShadowCastingEnabled()));
    // LIGHTING CHANNELS. ABSENT = ALL CHANNELS, which is what every scene
    // written before the key existed means and what "the feature is off"
    // means — so no old document changes appearance. Read through a double
    // (QJsonValue's only numeric type; it holds every uint32 exactly) and
    // masked back to 32 bits, so a hand-edited -1 also reads as "everything".
    sceneNode->setLightMask(static_cast<quint32>(
        static_cast<qlonglong>(nodeObj["lightMask"].toDouble(double(sceneNode->getLightMask()))) &
        0xFFFFFFFFll));
    // MOBILITY, the persisted USER SETTING (format v3). Absent — every node of
    // every scene written before v3, and the overwhelming majority after it —
    // means "no opinion": the resolution rule decides, in the
    // applyStaticDefaults pass that runs once the whole tree is built.
    //
    // The RAW setter, not setMobility: this node's parent chain does not exist
    // yet (the reader builds children before it attaches the subtree), so
    // asking the graph to make it static now would be refused by rule 2 and log
    // a warning per node. Recording the intent here and letting the pass at the
    // end of the load act on it is the same thing, in the right order.
    //
    // v2's BOOLEAN "static" key is still READ (true = static, false = movable,
    // which is exactly what v2's Static/Dynamic override meant) so scenes saved
    // before mobility open with the same meaning. It is never written again,
    // and a file carrying both keys is answered by the new one.
    iris::Mobility mobility = iris::Mobility::Auto;
    if (nodeObj.contains(QLatin1String("mobility"))) {
        if (!iris::mobilityFromName(nodeObj["mobility"].toString(), mobility)) {
            qWarning("SceneReader: node '%s' carries an unknown mobility '%s' — resolving it "
                     "automatically instead.",
                     qUtf8Printable(sceneNode->getName()),
                     qUtf8Printable(nodeObj["mobility"].toString()));
            mobility = iris::Mobility::Auto;
        }
    } else if (nodeObj.contains(QLatin1String("static"))) {
        mobility = nodeObj["static"].toBool() ? iris::Mobility::Static : iris::Mobility::Movable;
    }
    if (mobility != iris::Mobility::Auto) sceneNode->_setMobility(mobility);
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

        // THE LINK (AVATAR_ASSET_SPEC §5.4). Absent on a scene written before
        // avatars were library assets, and absent on an unlinked scratch
        // avatar (an `avatar.spawn` on a plain Object guid) — both load as
        // exactly what they always were. When present, the recorded VERSION is
        // what the load-time re-resolve compares against the project's pin.
        sceneNode->avatarLink.asset = a["asset"].toString();
        sceneNode->avatarLink.version = a["version"].toString();
        sceneNode->avatarLink.name = a["name"].toString();
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

	sceneNode->isPhysicsBody = nodeObj["physicsObject"].toBool(sceneNode->isPhysicsBody);

	if (sceneNode->isPhysicsBody) {
		QJsonObject physicsDef = nodeObj["physicsProperties"].toObject();
		sceneNode->physicsProperty.centerOfMass = readVector3(physicsDef["centerOfMass"].toObject());
		// THE FALLBACKS ARE PhysicsProperty's OWN (the reader-defaults law): a
		// body written before a key existed read mass 0, damping 0 and a zero
		// collision margin against the constructor's 1 / 0.1 / 0.01 — and a
		// mass of zero is a STATIC body to Bullet, which is not "no opinion".
		auto &phys = sceneNode->physicsProperty;
		phys.isStatic = physicsDef["static"].toBool(phys.isStatic);
		phys.objectCollisionMargin =
		    physicsDef["collisionMargin"].toDouble(phys.objectCollisionMargin);
		phys.objectDamping = physicsDef["damping"].toDouble(phys.objectDamping);
		phys.objectMass = physicsDef["mass"].toDouble(phys.objectMass);
		phys.objectFriction = physicsDef["friction"].toDouble(phys.objectFriction);
		phys.objectRestitution = physicsDef["bounciness"].toDouble(phys.objectRestitution);
		sceneNode->physicsProperty.pivotPoint = readVector3(physicsDef["pivot"].toObject());
		// ...and the two ENUMS through the same rule (READER-DEFAULTS-2): a bare
		// toInt() is an implicit 0, which is PhysicsCollisionShape::None and
		// PhysicsType::None by the order of those enums and by nothing else.
		phys.shape = static_cast<iris::PhysicsCollisionShape>(
		    physicsDef["shape"].toInt(static_cast<int>(phys.shape)));
		phys.type = static_cast<iris::PhysicsType>(
		    physicsDef["type"].toInt(static_cast<int>(phys.type)));

		QJsonArray constraints = physicsDef["constraints"].toArray();
		for (const auto &constraint : constraints) {
			QJsonObject constraintObject = constraint.toObject();

			iris::ConstraintProperty constraintProp;
			constraintProp.constraintFrom = constraintObject.value("constraintFrom").toString();
			constraintProp.constraintTo = constraintObject.value("constraintTo").toString();
			constraintProp.constraintType = static_cast<iris::PhysicsConstraintType>(
			    constraintObject.value("constraintType")
			        .toInt(static_cast<int>(constraintProp.constraintType)));

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
        // TWO REAL DISAGREEMENTS, found by the no-argument half of the
        // reader-defaults lint (READER-DEFAULTS-2): a clip whose file
        // carries no `length` read 0 against the constructor's 1 second
        // (a clip of zero length plays nothing), and one with no `loop`
        // read false against the constructor's true.
        animation->setLength(float(animObj["length"].toDouble(animation->getLength())));
        animation->setLooping(animObj["loop"].toBool(animation->getLooping()));

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

                    // The KEY exists (addKey made it), so its own slopes are
                    // the fallback — 0/0, the straight line through the key,
                    // stated by the class rather than by this line.
                    key->leftSlope = float(keyObj["leftSlope"].toDouble(key->leftSlope));
                    key->rightSlope = float(keyObj["rightSlope"].toDouble(key->rightSlope));

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
            const QString source = skelAnim["source"].toString();
            const QString clipGuid = skelAnim["guid"].toString();
            // The own-model route is the last resort when neither the clip's
            // guid nor its persisted path resolves — worked out for every
            // clip, because a clip WITH a guid whose row was purged needs it
            // too (15c review #2; the deleted by-name lookup ran in that case).
            // One walk of this node's subtree per skeletal clip.
            const QString ownModel = ownModelGuidFor(nodeObj, QFileInfo(source).fileName());

            auto skel = this->getSkeletalAnimation(source, skelAnim["name"].toString(),
                                                  clipGuid, ownModel);
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

    int meshIndex = nodeObj["meshIndex"].toInt(meshNode->meshIndex);
    QString meshGUID = nodeObj["guid"].toString();

    if (source.isEmpty() && !nodeObj["mesh"].toString().isEmpty()) {
        // A mesh the catalog cannot resolve used to load SILENTLY as a mesh node
        // with no mesh (CLIPBOARD_SPEC audit, 2026-09-09). Say so once per node:
        // the picture is wrong and the user deserves the file name.
        qWarning().noquote() << "scene reader: mesh" << nodeObj["mesh"].toString()
                             << "for node" << nodeObj["name"].toString()
                             << "did not resolve to a file — the node loads with no mesh";
    }
    if (!source.isEmpty()) {
        // A ":"-prefixed source is a BUILT-IN: a SEED KEY, resolved to the
        // baked library asset behind it (ATOM P2, services/primitiveassets.h).
        // It used to be a file assimp re-parsed per node on the UI thread — and
        // getMesh ran on it too, so every primitive in every scene also cost a
        // failed catalog lookup whose result was thrown away (measured
        // 2026-09-15: 5 per open of the Mirror Room sample, 6 per Showroom).
        // The eight shipped samples name five primitives, the Ground and the
        // Teapot between them; each of them arrives with its LOD chain now.
        auto mesh = source.startsWith(":")
                        ? PrimitiveAssets::mesh(source, handle)
                        : getMesh(source, meshIndex, nodeObj["mesh"].toString());

        if (source.startsWith(":")) {
            meshNode->setMesh(mesh);
			meshNode->meshPath = source;
            // A ":"-prefixed source IS a built-in primitive (addBuiltinPrimitive's
            // meshes). The flag was only ever set at creation and lost on reopen,
            // so the ground's outline exclusion (enginesceneviewport.cpp) silently
            // stopped after a save/load (samplescale lane finding, 2026-09-09).
            meshNode->isBuiltIn = true;
        } else {
            meshNode->setMesh(mesh);
			meshNode->meshPath = nodeObj["mesh"].toString();
        }

        meshNode->setGUID(meshGUID);
		meshNode->setVisible(nodeObj["visible"].toBool(meshNode->isVisible()));
        meshNode->meshIndex = meshIndex;
    }

    auto material = readMaterial(nodeObj);
    meshNode->setMaterial(material);

    // ABSENT LEAVES MeshNode's OWN MODE (the reader-defaults law, I-5): the
    // fallback here was the string "back", i.e. FaceCullingMode::Back, while
    // MeshNode is born DefinedInMaterial — so a mesh in a file written before
    // the key existed had its material's own cull mode overridden on the way
    // in. An unrecognised spelling still reads None, which is what a corrupt
    // value deserves.
    if (nodeObj.contains("faceCullingMode")) {
        const QString faceCullingMode = nodeObj["faceCullingMode"].toString();
        if (faceCullingMode == "back") {
            meshNode->setFaceCullingMode(iris::FaceCullingMode::Back);
        } else if (faceCullingMode == "front") {
            meshNode->setFaceCullingMode(iris::FaceCullingMode::Front);
        } else if (faceCullingMode == "material") {
            meshNode->setFaceCullingMode(iris::FaceCullingMode::DefinedInMaterial);
        } else {
            meshNode->setFaceCullingMode(iris::FaceCullingMode::None);
        }
    }

    // THE DEFAULT FLOOR (services/defaultfloor.h): a written flag, never a
    // guess from the name or the mesh path.
    meshNode->defaultFloor = nodeObj["defaultFloor"].toBool(meshNode->defaultFloor);

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
            socket.builtIn = socketObj["builtIn"].toBool(socket.builtIn);
            sockets.append(socket);
        }
        meshNode->setSockets(sockets);
    }

    meshNode->applyDefaultPose();

    return meshNode;
}

// SHADOWS ARE ON UNLESS THE FILE SAYS OTHERWISE (owner decision 1).
//
// This returned None for anything it did not recognise — INCLUDING an absent
// key — so the reader and `ShadowMap::ShadowMap()` (Soft, 2048, for years)
// stated two different defaults for one question. Our own writer always writes
// the key, so only a hand-edited file or a future importer could reach the
// divergence; that is exactly the kind of thing that is discovered late.
// An explicit "none" still means none, and always will.
iris::ShadowMapType evalShadowMapType(QString shadowType)
{
    if (shadowType=="hard")
        return iris::ShadowMapType::Hard;
    if (shadowType=="verysoft")
        return iris::ShadowMapType::VerySoft;
    if (shadowType=="none")
        return iris::ShadowMapType::None;

    return iris::ShadowMapType::Soft;
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
    // THE FALLBACKS ARE THE LIGHT'S OWN VALUES (the reader-defaults law, I-5).
    // `distance` — the point/spot RADIUS — read 1.0 here against LightNode's
    // own 10, so every light in every document written before the key existed
    // came back reaching a tenth as far as a light made in the editor.
    lightNode->intensity = (float)nodeObj["intensity"].toDouble(lightNode->intensity);
    lightNode->distance = (float)nodeObj["distance"].toDouble(lightNode->distance);
    lightNode->spotCutOff = (float)nodeObj["spotCutOff"].toDouble(lightNode->spotCutOff);
    // Serializer gap fixed (WEB_EXPORT_AUDIT §1): the mirror consumes softness
    // but it was never persisted. A document written before softness was
    // persisted never meant "all penumbra", it simply had nothing to say — so
    // it reads the constructor's narrow edge like everything else here.
    lightNode->spotCutOffSoftness =
        (float)nodeObj["spotCutOffSoftness"].toDouble(lightNode->spotCutOffSoftness);
    lightNode->spotFalloff = (float)nodeObj["spotFalloff"].toDouble(lightNode->spotFalloff);
    lightNode->rectWidth = (float)nodeObj["rectWidth"].toDouble(lightNode->rectWidth);
    lightNode->rectHeight = (float)nodeObj["rectHeight"].toDouble(lightNode->rectHeight);
    lightNode->doubleSided = nodeObj["doubleSided"].toBool(lightNode->doubleSided);
    lightNode->accurate = nodeObj["accurate"].toBool(lightNode->accurate);
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
	lightNode->setVisible(nodeObj["visible"].toBool(lightNode->isVisible()));

	// (`shadowAlpha`, `shadowColor` and `shadowBias` were READ here into three
	// document fields the renderer never consumed — two panel rows hidden, one
	// commented out — and are DELETED with them, CRUD law. The keys are still
	// TOLERATED in that an old file carrying them simply loses them: nothing
	// reads them, nothing writes them, no issue is raised, because no value
	// they could hold ever changed a pixel.)

    //shadow data
    auto shadowMap = lightNode->shadowMap;
    // THE RESOLUTION'S DEFAULT IS ShadowMap's OWN (the reader-defaults law):
    // it read 1024 here against the constructor's 2048, so every light in
    // every document written before the key existed came back at half the
    // shadow resolution a light made in the editor gets.
    auto res = qBound(512, nodeObj["shadowSize"].toInt(shadowMap->resolution), 4096);
    shadowMap->setResolution(res);
    // value(), not operator[]: a read through operator[] on a NON-CONST
    // QJsonObject INSERTS a null member.
    shadowMap->shadowType = evalShadowMapType(nodeObj.value("shadowType").toString());
    // FORWARD SHADING PRIORITY (directional lights only) — absent means the
    // default, which is what the writer relies on (it writes the key only when
    // it is non-zero).
    lightNode->forwardShadingPriority =
        qMax(0, nodeObj.value("forwardShadingPriority").toInt(lightNode->forwardShadingPriority));
    // (The sun's angular size used to be read here as `sunAngle`. The disc's
    // size is a WORLD row now — Scene::sunDiscSize — so the key is not read any
    // more and an old file's value is simply dropped: no migration exists and
    // nothing is owed to old data, the CRUD law.)
    // FOLLOWS ATMOSPHERE: absent = ON, which is the constructor's default too
    // (the two must agree — this file's own header records what happens when
    // they do not).
    lightNode->followsAtmosphere =
        nodeObj.value("followsAtmosphere").toBool(lightNode->followsAtmosphere);

    //TODO: move this to the sceneview widget or somewhere more appropriate
    if (lightNode->lightType == iris::LightType::Directional ||
        lightNode->lightType == iris::LightType::Sky) {
        lightNode->icon = iris::Texture2D::load(":/icons/light.png");      // the sun glyph
    } else if (lightNode->lightType == iris::LightType::Spot) {
        lightNode->icon = iris::Texture2D::load(":/icons/spotlight.png");
    } else if (lightNode->lightType == iris::LightType::Area) {
        // No bundled glyph: SceneMirror::syncLightIcon draws a procedural
        // rounded-rect billboard for area lights when icon is unset.
    } else {
        lightNode->icon = iris::Texture2D::load(":/icons/bulb.png");
    }

    return lightNode;
}

iris::DecalNodePtr SceneReader::createDecal(QJsonObject& nodeObj)
{
    auto decalNode = iris::DecalNode::create();

    decalNode->textureGuid  = nodeObj["decalTexture"].toString();
    decalNode->normalGuid   = nodeObj["decalNormal"].toString();
    decalNode->emissiveGuid = nodeObj["decalEmissive"].toString();
    // Fallbacks are DecalNode's own member initialisers (the reader-defaults law).
    decalNode->width  = (float) nodeObj["width"].toDouble(decalNode->width);
    decalNode->height = (float) nodeObj["height"].toDouble(decalNode->height);
    decalNode->depth  = (float) nodeObj["depth"].toDouble(decalNode->depth);
    decalNode->metalness = (float) nodeObj["metalness"].toDouble(decalNode->metalness);
    decalNode->roughness = (float) nodeObj["roughness"].toDouble(decalNode->roughness);
    decalNode->ignoreAlphaDiffuse =
        nodeObj["ignoreAlphaDiffuse"].toBool(decalNode->ignoreAlphaDiffuse);
    decalNode->setVisible(nodeObj["visible"].toBool(decalNode->isVisible()));

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
    cameraNode->angle       = (float) nodeObj["angle"].toDouble(cameraNode->angle);
    cameraNode->nearClip    = (float) nodeObj["nearClip"].toDouble(cameraNode->nearClip);
    cameraNode->farClip     = (float) nodeObj["farClip"].toDouble(cameraNode->farClip);
    cameraNode->aspectRatio = (float) nodeObj["aspectRatio"].toDouble(cameraNode->aspectRatio);
    cameraNode->setOrthagonalZoom(
        (float) nodeObj["orthogonalSize"].toDouble(cameraNode->orthoSize));
    // The five enum keys below are guarded by contains() rather than given a
    // default SPELLING: "perspective", "degrees", "vertical", "manual" and
    // "inherit" are the constructor's values and writing them here would copy
    // them (the reader-defaults law).
    if (nodeObj.contains("projectionMode"))
        cameraNode->setProjection(nodeObj["projectionMode"].toString() == "orthogonal"
                                      ? iris::CameraProjection::Orthogonal
                                      : iris::CameraProjection::Perspective);

    // CAMERAS_SPEC §2. The sensor is set through the raw fields, not
    // setSensorSize: the angle above is the authored truth and must not be
    // re-derived from a focal length the file does not carry.
    const float sw = (float) nodeObj["sensorWidth"].toDouble(cameraNode->sensorWidth);
    const float sh = (float) nodeObj["sensorHeight"].toDouble(cameraNode->sensorHeight);
    if (sw > 0.0f) cameraNode->sensorWidth = sw;
    if (sh > 0.0f) cameraNode->sensorHeight = sh;
    if (nodeObj.contains("authorMode"))
        cameraNode->authorMode = nodeObj["authorMode"].toString() == QLatin1String("mm")
                                     ? iris::CameraAuthorMode::Millimeters
                                     : iris::CameraAuthorMode::Degrees;
    // CAMERA_LENS_SPEC §3. Every key below defaults to the value the
    // constructor already set, so a file written before this phase existed
    // loads a camera that projects EXACTLY what it used to (vertical fit, no
    // squeeze, no shift). Set through the raw fields, not the setters, for the
    // same reason the sensor pair is: `angle` above is the authored truth.
    if (nodeObj.contains("sensorFit")) {
        const QString sensorFit = nodeObj["sensorFit"].toString();
        cameraNode->sensorFit = sensorFit == QLatin1String("horizontal") ? iris::CameraSensorFit::Horizontal
                              : sensorFit == QLatin1String("auto")       ? iris::CameraSensorFit::Auto
                                                                         : iris::CameraSensorFit::Vertical;
    }
    const float squeeze =
        (float) nodeObj["anamorphicSqueeze"].toDouble(cameraNode->anamorphicSqueeze);
    if (squeeze > 0.0f) cameraNode->anamorphicSqueeze = squeeze;
    cameraNode->lensShiftX =
        qBound(-1.0f, (float) nodeObj["lensShiftX"].toDouble(cameraNode->lensShiftX), 1.0f);
    cameraNode->lensShiftY =
        qBound(-1.0f, (float) nodeObj["lensShiftY"].toDouble(cameraNode->lensShiftY), 1.0f);
    cameraNode->constrainAspect =
        nodeObj["constrainAspect"].toBool(cameraNode->constrainAspect);
    cameraNode->dofEnabled      = nodeObj["dofEnabled"].toBool(cameraNode->dofEnabled);
    if (nodeObj.contains("focusMode")) {
        const QString focusMode = nodeObj["focusMode"].toString();
        cameraNode->focusMode = focusMode == QLatin1String("track") ? iris::CameraFocusMode::Track
                              : focusMode == QLatin1String("off")   ? iris::CameraFocusMode::Off
                                                                    : iris::CameraFocusMode::Manual;
    }
    cameraNode->focusDistance =
        std::max(0.0f, (float) nodeObj["focusDistance"].toDouble(cameraNode->focusDistance));
    cameraNode->focusTarget   = nodeObj["focusTarget"].toString();
    cameraNode->fStop         = std::max(0.0f, (float) nodeObj["fStop"].toDouble(cameraNode->fStop));
    // CAMERA_LENS_SPEC §3 P2, the focus block — absent keys keep the
    // constructor's defaults, which is what every pre-P2 file means, and the
    // fallback IS the constructor's value rather than a copy of it.
    cameraNode->focusOffset         = (float) nodeObj["focusOffset"].toDouble(cameraNode->focusOffset);
    cameraNode->smoothFocus         = nodeObj["smoothFocus"].toBool(cameraNode->smoothFocus);
    cameraNode->focusSmoothingSpeed = std::max(
        0.0f, (float) nodeObj["focusSmoothingSpeed"].toDouble(cameraNode->focusSmoothingSpeed));
    cameraNode->minFocusDistance    = std::max(
        0.0f, (float) nodeObj["minFocusDistance"].toDouble(cameraNode->minFocusDistance));
    cameraNode->bladeCount          = qBound(3, nodeObj["bladeCount"].toInt(cameraNode->bladeCount), 16);
    cameraNode->focusPlaneVisible   =
        nodeObj["focusPlaneVisible"].toBool(cameraNode->focusPlaneVisible);
    cameraNode->outputHeight  = qBound(1, nodeObj["outputHeight"].toInt(cameraNode->outputHeight), 16384);
    cameraNode->bodyVisible   = nodeObj["bodyVisible"].toBool(cameraNode->bodyVisible);
    // CAMERA_LENS_SPEC §4. Absent = "inherit", which is what every file written
    // before this phase existed means, and it is bit-for-bit the old behaviour.
    if (nodeObj.contains("exposureMode")) {
        const QString exposureMode = nodeObj["exposureMode"].toString();
        cameraNode->exposureMode = exposureMode == QLatin1String("auto")   ? iris::CameraExposureMode::Auto
                                 : exposureMode == QLatin1String("manual") ? iris::CameraExposureMode::Manual
                                                                           : iris::CameraExposureMode::Inherit;
    }
    cameraNode->exposure    = (float) nodeObj["exposure"].toDouble(cameraNode->exposure);
    cameraNode->exposureMin = (float) nodeObj["exposureMin"].toDouble(cameraNode->exposureMin);
    cameraNode->exposureMax = (float) nodeObj["exposureMax"].toDouble(cameraNode->exposureMax);
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

    // Same rule as readSceneNode's: an empty guid is not an identity.
    const QString storedParticleGuid = nodeObj["guid"].toString();
    particleNode->setGUID(storedParticleGuid.isEmpty() ? GUIDManager::generateGUID()
                                                       : storedParticleGuid);
    // THE FALLBACKS ARE THE EMITTER'S OWN AUTHORING DEFAULTS
    // (ParticleSystemNode::resetAuthoringDefaults — the reader-defaults law,
    // I-5). SIX of these disagreed with it and every one of them is visible:
    // 1 particle per second against 24, speed 1 against 12, dissipate and
    // random rotation OFF against ON, gravity 1 against 0 and additive
    // blending OFF against ON — so an emitter in a file written before any of
    // those keys existed came back as a slow, dark, static trickle instead of
    // the emitter the Add menu makes.
    particleNode->setPPS(
        (float) nodeObj["particlesPerSecond"].toDouble(particleNode->particlesPerSecond));
    particleNode->setParticleScale(
        (float) nodeObj["particleScale"].toDouble(particleNode->particleScale));
    particleNode->setDissipation(nodeObj["dissipate"].toBool(particleNode->dissipate));
    particleNode->setDissipationInv(nodeObj["dissipateInv"].toBool(particleNode->dissipateInv));
    particleNode->setRandomRotation(
        nodeObj["randomRotation"].toBool(particleNode->randomRotation));
    particleNode->setGravity(
        (float) nodeObj["gravityComplement"].toDouble(particleNode->gravityComplement));
    particleNode->setBlendMode(nodeObj["blendMode"].toBool(particleNode->useAdditive));
    particleNode->setLife((float) nodeObj["lifeLength"].toDouble(particleNode->lifeLength));
    particleNode->setName(nodeObj["name"].toString());
    particleNode->setSpeed((float) nodeObj["speed"].toDouble(particleNode->speed));

    // ---- ParticleFX2 keys (PARTICLES_FX2_SPEC §5) --------------------------
    // ALL OPTIONAL, all defaulted to the legacy behaviour: a scene written
    // before the adoption reads back with exactly the emitter it had. There is
    // no migration and none is needed — this ships as a new app, and the ten
    // legacy keys above map 1:1 onto the engine's emitter and affectors.
    //
    // The three "random" spreads (speedError/lifeError/scaleError) were edited
    // by the panel and never written for ten years (audit defect #7). They are
    // written now, absolute rather than fractional, and absent means 0.
    particleNode->speedError = (float) nodeObj["speedError"].toDouble(particleNode->speedError);
    particleNode->lifeError  = (float) nodeObj["lifeError"].toDouble(particleNode->lifeError);
    particleNode->scaleError = (float) nodeObj["scaleError"].toDouble(particleNode->scaleError);
    particleNode->maxParticles = nodeObj["maxParticles"].toInt(particleNode->maxParticles);

    // The three enums: absent leaves the emitter's own value rather than
    // repeating its spelling here (the reader-defaults law).
    if (nodeObj.contains("shape"))
        particleNode->shape = iris::ParticleSystemNode::shapeFromName(nodeObj["shape"].toString());
    if (nodeObj.contains("orientation"))
        particleNode->orientation =
            iris::ParticleSystemNode::orientationFromName(nodeObj["orientation"].toString());
    if (nodeObj.contains("preset"))
        particleNode->preset =
            iris::ParticleSystemNode::presetFromName(nodeObj["preset"].toString());
    particleNode->coneAngle        = (float) nodeObj["coneAngle"].toDouble(particleNode->coneAngle);
    particleNode->turbulence       = (float) nodeObj["turbulence"].toDouble(particleNode->turbulence);
    particleNode->rotationSpeedMin =
        (float) nodeObj["rotationSpeedMin"].toDouble(particleNode->rotationSpeedMin);
    particleNode->rotationSpeedMax =
        (float) nodeObj["rotationSpeedMax"].toDouble(particleNode->rotationSpeedMax);
    particleNode->burstDuration    =
        (float) nodeObj["burstDuration"].toDouble(particleNode->burstDuration);
    particleNode->burstRepeatDelay =
        (float) nodeObj["burstRepeatDelay"].toDouble(particleNode->burstRepeatDelay);
    particleNode->startDelay       = (float) nodeObj["startDelay"].toDouble(particleNode->startDelay);
    particleNode->alphaHash        = nodeObj["alphaHash"].toBool(particleNode->alphaHash);
    particleNode->distortion       = nodeObj["distortion"].toBool(particleNode->distortion);
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
    // ADDENDUM A-4, tolerant-absent: an old scene has none of these keys and
    // every default is the neutral value, so it loads unchanged.
    if (nodeObj.contains("colourFade1"))
        particleNode->colourFade1 = readColor(nodeObj["colourFade1"].toObject());
    if (nodeObj.contains("colourFade2"))
        particleNode->colourFade2 = readColor(nodeObj["colourFade2"].toObject());
    particleNode->colourFadeSwitch  =
        (float) nodeObj["colourFadeSwitch"].toDouble(particleNode->colourFadeSwitch);
    particleNode->colourRampGuid    = nodeObj["colourRampGuid"].toString();
    particleNode->scaleRate         = (float) nodeObj["scaleRate"].toDouble(particleNode->scaleRate);
    particleNode->scaleRateMultiply =
        nodeObj["scaleRateMultiply"].toBool(particleNode->scaleRateMultiply);

    particleNode->colourKeys.clear();
    for (const QJsonValue &v : nodeObj["colourKeys"].toArray()) {
        const QJsonObject o = v.toObject();
        iris::ParticleColourKey k;
        k.time = (float) o["time"].toDouble(k.time);
        k.r = (float) o["r"].toDouble(k.r); k.g = (float) o["g"].toDouble(k.g);
        k.b = (float) o["b"].toDouble(k.b); k.a = (float) o["a"].toDouble(k.a);
        particleNode->colourKeys.append(k);
    }
    particleNode->scaleKeys.clear();
    for (const QJsonValue &v : nodeObj["scaleKeys"].toArray()) {
        const QJsonObject o = v.toObject();
        iris::ParticleScaleKey k;
        k.time  = (float) o["time"].toDouble(k.time);
        k.scale = (float) o["scale"].toDouble(k.scale);
        particleNode->scaleKeys.append(k);
    }

    if (handle) {
        const QString texturePath = resolveAssetPath(nodeObj["texture"].toString());
        if (!texturePath.isEmpty())
            particleNode->setTexture(iris::Texture2D::load(texturePath));
    }
	particleNode->setVisible(nodeObj["visible"].toBool(particleNode->isVisible()));

    return particleNode;
}


iris::LightType SceneReader::getLightTypeFromName(QString lightType)
{
    if (lightType == "point")       return iris::LightType::Point;
    if (lightType == "directional") return iris::LightType::Directional;
    if (lightType == "spot")        return iris::LightType::Spot;
    if (lightType == "area")        return iris::LightType::Area;
    if (lightType == "sky")         return iris::LightType::Sky;

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
			const QString stored = repairTextureSlot(val.toString(), prop->name);
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
	if (useAlternativeLocation) reader.setSource(TextureSource::GlobalAssets);
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

QString SceneReader::assetCacheKey(const QString &filePath, const QString &assetGuid) const
{
    return filePath + QLatin1Char('|') + MeshBakeStore::settingsHashFor(filePath, assetGuid);
}

void SceneReader::extractAssetsFromAssimpScene(QString filePath, const QString &assetGuid)
{
    const QString cacheKey = assetCacheKey(filePath, assetGuid);
    // The PREWARM is planned from PATHS alone (the open's plan is a path list),
    // so it holds this content's DEFAULT variant. A node whose row asks for
    // different settings must not take it — it would get the other variant's
    // geometry, silently.
    const bool prewarmUsable =
        assetGuid.isEmpty()
        || MeshBakeStore::settingsHashFor(filePath, assetGuid)
               == MeshBakeStore::settingsHashFor(filePath, QString());
    if (!assimpScenes.contains(cacheKey)) {
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
        iris::BakedModelPtr baked = (prewarm && prewarmUsable) ? prewarm->baked(filePath)
                                                               : iris::BakedModelPtr();
        if (!baked) baked = MeshBakeStore::load(filePath, assetGuid);
        if (baked) {
            meshList = baked->meshes;
            animationss = baked->animations;
            for (auto &anim : animationss) anim->source = filePath;
            meshes.insert(cacheKey, meshList);
            assimpScenes.insert(cacheKey);
            animations.insert(cacheKey, animationss);
            return;
        }
        bakeAttempt.stop();   // a miss must not bank the parse below

        // The threaded open parses these on a worker BEFORE the reader runs
        // (irisgl/import/meshprewarm.h): consume that and this whole stage is
        // a copy out of a parsed scene instead of a parse.
        if (prewarm && prewarmUsable) {
            if (const iris::SceneSource *ready = prewarm->source(filePath)) {
                LoadTimeline::Accumulate hit(QStringLiteral("prewarm:sceneReaderHit"));
                iris::GraphicsHelper::loadAllMeshesAndAnimationsFromSource(*ready, filePath,
                                                                          meshList, animationss);
                meshes.insert(cacheKey, meshList);
                assimpScenes.insert(cacheKey);
                animations.insert(cacheKey, animationss);
                return;
            }
        }

        // ONE parse per distinct file per open — and it IS a parse: no bake
        // and no prewarm served this file, so it is read from the store
        // (measured by this counter; the ledger key keeps its historical
        // name). The session store used to be searched for an already
        // parsed scene first, but nothing has registered one there since the
        // asset pipeline — every Object entry is a built fragment — so the
        // search always fell through to this read.
        LoadTimeline::Accumulate parse(QStringLiteral("assimp:sceneReader"));
        // THE ASSET'S IMPORT TRANSFORM (IMPORT-1): a fallback parse stands in
        // for the bake, so it has to produce the same geometry the bake holds —
        // the asset's baked scale, orientation and origin included.
        iris::GraphicsHelper::loadAllMeshesAndAnimationsFromFile(
            filePath, meshList, animationss,
            MeshBakeStore::transformFor(filePath, assetGuid));

        meshes.insert(cacheKey, meshList);
        assimpScenes.insert(cacheKey);
        animations.insert(cacheKey, animationss);
    }
}

/**
 * Returns mesh from mesh file at index
 * if the mesh doesnt exist, nullptr is returned
 * @param filePath
 * @param index
 * @return
 */
iris::MeshPtr SceneReader::getMesh(QString filePath, int index, const QString &assetGuid)
{
    extractAssetsFromAssimpScene(filePath, assetGuid);

    // if the mesh is already in the hashmap then it was already loaded, just return the indexed mesh=
    auto meshList = meshes[assetCacheKey(filePath, assetGuid)];
    if (index < meshList.size()) return meshList[index];

    // maybe the mesh was modified after the file was saved
    return iris::MeshPtr();
}

QString SceneReader::ownModelGuidFor(const QJsonObject &nodeObj, const QString &sourceFileName) const
{
    if (!handle || sourceFileName.isEmpty()) return QString();
    QString found;
    std::function<void(const QJsonObject &)> walk = [&](const QJsonObject &obj) {
        if (!found.isEmpty()) return;
        if (obj.value(QLatin1String("type")).toString() == QLatin1String("mesh")) {
            // A mesh node names its model by the Mesh ROW's guid (the importer
            // rewrites it so); a built-in primitive names a ':/' resource and a
            // pre-store blob a path — neither is a row, and neither is looked up.
            const QString mesh = obj.value(QLatin1String("mesh")).toString();
            if (!mesh.isEmpty() && !mesh.startsWith(QLatin1Char(':'))
                && !mesh.contains(QLatin1Char('/')) && !mesh.contains(QLatin1Char('\\'))) {
                const QString rowName = handle->fetchAsset(mesh).name;
                if (!rowName.isEmpty()
                    && QFileInfo(rowName).fileName().compare(sourceFileName, Qt::CaseInsensitive) == 0)
                    found = mesh;
            }
        }
        for (const auto &child : obj.value(QLatin1String("children")).toArray())
            walk(child.toObject());
    };
    walk(nodeObj);
    return found;
}

iris::SkeletalAnimationPtr SceneReader::getSkeletalAnimation(QString filePath, QString animName,
                                                             const QString &assetGuid,
                                                             const QString &ownModelGuid)
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
        extractAssetsFromAssimpScene(resolvedByGuid, assetGuid);
        auto byGuid = animations[assetCacheKey(resolvedByGuid, assetGuid)];
        for (auto anim : byGuid) anim->source = relPath;
        if (byGuid.contains(animName)) return byGuid[animName];
        if (byGuid.size() == 1) return byGuid.first();
    }
    // The persisted relative source, for a clip saved without a guid (a clip
    // from a loose file on disk, or a model's own clip in an import blob).
    filePath = this->getAbsolutePath(filePath);
    // ...and when that file is gone, the model the clip's OWN subtree was
    // built from, by the guid its mesh nodes carry (ownModelGuidFor). This
    // used to be a catalog-wide by-NAME query for any row in the open project
    // called like the clip's file (Database::fetchAssetGUIDByName, deleted by
    // plan item 15c): it found the right row only when the model had been
    // imported into THIS project, and the wrong one whenever two assets
    // shared a file name.
    if ((filePath.isEmpty() || !QFileInfo::exists(filePath)) && !ownModelGuid.isEmpty()) {
        const QString resolved = resolveAssetPath(ownModelGuid);
        if (!resolved.isEmpty()) filePath = resolved;
    }
    extractAssetsFromAssimpScene(filePath, ownModelGuid);

    auto animMap = animations[assetCacheKey(filePath, ownModelGuid)];

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
    else
        // Nothing resolved at all — the guid, the persisted path and the
        // clip's own model all missed. Say so: the character is at bind pose
        // and this is the only trace of why (15c review #3).
        qWarning() << "getSkeletalAnimation: clip" << animName << "from" << relPath
                   << "could not be resolved (guid" << assetGuid << "/ own model" << ownModelGuid
                   << ") — the node keeps its bind pose";

    return iris::SkeletalAnimationPtr();
}
