/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/skypropertywidget.h"
#include "ui/panels/propertyrows.h"
#include "data/project.h"
#include "irisgl/core/irisutils.h"

#include "ui/controls/colorvaluewidget.h"
#include "ui/controls/colorpickerwidget.h"
#include "ui/controls/texturepickerwidget.h"
#include "ui/controls/labelwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/checkboxwidget.h"

#include "ui/panels/propertywidgets/cubemapwidget.h"
#include "ui/panels/propertywidgets/rowundo.h"

#include "commands/scenepropertycommand.h"
#include "data/database/database.h"
#include "services/subscriber.h"
#include "io/scenewriter.h"
#include "io/scenereader.h"
#include "io/assetmanager.h"

#include "viewport/ieditorviewport.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/worldmodes.h"
#include "services/services.h"
#include "services/selectionservice.h"
#include <QPointer>
#include <QSignalBlocker>
#include <QSqlDatabase>
#include <QTimer>

namespace {
// Sky texture references are asset guids: resolve them through the CAS (the
// pinned bytes in project context, else the library source). The legacy
// projectFolder + row-name join that followed is gone (plan item 15c).
QString resolveSkyAssetFile(Project *project, const QString &guid)
{
    if (guid.isEmpty() || !project) return QString();
    return AssetCas::resolvePinned(QSqlDatabase::database(), AssetStorePaths::root(),
                                   project->getProjectGuid(), guid);
}

// The asset a picked sky image IS. The picker carries the guid of the row the
// user chose or dropped; a path with no guid (only reachable programmatically)
// is recovered through the store object's oid, the way the scene writer does
// it. Never by the file's NAME (plan item 15c): this used to ask the catalog
// for a row CALLED like the picked file, and a store object is called
// <sha256>.<ext>, so a pick resolved only when a preset had copied a file into
// the project folder under the same name as a bare row.
QString skyTextureGuid(Project *project, const QString &path, const QString &carriedGuid)
{
    if (!carriedGuid.isEmpty()) return carriedGuid;
    if (path.isEmpty() || !project || project->getProjectGuid().isEmpty()) return QString();
    return AssetCas::guidForStorePath(QSqlDatabase::database(), AssetStorePaths::root(), path,
                                      project->getProjectGuid(),
                                      AssetCas::GuidPreference::Texture);
}

// The "Material" sky was broken even in the legacy renderer (its handlers were
// commented out and marked BROKEN!), so it is gone from the UI. Old scenes and
// old sky assets that still reference it fall back to a single-colour sky
// (SceneReader does the same). The combo row -> iris::SkyType mapping is this
// table — the enum values are NOT the row indices any more.
const iris::SkyType kSkyRows[] = {
    iris::SkyType::SINGLE_COLOR, iris::SkyType::CUBEMAP, iris::SkyType::EQUIRECTANGULAR,
    iris::SkyType::GRADIENT,     iris::SkyType::REALISTIC,
};
const int kSkyRowCount = int(sizeof(kSkyRows) / sizeof(kSkyRows[0]));
int skyRowFor(iris::SkyType t) {
    for (int i = 0; i < kSkyRowCount; ++i)
        if (kSkyRows[i] == t) return i;
    return 0;
}

/// The key each sky type's block is stored under, in a scene and in an asset.
QString skyDataKey(iris::SkyType type)
{
    switch (type) {
    case iris::SkyType::SINGLE_COLOR:    return QStringLiteral("SingleColor");
    case iris::SkyType::REALISTIC:       return QStringLiteral("Realistic");
    case iris::SkyType::GRADIENT:        return QStringLiteral("Gradient");
    case iris::SkyType::EQUIRECTANGULAR: return QStringLiteral("Equirectangular");
    case iris::SkyType::CUBEMAP:         return QStringLiteral("Cubemap");
    case iris::SkyType::MATERIAL:        break;
    }
    return QStringLiteral("SingleColor");
}
}   // namespace

SkyPropertyWidget::SkyPropertyWidget()
{
	setMouseTracking(true);
}

void SkyPropertyWidget::wireViewportEvents(IEditorViewport *viewport)
{
	// (Phase 4: was a Globals::sceneViewWidget reach — the panel chain
	// injects the viewport instead.)
	if (!viewport || !viewport->events()) return;
	connect(viewport->events(), &EditorViewportEvents::changeSkyFromAssetWidget, this, [this](int index) {
		skyTypeChanged(index);
	});
}

