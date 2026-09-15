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
#include <QFileInfo>
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
#include "irisgl/document/scenegraph/meshnode.h"
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

// SHOWING ANOTHER MESH'S MATERIAL IS A REFILL, NOT A REBUILD (ADD-1, 2026-09-15).
//
// THE MEASUREMENT: a pick between two cubes cost 44.3 ms and changed nothing on
// screen but the numbers. All of it was here — the Material combo built again
// with its forty items, its popup view and its item delegate; a fresh
// PropertyWidget with twenty-five rows; the destruction of the previous
// twenty-five; and the text shaping of every label of both. Every scripted
// `scene.addPrimitive` paid it too, because an add selects the node it makes.
//
// A material's rows are decided by its property list's SHAPE, and every
// primitive in a scene has the same shape, so the rows that are already here
// are the rows the next mesh needs. The refill points them at the new
// material's properties (PropertyWidget::rebind), sets the combo's current item
// without rebuilding its list, and leaves the "Detail Layers" section exactly
// where it is. Nothing is destroyed, so the property-row registry, the live
// filter and the expand snapshot never notice a pick happened.
//
// A SHAPE CHANGE still rebuilds — a different material class, a mesh with no
// material at all, a node that gained or lost its "Reset to <provider>" row,
// or a library that has grown a new material asset since the combo was filled.
// The rebuild is the same code it always was, so the fallback is never a
// second implementation.
void MaterialPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
    if (!(!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh)) {
        meshNode.clear();
        material.clear();
        snapshotTextures();
        clearShownRows();
        return;
    }

    const auto newNode = sceneNode.staticCast<iris::MeshNode>();
    const iris::MaterialPtr newMaterial = newNode->getMaterial();

    if (!newMaterial) {
        meshNode = newNode;
        material.clear();
        meshNodeGuid = newNode->getGUID();
        snapshotTextures();
        clearShownRows();
        return;
    }

    if (rebindTo(newNode, newMaterial)) { ++refills; return; }

    ++rebuilds;
    clearShownRows();
    meshNode = newNode;
    material = newMaterial;
    meshNodeGuid = meshNode->getGUID();
    snapshotTextures();

    setupShaderSelector();
    addResetRow();
    setWidgetProperties();
}

/// The rows this blade is showing go away. The one place that drops them, so
/// the "who cleared the panel" question has one answer (it used to be the
/// properties panel HOST, reaching in before every mesh pick).
void MaterialPropertyWidget::clearShownRows()
{
    materialSelector = nullptr;
    materialPropWidget = nullptr;
    detailPropWidget = nullptr;
    resetButton = nullptr;
    clearPanel(this->layout());
}

/// The base and detail halves of a material's rows, split by the document's own
/// naming rule (PbrMaterial::detailRow) — see setWidgetProperties.
void MaterialPropertyWidget::splitRows(const iris::MaterialPtr &mat,
                                       QList<iris::Property *> &base,
                                       QList<iris::Property *> &details)
{
    if (!mat) return;
    for (auto *prop : mat->properties) {
        if (!prop) continue;
        (prop->name.startsWith(QStringLiteral("detail")) ? details : base).append(prop);
    }
}

bool MaterialPropertyWidget::rebindTo(const QSharedPointer<iris::MeshNode> &node,
                                      const iris::MaterialPtr &mat)
{
    if (!materialPropWidget || !materialSelector) return false;

    QList<iris::Property *> base, details;
    splitRows(mat, base, details);
    if (!materialPropWidget->canRebind(base)) return false;
    // The detail section is built only when the material declares detail rows,
    // so its PRESENCE is part of the shape.
    if (details.isEmpty() != (detailPropWidget == nullptr)) return false;
    if (detailPropWidget && !detailPropWidget->canRebind(details)) return false;
    // "Reset to <provider>" is the NODE's row, not the material's.
    if (materialdefaults::providerName(node).isEmpty() != resetButton.isNull()) return false;
    if (!resetButton.isNull()
        && resetButton->text() != tr("Reset to %1").arg(materialdefaults::providerName(node)))
        return false;
    // THE COMBO'S ITEM LIST is the builtin presets plus the project's material
    // assets, and the library moves while this blade is alive. Comparing the
    // COUNT alone was wrong (COMBO-FP-1, from the ADD-1 second read): a renamed
    // asset keeps the count and the combo kept the old label; a deleted one
    // replaced by another kept the count too, and then
    // setCurrentItemData(guid) of a guid no longer in the list is findData
    // -1 — a BLANK Material combo on a node whose material was perfectly
    // fine. What decides staleness is the (guid, label) LIST, so that is what
    // is compared; a miscompare is only ever a rebuild.
    if (comboItemsKey() != materialItemsKey()) return false;

    meshNode = node;
    material = mat;
    meshNodeGuid = node->getGUID();
    snapshotTextures();
    materialPropWidget->rebind(base);
    if (detailPropWidget) detailPropWidget->rebind(details);
    {
        // setCurrentItemData drives currentIndexChanged into materialChanged(int),
        // which would rebuild the panel and rewrite the project's dependency
        // rows — this is a refill, not a pick.
        const QSignalBlocker block(materialSelector);
        materialSelector->setCurrentItemData(material->getGuid());
    }
    return true;
}

