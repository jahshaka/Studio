/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/decalpropertywidget.h"

#include <QSqlDatabase>

#include "ui/controls/checkboxwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/texturepickerwidget.h"

#include "data/database/database.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/projectassets.h"
#include "services/services.h"
#include "services/sceneeditservice.h"

#include "irisgl/document/scenegraph/decalnode.h"
#include "irisgl/document/scenegraph/scenenode.h"

DecalPropertyWidget::DecalPropertyWidget(QWidget *parent)
    : AccordianBladeWidget(parent)
{
    image = this->addTexturePicker("Decal Image");
    width  = this->addFloatValueSlider("Width",  0.05f, 20.0f);
    height = this->addFloatValueSlider("Height", 0.05f, 20.0f);
    depth  = this->addFloatValueSlider("Depth",  0.05f, 20.0f);
    metalness = this->addFloatValueSlider("Metalness", 0.0f, 1.0f);
    roughness = this->addFloatValueSlider("Roughness", 0.0f, 1.0f);

    normalImage = this->addTexturePicker("Normal Map");
    emissiveImage = this->addTexturePicker("Emissive Map");
    ignoreAlpha = this->addCheckBox("Alpha masks colour only");

    width->setToolTip(QStringLiteral(
        "The projector box's LOCAL X extent, in world units. The node's own scale "
        "(the scale gizmo) multiplies it."));
    height->setToolTip(QStringLiteral(
        "The projector box's LOCAL Z extent — the image's V axis. A decal projects "
        "straight down its node's -Y, like a light, onto surfaces facing back at it."));
    depth->setToolTip(QStringLiteral(
        "How far the decal reaches along its projection axis (the box's local Y extent). "
        "Surfaces outside the box are untouched."));
    // The honest caveat, in the panel rather than as a silent no-op
    // (DECALS_SPEC §3 row 7): the shader gates decal normals on the RECEIVING
    // material already having a normal map.
    normalImage->setToolTip(QStringLiteral(
        "Optional. Only visible on receiving materials that ALREADY have a normal map — "
        "the renderer drops decal normals entirely on materials that do not."));
    emissiveImage->setToolTip(QStringLiteral(
        "Optional. Adds glow inside the decal box."));
    ignoreAlpha->setToolTip(QStringLiteral(
        "When on, the image's alpha masks the base colour only and leaves the normal / "
        "emissive maps unmasked. Only meaningful with one of those bound."));

    connect(image, &TexturePickerWidget::valuesChanged, this, &DecalPropertyWidget::onImageChanged);
    connect(normalImage, &TexturePickerWidget::valuesChanged, this, &DecalPropertyWidget::onNormalChanged);
    connect(emissiveImage, &TexturePickerWidget::valuesChanged, this, &DecalPropertyWidget::onEmissiveChanged);
    connect(width,  SIGNAL(valueChanged(float)), this, SLOT(onWidthChanged(float)));
    connect(height, SIGNAL(valueChanged(float)), this, SLOT(onHeightChanged(float)));
    connect(depth,  SIGNAL(valueChanged(float)), this, SLOT(onDepthChanged(float)));
    connect(metalness, SIGNAL(valueChanged(float)), this, SLOT(onMetalnessChanged(float)));
    connect(roughness, SIGNAL(valueChanged(float)), this, SLOT(onRoughnessChanged(float)));
    connect(ignoreAlpha, SIGNAL(valueChanged(bool)), this, SLOT(onIgnoreAlphaChanged(bool)));
}

void DecalPropertyWidget::setProject(Project *proj)
{
    project = proj;
    // The pickers need the live Project for their drop handler.
    if (image) image->project = proj;
    if (normalImage) normalImage->project = proj;
    if (emissiveImage) emissiveImage->project = proj;
}

void DecalPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!sceneNode || sceneNode->getSceneNodeType() != iris::SceneNodeType::Decal) {
        decalNode.reset();
        return;
    }
    decalNode = sceneNode.staticCast<iris::DecalNode>();

    // Loading guard: setValue/setTexture fire the same signals the user's edits
    // do, and without this the first selection writes the widget defaults back
    // into the node.
    loading = true;
    width->setValue(decalNode->width);
    height->setValue(decalNode->height);
    depth->setValue(decalNode->depth);
    metalness->setValue(decalNode->metalness);
    roughness->setValue(decalNode->roughness);
    ignoreAlpha->setValue(decalNode->ignoreAlphaDiffuse);
    image->textureGuid = decalNode->textureGuid;
    image->setTexture(decalNode->resolvedTexturePath);
    normalImage->textureGuid = decalNode->normalGuid;
    normalImage->setTexture(decalNode->resolvedNormalPath);
    emissiveImage->textureGuid = decalNode->emissiveGuid;
    emissiveImage->setTexture(decalNode->resolvedEmissivePath);
    loading = false;
}

void DecalPropertyWidget::bindMap(const QString &guid, QString &guidField, QString &pathField)
{
    guidField = guid;
    pathField.clear();
    if (guid.isEmpty() || !db || !project || project->getProjectGuid().isEmpty()) return;
    // BINDING membership: binding an image to a decal must NOT mint the
    // companion PBR material a direct add would (ProjectAssets::AddKind).
    ProjectAssets::addToProject(guid, db, project, ProjectAssets::AddKind::Binding);
    pathField = AssetCas::resolvePinned(QSqlDatabase::database(), AssetStorePaths::root(),
                                        project->getProjectGuid(), guid);
}

void DecalPropertyWidget::onImageChanged(const QString &path, const QString &guid)
{
    Q_UNUSED(path);
    if (loading || !decalNode) return;
    // The service owns the dependency ROW (it knows the node's asset guid); this
    // panel only has to keep the node and the CAS path in step.
    if (services && services->sceneEdit) {
        services->sceneEdit->setDecalTexture(decalNode, guid);
        return;
    }
    bindMap(guid, decalNode->textureGuid, decalNode->resolvedTexturePath);
}

void DecalPropertyWidget::onNormalChanged(const QString &path, const QString &guid)
{
    Q_UNUSED(path);
    if (loading || !decalNode) return;
    bindMap(guid, decalNode->normalGuid, decalNode->resolvedNormalPath);
}

void DecalPropertyWidget::onEmissiveChanged(const QString &path, const QString &guid)
{
    Q_UNUSED(path);
    if (loading || !decalNode) return;
    bindMap(guid, decalNode->emissiveGuid, decalNode->resolvedEmissivePath);
}

void DecalPropertyWidget::onWidthChanged(float v)
{
    if (loading || !decalNode) return;
    decalNode->setPropertyValue(QStringLiteral("width"), v);
}

void DecalPropertyWidget::onHeightChanged(float v)
{
    if (loading || !decalNode) return;
    decalNode->setPropertyValue(QStringLiteral("height"), v);
}

void DecalPropertyWidget::onDepthChanged(float v)
{
    if (loading || !decalNode) return;
    decalNode->setPropertyValue(QStringLiteral("depth"), v);
}

void DecalPropertyWidget::onMetalnessChanged(float v)
{
    if (loading || !decalNode) return;
    decalNode->setPropertyValue(QStringLiteral("metalness"), v);
}

void DecalPropertyWidget::onRoughnessChanged(float v)
{
    if (loading || !decalNode) return;
    decalNode->setPropertyValue(QStringLiteral("roughness"), v);
}

void DecalPropertyWidget::onIgnoreAlphaChanged(bool v)
{
    if (loading || !decalNode) return;
    decalNode->setPropertyValue(QStringLiteral("ignoreAlphaDiffuse"), v);
}
