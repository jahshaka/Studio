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
#include "ui/controls/lightchannelswidget.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/lightnode.h"

#include "bridge/enginehost.h"
#include "commands/setnodepropertycommand.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "ui/panels/propertywidgets/rowundo.h"
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


// EVERY ROW HERE IS ONE REFLECTED LIGHT PROPERTY (debt L6 / N5): the rows write
// through LightNode::setPropertyValue — the exact call node.setProperty makes —
// and each gesture becomes one SetNodePropertyCommand. No new verb was needed:
// intensity, colour, distance, the spot cone, the area rectangle, the shadow
// type/size/static flag and the shadow tint are all reflected keys already, and
// the two asset-binding rows go through LightBindings, which is what
// node.setLightProfile / node.setLightTexture call.
LightPropertyWidget::LightPropertyWidget(QWidget* parent):
    AccordianBladeWidget(parent),
    rows([this]() { return iris::SceneNodePtr(lightNode); }, [this]() { return services; },
         [this]() { return !loading; })
{
    lightColor = this->addColorPicker("Color");
    intensity = this->addFloatValueSlider("Intensity", 0, 10.f);
    distance = this->addFloatValueSlider("Distance", 0, 100.f);
    // The cutoff is a HALF angle; 1..85 is what the renderer clamps to, so the
    // slider offers exactly that rather than a 0..90 range whose ends do
    // nothing. Softness is a 0..1 FRACTION and was on a 0.1..90 slider —
    // i.e. 99% of its travel was the same clamped value (LIGHTING_FIX F-S3).
    spotCutOff = this->addFloatValueSlider("Spotlight CutOff", 1.f, 85.f);
    spotCutOffSoftness = this->addFloatValueSlider("Spotlight Softness", 0.f, 1.f);
    spotFalloff = this->addFloatValueSlider("Spotlight Falloff", 0.1f, 8.f);

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

    // LIGHTING CHANNELS, light side. Note the row above is called "Light Mask"
    // and is a completely different thing (an area light's gobo IMAGE) — hence
    // "Lighting Channels" here, and the description line, rather than the
    // engine's `lightMask` spelling.
    lightChannels = new LightChannelsWidget(this);
    lightChannels->setDescription(
        tr("This light only lights objects that share a channel with it. Shadows are NOT "
           "filtered: an object this light does not light still casts a shadow from it."));
    this->addWidgetToContent(lightChannels);
    connect(lightChannels, &LightChannelsWidget::maskChanged,
            this, &LightPropertyWidget::lightChannelsChanged);

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

    // STATIC SHADOW (SHADOW_TOOLING_SPEC.md §4.3). Renders this light's shadow
    // map ONCE and keeps it until something invalidates it, instead of
    // re-rendering it every frame — for a point light that is six cube-face
    // passes plus a copy saved per frame. Hidden for directional lights (their
    // PSSM splits follow the camera) and for area lights (which never cast):
    // the renderer ignores the flag for both, and a control that does nothing
    // is worse than no control.
    shadowStatic = this->addCheckBox("Static Shadow");
    shadowStatic->setToolTip(
        QStringLiteral("Render this light's shadow map once and keep it. The renderer "
                       "re-renders it by itself when the light moves or changes, when "
                       "geometry is added, removed or MOVED, and on world.refreshShadows(). "
                       "Best for a fixed lamp over scenery that does not move: a shadow that "
                       "is re-dirtied every frame simply costs what a dynamic one costs."));

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

    wireRows();
}

// The reflected key each row writes. The two combo rows carry a NAME, so they
// map their row index onto the enum here — the panel is the only place that
// knows the order the names are offered in.
void LightPropertyWidget::wireRows()
{
    rowundo::bind(lightColor->getPicker(), rows(QStringLiteral("lightColor")));
    rowundo::bind(intensity, rows(QStringLiteral("intensity")));
    rowundo::bind(distance, rows(QStringLiteral("distance")));
    rowundo::bind(spotCutOff, rows(QStringLiteral("spotCutOff")));
    rowundo::bind(spotCutOffSoftness, rows(QStringLiteral("spotCutOffSoftness")));
    rowundo::bind(spotFalloff, rows(QStringLiteral("spotFalloff")));
    rowundo::bind(rectWidth, rows(QStringLiteral("rectWidth")));
    rowundo::bind(rectHeight, rows(QStringLiteral("rectHeight")));
    rowundo::bind(doubleSided, rows(QStringLiteral("doubleSided")));
    rowundo::bind(accurate, rows(QStringLiteral("accurate")));
    rowundo::bind(shadowAlpha, rows(QStringLiteral("shadowAlpha")));
    rowundo::bind(shadowColor->getPicker(), rows(QStringLiteral("shadowColor")));
    rowundo::bind(shadowStatic, rows(QStringLiteral("shadowStatic")));
    rowundo::bind(shadowType, rows(QStringLiteral("shadowMapType"), [this](const QVariant &row) {
        return QVariant(int(evalShadowMapType(shadowType->getWidget()->itemText(row.toInt()))));
    }));
    rowundo::bind(shadowSize, rows(QStringLiteral("shadowMapResolution"), [this](const QVariant &row) {
        return QVariant(shadowSize->getWidget()->itemText(row.toInt()).toInt());
    }));

    // Accurate (LTC) area lights ignore the mask entirely — say so the moment
    // the user flips the switch, not the next time the panel is rebuilt. (The
    // value itself is written by the binding above; this is its consequence.)
    connect(accurate, SIGNAL(valueChanged(bool)), this, SLOT(lightAccurateChanged(bool)));
    connect(lightChannels, &LightChannelsWidget::maskChanged,
            this, &LightPropertyWidget::lightChannelsChanged);
}

void LightPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    //this->sceneNode = sceneNode;
    if(!!sceneNode && sceneNode->getSceneNodeType()==iris::SceneNodeType::Light)
    {
        lightNode = sceneNode.staticCast<iris::LightNode>();

        // Populating, not editing: the sliders and the pickers emit from their
        // setters, and without this the first click on a light would write its
        // own values back into it — and record undo steps for doing so.
        loading = true;

        //apply properties to ui
        lightColor->setColorValue(lightNode->color);
        intensity->setValue(lightNode->intensity);
        distance->setValue(lightNode->distance);
        spotCutOff->setValue(lightNode->spotCutOff);
        spotCutOffSoftness->setValue(lightNode->spotCutOffSoftness);
        spotFalloff->setValue(lightNode->spotFalloff);

		shadowColor->setColorValue(lightNode->shadowColor);
		shadowAlpha->setValue(lightNode->shadowAlpha);

        // Does not emit: setMask is the quiet setter, so selecting a light
        // cannot write its own value back into it.
        lightChannels->setMask(lightNode->getLightMask());

        if (lightNode->getLightType()==iris::LightType::Spot) {
            spotCutOff->show();
            spotCutOffSoftness->show();
            spotFalloff->show();
        } else {
            spotCutOff->hide();
            spotCutOffSoftness->hide();
            spotFalloff->hide();
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
        shadowStatic->setValue(lightNode->shadowMap->staticMap);
        //shadowBias->setValue(lightNode->shadowMap->bias);

        // Point lights: legacy never shadowed them (controls hidden); the engine
        // does, so engine mode keeps Shadow Type/Size. Tint stays per-backend.
        // Static shadow maps are a POINT/SPOT feature: a directional light's
        // splits follow the camera and an area light never casts, so the
        // renderer ignores the flag for both (the document still stores it, so
        // switching a light's type back does not lose the setting).
        if (lightNode->getLightType()==iris::LightType::Point ||
            lightNode->getLightType()==iris::LightType::Spot) {
            shadowStatic->show();
        } else {
            shadowStatic->hide();
        }
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
        loading = false;
    }
    else
    {
        lightNode.clear();
        refreshBindingRows();
    }
}

void LightPropertyWidget::lightAccurateChanged(bool accurate)
{
    Q_UNUSED(accurate)
    refreshBindingRows();
}

void LightPropertyWidget::lightChannelsChanged(quint32 mask)
{
    if (loading || !lightNode) return;
    // Document only: the mirror sees the changed LightDesc on the next sync
    // (sameLight compares the mask) and pushes it. `lightMask` is a reflected
    // key (SceneNode::setPropertyValue takes both spellings of the 32 bits), so
    // this records the same command node.setLightMask pushes.
    const QVariant before = lightNode->getPropertyValue(QStringLiteral("lightMask"));
    lightNode->setLightMask(mask);
    const QVariant after = lightNode->getPropertyValue(QStringLiteral("lightMask"));
    if (before == after || !services || !services->undo) return;
    services->undo->push(new SetNodePropertyCommand(lightNode, QStringLiteral("lightMask"),
                                                    before, after));
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

// THE TWO ASSET ROWS. The binding is a SERVICE CALL (resolution, the project
// pin and the photometric re-calibration all live in LightBindings, which is
// also what the verbs call), so the undo step replays that call rather than a
// field write — NodeEditCommand's contract, the same one node.setLightProfile
// uses. Applied and verified FIRST: a refused binding records nothing.
void LightPropertyWidget::bindProfile(const QString &guid)
{
    if (!lightNode) return;
    const QString before = lightNode->iesProfileGuid;
    if (before == guid) return;
    QString error;
    if (!LightBindings::bindProfile(lightNode, guid, db, project, &error)) {
        if (profileNote) { profileNote->setText(error); profileNote->show(); }
        refreshBindingRows();
        return;
    }
    auto node = lightNode;
    Database *database = db;
    Project *proj = project;
    panelundo::pushEdit(services, tr("IES Profile"),
                        [node, guid, database, proj]() {
                            LightBindings::bindProfile(node, guid, database, proj);
                        },
                        [node, before, database, proj]() {
                            LightBindings::bindProfile(node, before, database, proj);
                        });
    refreshBindingRows();
}

void LightPropertyWidget::bindMask(const QString &guid)
{
    if (!lightNode) return;
    const QString before = lightNode->lightTextureGuid;
    if (before == guid) return;
    QString error;
    if (!LightBindings::bindTexture(lightNode, guid, db, project, &error)) {
        if (maskNote) { maskNote->setText(error); maskNote->show(); }
        refreshBindingRows();
        return;
    }
    auto node = lightNode;
    Database *database = db;
    Project *proj = project;
    panelundo::pushEdit(services, tr("Light Mask"),
                        [node, guid, database, proj]() {
                            LightBindings::bindTexture(node, guid, database, proj);
                        },
                        [node, before, database, proj]() {
                            LightBindings::bindTexture(node, before, database, proj);
                        });
    refreshBindingRows();
}

void LightPropertyWidget::pickProfile()
{
    if (!lightNode || !db) return;
    const QString guid = LibraryAssetPicker::pick(ModelTypes::LightProfile, db,
                                                  tr("Choose an IES light profile"), this);
    if (guid.isEmpty()) return;
    bindProfile(guid);
}

void LightPropertyWidget::clearProfile()
{
    bindProfile(QString());
}

void LightPropertyWidget::pickMask()
{
    if (!lightNode || !db) return;
    const QString guid = LibraryAssetPicker::pick(ModelTypes::Texture, db,
                                                  tr("Choose an area light mask"), this);
    if (guid.isEmpty()) return;
    bindMask(guid);
}

void LightPropertyWidget::clearMask()
{
    bindMask(QString());
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
