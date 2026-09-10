/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/skypropertywidget.h"
#include "data/project.h"
#include "irisgl/core/irisutils.h"

#include "ui/controls/colorvaluewidget.h"
#include "ui/controls/colorpickerwidget.h"
#include "ui/controls/texturepickerwidget.h"
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
#include "services/sunlink.h"
#include <QPointer>
#include <QSignalBlocker>
#include <QSqlDatabase>
#include <QTimer>

namespace {
// Sky texture references are asset guids: resolve them through the CAS
// (pinned bytes in project context, then library source) before falling back
// to the legacy projectFolder/name join (pre-pipeline projects).
QString resolveSkyAssetFile(Project *project, Database *db, const QString &guid)
{
    if (guid.isEmpty() || !project) return QString();
    QSqlDatabase conn = QSqlDatabase::database();
    QString path = AssetCas::resolvePinned(conn, AssetStorePaths::root(),
                                           project->getProjectGuid(), guid);
    if (path.isEmpty()) path = AssetCas::resolveSource(conn, AssetStorePaths::root(), guid);
    if (path.isEmpty() && db)
        path = IrisUtils::join(project->getProjectFolder(), db->fetchAsset(guid).name);
    return path;
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
			// Ranges are the MODEL's, not the legacy panel's (VISUAL_PARITY_SPEC
			// item 1). The old rows — Sun Height -0.99..10, Strafe X/Z 0..1,
			// Turbidity 0..1 — sat in a corner where the analytic sky cannot
			// react: its sunfade divides sunPosY by 450000 and Preetham's
			// turbidity band is 1..20. The sun is a polar control now (the three
			// stored sunPos floats remain the truth, at the model's radius), and
			// every scattering dial spans the range the bake actually uses.
			const iris::SkyRealistic defaults = iris::SkyRealistic::defaults();
			iris::SkyRealistic loaded = defaults;
			if (!skyDefinition.isEmpty()) {
				loaded.luminance       = skyDefinition.value("luminance").toDouble(defaults.luminance);
				loaded.reileigh        = skyDefinition.value("reileigh").toDouble(defaults.reileigh);
				loaded.mieCoefficient  = skyDefinition.value("mieCoefficient").toDouble(defaults.mieCoefficient);
				loaded.mieDirectionalG = skyDefinition.value("mieDirectionalG").toDouble(defaults.mieDirectionalG);
				loaded.turbidity       = skyDefinition.value("turbidity").toDouble(defaults.turbidity);
				loaded.sunPosX         = skyDefinition.value("sunPosX").toDouble(defaults.sunPosX);
				loaded.sunPosY         = skyDefinition.value("sunPosY").toDouble(defaults.sunPosY);
				loaded.sunPosZ         = skyDefinition.value("sunPosZ").toDouble(defaults.sunPosZ);
			}
			// Legacy documents hold values outside the model's ranges (turbidity
			// .32 against Preetham's 1..20 was the panel default for years). The
			// sliders would clamp the DISPLAY and silently disagree with the
			// document, so clamp the document instead: an old scene migrates to
			// the nearest value its dials can actually express. (The ASSET panel
			// never did this — one of the divergences the dedup closes.)
			loaded.turbidity       = qBound(1.0f,  loaded.turbidity,       20.0f);
			loaded.reileigh        = qBound(0.0f,  loaded.reileigh,         4.0f);
			loaded.mieCoefficient  = qBound(0.0f,  loaded.mieCoefficient,   0.1f);
			loaded.mieDirectionalG = qBound(0.0f,  loaded.mieDirectionalG, 0.99f);
			loaded.luminance       = qBound(0.01f, loaded.luminance,        2.0f);
			loaded.setSunAngles(loaded.sunAzimuth(), qBound(-10.0f, loaded.sunElevation(), 90.0f));
			if (auto live = liveScene()) live->skyRealistic = loaded;

			sunAzimuth = addFloatValueSlider("Sun Azimuth", 0.f, 360.f, defaults.sunAzimuth());
			sunElevation = addFloatValueSlider("Sun Elevation", -10.f, 90.f, defaults.sunElevation());
			turbidity = addFloatValueSlider("Turbidity", 1.f, 20.f, defaults.turbidity);
			reileigh = addFloatValueSlider("Rayleigh Scattering", 0.f, 4.f, defaults.reileigh);
			mieCoefficient = addFloatValueSlider("Mie Coefficient", 0.f, .1f, defaults.mieCoefficient);
			mieDirectionalG = addFloatValueSlider("Mie Directional G", 0.f, .99f, defaults.mieDirectionalG);
			luminance = addFloatValueSlider("Exposure", .01f, 2.f, defaults.luminance);
			if (binding == Binding::Scene) {
				skyDetail = this->addComboBox("Sky Detail");
				skyDetail->addItem("Normal (256)");
				skyDetail->addItem("High (512)");
				skyDetail->addItem("Ultra (1024)");
				skyDetail->setToolTip(QStringLiteral(
					"Width of the equirectangular image the sky is baked into. Higher is a sharper "
					"sun disc on a big display, at the cost of a longer bake on every change."));
				skyDetail->setCurrentIndex(scene->skyBakeResolution >= 1024 ? 2
										 : scene->skyBakeResolution >= 512  ? 1 : 0);
				connect(skyDetail, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
						this, &SkyPropertyWidget::onSkyDetailChanged);
			}
			addSunLinkRow();

			sunAzimuth->setValue(loaded.sunAzimuth());
			sunElevation->setValue(loaded.sunElevation());
			turbidity->setValue(loaded.turbidity);
			reileigh->setValue(loaded.reileigh);
			mieCoefficient->setValue(loaded.mieCoefficient);
			mieDirectionalG->setValue(loaded.mieDirectionalG);
			luminance->setValue(loaded.luminance);

			// Each dial writes THROUGH its binding (never a second, direct
			// connect) and each drag is ONE undo step in a scene.
			wireSkyRow(luminance, tr("Sky Exposure"),
			           [this](const QVariant &v) { onLuminanceChanged(v.toFloat()); });
			wireSkyRow(reileigh, tr("Rayleigh Scattering"),
			           [this](const QVariant &v) { onReileighChanged(v.toFloat()); });
			wireSkyRow(mieCoefficient, tr("Mie Coefficient"),
			           [this](const QVariant &v) { onMieCoeffGChanged(v.toFloat()); });
			wireSkyRow(mieDirectionalG, tr("Mie Directional G"),
			           [this](const QVariant &v) { onMieDireChanged(v.toFloat()); });
			wireSkyRow(turbidity, tr("Turbidity"),
			           [this](const QVariant &v) { onTurbidityChanged(v.toFloat()); });
			wireSkyRow(sunAzimuth, tr("Sun Azimuth"),
			           [this](const QVariant &v) { onSunAzimuthChanged(v.toFloat()); });
			wireSkyRow(sunElevation, tr("Sun Elevation"),
			           [this](const QVariant &v) { onSunElevationChanged(v.toFloat()); });

			// The stored blob is still sunPos*: azimuth/elevation are a view of it.
			realisticDefinition.insert("luminance", double(loaded.luminance));
			realisticDefinition.insert("reileigh", double(loaded.reileigh));
			realisticDefinition.insert("mieCoefficient", double(loaded.mieCoefficient));
			realisticDefinition.insert("mieDirectionalG", double(loaded.mieDirectionalG));
			realisticDefinition.insert("turbidity", double(loaded.turbidity));
			realisticDefinition.insert("sunPosX", double(loaded.sunPosX));
			realisticDefinition.insert("sunPosY", double(loaded.sunPosY));
			realisticDefinition.insert("sunPosZ", double(loaded.sunPosZ));
			updateAssetAndKeys();

			break;
		}

