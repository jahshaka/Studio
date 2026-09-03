/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/worldskypropertywidget.h"
#include "data/project.h"
#include "irisgl/core/irisutils.h"


#include "ui/controls/colorvaluewidget.h"
#include "ui/controls/colorpickerwidget.h"
#include "ui/controls/texturepickerwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/checkboxwidget.h"

#include "ui/panels/propertywidgets/cubemapwidget.h"

#include "data/database/database.h"
#include "services/subscriber.h"
#include "io/scenewriter.h"
#include "io/scenereader.h"
#include "io/assetmanager.h"

#include "viewport/ieditorviewport.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/worldmodes.h"
#include <QSqlDatabase>
#include <QTimer>

namespace {
// Sky texture references are asset guids: resolve them through the CAS
// (pinned bytes in project context, then library source) before falling back
// to the legacy projectFolder/name join (pre-pipeline projects).
QString resolveSkyAssetFile(Project *project, Database *db, const QString &guid)
{
    if (guid.isEmpty()) return QString();
    QSqlDatabase conn = QSqlDatabase::database();
    QString path = AssetCas::resolvePinned(conn, AssetStorePaths::root(),
                                           project->getProjectGuid(), guid);
    if (path.isEmpty()) path = AssetCas::resolveSource(conn, AssetStorePaths::root(), guid);
    if (path.isEmpty()) path = IrisUtils::join(project->getProjectFolder(), db->fetchAsset(guid).name);
    return path;
}
} // namespace

namespace {
// The "Material" sky was broken even in the legacy renderer (its handlers are
// commented out below, marked BROKEN!), so it is gone from the UI. Old scenes
// that still reference it fall back to a single-colour sky (SceneReader does the
// same). The combo row -> iris::SkyType mapping is this table — the enum values
// are NOT the row indices any more.
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
}

WorldSkyPropertyWidget::WorldSkyPropertyWidget()
{
}

void WorldSkyPropertyWidget::wireViewportEvents(IEditorViewport *viewport)
{
	// (Phase 4: was a Globals::sceneViewWidget reach — the panel chain
	// injects the viewport instead.)
	if (!viewport || !viewport->events()) return;
	connect(viewport->events(), &EditorViewportEvents::changeSkyFromAssetWidget, this, [this](int index) {
		skyTypeChanged(index);
	});
}

