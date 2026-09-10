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
#include "ui/panels/propertywidgets/rowundo.h"

// The five scalar rows write REFLECTED decal properties (`width`, `height`,
// `depth`, `metalness`, `roughness`, `ignoreAlphaDiffuse` — the same keys
// node.setProperty writes), so each gesture is one SetNodePropertyCommand
// (debt L6). The three image rows go through SceneEditService, which owns the
// dependency row, so they record the service call instead — the shape
// node.setDecalMaps uses.
DecalPropertyWidget::DecalPropertyWidget(QWidget *parent)
    : AccordianBladeWidget(parent),
      rows([this]() { return iris::SceneNodePtr(decalNode); }, [this]() { return services; },
           [this]() { return !loading; })
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
    rowundo::bind(width,  rows(QStringLiteral("width")));
    rowundo::bind(height, rows(QStringLiteral("height")));
    rowundo::bind(depth,  rows(QStringLiteral("depth")));
    rowundo::bind(metalness, rows(QStringLiteral("metalness")));
    rowundo::bind(roughness, rows(QStringLiteral("roughness")));
    rowundo::bind(ignoreAlpha, rows(QStringLiteral("ignoreAlphaDiffuse")));
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

// One map rebind, undoable. The service owns the dependency ROW (it knows the
// node's asset guid); this panel only has to keep the node and the CAS path in
// step, and — since the write is a service call and not a field — the undo step
// replays the call (NodeEditCommand's contract, as node.setDecalMaps does).
void DecalPropertyWidget::bindKind(DecalMapKind kind, const QString &guid, const QString &text)
{
    if (loading || !decalNode) return;
    QString *guidField = kind == DecalMapKind::Normal     ? &decalNode->normalGuid
                       : kind == DecalMapKind::Emissive   ? &decalNode->emissiveGuid
                                                          : &decalNode->textureGuid;
    const QString before = *guidField;
    if (before == guid) return;

    if (!services || !services->sceneEdit) {
        // No service (headless hosts, the panel suites): keep the node and the
        // resolved path in step by hand, exactly as this row always did.
        QString *pathField = kind == DecalMapKind::Normal   ? &decalNode->resolvedNormalPath
                           : kind == DecalMapKind::Emissive ? &decalNode->resolvedEmissivePath
                                                            : &decalNode->resolvedTexturePath;
        bindMap(guid, *guidField, *pathField);
        return;
    }
    auto node = decalNode;
    SceneEditService *edit = services->sceneEdit;
    edit->setDecalMap(node, kind, guid);
    panelundo::pushEdit(services, text,
                        [edit, node, kind, guid]() { edit->setDecalMap(node, kind, guid); },
                        [edit, node, kind, before]() { edit->setDecalMap(node, kind, before); });
}

void DecalPropertyWidget::onImageChanged(const QString &path, const QString &guid)
{
    Q_UNUSED(path);
    bindKind(DecalMapKind::Diffuse, guid, tr("Decal Image"));
}

void DecalPropertyWidget::onNormalChanged(const QString &path, const QString &guid)
{
    Q_UNUSED(path);
    // MATERIAL_GAPS_SPEC §4 item 3: these two rows used to call bindMap
    // directly, which pinned the asset but wrote NO dependency row and removed
    // none when the guid changed or was cleared. They go through the same
    // service the image row does.
    bindKind(DecalMapKind::Normal, guid, tr("Decal Normal Map"));
}

void DecalPropertyWidget::onEmissiveChanged(const QString &path, const QString &guid)
{
    Q_UNUSED(path);
    bindKind(DecalMapKind::Emissive, guid, tr("Decal Emissive Map"));
}

// The scalar rows are wired through rowundo (the reflected keys above); these
// slots stay only because the .ui-era connections named them.
void DecalPropertyWidget::onWidthChanged(float v) { Q_UNUSED(v) }
void DecalPropertyWidget::onHeightChanged(float v) { Q_UNUSED(v) }
void DecalPropertyWidget::onDepthChanged(float v) { Q_UNUSED(v) }
void DecalPropertyWidget::onMetalnessChanged(float v) { Q_UNUSED(v) }
void DecalPropertyWidget::onRoughnessChanged(float v) { Q_UNUSED(v) }
void DecalPropertyWidget::onIgnoreAlphaChanged(bool v) { Q_UNUSED(v) }
