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

#include "data/database/database.h"
#include "services/subscriber.h"
#include "io/scenewriter.h"
#include "io/scenereader.h"
#include "io/assetmanager.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include <QSqlDatabase>

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
// The "Material" sky is gone from the UI (it was broken even in the legacy
// renderer — its panel section was commented out and marked BROKEN!); old sky
// assets that reference it fall back to a single-colour sky. Combo rows map to
// sky types through this table.
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

SkyPropertyWidget::SkyPropertyWidget()
{
	setMouseTracking(true);
}

void SkyPropertyWidget::skyTypeChanged(int index)
{
	if (skyGuid.isEmpty()) return;
	if (static_cast<iris::SkyType>(index) == iris::SkyType::MATERIAL)
		index = static_cast<int>(iris::SkyType::SINGLE_COLOR);

	// The current sky gets set when the asset is selected to be the type it was saved as
	// This function can be called after it has been initialized so if the starting type
	// and the index differ it is a new sky. Wipe the properties and start over.
	// If it's the same, do nothing but if it's new, reset to defaults
	const QJsonObject skyDefinition = currentSky == static_cast<iris::SkyType>(index)
                                        ? QJsonDocument::fromJson(db->fetchAssetData(skyGuid)).object()
										: QJsonObject();
	currentSky = static_cast<iris::SkyType>(index);

	clearPanel(this->layout());

	skySelector = this->addComboBox("Sky Type");
	skySelector->addItem("Single Color");
	skySelector->addItem("Cubemap");
	skySelector->addItem("Equirectangular");
	skySelector->addItem("Gradient");
	skySelector->addItem("Realistic");
	skySelector->setCurrentIndex(skyRowFor(static_cast<iris::SkyType>(index)));

	// Combo rows are NOT SkyType values (MATERIAL is gone): translate via the table.
	connect(skySelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged), this, [this](int row) {
		if (row >= 0 && row < kSkyRowCount)
			skyTypeChanged(static_cast<int>(kSkyRows[row]));
	});

	switch (static_cast<iris::SkyType>(index)) {
		case iris::SkyType::SINGLE_COLOR: {
			singleColor = this->addColorPicker("Sky Color");
			connect(singleColor->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onSingleSkyColorChanged(QColor)));

			// This should be revisited, some differences between scene color and our default
			if (skyDefinition.isEmpty()) {
				singleColorDefinition.insert("skyColor", SceneWriter::jsonColor(QColor(72, 72, 72)));
				singleColor->setColorValue(QColor(72, 72, 72));
			}
			else {
				singleColor->setColorValue(SceneReader::readColor(skyDefinition.value("skyColor").toObject()));
				onSingleSkyColorChanged(SceneReader::readColor(skyDefinition.value("skyColor").toObject()));
			}

			break;
		}

		case iris::SkyType::REALISTIC: {
			// Same re-range as the World panel (VISUAL_PARITY_SPEC item 1) —
			// these two panels have always been copies of each other, and a sky
			// ASSET has to place its sun with the same dials a scene does.
			const iris::SkyRealistic skyDefaults = iris::SkyRealistic::defaults();
			sunAzimuth = addFloatValueSlider("Sun Azimuth", 0.f, 360.f, skyDefaults.sunAzimuth());
			sunElevation = addFloatValueSlider("Sun Elevation", -10.f, 90.f, skyDefaults.sunElevation());
			turbidity = addFloatValueSlider("Turbidity", 1.f, 20.f, skyDefaults.turbidity);
			reileigh = addFloatValueSlider("Rayleigh Scattering", 0.f, 4.f, skyDefaults.reileigh);
			mieCoefficient = addFloatValueSlider("Mie Coefficient", 0.f, .1f, skyDefaults.mieCoefficient);
			mieDirectionalG = addFloatValueSlider("Mie Directional G", 0.f, .99f, skyDefaults.mieDirectionalG);
			luminance = addFloatValueSlider("Exposure", .01f, 2.f, skyDefaults.luminance);

			connect(luminance, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onLuminanceChanged);
			connect(reileigh, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onReileighChanged);
			connect(mieCoefficient, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onMieCoeffGChanged);
			connect(mieDirectionalG, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onMieDireChanged);
			connect(turbidity, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onTurbidityChanged);
			connect(sunAzimuth, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onSunAzimuthChanged);
			connect(sunElevation, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onSunElevationChanged);

			if (skyDefinition.isEmpty()) {
				realisticDefinition.insert("luminance", luminance->getValue());
				realisticDefinition.insert("reileigh", reileigh->getValue());
				realisticDefinition.insert("mieCoefficient", mieCoefficient->getValue());
				realisticDefinition.insert("mieDirectionalG", mieDirectionalG->getValue());
				realisticDefinition.insert("turbidity", turbidity->getValue());
				realisticDefinition.insert("sunPosX", double(skyDefaults.sunPosX));
				realisticDefinition.insert("sunPosY", double(skyDefaults.sunPosY));
				realisticDefinition.insert("sunPosZ", double(skyDefaults.sunPosZ));
			}
			else {
				iris::SkyRealistic loaded = skyDefaults;
				loaded.luminance       = skyDefinition.value("luminance").toDouble(skyDefaults.luminance);
				loaded.reileigh        = skyDefinition.value("reileigh").toDouble(skyDefaults.reileigh);
				loaded.mieCoefficient  = skyDefinition.value("mieCoefficient").toDouble(skyDefaults.mieCoefficient);
				loaded.mieDirectionalG = skyDefinition.value("mieDirectionalG").toDouble(skyDefaults.mieDirectionalG);
				loaded.turbidity       = skyDefinition.value("turbidity").toDouble(skyDefaults.turbidity);
				loaded.sunPosX         = skyDefinition.value("sunPosX").toDouble(skyDefaults.sunPosX);
				loaded.sunPosY         = skyDefinition.value("sunPosY").toDouble(skyDefaults.sunPosY);
				loaded.sunPosZ         = skyDefinition.value("sunPosZ").toDouble(skyDefaults.sunPosZ);
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
				reileigh->setValue(loaded.reileigh);
				luminance->setValue(loaded.luminance);
				mieCoefficient->setValue(loaded.mieCoefficient);
				mieDirectionalG->setValue(loaded.mieDirectionalG);
				turbidity->setValue(loaded.turbidity);
				sunAzimuth->setValue(loaded.sunAzimuth());
				sunElevation->setValue(loaded.sunElevation());
			}

			break;
		}

		case iris::SkyType::EQUIRECTANGULAR: {
			equiTexture = this->addTexturePicker("Equi Map");

			// There are no default values, this definition gets set whenever we change the texture
			setEquiMap(skyDefinition.value("equiSkyGuid").toString());

			connect(equiTexture, &TexturePickerWidget::valueChanged, this, [this](QString value) {
				// Remember that asset names are unique (auto incremented) so this is fine
				QString assetGuid = db->fetchAssetGUIDByName(QFileInfo(value).fileName(), project->getProjectGuid());
				db->removeDependenciesByType(skyGuid, ModelTypes::Texture);
				if (!assetGuid.isEmpty()) {
					db->createDependency(
						static_cast<int>(ModelTypes::Sky),
						static_cast<int>(ModelTypes::Texture),
						skyGuid, assetGuid,
						project->getProjectGuid()
					);

					onEquiTextureChanged(assetGuid);
				}
			});

			break;
		}

		case iris::SkyType::CUBEMAP: {
			skyMapWidget = this->addCubeMapWidget();

			connect(skyMapWidget, &CubeMapWidget::valuesChanged, [=](QString value, QString guid, CubeMapPosition pos) {
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

			if (skyDefinition.isEmpty()) {
				gradientDefinition.insert("gradientTop", SceneWriter::jsonColor(QColor(255, 146, 138)));
				gradientDefinition.insert("gradientMid", SceneWriter::jsonColor(QColor("white")));
				gradientDefinition.insert("gradientBot", SceneWriter::jsonColor(QColor(64, 128, 255)));
				gradientDefinition.insert("gradientOffset", .73f);

				colorTop->setColorValue(QColor(255, 146, 138));
				colorMid->setColorValue(QColor("white"));
				colorBot->setColorValue(QColor(64, 128, 255));
			}
			else {
				colorTop->setColorValue(SceneReader::readColor(skyDefinition.value("gradientTop").toObject()));
				colorMid->setColorValue(SceneReader::readColor(skyDefinition.value("gradientMid").toObject()));
				colorBot->setColorValue(SceneReader::readColor(skyDefinition.value("gradientBot").toObject()));
				offset->setValue(skyDefinition.value("gradientOffset").toDouble());
			}

			break;
		}
	}

	if (skyDefinition.isEmpty()) {
		updateAssetAndKeys();
	}

}

