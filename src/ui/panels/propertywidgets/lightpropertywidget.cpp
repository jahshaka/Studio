/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/accordionbladewidget.h"
#include <QWidget>
#include <QDebug>

#include "ui/panels/propertywidgets/lightpropertywidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/colorpickerwidget.h"
#include "ui/controls/colorvaluewidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/checkboxwidget.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/lightnode.h"

#include "bridge/enginehost.h"
#include "ui/controls/libraryassetpicker.h"
#include "services/lightbindings.h"
#include "data/database/database.h"
#include "data/project.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

/// One asset-binding row: a name label, Choose and Clear buttons, and a note
/// line underneath for the renderer limitation that applies right now.
QWidget *makeBindingRow(const QString &title, QLabel **valueOut, QPushButton **pickOut,
                        QPushButton **clearOut, QLabel **noteOut)
{
    auto *row = new QWidget;
    auto *outer = new QVBoxLayout(row);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(2);

    auto *line = new QHBoxLayout;
    line->setContentsMargins(0, 0, 0, 0);
    line->addWidget(new QLabel(title, row));
    auto *value = new QLabel(QObject::tr("None"), row);
    // The bound file's on-disk name is a content hash (CAS objects are named by
    // oid), so the label always shows the LIBRARY row's name — and elides it,
    // or a long asset name squeezes the two buttons off the row.
    value->setMinimumWidth(60);
    value->setMaximumWidth(120);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    line->addStretch();
    line->addWidget(value);
    auto *pick = new QPushButton(QObject::tr("Choose…"), row);
    auto *clear = new QPushButton(QObject::tr("Clear"), row);
    line->addWidget(pick);
    line->addWidget(clear);
    outer->addLayout(line);

    auto *note = new QLabel(row);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #d08b3c;"));
    note->hide();
    outer->addWidget(note);

    *valueOut = value; *pickOut = pick; *clearOut = clear; *noteOut = note;
    return row;
}

/// The bound asset's DISPLAY name (the file on disk is a content-hash object),
/// elided to the label's width, with the resolved path as the tooltip.
void setBindingLabel(QLabel *label, const QString &guid, const QString &path, Database *db)
{
    if (guid.isEmpty()) {
        label->setText(QObject::tr("None"));
        label->setToolTip(QString());
        return;
    }
    QString name;
    if (db) name = db->fetchAsset(guid).name;
    if (name.isEmpty()) name = QFileInfo(path).fileName();
    if (name.isEmpty()) name = QObject::tr("(missing)");
    label->setText(label->fontMetrics().elidedText(name, Qt::ElideMiddle,
                                                   label->maximumWidth() - 4));
    label->setToolTip(path.isEmpty() ? QObject::tr("the asset's bytes are not in the store")
                                     : path);
}

}  // namespace