void SkyPropertyWidget::setDatabase(Database *db)
{
    this->db = db;
}

void SkyPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        binding = Binding::Scene;
        skyGuid = scene->skyGuid;
        currentSky = scene->skyType;
        skyTypeChanged(static_cast<int>(scene->skyType));
    } else {
        this->scene.clear();
    }
}

void SkyPropertyWidget::setSkyAlongWithProperties(const QString &guid, iris::SkyType skyType)
{
    binding = Binding::Asset;
    skyGuid = guid;
    currentSky = skyType;
    skyTypeChanged(static_cast<int>(skyType));
}

// The scene whose LIVE fields this panel may write. In Scene binding that is
// the open scene; in Asset binding it is the open scene ONLY while the asset
// being edited is the sky that scene is using — a sky sitting in the library
// renders nothing and must not touch the viewport.
iris::ScenePtr SkyPropertyWidget::liveScene() const
{
    if (!scene) return iris::ScenePtr();
    if (binding == Binding::Scene) return scene;
    return scene->skyGuid == skyGuid ? scene : iris::ScenePtr();
}

QJsonObject SkyPropertyWidget::storedDefinition(iris::SkyType type) const
{
    if (binding == Binding::Scene)
        return scene ? scene->skyData.value(skyDataKey(type)) : QJsonObject();
    // The asset's stored blob is only the definition for the type it was SAVED
    // as: switching an asset to a different sky type starts from that type's
    // defaults, exactly as the asset panel always did.
    if (!db || skyGuid.isEmpty() || currentSky != type) return QJsonObject();
    return QJsonDocument::fromJson(db->fetchAssetData(skyGuid)).object();
}