void WorldSkyPropertyWidget::skyTypeChanged(int index)
{
	// MATERIAL is no longer offered; scenes saved with it get a single colour.
	if (static_cast<iris::SkyType>(index) == iris::SkyType::MATERIAL)
		index = static_cast<int>(iris::SkyType::SINGLE_COLOR);
	scene->skyType = static_cast<iris::SkyType>(index);

	clearPanel(this->layout());
	setMouseTracking(true);

	QJsonObject skyDefinition;
	switch (static_cast<iris::SkyType>(index)) {
	case iris::SkyType::SINGLE_COLOR:
		skyDefinition = scene->skyData.value("SingleColor");
		break;
	case iris::SkyType::REALISTIC:
		skyDefinition = scene->skyData.value("Realistic");
		break;
	case iris::SkyType::GRADIENT:
		skyDefinition = scene->skyData.value("Gradient");
		break;
	case iris::SkyType::EQUIRECTANGULAR:
		skyDefinition = scene->skyData.value("Equirectangular");
		break;
	case iris::SkyType::CUBEMAP:
		skyDefinition = scene->skyData.value("Cubemap");
		break;
	}

	skySelector = this->addComboBox("Sky Type");
	skySelector->addItem("Single Color");
	skySelector->addItem("Cubemap");
	skySelector->addItem("Equirectangular");
	skySelector->addItem("Gradient");
	skySelector->addItem("Realistic");
	skySelector->setCurrentIndex(skyRowFor(static_cast<iris::SkyType>(index)));

	// Combo rows are NOT SkyType values (MATERIAL is gone): translate via the table.
	// The rebuild is DEFERRED by one event-loop turn on purpose: skyTypeChanged
	// clearPanel()s, which deletes the very combo whose currentIndexChanged is
	// running (and its popup container, mid-teardown) and then builds fresh
	// widgets in its place. Doing that synchronously crashed the renderer
	// reproducibly (3/3) once the rebuilt panel contained a second QComboBox —
	// the Qt 6.10 + qlementine combo-container hazard CLAUDE.md records, hit
	// from the other end. One queued turn lets the popup finish dying first.
	connect(skySelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged), this, [this](int row) {
		if (row >= 0 && row < kSkyRowCount)
			skyTypeChanged(static_cast<int>(kSkyRows[row]));
	});

	switch (scene->skyType) {
		case iris::SkyType::SINGLE_COLOR: {
			singleColor = this->addColorPicker("Sky Color");
			connect(singleColor->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onSingleSkyColorChanged(QColor)));

			QColor skyColor = SceneReader::readColor(skyDefinition.value("skyColor").toObject());
			singleColor->setColorValue(skyColor);
			singleColorDefinition.insert("skyColor", SceneWriter::jsonColor(skyColor));
			onSingleSkyColorChanged(skyColor);

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
			// the nearest value its dials can actually express.
			loaded.turbidity       = qBound(1.0f,  loaded.turbidity,       20.0f);
			loaded.reileigh        = qBound(0.0f,  loaded.reileigh,         4.0f);
			loaded.mieCoefficient  = qBound(0.0f,  loaded.mieCoefficient,   0.1f);
			loaded.mieDirectionalG = qBound(0.0f,  loaded.mieDirectionalG, 0.99f);
			loaded.luminance       = qBound(0.01f, loaded.luminance,        2.0f);
			loaded.setSunAngles(loaded.sunAzimuth(), qBound(-10.0f, loaded.sunElevation(), 90.0f));
			scene->skyRealistic = loaded;

			sunAzimuth = addFloatValueSlider("Sun Azimuth", 0.f, 360.f, defaults.sunAzimuth());
			sunElevation = addFloatValueSlider("Sun Elevation", -10.f, 90.f, defaults.sunElevation());
			turbidity = addFloatValueSlider("Turbidity", 1.f, 20.f, defaults.turbidity);
			reileigh = addFloatValueSlider("Rayleigh Scattering", 0.f, 4.f, defaults.reileigh);
			mieCoefficient = addFloatValueSlider("Mie Coefficient", 0.f, .1f, defaults.mieCoefficient);
			mieDirectionalG = addFloatValueSlider("Mie Directional G", 0.f, .99f, defaults.mieDirectionalG);
			luminance = addFloatValueSlider("Exposure", .01f, 2.f, defaults.luminance);
			skyDetail = this->addComboBox("Sky Detail");
			skyDetail->addItem("Normal (256)");
			skyDetail->addItem("High (512)");
			skyDetail->addItem("Ultra (1024)");
			skyDetail->setToolTip(QStringLiteral(
				"Width of the equirectangular image the sky is baked into. Higher is a sharper "
				"sun disc on a big display, at the cost of a longer bake on every change."));
			skyDetail->setCurrentIndex(scene->skyBakeResolution >= 1024 ? 2
									 : scene->skyBakeResolution >= 512  ? 1 : 0);

			sunAzimuth->setValue(loaded.sunAzimuth());
			sunElevation->setValue(loaded.sunElevation());
			turbidity->setValue(loaded.turbidity);
			reileigh->setValue(loaded.reileigh);
			mieCoefficient->setValue(loaded.mieCoefficient);
			mieDirectionalG->setValue(loaded.mieDirectionalG);
			luminance->setValue(loaded.luminance);

			connect(luminance, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onLuminanceChanged);
			connect(reileigh, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onReileighChanged);
			connect(mieCoefficient, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onMieCoeffGChanged);
			connect(mieDirectionalG, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onMieDireChanged);
			connect(turbidity, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onTurbidityChanged);
			connect(sunAzimuth, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onSunAzimuthChanged);
			connect(sunElevation, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onSunElevationChanged);
			connect(skyDetail, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
					this, &WorldSkyPropertyWidget::onSkyDetailChanged);

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
				// Remember that asset names are unique (auto incremented) so this is fine
				QString assetGuid = db->fetchAssetGUIDByName(QFileInfo(value).fileName(), project->getProjectGuid());
				db->removeDependenciesByType(scene->skyGuid, ModelTypes::Texture);
				if (!assetGuid.isEmpty()) {
					db->createDependency(
						static_cast<int>(ModelTypes::Sky),		// Not really but who cares
						static_cast<int>(ModelTypes::Texture),
						scene->skyGuid, assetGuid,
						project->getProjectGuid()
					);

					onEquiTextureChanged(assetGuid);
				}
			});

			break;
		}

		case iris::SkyType::CUBEMAP: {
			cubeMapWidget = this->addCubeMapWidget();

			connect(cubeMapWidget, &CubeMapWidget::valuesChanged, [=](QString value, QString guid, CubeMapPosition pos) {
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

			connect(colorTop->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onGradientTopColorChanged(QColor)));
			connect(colorMid->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onGradientMidColorChanged(QColor)));
			connect(colorBot->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onGradientBotColorChanged(QColor)));
			connect(offset, SIGNAL(valueChanged(float)), SLOT(onGradientOffsetChanged(float)));

			colorTop->setColorValue(SceneReader::readColor(skyDefinition.value("gradientTop").toObject()));
			colorMid->setColorValue(SceneReader::readColor(skyDefinition.value("gradientMid").toObject()));
			colorBot->setColorValue(SceneReader::readColor(skyDefinition.value("gradientBot").toObject()));
			offset->setValue(skyDefinition.value("gradientOffset").toDouble());

			gradientDefinition.insert("gradientTop", SceneWriter::jsonColor(colorTop->getPicker()->getColor()));
			gradientDefinition.insert("gradientMid", SceneWriter::jsonColor(colorMid->getPicker()->getColor()));
			gradientDefinition.insert("gradientBot", SceneWriter::jsonColor(colorBot->getPicker()->getColor()));
			gradientDefinition.insert("gradientOffset", .73f);

			scene->gradientTop = SceneReader::readColor(skyDefinition.value("gradientTop").toObject());
			scene->gradientMid = SceneReader::readColor(skyDefinition.value("gradientMid").toObject());
			scene->gradientBot = SceneReader::readColor(skyDefinition.value("gradientBot").toObject());
			scene->gradientOffset = skyDefinition.value("gradientOffset").toDouble();

			break;
		}
	}

	addAmbientFromSkyRow();
}

