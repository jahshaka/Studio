/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/materialpropertywidget.h"
#include "data/project.h"

#include <QJsonObject>
#include <QDirIterator>
#include <QHBoxLayout>
#include <QPushButton>

#include "ui/controls/accordionbladewidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/colorpickerwidget.h"
#include "ui/controls/colorvaluewidget.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/texturepickerwidget.h"
#include "ui/controls/labelwidget.h"
#include "ui/panels/propertywidget.h"

#include "data/constants.h"
#include "io/assetiobase.h"

#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "io/builtinmaterials.h"
#include "irisgl/core/properties/property.h"

#include "services/services.h"
#include "services/undoservice.h"
#include "services/sceneeditservice.h"
#include "services/materialdefaults.h"
#include "commands/changematerialpropertycommand.h"

#include "io/scenewriter.h"

#include "data/database/database.h"
#include "io/materialreader.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include <QSqlDatabase>

// WHAT THE SHOWN MATERIAL'S TEXTURE ROWS HELD, recorded fresh every time the
// shown material changes.
//
// This map is the "before" side of updateTextureDependency: when an edit clears
// a texture row, the project dependency to be DELETED is looked up from the
// path the row used to hold. It was only ever inserted into — never cleared,
// and never retaken when the MATERIAL PICKER swapped the material under the
// same panel — while the properties panel keeps its blades as hidden children
// and reuses them across selections (the selection-stall fix), so one instance
// sees every mesh the user ever clicks. Two consequences, both real:
//
//   * selecting a light, a camera or an empty takes the early return in
//     setSceneNode and used to leave the previous MESH's texture paths in
//     place, describing a material the panel is no longer showing;
//   * picking a different material in the dropdown replaces `material` and used
//     to leave this map describing the OLD one, so the first texture edit
//     afterwards deleted a dependency belonging to the material just left.
//
// So it is retaken at every point where the shown material changes, and emptied
// when there is no material to describe.
void MaterialPropertyWidget::snapshotTextures()
{
    existingTextures.clear();
    if (!material) return;
    for (auto prop : material->properties)
        if (prop->type == iris::PropertyType::Texture)
            existingTextures.insert(prop->name, prop->getValue().toString());
}

void MaterialPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
    if (!(!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh)) {
        meshNode.clear();
        material.clear();
        snapshotTextures();
        return;
    }

    meshNode = sceneNode.staticCast<iris::MeshNode>();
    material = meshNode->getMaterial();
    meshNodeGuid = meshNode->getGUID();
    snapshotTextures();
    if (!material) return;

    setupShaderSelector();
    addResetRow();
    setWidgetProperties();
}

void MaterialPropertyWidget::addResetRow()
{
    // THE NODE'S OWN DEFAULT (services/materialdefaults.h): only a node that
    // HAS one gets the action — the scene's default floor today. The button is
    // material.reset's panel face: it calls the same SceneEditService function
    // (one undo step), and the panel repaints from the undo stack's hook.
    const QString provider = materialdefaults::providerName(meshNode);
    resetButton = nullptr;
    if (provider.isEmpty()) return;
    auto *row = new QWidget;
    row->setObjectName(QStringLiteral("MaterialResetRow"));
    auto *line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->addStretch();
    resetButton = new QPushButton(tr("Reset to %1").arg(provider), row);
    resetButton->setObjectName(QStringLiteral("MaterialResetButton"));
    resetButton->setToolTip(tr("Clears the material applied to this %1 and restores its own "
                               "default (the checker). One undo step.")
                                .arg(provider.toLower()));
    line->addWidget(resetButton);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        if (meshNode && services && services->sceneEdit)
            services->sceneEdit->resetMaterial(meshNode);
    });
    this->addWidgetToContent(row);
}

void MaterialPropertyWidget::forceShaderRefresh(const QString &materialName)
{
    emit materialChanged(materialName);
}