void SkyPropertyWidget::skyTypeChanged(int index)
{
	// MATERIAL is no longer offered; documents saved with it get a single colour.
	if (static_cast<iris::SkyType>(index) == iris::SkyType::MATERIAL)
		index = static_cast<int>(iris::SkyType::SINGLE_COLOR);
	if (binding == Binding::Asset && skyGuid.isEmpty()) return;

	const iris::SkyType type = static_cast<iris::SkyType>(index);
	const QJsonObject skyDefinition = storedDefinition(type);
	currentSky = type;
	if (binding == Binding::Scene && !!scene) scene->skyType = type;

	loading = true;
	clearPanel(this->layout());
	setMouseTracking(true);

	skySelector = this->addComboBox("Sky Type");
	PropertyRows::identify(skySelector, QStringLiteral("sky.type"),
	                       { QStringLiteral("environment"), QStringLiteral("hdri"),
	                         QStringLiteral("background") });
	skySelector->addItem("Single Color");
	skySelector->addItem("Cubemap");
	skySelector->addItem("Equirectangular");
	skySelector->addItem("Gradient");
	skySelector->addItem("Realistic");
	skySelector->setCurrentIndex(skyRowFor(type));

	// Combo rows are NOT SkyType values (MATERIAL is gone): translate via the table.
	// The rebuild is DEFERRED by one event-loop turn on purpose: skyTypeChanged
	// clearPanel()s, which deletes the very combo whose currentIndexChanged is
	// running (and its popup container, mid-teardown) and then builds fresh
	// widgets in its place. Doing that synchronously crashed the renderer
	// reproducibly (3/3) once the rebuilt panel contained a second QComboBox —
	// the Qt 6.10 + qlementine combo-container hazard CLAUDE.md records, hit
	// from the other end. One queued turn lets the popup finish dying first.
	connect(skySelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged), this, [this](int row) {
		if (loading || row < 0 || row >= kSkyRowCount) return;
		const iris::SkyType picked = kSkyRows[row];
		// The TYPE is a sky edit like any other: one undo step over the whole
		// sky block (Scene binding), then the panel follows the document.
		const QVariant before = sceneprops::get(liveScene(), QStringLiteral("sky"));
		QTimer::singleShot(0, this, [this, picked, before]() {
			skyTypeChanged(static_cast<int>(picked));
			if (binding == Binding::Scene) commitSky(before, tr("Sky Type"));
		});
	});

	switch (type) {
		case iris::SkyType::SINGLE_COLOR: {
			singleColor = this->addColorPicker("Sky Color");

			QColor skyColor = skyDefinition.contains("skyColor")
			                      ? SceneReader::readColor(skyDefinition.value("skyColor").toObject())
			                      : QColor(72, 72, 72);
			singleColor->setColorValue(skyColor);
			singleColorDefinition.insert("skyColor", SceneWriter::jsonColor(skyColor));
			if (auto live = liveScene()) live->skyColor = skyColor;
			updateAssetAndKeys();
			wireSkyRow(singleColor->getPicker(), tr("Sky Colour"),
			           [this](const QVariant &v) { onSingleSkyColorChanged(v.value<QColor>()); });

			break;
		}

		case iris::SkyType::REALISTIC: {
			// THE DIALS ARE THE ENGINE'S (SKY-GPU, owner pick 5). The five
			// Preetham sliders that stood here — Turbidity, Rayleigh, Mie
			// Coefficient, Mie Directional G, Exposure — described a CPU bake
			// that no longer exists; the sky is Ogre's own analytic atmosphere,
			// a fragment shader over the camera ray, and these are ITS
			// parameters. An old document's Realistic block has none of them
			// and opens at the defaults (no migrations, ever).
			const iris::SkyRealistic defaults = iris::SkyRealistic::defaults();
			// THE BLOCK, THROUGH THE DOCUMENT'S OWN READER AND WRITER
			// (SKY-WRITE-1). The per-key defaults, the clamps and the colour
			// fallback were all spelled out again here — a fourth copy, and the
			// one that decided what a value a VERB wrote was worth: the panel
			// re-derived the struct from its stored definition on every bind,
			// so any writer that had kept only one of the two representations
			// was silently corrected at the next selection. There is one
			// representation of the pair now and one function that maps
			// between them; a bind reads and re-writes the SAME fact.
			iris::SkyRealistic loaded = iris::Scene::skyRealisticFromJson(skyDefinition);
			if (auto live = liveScene()) {
				live->setSkyRealistic(loaded);
				loaded = live->skyRealistic;         // as clamped
			} else {
				loaded = iris::Scene::clampSkyRealistic(loaded);
			}

			// NO SUN DIALS (SKY_LIGHT_SPEC.md §3, owner decision D15). The sky's
			// sun IS the scene's sun light: rotate the light and the sky moves.
			skyDensity   = addFloatValueSlider("Density", 0.01f, 1.f, defaults.density);
			skyDiffusion = addFloatValueSlider("Diffusion", 0.f, 4.f, defaults.diffusion);
			skyHorizon   = addFloatValueSlider("Horizon", 0.f, .5f, defaults.horizon);
			skyPower     = addFloatValueSlider("Sky Power", 0.f, 4.f, defaults.power);
			// NOT a sky-look row: it colours the SUNLIGHT and moves no sky pixel
			// (the row above moves the sky and no sunlight — two quantities,
			// two dials, since 2026-09-15).
			sunHaze      = addFloatValueSlider("Sun Haze", 1.f, 10.f, defaults.sunHaze);
			skyColour    = this->addColorPicker("Sky Colour");
			addSunReadoutRow();

			skyDensity->setValue(loaded.density);
			skyDiffusion->setValue(loaded.diffusion);
			skyHorizon->setValue(loaded.horizon);
			skyPower->setValue(loaded.power);
			sunHaze->setValue(loaded.sunHaze);
			skyColour->setColorValue(loaded.skyColour);

			// Each dial writes THROUGH its binding (never a second, direct
			// connect) and each drag is ONE undo step in a scene.
			wireSkyRow(skyDensity, tr("Sky Density"),
			           [this](const QVariant &v) { onSkyDensityChanged(v.toFloat()); });
			wireSkyRow(skyDiffusion, tr("Sky Diffusion"),
			           [this](const QVariant &v) { onSkyDiffusionChanged(v.toFloat()); });
			wireSkyRow(skyHorizon, tr("Sky Horizon"),
			           [this](const QVariant &v) { onSkyHorizonChanged(v.toFloat()); });
			wireSkyRow(skyPower, tr("Sky Power"),
			           [this](const QVariant &v) { onSkyPowerChanged(v.toFloat()); });
			wireSkyRow(sunHaze, tr("Sun Haze"),
			           [this](const QVariant &v) { onSunHazeChanged(v.toFloat()); });
			wireSkyRow(skyColour->getPicker(), tr("Sky Colour"),
			           [this](const QVariant &v) { onSkyColourChanged(v.value<QColor>()); });

			realisticDefinition = iris::Scene::skyRealisticJson(loaded);
			updateAssetAndKeys();

			break;
		}

		case iris::SkyType::EQUIRECTANGULAR: {
			equiTexture = this->addTexturePicker("Equi Map");

			// There are no default values, this definition gets set whenever we change the texture
			setEquiMap(skyDefinition.value("equiSkyGuid").toString());

			connect(equiTexture, &TexturePickerWidget::valuesChanged, this,
			        [this](QString value, QString carriedGuid) {
				if (loading || !db || !project) return;
				const QString assetGuid = skyTextureGuid(project, value, carriedGuid);
				const QVariant before = sceneprops::get(liveScene(), QStringLiteral("sky"));
				db->removeDependenciesByType(skyGuid, ModelTypes::Texture);
				if (!assetGuid.isEmpty()) {
					db->createDependency(
						static_cast<int>(ModelTypes::Sky),		// Not really but who cares
						static_cast<int>(ModelTypes::Texture),
						skyGuid, assetGuid,
						project->getProjectGuid()
					);

					onEquiTextureChanged(assetGuid);
					if (binding == Binding::Scene) commitSky(before, tr("Sky Image"));
				}
			});

			break;
		}

		case iris::SkyType::CUBEMAP: {
			cubeMapWidget = this->addCubeMapWidget();

			connect(cubeMapWidget, &CubeMapWidget::valuesChanged, this,
			        [this](QString value, QString guid, CubeMapPosition pos) {
				onSlotChanged(value, guid, static_cast<int>(pos));
			});

			setSkyMap(skyDefinition);

			break;
		}

		case iris::SkyType::MATERIAL:
			// Unreachable (coerced to SINGLE_COLOR above); kept for -Wswitch.
			break;

		case iris::SkyType::GRADIENT: {
			colorTop = this->addColorPicker("Top Color");
			colorMid = this->addColorPicker("Middle Color");
			colorBot = this->addColorPicker("Bottom Color");
			offset = this->addFloatValueSlider("Offset", 0.01, .9f, .73f);

			const bool fresh = skyDefinition.isEmpty();
			const QColor top = fresh ? QColor(255, 146, 138)
			                         : SceneReader::readColor(skyDefinition.value("gradientTop").toObject());
			const QColor mid = fresh ? QColor("white")
			                         : SceneReader::readColor(skyDefinition.value("gradientMid").toObject());
			const QColor bot = fresh ? QColor(64, 128, 255)
			                         : SceneReader::readColor(skyDefinition.value("gradientBot").toObject());
			const float off = fresh ? .73f : float(skyDefinition.value("gradientOffset").toDouble());
			colorTop->setColorValue(top);
			colorMid->setColorValue(mid);
			colorBot->setColorValue(bot);
			offset->setValue(off);

			gradientDefinition.insert("gradientTop", SceneWriter::jsonColor(top));
			gradientDefinition.insert("gradientMid", SceneWriter::jsonColor(mid));
			gradientDefinition.insert("gradientBot", SceneWriter::jsonColor(bot));
			gradientDefinition.insert("gradientOffset", off);

			if (auto live = liveScene()) {
				live->gradientTop = top;
				live->gradientMid = mid;
				live->gradientBot = bot;
				live->gradientOffset = off;
			}
			updateAssetAndKeys();

			wireSkyRow(colorTop->getPicker(), tr("Sky Gradient"),
			           [this](const QVariant &v) { onGradientTopColorChanged(v.value<QColor>()); });
			wireSkyRow(colorMid->getPicker(), tr("Sky Gradient"),
			           [this](const QVariant &v) { onGradientMidColorChanged(v.value<QColor>()); });
			wireSkyRow(colorBot->getPicker(), tr("Sky Gradient"),
			           [this](const QVariant &v) { onGradientBotColorChanged(v.value<QColor>()); });
			wireSkyRow(offset, tr("Sky Gradient Offset"),
			           [this](const QVariant &v) { onGradientOffsetChanged(v.toFloat()); });

			break;
		}
	}

	loading = false;
}