LightPropertyWidget::LightPropertyWidget(QWidget* parent):
    AccordianBladeWidget(parent)
{
    lightColor = this->addColorPicker("Color");
    intensity = this->addFloatValueSlider("Intensity", 0, 10.f);
    distance = this->addFloatValueSlider("Distance", 0, 100.f);
    spotCutOff = this->addFloatValueSlider("Spotlight CutOff", 0, 90.f);
    spotCutOffSoftness = this->addFloatValueSlider("Spotlight Softness", .1f, 90.f);

    // Area lights only (engine viewport): the emitting rectangle and its modes.
    rectWidth = this->addFloatValueSlider("Rect Width", 0.05f, 20.f);
    rectHeight = this->addFloatValueSlider("Rect Height", 0.05f, 20.f);
    doubleSided = this->addCheckBox("Double Sided");
    accurate = this->addCheckBox("Accurate (LTC)");

    // Asset bindings. The panel owns only the widgets: resolution, the project
    // dependency pin and the photometric re-calibration all live in
    // LightBindings, which is also what the scripting verbs call.
    profileRow = makeBindingRow(tr("IES Profile"), &profileLabel, &profilePick,
                                &profileClear, &profileNote);
    this->addWidgetToContent(profileRow);
    maskRow = makeBindingRow(tr("Light Mask"), &maskLabel, &maskPick, &maskClear, &maskNote);
    this->addWidgetToContent(maskRow);
    connect(profilePick, &QPushButton::clicked, this, &LightPropertyWidget::pickProfile);
    connect(profileClear, &QPushButton::clicked, this, &LightPropertyWidget::clearProfile);
    connect(maskPick, &QPushButton::clicked, this, &LightPropertyWidget::pickMask);
    connect(maskClear, &QPushButton::clicked, this, &LightPropertyWidget::clearMask);

    shadowType = this->addComboBox("Shadow Type");
    shadowType->addItem("None");
    shadowType->addItem("Hard");
	shadowType->addItem("Soft");
	shadowType->addItem("Very Soft");
    //shadowType->addItem("Softer");
    shadowSize = this->addComboBox("Shadow Size");
    shadowSize->addItem("512");
    shadowSize->addItem("1024");
    shadowSize->addItem("2048");
    shadowSize->addItem("4096");
    // The renderer has ONE shadow atlas for the whole scene, so this is a
    // request and not a guarantee (VISUAL_PARITY_SPEC item 2): the largest
    // request among the scene's shadow-casting lights sizes the atlas, and the
    // World panel's Shadow Quality row can override the lot.
    shadowSize->setToolTip(
        QStringLiteral("A REQUEST, not a guarantee. Every light shares one shadow atlas: the "
                       "largest Shadow Size in the scene sizes it, and World > Shadows > "
                       "Shadow Quality overrides that."));
    //shadowBias = this->addFloatValueSlider("Shadow Bias",0,1);

	shadowAlpha = this->addFloatValueSlider("Shadow Transparency", 0, 1.f);
	shadowColor = this->addColorPicker("Shadow Color");

	// Per-light shadow colour/transparency have no engine equivalent (Ogre-Next's
	// PBR pipeline has no per-light shadow tint — and legacy's own PBR shader
	// ignored the colour too). Hide the controls in engine mode; legacy keeps them.
	mShadowTintSupported = false;  // engine viewport: HlmsPbs has no shadow tint
	if (!mShadowTintSupported) {
		shadowAlpha->hide();
		shadowColor->hide();
	}
	// The legacy renderer never shadowed point lights, so the panel hid their
	// shadow controls. The engine renders point shadows (focused/DPSM maps), so
	// in engine mode Shadow Type and Size stay available for point lights too.
	mPointShadowsSupported = true;

    connect(lightColor->getPicker(),SIGNAL(onColorChanged(QColor)),this,SLOT(lightColorChanged(QColor)));
    connect(lightColor->getPicker(),SIGNAL(onSetColor(QColor)),this,SLOT(lightColorChanged(QColor)));

    connect(intensity,SIGNAL(valueChanged(float)),this,SLOT(lightIntensityChanged(float)));
    connect(distance,SIGNAL(valueChanged(float)),this,SLOT(lightDistanceChanged(float)));
    connect(spotCutOff,SIGNAL(valueChanged(float)),this,SLOT(lightSpotCutoffChanged(float)));
    connect(spotCutOffSoftness,SIGNAL(valueChanged(float)),this,SLOT(lightSpotCutoffSoftnessChanged(float)));

    connect(rectWidth,SIGNAL(valueChanged(float)),this,SLOT(lightRectWidthChanged(float)));
    connect(rectHeight,SIGNAL(valueChanged(float)),this,SLOT(lightRectHeightChanged(float)));
    connect(doubleSided,SIGNAL(valueChanged(bool)),this,SLOT(lightDoubleSidedChanged(bool)));
    connect(accurate,SIGNAL(valueChanged(bool)),this,SLOT(lightAccurateChanged(bool)));

	connect(shadowAlpha, SIGNAL(valueChanged(float)), this, SLOT(shadowAlphaChanged(float)));
	connect(shadowColor->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(shadowColorChanged(QColor)));
	connect(shadowColor->getPicker(), SIGNAL(onSetColor(QColor)), this, SLOT(shadowColorChanged(QColor)));

    connect(shadowType, SIGNAL(currentIndexChanged(QString)), this, SLOT(shadowTypeChanged(QString)));
    connect(shadowSize, SIGNAL(currentIndexChanged(QString)), this, SLOT(shadowSizeChanged(QString)));
    //connect(shadowBias, SIGNAL(valueChanged(float)), this, SLOT(shadowBiasChanged(float)));
}

void LightPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    //this->sceneNode = sceneNode;
    if(!!sceneNode && sceneNode->getSceneNodeType()==iris::SceneNodeType::Light)
    {
        lightNode = sceneNode.staticCast<iris::LightNode>();

        //apply properties to ui
        lightColor->setColorValue(lightNode->color);
        intensity->setValue(lightNode->intensity);
        distance->setValue(lightNode->distance);
        spotCutOff->setValue(lightNode->spotCutOff);
        spotCutOffSoftness->setValue(lightNode->spotCutOffSoftness);

		shadowColor->setColorValue(lightNode->shadowColor);
		shadowAlpha->setValue(lightNode->shadowAlpha);

        if (lightNode->getLightType()==iris::LightType::Spot) {
            spotCutOff->show();
            spotCutOffSoftness->show();
        } else {
            spotCutOff->hide();
            spotCutOffSoftness->hide();
        }

        // Area lights: the emitting rectangle replaces the cone controls.
        if (lightNode->getLightType()==iris::LightType::Area) {
            rectWidth->setValue(lightNode->rectWidth);
            rectHeight->setValue(lightNode->rectHeight);
            doubleSided->setValue(lightNode->doubleSided);
            accurate->setValue(lightNode->accurate);
            rectWidth->show();
            rectHeight->show();
            doubleSided->show();
            accurate->show();
        } else {
            rectWidth->hide();
            rectHeight->hide();
            doubleSided->hide();
            accurate->hide();
        }

        refreshBindingRows();

        shadowSize->setCurrentItem(QString("%1").arg(lightNode->shadowMap->resolution));
        shadowType->setCurrentItem(evalShadowTypeName(lightNode->shadowMap->shadowType));
        //shadowBias->setValue(lightNode->shadowMap->bias);

        // Point lights: legacy never shadowed them (controls hidden); the engine
        // does, so engine mode keeps Shadow Type/Size. Tint stays per-backend.
        if (lightNode->getLightType()==iris::LightType::Area) {
            // Ogre-Next cannot shadow area lights: hide every shadow control.
            shadowSize->hide();
            shadowType->hide();
            shadowColor->hide();
            shadowAlpha->hide();
        } else if (lightNode->getLightType()==iris::LightType::Point) {
            if (mPointShadowsSupported) {
                shadowSize->show();
                shadowType->show();
            } else {
                shadowSize->hide();
                shadowType->hide();
            }
			shadowColor->hide();
			shadowAlpha->hide();
            //shadowBias->hide();
        } else {
            shadowSize->show();
            shadowType->show();
			if (mShadowTintSupported) {
				shadowColor->show();
				shadowAlpha->show();
			}
            //shadowBias->show();
        }
    }
    else
    {
        lightNode.clear();
        refreshBindingRows();
    }
}

void LightPropertyWidget::lightColorChanged(QColor color)
{
    if(!!lightNode)
        lightNode->color = color;
}

void LightPropertyWidget::lightIntensityChanged(float intensity)
{
    if(!!lightNode)
        lightNode->intensity = intensity;
}

void LightPropertyWidget::lightDistanceChanged(float distance)
{
    if(!!lightNode)
        lightNode->distance = distance;
}

void LightPropertyWidget::lightSpotCutoffChanged(float spotCutOff)
{
    if(!!lightNode)
        lightNode->spotCutOff = spotCutOff;
}

void LightPropertyWidget::lightSpotCutoffSoftnessChanged(float spotCutOffSoftness)
{
    if(!!lightNode)
        lightNode->spotCutOffSoftness = spotCutOffSoftness;
}

void LightPropertyWidget::lightRectWidthChanged(float width)
{
    if(!!lightNode)
        lightNode->rectWidth = width;
}

void LightPropertyWidget::lightRectHeightChanged(float height)
{
    if(!!lightNode)
        lightNode->rectHeight = height;
}

void LightPropertyWidget::lightDoubleSidedChanged(bool doubleSided)
{
    if(!!lightNode)
        lightNode->doubleSided = doubleSided;
}

void LightPropertyWidget::lightAccurateChanged(bool accurate)
{
    if(!!lightNode)
        lightNode->accurate = accurate;
    // Accurate (LTC) area lights ignore the mask entirely — say so the moment
    // the user flips the switch, not the next time the panel is rebuilt.
    refreshBindingRows();
}

// --- Asset bindings -------------------------------------------------------