void SkyPropertyWidget::onSlotChanged(QString value, QString guid, int index)
{
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

	if (!!scene) {
		if (scene->skyGuid == skyGuid) setSkyMap(cubeMapDefinition);
	}
}

void SkyPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
    } else {
        this->scene.clear();
    }
}

void SkyPropertyWidget::setDatabase(Database *db)
{
    this->db = db;
}

void SkyPropertyWidget::setSkyAlongWithProperties(const QString &guid, iris::SkyType skyType)
{
	skyGuid = guid;
	currentSky = skyType;
	skyTypeChanged(static_cast<int>(skyType));
}

// Let's repurpose this event and use it to update the asset in the db (iKlsR)
void SkyPropertyWidget::hideEvent(QHideEvent *event)
{
	Q_UNUSED(event)
	updateAssetAndKeys();
}

void SkyPropertyWidget::updateAssetAndKeys()
{
	QJsonObject properties;
	QJsonObject skyProps;
	skyProps.insert("type", static_cast<int>(currentSky));
	properties.insert("sky", skyProps);

	skyProperties = QJsonObject();
	skyProperties.insert("guid", skyGuid);

	switch (currentSky) {
	case iris::SkyType::SINGLE_COLOR: {
		for (const QString& key : singleColorDefinition.keys()) {
			skyProperties.insert(key, singleColorDefinition.value(key));
		}
		break;
	}

	case iris::SkyType::REALISTIC: {
		for (const QString& key : realisticDefinition.keys()) {
			skyProperties.insert(key, realisticDefinition.value(key));
		}
		break;
	}

	case iris::SkyType::EQUIRECTANGULAR: {
		for (const QString& key : equiSkyDefinition.keys()) {
			skyProperties.insert(key, equiSkyDefinition.value(key));
		}
		break;
	}

	case iris::SkyType::CUBEMAP: {
		for (const QString& key : cubeMapDefinition.keys()) {
			skyProperties.insert(key, cubeMapDefinition.value(key));
		}
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
		break;
	}
	}

    db->updateAssetAsset(skyGuid, QJsonDocument(skyProperties).toJson());
    db->updateAssetProperties(skyGuid, QJsonDocument(properties).toJson());
	// Not really used but keep around for now, the intent is clear (iKlsR)
	if (eventBus) emit eventBus->updateAssetSkyItemFromSkyPropertyWidget(skyGuid, currentSky);
}

