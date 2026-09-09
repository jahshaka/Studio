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
    setWidgetProperties();
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
    if (!!mat)
        materialPropWidget->setProperties(mat->properties);
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
        // project context); the flat projectFolder join stays as a last-resort
        // fallback for pre-pipeline projects.
        QSqlDatabase conn = QSqlDatabase::database();
        QString path = AssetCas::resolvePinned(conn, AssetStorePaths::root(),
                                               project->getProjectGuid(), guidValue);
        if (path.isEmpty())
            path = AssetCas::resolveSource(conn, AssetStorePaths::root(), guidValue);
        if (path.isEmpty())
            path = QDir(project->getProjectFolder()).filePath(db->fetchAsset(guidValue).name);
        if (QFile::exists(path))
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

    // HANDLE CASE where the widget isn't deselected
    QString assetGuid = db->fetchAssetGUIDByName(QFileInfo(prop->getValue().toString()).fileName(), project->getProjectGuid());
    if (assetGuid.isEmpty()) {
        db->deleteDependency(
            meshNodeGuid,
            db->fetchAssetGUIDByName(QFileInfo(existingTextures.value(prop->name)).fileName(), project->getProjectGuid())
        );
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
