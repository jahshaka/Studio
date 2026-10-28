/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/accordionbladewidget.h"
#include "ui/panels/propertyrows.h"
#include <QWidget>
#include <QDebug>

#include "ui/panels/propertywidgets/lightpropertywidget.h"
#include "services/editgate.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/colorpickerwidget.h"
#include "ui/controls/colorvaluewidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/lightchannelswidget.h"
#include "ui/controls/labelwidget.h"
#include "ui/controls/dragvaluewidgets.h"

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
#include "ui/style/stylesheet.h"
#include "ui/style/themeroles.h"

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
    note->setStyleSheet(StyleSheet::WarningNote());
    ThemeRoles::setTone(note, ThemeRoles::Tone::Warning);
    PropertyRows::setPanelVisible(note, false);
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
// intensity, colour, range (`distance`), source radius, the spot cone, the area rectangle, the shadow
// type/size/static flag and the shadow tint are all reflected keys already, and
// the asset-binding row goes through LightBindings, which is what
// node.setLightProfile calls.
LightPropertyWidget::LightPropertyWidget(QWidget* parent):
    AccordianBladeWidget(parent),
    rows([this]() { return iris::SceneNodePtr(lightNode); }, [this]() { return services; },
         [this]() { return !loading; })
{
    lightColor = this->addColorPicker("Color");
    // A SOFT SLIDER, AN UNCAPPED FIELD (IMAGE-1): under the inverse square law a
    // lamp's intensity is its light at 1 m, so a room's lamps run to tens and a
    // high spot to hundreds (the re-lit samples: 1.1 to 221; a new lamp is 8).
    // The slider scrubs 0..100; the typed field takes up to 100000 and keeps
    // what was typed (the row never clamps a value it did not choose).
    intensity = this->addFloatValueSlider("Intensity", 0, 100.f);
    intensity->setTypedMaximum(100000.f);
    // THE RANGE: where the light's inverse-square falloff is windowed to zero
    // (IMAGE-1; the key is still `distance`).
    distance = this->addFloatValueSlider("Range", 0, 100.f);
    // THE SOURCE RADIUS (IMAGE-1): the emitter's size — inside it the light
    // stops getting brighter. Points and spots only.
    sourceRadius = this->addFloatValueSlider("Source Radius", 0.001f, 2.f);
    // The cutoff is a HALF angle; 1..85 is what the renderer clamps to, so the
    // slider offers exactly that rather than a 0..90 range whose ends do
    // nothing. Softness is a 0..1 FRACTION and was on a 0.1..90 slider —
    // i.e. 99% of its travel was the same clamped value (LIGHTING_FIX F-S3).
    spotCutOff = this->addFloatValueSlider("Spotlight CutOff", 1.f, 85.f);
    spotCutOffSoftness = this->addFloatValueSlider("Spotlight Softness", 0.f, 1.f);
    spotFalloff = this->addFloatValueSlider("Spotlight Falloff", 0.1f, 8.f);

    // Area lights only (engine viewport): the emitting rectangle and its sides.
    // Every area light is LTC (PHOTON-7 7-0): the fast approximation, its
    // "Accurate (LTC)" switch and its Light Mask row are deleted.
    rectWidth = this->addFloatValueSlider("Rect Width", 0.05f, 20.f);
    rectHeight = this->addFloatValueSlider("Rect Height", 0.05f, 20.f);
    doubleSided = this->addCheckBox("Double Sided");

    // The asset binding. The panel owns only the widgets: resolution, the project
    // dependency pin and the photometric re-calibration all live in
    // LightBindings, which is also what the scripting verbs call.
    profileRow = makeBindingRow(tr("IES Profile"), &profileLabel, &profilePick,
                                &profileClear, &profileNote);
    this->addWidgetToContent(profileRow, tr("IES Profile"),
                             { QStringLiteral("photometric"), QStringLiteral("ies") });
    connect(profilePick, &QPushButton::clicked, this, &LightPropertyWidget::pickProfile);
    connect(profileClear, &QPushButton::clicked, this, &LightPropertyWidget::clearProfile);

    // LIGHTING CHANNELS, light side — "Lighting Channels" here, and the
    // description line, rather than the engine's `lightMask` spelling.
    lightChannels = new LightChannelsWidget(this);
    lightChannels->setDescription(
        tr("This light only lights objects that share a channel with it. Shadows are NOT "
           "filtered: an object this light does not light still casts a shadow from it. Turn "
           "off Cast Shadow on the object itself - the row of that name on its Mesh blade - "
           "to stop that."));
    this->addWidgetToContent(lightChannels, tr("Lighting Channels"),
                             { QStringLiteral("channels") });
    connect(lightChannels, &LightChannelsWidget::maskChanged,
            this, &LightPropertyWidget::lightChannelsChanged);

    // THE SUN (SUN_AND_LIGHT_DEFAULTS Q1/Q1d). Directional lights only, and it
    // is the only place the word "sun" is a thing in this editor: there is one
    // directional light type and the sun is the ROLE the lowest priority wins.
    sunReadout = this->addLabel(tr("Sun"), QString());
    forwardShadingPriority =
        this->addDragFloat(tr("Forward Shading Priority"), 0.0, 0.0, 15.0, 0.05, 0);
    forwardShadingPriority->setToolTip(
        tr("0 is the sun. The first directional light in a scene takes 0 and lights and shadows "
           "the whole world; every further one slots into the next free number automatically and "
           "is a SECONDARY light — it lights the scene fully but casts no shadow, because the "
           "renderer has exactly one directional shadow slot. Lower wins."));
    // (The "Sun Angle" row stood here. It is GONE — lane SUN-DISC-1: the only
    // thing that read it was the sun DISC, and the disc's drawn size is not the
    // sun's physical angle (the default is four times it, which is what a
    // photograph's sun looks like). It is a World-panel row now, beside the two
    // rows that already decide where the disc is shown.)

    // FOLLOWS ATMOSPHERE (SUN_FOLLOWS_ATMOSPHERE). The sun's own row, beside
    // Sun Angle, and shown on the sun alone for the same reason.
    followsAtmosphere = this->addCheckBox(tr("Follows Atmosphere"));
    followsAtmosphere->setToolTip(
        tr("With the Realistic sky, the sun's light takes the colour the air gives it: the "
           "colour you picked is its NOON colour, and a low sun arrives redder and dimmer, like "
           "a real sunset, and at night the sun is dark — below the horizon it lights nothing, "
           "draws no disc and casts no shadow. The sun disc in the sky changes with it, so the "
           "two always agree. Switch it off to use the colour you picked at every time of day. "
           "On any other sky this does nothing — a picture of a sky knows nothing about the "
           "air."));

    shadowType = this->addComboBox("Shadow Type");
    // THE SHADOW'S FILTER - every light casts one (PHOTON-7 10-4 R9; L9: a light is blocked by geometry on every
    // path). "Off (fill light)" is DELETED: the engine traces every lamp the shadow maps do not hold (a ray at the ray
    // tier, the voxels' march where the GI is on) - with GI off and no ray tracing, the lamps beyond the shadow maps are
    // unshadowed, and the scene issue `lamps.unshadowed` names them.
    shadowType->addItem("Hard");
	shadowType->addItem("Soft");
	shadowType->addItem("Very Soft");
    shadowType->setToolTip(
        tr("How soft this light's shadow is. Every light casts one. A lamp beyond the scene's shadow "
           "maps is shadowed by ray tracing, or by Global Illumination's voxels; with GI off and no ray "
           "tracing it is unshadowed, and the scene's issue bar names it."));
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
	// (THREE ROWS ARE GONE — Shadow Transparency, Shadow Color and the
	// commented-out Shadow Bias, render audit I-6 / A5 "dead or misleading".
	// Two of them were BUILT and then hidden on every light in every scene,
	// because Ogre-Next's PBR pipeline has no per-light shadow tint and never
	// will: a shadow is the absence of light. The document fields are deleted
	// with them, so there is nothing left to hide.)

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
    rowundo::bind(sourceRadius, rows(QStringLiteral("sourceRadius")));
    rowundo::bind(spotCutOff, rows(QStringLiteral("spotCutOff")));
    rowundo::bind(spotCutOffSoftness, rows(QStringLiteral("spotCutOffSoftness")));
    rowundo::bind(spotFalloff, rows(QStringLiteral("spotFalloff")));
    rowundo::bind(rectWidth, rows(QStringLiteral("rectWidth")));
    rowundo::bind(rectHeight, rows(QStringLiteral("rectHeight")));
    rowundo::bind(doubleSided, rows(QStringLiteral("doubleSided")));
    rowundo::bind(shadowType, rows(QStringLiteral("shadowMapType"), [this](const QVariant &row) {
        return QVariant(int(evalShadowMapType(shadowType->getWidget()->itemText(row.toInt()))));
    }));
    rowundo::bind(shadowSize, rows(QStringLiteral("shadowMapResolution"), [this](const QVariant &row) {
        return QVariant(shadowSize->getWidget()->itemText(row.toInt()).toInt());
    }));
    rowundo::bind(forwardShadingPriority, rows(QStringLiteral("forwardShadingPriority")));
    rowundo::bind(followsAtmosphere, rows(QStringLiteral("followsAtmosphere")));
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
        sourceRadius->setValue(lightNode->sourceRadius);
        spotCutOff->setValue(lightNode->spotCutOff);
        spotCutOffSoftness->setValue(lightNode->spotCutOffSoftness);
        spotFalloff->setValue(lightNode->spotFalloff);

        // Does not emit: setMask is the quiet setter, so selecting a light
        // cannot write its own value back into it.
        lightChannels->setMask(lightNode->getLightMask());

        // A SKY LIGHT HAS TWO ROWS (SKY_LIGHT_SPEC.md §2): Color, which is a
        // TINT on the sky's own light, and Intensity, which is the strength.
        // Everything else on this blade describes a light that has a PLACE in
        // the world, which this one does not — it is the sky.
        const bool isSky = lightNode->getLightType() == iris::LightType::Sky;
        lightColor->setLabel(isSky ? tr("Tint") : tr("Color"));
        PropertyRows::setPanelVisible(distance, !isSky);
        PropertyRows::setPanelVisible(sourceRadius,
                                      lightNode->getLightType() == iris::LightType::Point ||
                                          lightNode->getLightType() == iris::LightType::Spot);
        PropertyRows::setPanelVisible(lightChannels, !isSky);
        if (lightNode->getLightType()==iris::LightType::Spot) {
            PropertyRows::setPanelVisible(spotCutOff, true);
            PropertyRows::setPanelVisible(spotCutOffSoftness, true);
            PropertyRows::setPanelVisible(spotFalloff, true);
        } else {
            PropertyRows::setPanelVisible(spotCutOff, false);
            PropertyRows::setPanelVisible(spotCutOffSoftness, false);
            PropertyRows::setPanelVisible(spotFalloff, false);
        }

        // Area lights: the emitting rectangle replaces the cone controls.
        if (lightNode->getLightType()==iris::LightType::Area) {
            rectWidth->setValue(lightNode->rectWidth);
            rectHeight->setValue(lightNode->rectHeight);
            doubleSided->setValue(lightNode->doubleSided);
            PropertyRows::setPanelVisible(rectWidth, true);
            PropertyRows::setPanelVisible(rectHeight, true);
            PropertyRows::setPanelVisible(doubleSided, true);
        } else {
            PropertyRows::setPanelVisible(rectWidth, false);
            PropertyRows::setPanelVisible(rectHeight, false);
            PropertyRows::setPanelVisible(doubleSided, false);
        }

        refreshBindingRows();

        shadowSize->setCurrentItem(QString("%1").arg(lightNode->shadowMap.resolution));
        shadowType->setCurrentItem(evalShadowTypeName(lightNode->shadowMap.shadowType));
        refreshSunRows();

        // Point lights: legacy never shadowed them (controls hidden); the engine
        // does, so engine mode keeps Shadow Type/Size. Tint stays per-backend.
        if (isSky ||
            lightNode->getLightType()==iris::LightType::Area) {
            // Neither can shadow: an area light because Ogre-Next cannot, a Sky
            // Light because it has no place to cast from. Hide every control.
            PropertyRows::setPanelVisible(shadowSize, false);
            PropertyRows::setPanelVisible(shadowType, false);
        } else if (lightNode->getLightType()==iris::LightType::Point) {
            if (mPointShadowsSupported) {
                PropertyRows::setPanelVisible(shadowSize, true);
                PropertyRows::setPanelVisible(shadowType, true);
            } else {
                PropertyRows::setPanelVisible(shadowSize, false);
                PropertyRows::setPanelVisible(shadowType, false);
            }
        } else {
            PropertyRows::setPanelVisible(shadowSize, true);
            PropertyRows::setPanelVisible(shadowType, true);
        }
        loading = false;
    }
    else
    {
        lightNode.clear();
        refreshBindingRows();
    }
}