void MaterialPropertyWidget::setWidgetProperties()
{
    materialPropWidget = this->addPropertyWidget();
    materialPropWidget->setListener(this);

    auto mat = currentMaterial();
    if (!mat) return;

    // ---- MATERIAL_GAPS_SPEC GAP 2: detail layers get their OWN section ----
    // Two layers x eight rows plus the weight map is seventeen rows, and they
    // are all secondary to the base surface. They go in a nested accordion
    // blade that starts COLLAPSED, so a material that uses no detail layer
    // shows exactly the panel it always did plus one closed header.
    //
    // The split is by ROW NAME, from the document's own naming rule
    // (PbrMaterial::detailRow), not by index or position: adding a base row
    // later must not silently push a detail row into the wrong section.
    QList<iris::Property *> base, details;
    for (auto *prop : mat->properties) {
        if (!prop) continue;
        const bool isDetail = prop->name.startsWith(QStringLiteral("detail"));
        (isDetail ? details : base).append(prop);
    }

    materialPropWidget->setProperties(base);

    if (!details.isEmpty()) {
        auto *section = this->addSection(tr("Detail Layers"));
        detailPropWidget = section->addPropertyWidget();
        detailPropWidget->setListener(this);
        detailPropWidget->setProperties(details);
        // The one thing a user cannot see from the rows: detail tiling is the
        // per-layer Scale, NOT the material's Texture Scale (D-5 / §3.3).
        section->setToolTip(tr(
            "A detail layer is a second map blended into the base colour, with its own "
            "blend mode, tiling and weight.\n"
            "Tile a detail map with its own Scale rows — the material's Texture Scale "
            "tiles the BASE maps only and deliberately does not reach detail UVs.\n"
            "A detail Normal Map needs a mesh with tangents, and only ever adds to the "
            "base normal map."));
    }
}

void MaterialPropertyWidget::materialChanged(const QString &text)
{
    Q_UNUSED(text)
}

// The picker now chooses a MATERIAL, not a shader (HLMS_ADOPTION P4b): a
// reserved builtin guid resolves to its PbrMaterial preset, a Shader asset to
// the baked PbrMaterial its graph evaluates to. It used to swap the shader
// definition inside a CustomMaterial, which is a thing that no longer exists.
void MaterialPropertyWidget::materialChanged(int index)
{
    Q_UNUSED(index);
    if (!meshNode) return;
    const QString guid = materialSelector->getCurrentItemData();
    clearPanel(this->layout());

    MaterialReader reader;
    reader.setProject(project);
    iris::MaterialPtr picked = BuiltinMaterials::isBuiltin(guid)
                                   ? reader.createMaterialFromShaderGuid(guid, db)
                                         .staticCast<iris::Material>()
                                   : reader.parseShaderAsPbr(guid, db);
    // A graph asset with no baked material yet (a definition predating the
    // evaluator) must not silently blank the mesh: keep what it had.
    if (!picked) { setupShaderSelector(); setWidgetProperties(); return; }

    picked->setName(materialSelector->getCurrentItem());
    picked->setGuid(guid);
    material = picked;
    // The shown material just changed, so the texture-row snapshot describes
    // the wrong material until it is retaken.
    snapshotTextures();
    meshNode->setMaterial(material);
    setupShaderSelector();

    QJsonObject node;
    SceneWriter::writeSceneNode(node, meshNode, false);

    db->updateAssetAsset(meshNode->getGUID(), QJsonDocument(node).toJson());
    db->removeDependenciesByType(meshNode->getGUID(), ModelTypes::Shader);

    // Don't create dependencies to builtins — they ship with the app.
    if (!BuiltinMaterials::isBuiltin(guid)) {
        db->createDependency(
            static_cast<int>(ModelTypes::Object),
            static_cast<int>(ModelTypes::Shader),
            meshNodeGuid, guid,
            project->getProjectGuid()
        );
    }

    for (auto prop : material->properties) {
        if (prop->type != iris::PropertyType::Texture) continue;
        auto guidValue = prop->getValue().toString();
        if (guidValue.isEmpty() || QFile::exists(guidValue)) continue;
        // guid-valued texture reference: resolve through the CAS (pinned in
        // project context, else the library source). The flat projectFolder
        // + row-name join that followed is gone (plan item 15c).
        const QString path = AssetCas::resolvePinned(QSqlDatabase::database(),
                                                     AssetStorePaths::root(),
                                                     project->getProjectGuid(), guidValue);
        if (!path.isEmpty() && QFile::exists(path))
            material->setValue(prop->name, path);
    }

    setWidgetProperties();
}