void WorldSkyPropertyWidget::addAmbientFromSkyRow()
{
	// VISUAL_PARITY_SPEC item 3b. Only offered where there is a sky to
	// integrate: a single-colour sky has no hemispheres, so it always falls back
	// to the flat World > Ambient Color.
	if (!scene || scene->skyType == iris::SkyType::SINGLE_COLOR) { ambientFromSky = nullptr; return; }
	ambientFromSky = this->addCheckBox("Ambient From Sky", scene->ambientFromSky);
	// DEFECT (pre-existing, accordionbladewidget.cpp:216): addCheckBox drops its
	// `value` argument on the floor — every caller has to set it afterwards.
	ambientFromSky->setValue(scene->ambientFromSky);
	ambientFromSky->setToolTip(QStringLiteral(
		"Light the scene's ambient with the sky itself: the upper and lower hemisphere colours "
		"are integrated from the sky image, so a red sky reddens what it lights. World > Ambient "
		"Color then sets the strength and tint of that instead of being the ambient itself."));
	connect(ambientFromSky, &CheckBoxWidget::valueChanged,
			this, &WorldSkyPropertyWidget::onAmbientFromSkyChanged);
}

void WorldSkyPropertyWidget::onAmbientFromSkyChanged(bool on)
{
	if (!scene) return;
	scene->ambientFromSky = on;
	// A direct edit of a backing field is a World Mode PIN (POST_CHAIN_SPEC §9.1).
	worldmodes::pinRowValue(scene, QStringLiteral("ambientFromSky"), on ? 1 : 0);
}