// ONE UNDO STEP PER SKY GESTURE (debt L6). The sky is recorded WHOLE — type,
// the per-type blobs and the live colour / gradient / analytic fields — because
// a row here writes the blob AND the live field, and an undo that put back only
// one of them would leave the panel showing a sky the renderer is not drawing.
//
// THE ROW'S WRITE GOES THROUGH HERE, and nowhere else: a row that ALSO had its
// slot connected to the control directly wrote the document BEFORE rowundo
// could snapshot it, so an unbracketed tick (a keyboard arrow, a typed value)
// compared equal to itself and was dropped from the undo history (code review).
// Both bindings write; only a SCENE binding commits — library asset content has
// no undo history to commit to.
void SkyPropertyWidget::wireSkyRow(QWidget *row, const QString &text,
                                   const std::function<void(const QVariant &)> &write)
{
    if (!row || !write) return;
    rowundo::Binding b;
    b.guard = [this]() { return !loading; };
    b.read = [this]() { return sceneprops::get(liveScene(), QStringLiteral("sky")); };
    b.write = write;
    if (binding == Binding::Scene)
        b.commit = [this, text](const QVariant &before, const QVariant &) {
            commitSky(before, text);
        };
    if (auto *slider = qobject_cast<HFloatSliderWidget *>(row))      rowundo::bind(slider, b);
    else if (auto *picker = qobject_cast<ColorPickerWidget *>(row))  rowundo::bind(picker, b);
}