		case iris::SkyType::EQUIRECTANGULAR: {
			equiTexture = this->addTexturePicker("Equi Map");

			// There are no default values, this definition gets set whenever we change the texture
			setEquiMap(skyDefinition.value("equiSkyGuid").toString());

			connect(equiTexture, &TexturePickerWidget::valueChanged, this, [this](QString value) {
				if (loading || !db || !project) return;
				// Remember that asset names are unique (auto incremented) so this is fine
				QString assetGuid = db->fetchAssetGUIDByName(QFileInfo(value).fileName(), project->getProjectGuid());
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

	addAmbientFromSkyRow();
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

void SkyPropertyWidget::addAmbientFromSkyRow()
{
	// VISUAL_PARITY_SPEC item 3b. Only offered where there is a sky to
	// integrate: a single-colour sky has no hemispheres, so it always falls back
	// to the flat World > Ambient Color. Scene binding only — it is a world
	// setting, not a property of a sky in the library.
	if (binding != Binding::Scene || !scene || scene->skyType == iris::SkyType::SINGLE_COLOR) {
		ambientFromSky = nullptr;
		return;
	}
	ambientFromSky = this->addCheckBox("Ambient From Sky", scene->ambientFromSky);
	// DEFECT (pre-existing, accordionbladewidget.cpp:216): addCheckBox drops its
	// `value` argument on the floor — every caller has to set it afterwards.
	ambientFromSky->setValue(scene->ambientFromSky);
	ambientFromSky->setToolTip(QStringLiteral(
		"Light the scene's ambient with the sky itself: the upper and lower hemisphere colours "
		"are integrated from the sky image, so a red sky reddens what it lights. World > Ambient "
		"Color then sets the strength and tint of that instead of being the ambient itself."));
	connect(ambientFromSky, &CheckBoxWidget::valueChanged,
			this, &SkyPropertyWidget::onAmbientFromSkyChanged);
}

void SkyPropertyWidget::addSunLinkRow()
{
	// Sun coupling (re-audit F5). Realistic sky only: it is the one sky that
	// HAS a sun, so the row is built inside that case and nowhere else — and,
	// in Asset binding, only while the asset IS the open scene's sky (a library
	// sky has no scene and therefore no light to drive).
	sunDrivesLight = nullptr;
	auto live = liveScene();
	if (!live) return;
	sunDrivesLight = this->addCheckBox("Drive Selected Directional Light",
									   !live->sunLightGuid.isEmpty());
	// addCheckBox drops its `value` argument (accordionbladewidget.cpp:216) —
	// the same pre-existing defect the Ambient From Sky row works around.
	sunDrivesLight->setValue(!live->sunLightGuid.isEmpty());
	sunDrivesLight->setToolTip(QStringLiteral(
		"Point a directional light down the sky's sun: the Sun Azimuth and Sun Elevation dials "
		"then drive its rotation, so the shadows and the lighting follow the sky. Uses the "
		"selected directional light, or the scene's first one if the selection is something "
		"else. Turning it off gives the light back the rotation it had before it was linked."));
	connect(sunDrivesLight, &CheckBoxWidget::valueChanged,
			this, &SkyPropertyWidget::onSunDrivesLightChanged);
}

void SkyPropertyWidget::onSunDrivesLightChanged(bool on)
{
	auto live = liveScene();
	if (loading || !live) return;
	const iris::SceneNodePtr selected =
		(services && services->selection) ? services->selection->selected() : iris::SceneNodePtr();
	// sunlink::setDriven records its OWN undo step (SunLightLinkCommand) — the
	// same one world.sunLight pushes.
	const QString linked = sunlink::setDriven(live, services, on, selected);
	// A scene with no directional light cannot honour the request: put the box
	// back rather than leaving it showing a coupling that does not exist.
	if (on && linked.isEmpty() && sunDrivesLight) {
		QSignalBlocker block(sunDrivesLight);
		sunDrivesLight->setValue(false);
	}
}

void SkyPropertyWidget::onAmbientFromSkyChanged(bool on)
{
	if (loading || !scene || binding != Binding::Scene) return;
	// A direct edit of a backing field is a World Mode PIN (POST_CHAIN_SPEC
	// §9.1), so this row is a registry edit: value AND pin, one command.
	panelundo::runWorldModeEdit(services, scene, tr("Ambient From Sky"), [this, on]() {
		worldmodes::setRowValue(scene, QStringLiteral("ambientFromSky"), on ? 1 : 0);
	}, [this]() {
		if (ambientFromSky && scene) {
			QSignalBlocker quiet(ambientFromSky);
			ambientFromSky->setValue(scene->ambientFromSky);
		}
	});
}

void SkyPropertyWidget::onSkyDetailChanged(int row)
{
	if (loading || !scene || binding != Binding::Scene) return;
	// Not a sky *parameter* (it never enters skyData): a scene render setting,
	// serialized beside antiAliasing, and a World Mode registry row — so the
	// edit carries the value and the pin. SceneMirror re-bakes when it changes.
	const int resolution = row >= 2 ? 1024 : row >= 1 ? 512 : 256;
	panelundo::runWorldModeEdit(services, scene, tr("Sky Detail"), [this, resolution]() {
		worldmodes::setRowValue(scene, QStringLiteral("skyBakeResolution"), resolution);
	}, [this]() {
		if (skyDetail && scene) {
			QSignalBlocker quiet(skyDetail->getWidget());
			skyDetail->setCurrentIndex(scene->skyBakeResolution >= 1024 ? 2
									 : scene->skyBakeResolution >= 512  ? 1 : 0);
		}
	});
}

void SkyPropertyWidget::onSlotChanged(QString value, QString guid, int index)
{
	if (!db || !project) return;
	const QVariant before = sceneprops::get(liveScene(), QStringLiteral("sky"));
	// Normally there'd be a check for if value is empty here but in that case we can clear the guid
	QString assetGuid = db->fetchAssetGUIDByName(QFileInfo(value).fileName(), project->getProjectGuid());
	db->deleteDependency(skyGuid, guid);
	// Remember that asset names are unique (auto incremented) so this is fine
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
    auto image = resolveSkyAssetFile(project, db, guid);
    if (equiTexture) equiTexture->setTexture(QFileInfo(image).isFile() ? image : QString());
    if (auto live = liveScene()) live->setSkyTexture(iris::Texture2D::load(image, false));
	updateAssetAndKeys();
}

void SkyPropertyWidget::setSkyMap(const QJsonObject &skyDataDefinition)
{
	auto front = resolveSkyAssetFile(project, db, skyDataDefinition["front"].toString());
	auto back = resolveSkyAssetFile(project, db, skyDataDefinition["back"].toString());
	auto left = resolveSkyAssetFile(project, db, skyDataDefinition["left"].toString());
	auto right = resolveSkyAssetFile(project, db, skyDataDefinition["right"].toString());
	auto top = resolveSkyAssetFile(project, db, skyDataDefinition["top"].toString());
	auto bottom = resolveSkyAssetFile(project, db, skyDataDefinition["bottom"].toString());

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

void SkyPropertyWidget::onReileighChanged(float val)
{
	realisticDefinition.insert("reileigh", val);
	if (auto live = liveScene()) live->skyRealistic.reileigh = val;
	updateAssetAndKeys();
}

void SkyPropertyWidget::onLuminanceChanged(float val)
{
	realisticDefinition.insert("luminance", val);
	if (auto live = liveScene()) live->skyRealistic.luminance = val;
	updateAssetAndKeys();
}

void SkyPropertyWidget::onTurbidityChanged(float val)
{
	realisticDefinition.insert("turbidity", val);
	if (auto live = liveScene()) live->skyRealistic.turbidity = val;
	updateAssetAndKeys();
}

void SkyPropertyWidget::onMieCoeffGChanged(float val)
{
	realisticDefinition.insert("mieCoefficient", val);
	if (auto live = liveScene()) live->skyRealistic.mieCoefficient = val;
	updateAssetAndKeys();
}

void SkyPropertyWidget::onMieDireChanged(float val)
{
	realisticDefinition.insert("mieDirectionalG", val);
	if (auto live = liveScene()) live->skyRealistic.mieDirectionalG = val;
	updateAssetAndKeys();
}

// Azimuth/elevation are a lossless view of the three stored sunPos floats — the
// panel edits the angles, the document (and the saved blob) keeps the vector.
void SkyPropertyWidget::writeSunAngles()
{
	if (!sunAzimuth || !sunElevation) return;
	iris::SkyRealistic sun;
	if (auto live = liveScene()) sun = live->skyRealistic;
	sun.setSunAngles(sunAzimuth->getValue(), sunElevation->getValue());
	realisticDefinition.insert("sunPosX", double(sun.sunPosX));
	realisticDefinition.insert("sunPosY", double(sun.sunPosY));
	realisticDefinition.insert("sunPosZ", double(sun.sunPosZ));
	if (auto live = liveScene()) live->skyRealistic = sun;
	updateAssetAndKeys();
}

void SkyPropertyWidget::onSunAzimuthChanged(float)   { writeSunAngles(); }
void SkyPropertyWidget::onSunElevationChanged(float) { writeSunAngles(); }

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