/// The entries setupShaderSelector would put in the Material combo right now,
/// guid and label, in its order — see the header. The separators are ASCII unit
/// and record separators so no name or guid can forge a boundary.
QString MaterialPropertyWidget::materialItemsKey() const
{
    QString key;
    QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
    while (it.hasNext()) {
        it.next();
        key += it.key() + QLatin1Char('\x1f') + QFileInfo(it.value()).baseName()
             + QLatin1Char('\x1e');
    }
    for (auto asset : AssetManager::getAssets()) {
        if (!asset || asset->type != ModelTypes::Shader) continue;
        key += asset->assetGuid + QLatin1Char('\x1f') + QFileInfo(asset->fileName).baseName()
             + QLatin1Char('\x1e');
    }
    return key;
}

/// The same list, read off the combo that is on screen.
QString MaterialPropertyWidget::comboItemsKey() const
{
    QString key;
    QComboBox *box = materialSelector ? materialSelector->getWidget() : nullptr;
    if (!box) return key;
    for (int i = 0; i < box->count(); ++i)
        key += box->itemData(i).toString() + QLatin1Char('\x1f') + box->itemText(i)
             + QLatin1Char('\x1e');
    return key;
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
    this->addWidgetToContent(row, tr("Material"),
                             { QStringLiteral("reset"), QStringLiteral("assign") });
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
    splitRows(mat, base, details);

    materialPropWidget->setProperties(base);

    detailPropWidget = nullptr;
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

// The picker now chooses a MATERIAL, not a shader (HLMS_ADOPTION P4b): a
// reserved builtin guid resolves to its PbrMaterial preset, a Shader asset to
// the baked PbrMaterial its graph evaluates to. It used to swap the shader
// definition inside a CustomMaterial, which is a thing that no longer exists.
void MaterialPropertyWidget::materialChanged(int index)
{
    Q_UNUSED(index);
    if (!meshNode) return;
    // READ THE COMBO BEFORE IT GOES. clearShownRows retires this row with the
    // rest, and reading a retired widget later (the panel did, for the name)
    // only worked because deleteLater had not run yet.
    const QString guid = materialSelector->getCurrentItemData();
    const QString pickedName = materialSelector->getCurrentItem();
    clearShownRows();

    MaterialReader reader;
    reader.setProject(project);
    iris::MaterialPtr picked = BuiltinMaterials::isBuiltin(guid)
                                   ? reader.createMaterialFromShaderGuid(guid, db)
                                         .staticCast<iris::Material>()
                                   : reader.parseShaderAsPbr(guid, db);
    // A graph asset with no baked material yet (a definition predating the
    // evaluator) must not silently blank the mesh: keep what it had.
    if (!picked) { setupShaderSelector(); addResetRow(); setWidgetProperties(); return; }

    picked->setName(pickedName);
    picked->setGuid(guid);
    material = picked;
    // The shown material just changed, so the texture-row snapshot describes
    // the wrong material until it is retaken.
    snapshotTextures();
    meshNode->setMaterial(material);
    setupShaderSelector();
    // ...AND THE RESET ROW COMES BACK. clearShownRows drops it with every other
    // row, and this path never rebuilt it: picking a material from the dropdown
    // left the default floor without its "Reset to Floor" button until the next
    // selection change put it back (found by ADD-1's shape comparison, which
    // needs the two build paths to produce the SAME panel).
    addResetRow();

    // THE PICK ALWAYS REACHES THE NODE; the library bookkeeping below needs a
    // library (lane DBPTR-1). This blade is built on the first mesh selection
    // and `setDatabase` is called on it right after — but it is a combo slot,
    // so it can fire in any host that never handed one down, and the member
    // used to be uninitialised rather than null.
    if (db && project) {
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
    }

    for (auto prop : material->properties) {
        if (!project) break;
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
    // A GESTURE THAT REALLY STARTED (round 2, item 9). The edit gate makes
    // these rows inert while a script runs — the row's own handlers return
    // before this is ever called — so a picker held open ACROSS the end of a
    // run would have reached the End below with a startValue left over from
    // the last gesture before the script, and pushed a command undoing to a
    // value the user never saw. One flag: the End half only records what this
    // half opened.
    gestureOpen = true;
}

void MaterialPropertyWidget::onPropertyChangeEnd(iris::Property* prop)
{
    if (!gestureOpen) return;       // never started (see onPropertyChangeStart)
    gestureOpen = false;
    // A gesture that ended on its starting value (slider pressed and released
    // in place, colour dialog cancelled, Enter on an unchanged field) is not
    // an edit - don't pollute the undo stack with a no-op command.
    if (startValue == prop->getValue()) return;

    if (services && services->undo)
        services->undo->push(new ChangeMaterialPropertyCommand(material, prop->name, startValue, prop->getValue()));
}