void WorldSkyPropertyWidget::onSlotChanged(QString value, QString guid, int index)
{
	// Normally there'd be a check for if value is empty here but in that case we can clear the guid
	QString assetGuid = db->fetchAssetGUIDByName(QFileInfo(value).fileName(), project->getProjectGuid());
	db->deleteDependency(scene->skyGuid, guid);
	// Remember that asset names are unique (auto incremented) so this is fine
	if (!assetGuid.isEmpty()) {
		db->createDependency(
			static_cast<int>(ModelTypes::Sky),
			static_cast<int>(ModelTypes::Texture),
			scene->skyGuid,
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

	if (!!scene) { 
		setSkyMap(cubeMapDefinition); 
	}
}

void WorldSkyPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
		skyTypeChanged(static_cast<int>(scene->skyType));
    } else {
        this->scene.clear();
    }
}

void WorldSkyPropertyWidget::setDatabase(Database *db)
{
    this->db = db;
}

void WorldSkyPropertyWidget::updateAssetAndKeys()
{
	QJsonObject skyProperties = QJsonObject();

	switch (scene->skyType) {
	case iris::SkyType::SINGLE_COLOR: {
		for (const QString& key : singleColorDefinition.keys()) {
			skyProperties.insert(key, singleColorDefinition.value(key));
		}
		scene->skyData.insert("SingleColor", skyProperties);
		break;
	}

	case iris::SkyType::REALISTIC: {
		for (const QString& key : realisticDefinition.keys()) {
			skyProperties.insert(key, realisticDefinition.value(key));
		}
		scene->skyData.insert("Realistic", skyProperties);
		break;
	}

	case iris::SkyType::EQUIRECTANGULAR: {
		for (const QString& key : equiSkyDefinition.keys()) {
			skyProperties.insert(key, equiSkyDefinition.value(key));
		}
		scene->skyData.insert("Equirectangular", skyProperties);
		break;
	}

	case iris::SkyType::CUBEMAP: {
		for (const QString& key : cubeMapDefinition.keys()) {
			skyProperties.insert(key, cubeMapDefinition.value(key));
		}
		scene->skyData.insert("Cubemap", skyProperties);
		break;
	}

	case iris::SkyType::MATERIAL: {
		for (const QString& key : materialDefinition.keys()) {
			skyProperties.insert(key, materialDefinition.value(key));
		}
		break;
	}

	case iris::SkyType::GRADIENT: {
		for (const QString& key : gradientDefinition.keys()) {
			skyProperties.insert(key, gradientDefinition.value(key));
		}
		scene->skyData.insert("Gradient", skyProperties);
		break;
	}
	}

}

void WorldSkyPropertyWidget::setEquiMap(const QString &guid)
{
    if (!guid.isEmpty()) {
		equiSkyDefinition.insert("equiSkyGuid", guid);
        auto image = resolveSkyAssetFile(project, db, guid);
        equiTexture->setTexture(QFileInfo(image).isFile() ? image : QString());
        scene->setSkyTexture(iris::Texture2D::load(image, false));
		updateAssetAndKeys();

    }
}

void WorldSkyPropertyWidget::setSkyMap(const QJsonObject &skyDataDefinition)
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

	cubeMapWidget->addCubeMapImages(top, bottom, left, front, right, back);

	// We need at least one valid image to get some metadata from
	QImage *info;
	bool useTex = false;
	QVector<QString> sides = { front, back, left, right, top, bottom };
	for (const auto image : sides) {
		if (!image.isEmpty() && QFileInfo(image).isFile()) {
			info = new QImage(image);
			useTex = true;
			break;
		}
	}

	if (useTex) {
		scene->setSkyTexture(iris::Texture2D::createCubeMap(front, back, top, bottom, left, right, info));
		updateAssetAndKeys();
	}

}

void WorldSkyPropertyWidget::setSkyFromCustomMaterial(const QJsonObject& definition)
{
	//auto vert = materialDefinition.value("vertexShader").toString();
	//auto frag = materialDefinition.value("fragmentShader").toString();

	//auto vPath = resolveSkyAssetFile(project, db, vert);
	//auto fPath = resolveSkyAssetFile(project, db, frag);

	//scene->skyMaterial->createProgramFromShaderSource(vPath, fPath);
}