void SkyPropertyWidget::setEquiMap(const QString &guid)
{
    if (!guid.isEmpty()) {
		equiSkyDefinition.insert("equiSkyGuid", guid);
        auto image = resolveSkyAssetFile(project, db, guid);
        equiTexture->setTexture(QFileInfo(image).isFile() ? image : QString());
        scene->setSkyTexture(iris::Texture2D::load(image, false));
    }
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

	skyMapWidget->addCubeMapImages(top, bottom, left, front, right, back);

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


	//if (useTex) {
	//	scene->setSkyTexture(iris::Texture2D::createCubeMap(front, back, top, bottom, left, right, info));
	//}
}

void SkyPropertyWidget::setSkyFromCustomMaterial(const QJsonObject& definition)
{
	auto vert = materialDefinition.value("vertexShader").toString();
	auto frag = materialDefinition.value("fragmentShader").toString();

	auto vPath = resolveSkyAssetFile(project, db, vert);
	auto fPath = resolveSkyAssetFile(project, db, frag);

	//scene->skyMaterial->createProgramFromShaderSource(vPath, fPath);
}

void SkyPropertyWidget::onSingleSkyColorChanged(QColor color)
{
	singleColorDefinition.insert("skyColor", SceneWriter::jsonColor(color));

	if (!!scene) {
		if (scene->skyGuid == skyGuid) {
			scene->skyColor = color;
		}
	}
}