void LightPropertyWidget::lightChannelsChanged(quint32 mask)
{
    if (loading || !lightNode) return;
    // The edit gate (ledger §423), as in the mesh panel's channel grid: it
    // writes the node first and records the step after it.
    if (editgate::refuse()) return;
    // Document only: the mirror sees the changed LightDesc on the next sync
    // (LightDesc::operator== compares the mask) and pushes it. `lightMask` is a reflected
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
    if (!profileRow) return;
    if (!lightNode) {
        PropertyRows::setPanelVisible(profileRow, false);
        return;
    }
    const auto type = lightNode->getLightType();

    // A profile is a spot/point affordance only: directional and area lights
    // have no profile term in the renderer at all, so the row is not shown.
    const bool profileRelevant = type == iris::LightType::Spot || type == iris::LightType::Point;
    PropertyRows::setPanelVisible(profileRow, profileRelevant);
    if (profileRelevant) {
        setBindingLabel(profileLabel, lightNode->iesProfileGuid,
                        lightNode->iesProfilePath, db);
        profileClear->setEnabled(!lightNode->iesProfileGuid.isEmpty());
        // (the note row carries a binding error only: a spot or point light honours
        // its profile, shadowed or not)
        PropertyRows::setPanelVisible(profileNote, false);
    }
}