void LightPropertyWidget::refreshBindingRows()
{
    if (!profileRow || !maskRow) return;
    if (!lightNode) {
        profileRow->hide();
        maskRow->hide();
        return;
    }
    const auto type = lightNode->getLightType();

    // A profile is a spot/point affordance only: directional and area lights
    // have no profile term in the renderer at all, so the row is not shown.
    const bool profileRelevant = type == iris::LightType::Spot || type == iris::LightType::Point;
    profileRow->setVisible(profileRelevant);
    if (profileRelevant) {
        setBindingLabel(profileLabel, lightNode->iesProfileGuid,
                        lightNode->iesProfilePath, db);
        profileClear->setEnabled(!lightNode->iesProfileGuid.isEmpty());
        // THE user-facing gotcha: turning shadows on a point light silently
        // removes its profile, because a shadow-casting point light is shaded
        // from a code path that has no profile term. There is no engine signal
        // for it, so the panel is the only place this can be said.
        const bool shadows = lightNode->shadowMap &&
                             lightNode->shadowMap->shadowType != iris::ShadowMapType::None;
        const bool lost = type == iris::LightType::Point && shadows;
        profileNote->setVisible(lost);
        if (lost)
            profileNote->setText(tr("Shadow-casting point lights ignore their IES profile — "
                                    "the renderer has no photometric term on that path. Set "
                                    "Shadow Type to None, or use a spotlight."));
        profileLabel->setEnabled(!lost);
    }

    // A mask is an area-light affordance, and only the fast approximation
    // samples it.
    const bool maskRelevant = type == iris::LightType::Area;
    maskRow->setVisible(maskRelevant);
    if (maskRelevant) {
        setBindingLabel(maskLabel, lightNode->lightTextureGuid,
                        lightNode->lightTexturePath, db);
        maskClear->setEnabled(!lightNode->lightTextureGuid.isEmpty());
        const bool lost = lightNode->accurate;
        maskNote->setVisible(lost);
        if (lost)
            maskNote->setText(tr("Accurate (LTC) area lights ignore their mask — only the fast "
                                 "approximation samples it. Turn Accurate off to use the mask."));
        maskLabel->setEnabled(!lost);
    }
}

void LightPropertyWidget::pickProfile()
{
    if (!lightNode || !db) return;
    const QString guid = LibraryAssetPicker::pick(ModelTypes::LightProfile, db,
                                                  tr("Choose an IES light profile"), this);
    if (guid.isEmpty()) return;
    QString error;
    if (!LightBindings::bindProfile(lightNode, guid, db, project, &error))
        profileNote->setText(error), profileNote->show();
    refreshBindingRows();
}

void LightPropertyWidget::clearProfile()
{
    if (!lightNode) return;
    LightBindings::bindProfile(lightNode, QString(), db, project);
    refreshBindingRows();
}

void LightPropertyWidget::pickMask()
{
    if (!lightNode || !db) return;
    const QString guid = LibraryAssetPicker::pick(ModelTypes::Texture, db,
                                                  tr("Choose an area light mask"), this);
    if (guid.isEmpty()) return;
    QString error;
    if (!LightBindings::bindTexture(lightNode, guid, db, project, &error))
        maskNote->setText(error), maskNote->show();
    refreshBindingRows();
}

void LightPropertyWidget::clearMask()
{
    if (!lightNode) return;
    LightBindings::bindTexture(lightNode, QString(), db, project);
    refreshBindingRows();
}

void LightPropertyWidget::shadowTypeChanged(QString name)
{
    auto shadowType = evalShadowMapType(name);
    lightNode->shadowMap->shadowType = shadowType;
}

void LightPropertyWidget::shadowSizeChanged(QString size)
{
    int res = size.toInt();
    lightNode->shadowMap->setResolution(res);
}

void LightPropertyWidget::shadowBiasChanged(float bias)
{
    lightNode->shadowMap->bias = bias;
}

void LightPropertyWidget::shadowColorChanged(QColor color)
{
	if (!!lightNode)
		lightNode->shadowColor = color;
}

void LightPropertyWidget::shadowAlphaChanged(float alpha)
{
	if (!!lightNode)
		lightNode->shadowAlpha = alpha;
}

QString LightPropertyWidget::evalShadowTypeName(iris::ShadowMapType shadowType)
{
    switch(shadowType){
    case iris::ShadowMapType::None:
        return "None";
    case iris::ShadowMapType::Hard:
        return "Hard";
    case iris::ShadowMapType::Soft:
        return "Soft";
    case iris::ShadowMapType::VerySoft:
        return "Very Soft";
    }

    return "None";
}

iris::ShadowMapType LightPropertyWidget::evalShadowMapType(QString shadowType)
{
    if (shadowType=="Hard")
        return iris::ShadowMapType::Hard;
    if (shadowType=="Soft")
        return iris::ShadowMapType::Soft;
    if (shadowType=="Very Soft")
        return iris::ShadowMapType::VerySoft;

    return iris::ShadowMapType::None;
}