void SkyPropertyWidget::onMaterialChanged(int index)
{
	Q_UNUSED(index)

    QJsonDocument shaderData = QJsonDocument::fromJson(db->fetchAssetData(shaderSelector->getCurrentItemData()));
	QJsonObject shaderDataDefinition = shaderData.object();

	auto vert = shaderDataDefinition.value("vertex_shader").toString();
	auto frag = shaderDataDefinition.value("fragment_shader").toString();

	materialDefinition.insert("materialGuid", shaderSelector->getCurrentItemData());
	materialDefinition.insert("vertexShader", vert);
	materialDefinition.insert("fragmentShader", frag);

	if (!!scene) {
		if (scene->skyGuid == skyGuid) setSkyFromCustomMaterial(materialDefinition);
	}
}

void SkyPropertyWidget::onEquiTextureChanged(QString guid)
{
	equiSkyDefinition.insert("equiSkyGuid", guid);

	if (!!scene) {
		if (scene->skyGuid == skyGuid) setEquiMap(guid);
	}
}

void SkyPropertyWidget::onReileighChanged(float val)
{
	realisticDefinition.insert("reileigh", val);
	if (!!scene) {
		if (scene->skyGuid == skyGuid) {
			scene->skyRealistic.reileigh = val;
		}
	}
}

void SkyPropertyWidget::onLuminanceChanged(float val)
{
	realisticDefinition.insert("luminance", val);
	if (!!scene) {
		if (scene->skyGuid == skyGuid) {
			scene->skyRealistic.luminance = val;
		}
	}
}

void SkyPropertyWidget::onTurbidityChanged(float val)
{
	realisticDefinition.insert("turbidity", val);
	if (!!scene) {
		if (scene->skyGuid == skyGuid) {
			scene->skyRealistic.turbidity = val;
		}
	}
}

void SkyPropertyWidget::onMieCoeffGChanged(float val)
{
	realisticDefinition.insert("mieCoefficient", val);
	if (!!scene) {
		if (scene->skyGuid == skyGuid) {
			scene->skyRealistic.mieCoefficient = val;
		}
	}
}

void SkyPropertyWidget::onMieDireChanged(float val)
{
	realisticDefinition.insert("mieDirectionalG", val);
	if (!!scene) {
		if (scene->skyGuid == skyGuid) {
			scene->skyRealistic.mieDirectionalG = val;
		}
	}
}

// Azimuth/elevation are a lossless view of the three stored sunPos floats — the
// panel edits the angles, the asset blob keeps the vector.
void SkyPropertyWidget::writeSunAngles()
{
	if (!sunAzimuth || !sunElevation) return;
	iris::SkyRealistic s;
	s.setSunAngles(sunAzimuth->getValue(), sunElevation->getValue());
	realisticDefinition.insert("sunPosX", double(s.sunPosX));
	realisticDefinition.insert("sunPosY", double(s.sunPosY));
	realisticDefinition.insert("sunPosZ", double(s.sunPosZ));
	if (!!scene && scene->skyGuid == skyGuid) {
		scene->skyRealistic.sunPosX = s.sunPosX;
		scene->skyRealistic.sunPosY = s.sunPosY;
		scene->skyRealistic.sunPosZ = s.sunPosZ;
	}
}

void SkyPropertyWidget::onSunAzimuthChanged(float)   { writeSunAngles(); }
void SkyPropertyWidget::onSunElevationChanged(float) { writeSunAngles(); }

void SkyPropertyWidget::onGradientTopColorChanged(QColor color)
{
	gradientDefinition.insert("gradientTop", SceneWriter::jsonColor(color));

	if (!!scene) {
		if (scene->skyGuid == skyGuid) {
			scene->gradientTop = color;
		}
	}
}

void SkyPropertyWidget::onGradientMidColorChanged(QColor color)
{
	gradientDefinition.insert("gradientMid", SceneWriter::jsonColor(color));

	if (!!scene) {
		if (scene->skyGuid == skyGuid) {
			scene->gradientMid = color;
		}
	}
}

void SkyPropertyWidget::onGradientBotColorChanged(QColor color)
{
	gradientDefinition.insert("gradientBot", SceneWriter::jsonColor(color));

	if (!!scene) {
		if (scene->skyGuid == skyGuid) {
			scene->gradientBot = color;
		}
	}
}

void SkyPropertyWidget::onGradientOffsetChanged(float offset)
{
	gradientDefinition.insert("gradientOffset", offset);

	if (!!scene) {
		if (scene->skyGuid == skyGuid) {
			scene->gradientOffset = offset;
		}
	}
}