void SkyPropertyWidget::commitSky(const QVariant &before, const QString &text)
{
    auto live = liveScene();
    if (!live || binding != Binding::Scene) return;
    QPointer<SkyPropertyWidget> self(this);
    panelundo::pushSceneEdit(services, live, QStringLiteral("sky"), text, before,
                             sceneprops::get(live, QStringLiteral("sky")), [self]() {
                                 // The rows ARE the sky: repaint them from the
                                 // document the undo just restored.
                                 if (self && !self->scene.isNull())
                                     self->skyTypeChanged(int(self->scene->skyType));
                             });
}

// THE SUN READOUT (SKY_LIGHT_SPEC.md §3). Not a control — the analytic sky has
// no dial of its own any more. It names the light the sky is taking its sun
// from, so a user who rotates the wrong light (or has no directional light at
// all, which bakes the model's night) is told where the sun actually comes from.
void SkyPropertyWidget::addSunReadoutRow()
{
	sunReadout = nullptr;
	auto live = liveScene();
	if (!live) return;
	const auto sun = live->sunLight();
	sunReadout = this->addLabel(
		"Sun",
		sun ? tr("%1 — rotate it to move the sun").arg(sun->getName())
		    : tr("none — add a directional light"));
	if (!sunReadout) return;
	sunReadout->setToolTip(QStringLiteral(
		"The sky's sun is the scene's SUN — its primary directional light (the lowest Forward "
		"Shading Priority; the World panel's Sun row says which one and lets you pin another). "
		"Rotate that light and this sky's sun, its tint (the air's transmittance at that elevation) and the sun disc all follow. A scene with "
		"no directional light has no sun, and the analytic sky bakes its own night."));
}

void SkyPropertyWidget::onSlotChanged(QString value, QString guid, int index)
{
	if (!db || !project) return;
	const QVariant before = sceneprops::get(liveScene(), QStringLiteral("sky"));
	// The face's asset is the guid the slot CARRIES (a drop, a pick, the sky
	// presets' pinned faces), else the store object behind the path — never a
	// row found by the file's name (plan item 15c). An empty value clears the
	// face.
	static const char *const kFaces[] = { "front", "back", "left", "right", "top", "bottom" };
	const QString face = (index >= 0 && index < 6) ? QLatin1String(kFaces[index]) : QString();
	const QString assetGuid = value.isEmpty() ? QString() : skyTextureGuid(project, value, guid);
	// The dependency the face HAD goes (it used to delete the edge to the NEW
	// guid, just before re-creating it, and leave the replaced face's edge
	// behind forever).
	const QString previous = face.isEmpty() ? QString() : cubeMapDefinition.value(face).toString();
	if (!previous.isEmpty() && previous != assetGuid) db->deleteDependency(skyGuid, previous);
	if (!assetGuid.isEmpty()) {
		db->createDependency(
			static_cast<int>(ModelTypes::Sky),
			static_cast<int>(ModelTypes::Texture),
			skyGuid,
			assetGuid,
			project->getProjectGuid()
		);
	}

	switch (index) {
		case 0: { cubeMapDefinition.insert("front", !assetGuid.isEmpty() ? assetGuid : QString()); break; }
		case 1: { cubeMapDefinition.insert("back",	!assetGuid.isEmpty() ? assetGuid : QString()); break; }
		case 2: { cubeMapDefinition.insert("left",	!assetGuid.isEmpty() ? assetGuid : QString()); break; }
		case 3: { cubeMapDefinition.insert("right",	!assetGuid.isEmpty() ? assetGuid : QString()); break; }
		case 4: { cubeMapDefinition.insert("top",	!assetGuid.isEmpty() ? assetGuid : QString()); break; }
		case 5: { cubeMapDefinition.insert("bottom",!assetGuid.isEmpty() ? assetGuid : QString()); break; }
		default: break;
	}

	setSkyMap(cubeMapDefinition);
	if (binding == Binding::Scene && !loading) commitSky(before, tr("Sky Cubemap"));
}