// THE ASSET ROW. The binding is a SERVICE CALL (resolution, the project
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
        if (profileNote) { profileNote->setText(error); PropertyRows::setPanelVisible(profileNote, true); }
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

// THE SUN ROW. Directional lights only — on any other type the two rows say
// nothing true, so they are not shown at all rather than shown disabled.
void LightPropertyWidget::refreshSunRows()
{
    if (!sunReadout || !forwardShadingPriority || !followsAtmosphere) return;
    if (!lightNode || lightNode->lightType != iris::LightType::Directional) {
        PropertyRows::setPanelVisible(sunReadout, false);
        PropertyRows::setPanelVisible(forwardShadingPriority, false);
        PropertyRows::setPanelVisible(followsAtmosphere, false);
        return;
    }
    PropertyRows::setPanelVisible(sunReadout, true);
    PropertyRows::setPanelVisible(forwardShadingPriority, true);
    forwardShadingPriority->setValue(lightNode->forwardShadingPriority);

    auto scene = lightNode->getScene();
    auto sun = scene ? scene->sunLight() : iris::LightNodePtr();
    const bool isSun = sun && sun.data() == lightNode.data();
    // FOLLOWS ATMOSPHERE IS THE SUN'S: it tints THE SUN's direct light, and a
    // secondary directional is not the sun — it draws no disc and the sky is
    // not lit by it, so the row on one would say nothing true (round-2 review
    // item 11). It follows the RESOLVER, like the readout below.
    PropertyRows::setPanelVisible(followsAtmosphere, isSun);
    followsAtmosphere->setValue(lightNode->followsAtmosphere);

    const QString reason = scene ? scene->sunReason() : QStringLiteral("none");
    QString text;
    if (isSun && reason == QLatin1String("pinned"))
        text = tr("This light IS the sun (chosen by hand in the World panel).");
    else if (isSun)
        text = tr("This light IS the sun — the lowest Forward Shading Priority in the scene.");
    else if (sun)
        text = tr("Secondary light. \"%1\" is the sun (priority %2); this one lights the scene "
                  "but casts no shadow.")
                   .arg(sun->getName())
                   .arg(sun->forwardShadingPriority);
    else
        text = tr("This light is not in a scene yet.");
    sunReadout->setText(text);
}

QString LightPropertyWidget::evalShadowTypeName(iris::ShadowMapType shadowType)
{
    switch(shadowType){
    case iris::ShadowMapType::Hard:
        return "Hard";
    case iris::ShadowMapType::Soft:
        return "Soft";
    case iris::ShadowMapType::VerySoft:
        return "Very Soft";
    }

    return "Soft";
}

iris::ShadowMapType LightPropertyWidget::evalShadowMapType(QString shadowType)
{
    if (shadowType=="Hard")
        return iris::ShadowMapType::Hard;
    if (shadowType=="Very Soft")
        return iris::ShadowMapType::VerySoft;

    return iris::ShadowMapType::Soft;
}