void WorldSkyPropertyWidget::onSingleSkyColorChanged(QColor color)
{
	singleColorDefinition.insert("skyColor", SceneWriter::jsonColor(color));

	if (!!scene) {
		scene->skyColor = color;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onMaterialChanged(int index)
{
	Q_UNUSED(index)

	//QJsonDocument shaderData = QJsonDocument::fromBinaryData(db->fetchAssetData(shaderSelector->getCurrentItemData()));
	//QJsonObject shaderDataDefinition = shaderData.object();

	//auto vert = shaderDataDefinition.value("vertex_shader").toString();
	//auto frag = shaderDataDefinition.value("fragment_shader").toString();

	//materialDefinition.insert("materialGuid", shaderSelector->getCurrentItemData());
	//materialDefinition.insert("vertexShader", vert);
	//materialDefinition.insert("fragmentShader", frag);

	//if (!!scene) {
	//	if (scene->skyGuid == skyGuid) {
	//		setSkyFromCustomMaterial(materialDefinition);
	//		updateAssetAndKeys();
	//	}
	//}
}

void WorldSkyPropertyWidget::onEquiTextureChanged(QString guid)
{
	equiSkyDefinition.insert("equiSkyGuid", guid);

	if (!!scene) {
		setEquiMap(guid);
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onReileighChanged(float val)
{
	realisticDefinition.insert("reileigh", val);
	if (!!scene) {
		scene->skyRealistic.reileigh = val;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onLuminanceChanged(float val)
{
	realisticDefinition.insert("luminance", val);
	if (!!scene) {
		scene->skyRealistic.luminance = val;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onTurbidityChanged(float val)
{
	realisticDefinition.insert("turbidity", val);
	if (!!scene) {
		scene->skyRealistic.turbidity = val;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onMieCoeffGChanged(float val)
{
	realisticDefinition.insert("mieCoefficient", val);
	if (!!scene) {
		scene->skyRealistic.mieCoefficient = val;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onMieDireChanged(float val)
{
	realisticDefinition.insert("mieDirectionalG", val);
	if (!!scene) {
		scene->skyRealistic.mieDirectionalG = val;
		updateAssetAndKeys();
	}
}

// Azimuth/elevation are a lossless view of the three stored sunPos floats — the
// panel edits the angles, the document (and the saved blob) keeps the vector.
void WorldSkyPropertyWidget::writeSunAngles()
{
	if (!scene || !sunAzimuth || !sunElevation) return;
	scene->skyRealistic.setSunAngles(sunAzimuth->getValue(), sunElevation->getValue());
	realisticDefinition.insert("sunPosX", double(scene->skyRealistic.sunPosX));
	realisticDefinition.insert("sunPosY", double(scene->skyRealistic.sunPosY));
	realisticDefinition.insert("sunPosZ", double(scene->skyRealistic.sunPosZ));
	updateAssetAndKeys();
}

void WorldSkyPropertyWidget::onSkyDetailChanged(int row)
{
	if (!scene) return;
	// Not a sky *parameter* (it never enters skyData): a scene render setting,
	// serialized beside antiAliasing. SceneMirror re-bakes when it changes.
	scene->skyBakeResolution = row >= 2 ? 1024 : row >= 1 ? 512 : 256;
	worldmodes::pinRowValue(scene, QStringLiteral("skyBakeResolution"), scene->skyBakeResolution);
}

void WorldSkyPropertyWidget::onSunAzimuthChanged(float)   { writeSunAngles(); }
void WorldSkyPropertyWidget::onSunElevationChanged(float) { writeSunAngles(); }

void WorldSkyPropertyWidget::onGradientTopColorChanged(QColor color)
{
	gradientDefinition.insert("gradientTop", SceneWriter::jsonColor(color));

	if (!!scene) {
		scene->gradientTop = color;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onGradientMidColorChanged(QColor color)
{
	gradientDefinition.insert("gradientMid", SceneWriter::jsonColor(color));

	if (!!scene) {
		scene->gradientMid = color;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onGradientBotColorChanged(QColor color)
{
	gradientDefinition.insert("gradientBot", SceneWriter::jsonColor(color));

	if (!!scene) {
		scene->gradientBot = color;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onGradientOffsetChanged(float offset)
{
	gradientDefinition.insert("gradientOffset", offset);

	if (!!scene) {
		scene->gradientOffset = offset;
		updateAssetAndKeys();
	}
}