// Flushes the per-type definition back to wherever it came from: the scene's
// skyData block, or the library row (which is written when the panel is hidden
// — "let's repurpose this event and use it to update the asset in the db",
// iKlsR, and it is still the only moment an asset edit is durable).
void SkyPropertyWidget::updateAssetAndKeys()
{
	QJsonObject skyProperties;
	const QJsonObject *source = nullptr;
	switch (currentSky) {
	case iris::SkyType::SINGLE_COLOR:    source = &singleColorDefinition; break;
	case iris::SkyType::REALISTIC:       source = &realisticDefinition; break;
	case iris::SkyType::EQUIRECTANGULAR: source = &equiSkyDefinition; break;
	case iris::SkyType::CUBEMAP:         source = &cubeMapDefinition; break;
	case iris::SkyType::GRADIENT:        source = &gradientDefinition; break;
	case iris::SkyType::MATERIAL:        break;
	}
	if (!source) return;
	for (const QString &key : source->keys()) skyProperties.insert(key, source->value(key));

	if (binding == Binding::Scene) {
		if (scene) scene->skyData.insert(skyDataKey(currentSky), skyProperties);
		return;
	}

	if (!db || skyGuid.isEmpty()) return;
	QJsonObject properties;
	QJsonObject skyProps;
	skyProps.insert("type", static_cast<int>(currentSky));
	properties.insert("sky", skyProps);
	skyProperties.insert("guid", skyGuid);
	db->updateAssetAsset(skyGuid, QJsonDocument(skyProperties).toJson());
	db->updateAssetProperties(skyGuid, QJsonDocument(properties).toJson());
	// Not really used but keep around for now, the intent is clear (iKlsR)
	if (eventBus) emit eventBus->updateAssetSkyItemFromSkyPropertyWidget(skyGuid, currentSky);
}

void SkyPropertyWidget::hideEvent(QHideEvent *event)
{
	Q_UNUSED(event)
	// The ASSET's edits become durable here; a scene's are already on the
	// document (and saved with it).
	if (binding == Binding::Asset) updateAssetAndKeys();
}

void SkyPropertyWidget::setEquiMap(const QString &guid)
{
    if (guid.isEmpty()) return;
	equiSkyDefinition.insert("equiSkyGuid", guid);
    auto image = resolveSkyAssetFile(project, guid);
    if (equiTexture) equiTexture->setTexture(QFileInfo(image).isFile() ? image : QString());
    if (auto live = liveScene()) live->setSkyTexture(iris::Texture2D::load(image, false));
	updateAssetAndKeys();
}

void SkyPropertyWidget::setSkyMap(const QJsonObject &skyDataDefinition)
{
	auto front = resolveSkyAssetFile(project, skyDataDefinition["front"].toString());
	auto back = resolveSkyAssetFile(project, skyDataDefinition["back"].toString());
	auto left = resolveSkyAssetFile(project, skyDataDefinition["left"].toString());
	auto right = resolveSkyAssetFile(project, skyDataDefinition["right"].toString());
	auto top = resolveSkyAssetFile(project, skyDataDefinition["top"].toString());
	auto bottom = resolveSkyAssetFile(project, skyDataDefinition["bottom"].toString());

	cubeMapDefinition.insert("front", skyDataDefinition["front"].toString());
	cubeMapDefinition.insert("back", skyDataDefinition["back"].toString());
	cubeMapDefinition.insert("left", skyDataDefinition["left"].toString());
	cubeMapDefinition.insert("right", skyDataDefinition["right"].toString());
	cubeMapDefinition.insert("top", skyDataDefinition["top"].toString());
	cubeMapDefinition.insert("bottom", skyDataDefinition["bottom"].toString());

	if (cubeMapWidget) cubeMapWidget->addCubeMapImages(top, bottom, left, front, right, back);

	// We need at least one valid image to get some metadata from
	QImage *info = nullptr;
	bool useTex = false;
	QVector<QString> sides = { front, back, left, right, top, bottom };
	for (const auto &image : sides) {
		if (!image.isEmpty() && QFileInfo(image).isFile()) {
			info = new QImage(image);
			useTex = true;
			break;
		}
	}

	if (useTex) {
		if (auto live = liveScene())
			live->setSkyTexture(iris::Texture2D::createCubeMap(front, back, top, bottom, left, right, info));
		updateAssetAndKeys();
	}
	delete info;
}