void MaterialPropertyWidget::setupShaderSelector()
{
    // "Material", not "Shader": the entries are the reserved builtin PRESETS
    // and the project's graph-backed material assets. Nothing here selects a
    // shader any more (HLMS_ADOPTION P4b).
    materialSelector = this->addComboBox("Material");

    QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
    while (it.hasNext()) {
        it.next();
        materialSelector->addItem(QFileInfo(it.value()).baseName(), it.key());
    }

    for (auto asset : AssetManager::getAssets()) {
        if (asset->type == ModelTypes::Shader) {
            materialSelector->addItem(QFileInfo(asset->fileName).baseName(), asset->assetGuid);
        }
    }

    if (material) materialSelector->setCurrentItemData(material->getGuid());

    connect(materialSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(materialChanged(int)));
}

void MaterialPropertyWidget::onPropertyChanged(iris::Property *prop)
{
    if (!material) return;
    // Material::setValue is the ONLY bridge onto the real fields the mirror
    // reads (SceneMirror::toPbrParams reads pbr->textureScale etc., not the
    // Property list). Writing the Property object alone changes what gets SAVED
    // but not what RENDERS — that was the dead material panel: edits appeared to
    // do nothing live and only showed up after a scene reload rebuilt the
    // material from JSON through setValue.
    material->setValue(prop->name, prop->getValue());
    if (prop->type == iris::PropertyType::Texture)
        updateTextureDependency(prop);
}

// Keep the project database's object->texture dependency in step with a texture
// property edit (the packaged .jaf carries the texture because of this row).
// Works for both the shader-graph material and generic materials.
void MaterialPropertyWidget::updateTextureDependency(iris::Property *prop)
{
    if (!db || !project) return;

    // The texture row holds a RESOLVED PATH; the asset behind it is found the
    // way the scene writer finds it — through the store object's oid, never by
    // the file's NAME (plan item 15c: a pinned texture's file is called
    // <sha256>.<ext>, so the by-name lookup that was here matched nothing for
    // any image a user imported, and both dependency writes below were dead).
    auto textureGuidFor = [this](const QString &path) {
        if (path.isEmpty() || project->getProjectGuid().isEmpty()) return QString();
        return AssetCas::guidForStorePath(QSqlDatabase::database(), AssetStorePaths::root(),
                                          path, project->getProjectGuid(),
                                          AssetCas::GuidPreference::Texture);
    };

    // HANDLE CASE where the widget isn't deselected
    const QString assetGuid = textureGuidFor(prop->getValue().toString());
    if (assetGuid.isEmpty()) {
        const QString previous = textureGuidFor(existingTextures.value(prop->name));
        if (!previous.isEmpty()) db->deleteDependency(meshNodeGuid, previous);
    }
    else {
        db->createDependency(
            static_cast<int>(ModelTypes::Object),
            static_cast<int>(ModelTypes::Texture),
            meshNodeGuid, assetGuid,
            project->getProjectGuid()
        );
    }
}

void MaterialPropertyWidget::onPropertyChangeStart(iris::Property* prop)
{
    startValue = prop->getValue();
}

void MaterialPropertyWidget::onPropertyChangeEnd(iris::Property* prop)
{
    // A gesture that ended on its starting value (slider pressed and released
    // in place, colour dialog cancelled, Enter on an unchanged field) is not
    // an edit - don't pollute the undo stack with a no-op command.
    if (startValue == prop->getValue()) return;

    if (services && services->undo)
        services->undo->push(new ChangeMaterialPropertyCommand(material, prop->name, startValue, prop->getValue()));
}