void SkyPropertyWidget::onSingleSkyColorChanged(QColor color)
{
	singleColorDefinition.insert("skyColor", SceneWriter::jsonColor(color));
	if (auto live = liveScene()) live->skyColor = color;
	updateAssetAndKeys();
}

void SkyPropertyWidget::onEquiTextureChanged(QString guid)
{
	equiSkyDefinition.insert("equiSkyGuid", guid);
	setEquiMap(guid);
	updateAssetAndKeys();
}

void SkyPropertyWidget::onSkyDensityChanged(float val)
{
	writeRealisticDial([val](iris::SkyRealistic &r) { r.density = val; });
}

void SkyPropertyWidget::onSkyDiffusionChanged(float val)
{
	writeRealisticDial([val](iris::SkyRealistic &r) { r.diffusion = val; });
}

void SkyPropertyWidget::onSkyHorizonChanged(float val)
{
	writeRealisticDial([val](iris::SkyRealistic &r) { r.horizon = val; });
}

void SkyPropertyWidget::onSkyPowerChanged(float val)
{
	writeRealisticDial([val](iris::SkyRealistic &r) { r.power = val; });
}

void SkyPropertyWidget::onSunHazeChanged(float val)
{
	writeRealisticDial([val](iris::SkyRealistic &r) { r.sunHaze = val; });
}

void SkyPropertyWidget::onSkyColourChanged(QColor colour)
{
	writeRealisticDial([colour](iris::SkyRealistic &r) { r.skyColour = colour; });
}

/// ONE DIAL, ONE WRITE (SKY-WRITE-1). Every realistic row used to insert its
/// own JSON key AND assign its own struct field — six copies of the pair, and
/// six chances to keep only one half. The row says which field it is; the
/// document owns the clamp and both representations; the definition this widget
/// carries is re-read from the document afterwards, so a clamped value shows up
/// in the stored block too.
void SkyPropertyWidget::writeRealisticDial(const std::function<void(iris::SkyRealistic &)> &edit)
{
	iris::SkyRealistic r = iris::Scene::skyRealisticFromJson(realisticDefinition);
	if (auto live = liveScene()) r = live->skyRealistic;
	edit(r);
	if (auto live = liveScene()) {
		live->setSkyRealistic(r);
		r = live->skyRealistic;
	} else {
		r = iris::Scene::clampSkyRealistic(r);
	}
	realisticDefinition = iris::Scene::skyRealisticJson(r);
	updateAssetAndKeys();
}

void SkyPropertyWidget::onGradientTopColorChanged(QColor color)
{
	gradientDefinition.insert("gradientTop", SceneWriter::jsonColor(color));
	if (auto live = liveScene()) live->gradientTop = color;
	updateAssetAndKeys();
}

void SkyPropertyWidget::onGradientMidColorChanged(QColor color)
{
	gradientDefinition.insert("gradientMid", SceneWriter::jsonColor(color));
	if (auto live = liveScene()) live->gradientMid = color;
	updateAssetAndKeys();
}

void SkyPropertyWidget::onGradientBotColorChanged(QColor color)
{
	gradientDefinition.insert("gradientBot", SceneWriter::jsonColor(color));
	if (auto live = liveScene()) live->gradientBot = color;
	updateAssetAndKeys();
}

void SkyPropertyWidget::onGradientOffsetChanged(float offset)
{
	gradientDefinition.insert("gradientOffset", offset);
	if (auto live = liveScene()) live->gradientOffset = offset;
	updateAssetAndKeys();
}
