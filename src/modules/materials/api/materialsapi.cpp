/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/qtinterop.h"
#include "irisgl/core/math/vec.h"
#include "modules/materials/api/materialsapi.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "scripting/modules/moduleshared.h"
#include "data/constants.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/materialpreset.h"
#include "data/project.h"
#include "commands/changematerialpropertycommand.h"
#include "bridge/enginehost.h"
#include "io/assetmanager.h"
#include "io/materialpresetreader.h"
#include "shell/mainwindow.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/imagematerial.h"
#include "services/livetextures.h"
#include "services/projectassets.h"
#include "../core/graphdefinition.h"
#include "services/shippedassets.h"
#include "services/materialbundle.h"
#include "services/projectfolders.h"
#include "services/materialpresetassets.h"
#include "services/presetedit.h"
#include "services/materialpresetseeder.h"
#include "services/materialmembers.h"
#include "services/memberstamp.h"
#include "bridge/previewenvironment.h"
#include "services/materialtile.h"
#include "services/thumbnailrebuild.h"
#include "services/materialdefaults.h"
#include "io/materialpresets.h"
#include "services/materialpreviewservice.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "viewport/ieditorviewport.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"

#include "modules/materials/core/graphbaker.h"
#include "modules/materials/core/pieceemitter.h"
#include "modules/materials/core/materialhelper.h"
#include "modules/materials/core/pbrgraphevaluator.h"
#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/models/connectionmodel.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/models/nodemodel.h"
#include "modules/materials/models/socketmodel.h"
#include "modules/materials/nodes/pbrmasternode.h"
#include "modules/materials/nodes/test.h"     // TextureNode (B2 graph twin)

using namespace scriptmod;

namespace {

// The material.* key vocabulary, at file scope so material.set, its refusal
// message and material.properties all read the SAME lists. They used to be
// function-statics inside set(), which is how the "unknown property" error and
// the set of keys that actually work drift apart.
const QStringList kColorKeys = { "baseColor", "emissiveColor",
                                 "ambientColor", "diffuseColor", "specularColor",
                                 // MATERIAL_GAPS_SPEC GAP 1: F0 as a colour.
                                 "fresnelColor" };
/// The PBR texture slots. PbrMaterial::createProperties DOES declare all six as
/// rows today (verified at this pin), so they arrive through the declared path;
/// this list is what keeps material.set working on a material whose property
/// list does not carry them, and what the F7 refusal message quotes.
QStringList makePbrMapKeys()
{
    QStringList keys = { "baseColorMap", "metallicMap", "roughnessMap",
                         "normalMap", "emissiveMap",
                         // ADDENDUM A-5: the per-material reflection cubemap
                         // override. In the SAME list because it resolves the
                         // same way — a file path or an asset guid — even
                         // though the renderer binds it as a cube.
                         "reflectionMap" };
    // MATERIAL_GAPS_SPEC GAP 2: the detail maps take file paths and asset guids
    // exactly like the base ones, so they belong in the SAME list — that is
    // what makes material.set resolve a guid through the CAS for them, and what
    // the writableKeys answer quotes.
    for (int i = 0; i < iris::PbrMaterial::kDetailLayers; ++i) {
        keys << iris::PbrMaterial::detailRow(i, "Map");
        keys << iris::PbrMaterial::detailRow(i, "NormalMap");
    }
    keys << QStringLiteral("detailWeightMap");
    return keys;
}
const QStringList kPbrMapKeys = makePbrMapKeys();
/// The RETIRED builtin shaders' texture spellings (Default.shader declared them
/// as uniforms). They name nothing on a PbrMaterial — which is now the only
/// material class there is — and are REFUSED by name (F7) rather than falling
/// into the texture branch and complaining about a missing file.
const QStringList kLegacyMapKeys = { "diffuseTexture", "specularTexture",
                                     "normalTexture", "reflectionTexture" };
const QStringList kMapKeys = kPbrMapKeys + kLegacyMapKeys;

/// The material's declared Property rows. `properties` lives on the base class,
/// so this no longer has to guess which subclass it is holding (it used to try
/// CustomMaterial first, then PbrMaterial, and return nothing for anything else).
QList<iris::Property *> declaredProperties(const iris::MaterialPtr &material)
{
    return material ? material->properties : QList<iris::Property *>();
}

/// The keys material.set will accept on this material — the declared rows plus,
/// on a PbrMaterial only, the undeclared PBR texture slots.
QStringList writableMaterialKeys(const iris::MaterialPtr &material)
{
    QStringList keys;
    for (auto *prop : declaredProperties(material)) keys << prop->name;
    // The union, deduplicated: the six slots are declared rows on today's
    // PbrMaterial, but material.set accepts them either way, and a key listed
    // twice in an error message reads as a bug in the message.
    if (!material.dynamicCast<iris::PbrMaterial>().isNull()) keys << kPbrMapKeys;
    keys.removeDuplicates();
    return keys;
}

/// THE UV TRANSFORM'S TWO-NUMBER SPELLINGS (MATERIAL_UV_NODES_SPEC phase 3).
///
/// The document stores five scalar rows — textureScale (U), textureScaleV,
/// textureOffsetU, textureOffsetV, textureRotation — because that is what the
/// property/serialization machinery is built out of and what keeps every file
/// written before per-axis tiling loading correctly. A SCRIPT should not have
/// to know that: `material.set(n, { textureScale: [4, 1] })` is the natural
/// way to write it, and `{ textureScale: 4 }` has to keep meaning uniform
/// tiling forever, because it is what every existing script says.
///
/// So this expands the pair spellings into the scalar keys before anything
/// else looks at them. `textureOffset` exists ONLY as a pair spelling (there
/// was never a scalar key by that name), and accepts a single number too, for
/// symmetry with textureScale.
QVariantMap expandUvPairKeys(const QVariantMap &values, QString *error)
{
    auto pair = [&](const QVariant &v, double &u, double &w) {
        if (v.canConvert<QVariantList>() && v.typeId() != QMetaType::QString) {
            const QVariantList list = v.toList();
            if (list.size() != 2) return false;
            u = list[0].toDouble();
            w = list[1].toDouble();
            return true;
        }
        u = w = v.toDouble();
        return true;
    };

    QVariantMap out;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const QString &key = it.key();
        if (key != QLatin1String("textureScale") && key != QLatin1String("textureOffset")) {
            out.insert(key, it.value());
            continue;
        }
        double u = 0, w = 0;
        if (!pair(normalizeJs(it.value()), u, w)) {
            if (error)
                *error = QStringLiteral("material.set: '%1' takes a number (uniform) or a "
                                        "two-element array [u, v]").arg(key);
            return QVariantMap();
        }
        if (key == QLatin1String("textureScale")) {
            out.insert(QStringLiteral("textureScale"), u);
            out.insert(QStringLiteral("textureScaleV"), w);
        }
        else {
            out.insert(QStringLiteral("textureOffsetU"), u);
            out.insert(QStringLiteral("textureOffsetV"), w);
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------- materials.*

QVector<VerbInfo> MaterialsApi::verbs() const
{
    return {
        { "presets", "materials.presets() -> [{name, type, guid}]",
          "The built-in material presets (PBR only in engine mode); guid is the reserved id when one exists.",
          Needs::Document },
        { "previewEnvironment", "materials.previewEnvironment() -> {exposureEv, exposureChain, size, "
                                "meanRadiance, keyRadiance, viewDirection, softboxes}",
          "The ONE studio environment every material is shown in — the Materials dock's preview, a material "
          "tile and an asset tile alike. It is GENERATED (a neutral room with three soft rectangular panels, "
          "written into the preview scene as an equirect sky), so this answers with the constants that make "
          "it rather than a file name: `exposureEv` is the manual exposure in stops that puts an 18 % grey "
          "ball on the grey card, `keyRadiance` the mean incident radiance on a face turned to the camera "
          "(the exposure is derived from it), and `softboxes` the panels with their directions, half-extents "
          "in degrees and radiances.",
          Needs::Document },
        { "create", "materials.create(name, {graph, folder}) -> guid",
          "Creates a LIBRARY material bundle: ONE Material asset whose definition is its own file in the "
          "content-addressed store, naming its textures by guid. With {graph: true} the definition also carries a "
          "node graph as its PAYLOAD and the graph opens as the current one for graph.* verbs — there is no "
          "separate shader/effect asset any more, and no second row. Adding it to a project is a separate gesture "
          "(assets.addToProject), which pins the bundle and its members at the version the project took. "
          "{folder} DOES BOTH IN ONE CALL (DRAWERS-1): the material is pinned into the OPEN project and filed in "
          "that folder — a folder guid from assets.folders(), or the project's own guid (or an empty string) for "
          "the root — and both project drawers, the editor's asset tray and the Materials module's, show it at "
          "once. It is what the tray's right-click > Create Material passes. Without the key nothing is pinned.",
          Needs::Document },
        { "projectDrawer", "materials.projectDrawer() -> [{guid, name}]",
          "What the Materials module's PROJECT drawer is showing, in order — read off the widget, so a script "
          "(and scripting.e2e.tray_panel) can prove the two project drawers are ONE list. The drawer is the "
          "project's materials wherever they are filed: it has no folder navigation of its own, so a material "
          "in a folder the user made in the editor's tray is listed here all the same. Refuses (without "
          "throwing) in a session with no Materials module.",
          Needs::Window },
        { "customDrawer", "materials.customDrawer() -> [{guid, name}]",
          "What the Materials module's CUSTOM drawer is showing, in order — read off the widget. The drawer "
          "is the user's own LIBRARY materials: not one the open project holds (that is the Project drawer), "
          "not a shipped preset (Presets) and not a PROJECT'S COPY of one — the copy a project's first edit "
          "of 'Wood PBR' mints is that project's, and it is shown only in its own project's views. Refuses "
          "(without throwing) in a session with no Materials module.",
          Needs::Window },
        { "addTexture", "materials.addTexture(materialGuid, pathOrGuid, {slot}) -> textureGuid",
          "Puts an image on a material as a MEMBER. A path from anywhere on disk is imported through the one "
          "import pipeline at that moment, keyed on its CONTENT (so picking the same image twice answers the same "
          "library row — byte-identical duplicates are impossible); a guid already in the library is reused. The "
          "image is pinned into the project that holds the material. With {slot: 'baseColorMap'} it is also "
          "written into the definition and every mesh wearing the material is re-dressed — on a VALUES "
          "material only: a GRAPH material's slots come from its graph, so a slot write there is refused "
          "(the image is still imported and pinned; put it on a texture node with graph.addNode('texture') "
          "+ graph.setValue). A SHIPPED PRESET is refused outright, before anything is imported: its library row "
          "is read-only, and materials.edit gives you this project's own copy to write to.",
          Needs::Document },
        { "members", "materials.members(guid) -> [{guid, name, slot, node, role, bytes, usedBy, "
                     "pinned, member, hidden}]",
          "The bundle's members — the textures its definition names, including the maps a graph bake produced. "
          "`slot` is the master slot it fills and `node` the graph texture node that holds it (one or the "
          "other); `role` is 'baked' for a map the bake produced and 'source' for a picture; `bytes` is the "
          "stored object's size; `usedBy` counts EVERY asset and node that names it, not one project's; "
          "`member` says it arrived through a material's picker and `hidden` that it therefore folds into the "
          "bundle instead of standing as its own library tile (the owner's rule V-2).",
          Needs::Document },
        { "cleanUnused", "materials.cleanUnused(guid?, {dryRun}) -> [{guid, name, bytes, scope}]",
          "What is no longer used, and — with {dryRun: false} — its removal. DRY RUN IS THE DEFAULT: the store's "
          "standing law is that nothing goes without showing the list first. With a material guid the scope is "
          "that bundle's own born-inside textures with no user and no pin, and the LIBRARY ROW goes; with no guid "
          "it is the open project's member pins that nothing in the project uses, and only the PIN goes. BYTES "
          "are never removed here — a superseded object waits for assets.gc, which lists before it removes too.",
          Needs::Document },
        { "duplicate", "materials.duplicate(guid, {name}) -> guid",
          "Copies ONE material into a second LIBRARY bundle — the drawer's Duplicate, as a verb. The copy names "
          "the same TEXTURES (one object, 'used by 2' — sharing is what the bundle model is for; materials."
          "makeUnique gives one back its privacy), but it carries NO BAKED MAPS: a bake is born inside exactly "
          "one material and is never shared, so the copy bakes its own at its next save. `name` defaults to "
          "'<original> copy' and is numbered against the library's own material names.",
          Needs::Document },
        { "makeUnique", "materials.makeUnique(materialGuid, textureGuid) -> guid",
          "Gives THIS material its own copy of a shared texture: a second Texture row over the SAME bytes (the "
          "store is content-addressed, so this costs no disk), swapped into this material's definition "
          "everywhere it appears — master slot, bake record and graph node. Every other material that shared the "
          "picture keeps it and never notices. On a material the open project holds, the swap is a copy-on-write: "
          "the library original is untouched.",
          Needs::Document },
        { "loadGraph",
          "materials.loadGraph(guidOrPath) -> {nodes, master, name, texturesResolved, "
          "texturesImported, readOnly, editable, presetMaster}",
          "Opens a material bundle's GRAPH (a Material asset guid, a shipped PRESET by name or by its reserved "
          "guid, or a .effect/.shader file path) as the current graph for graph.* verbs. EVERY SHIPPED PRESET HAS "
          "A GRAPH and opening one costs nothing: a preset nobody has used yet has no library row, so its graph "
          "is read from the shipped file and no row is written — looking at a preset is never what seeds it. "
          "`readOnly` is true only when this graph cannot be edited AT ALL — a shipped preset with no project "
          "open, which is the one lock left on a master (PRESET-EDIT-1). With a project open a preset is that "
          "project's to edit: graph.save() makes the project its own copy of it first, keeping its name, and "
          "`presetMaster` names the shipped material behind this one (itself for a preset, the master it came "
          "from for a copy). A script that needs the guid the save will move to calls materials.edit() itself. "
          "Texture nodes "
          "naming an image by FILE rather than by guid are imported through the one content import and connected "
          "here; texturesResolved reports how many. A PRESET'S open IMPORTS NOTHING — it binds the "
          "shipped file to the node instead, so looking at a preset writes no row, pins nothing into "
          "the open project and never waits for the device; texturesImported is 0 for one and equal "
          "to texturesResolved otherwise.",
          Needs::Document },
        { "regenerate", "materials.regenerate(materialGuid) -> bool",
          "Re-evaluates and re-bakes a stored material bundle's maps (the 'cache deleted / app upgraded' recovery) "
          "and re-dresses every mesh in the open scene wearing it. The maps land as MEMBER TEXTURES in the "
          "content-addressed store, named in the definition by guid — there is no project-folder BakedMaps/ tree "
          "any more, so this works with no project open and on any machine.",
          Needs::Document },
        { "createFromImage", "materials.createFromImage(textureGuid, {graph}) -> materialGuid",
          "Creates the standard image material asset for a Texture (IMAGE_PLANE_SPEC option B1): a PBR .material "
          "with the image as baseColorMap (roughness 1, metallic 0; alpha images blend), a Material→Texture "
          "dependency row and an image-derived thumbnail. Created in the library; with a project open it is also "
          "pinned into the project (bin-visible, droppable). Direct image add-to-project runs this automatically; "
          "re-creating for the same image returns a fresh asset. With {graph: true} (B2, needs an open project) "
          "it instead creates an editable Shader GRAPH asset — texture → textureSampler → PbrMaster.BaseColor — "
          "returning the new MATERIAL's guid (one row: the graph rides its definition as a payload; opens in the "
          "Materials page; applies via the drawer/graph.toMaterial). "
          "NOT undoable.",
          Needs::Document },
        { "seedPresets", "materials.seedPresets() -> int",
          "Seeds every shipped preset into the library as its read-only bundle — the FIRST-RUN seed, "
          "on demand — and answers how many exist afterwards (20). Idempotent and normally "
          "unnecessary: the app runs this at launch, with the maps' bytes put in the store on a "
          "worker thread so the row pass has no device wait in it (services/materialpresetseeder.h). "
          "Call it when you need the rows to be there NOW — a script that counts material rows, or a "
          "library whose preset rows were removed — and it will do any work the seeder has not "
          "finished, on the calling thread.",
          Needs::Document },
        { "createFromPreset", "materials.createFromPreset(presetOrGuid, {name}) -> materialGuid",
          "Customises a SHIPPED PRESET (R18): an editable copy of it as an ordinary library material bundle, "
          "because a preset itself is read-only — the definition writer refuses one by name, not just the UI. "
          "THE COPY CARRIES THE PRESET'S GRAPH, so it opens in the node editor and every edit gesture works "
          "on it — that is what 'a custom preset is a new material based on the preset it was customised from' "
          "means (PRESET-UNIFY-1). "
          "The copy names the preset's own member textures (one object, shared) and takes the name "
          "'<Preset>-1', the suffix bumped against the material names the library already holds, unless {name} "
          "says otherwise. With a project open it is added to the project too, so it lands in the project's "
          "materials drawer and the editor's asset tray. NOT undoable (it is an asset, like an import).",
          Needs::Document },
        { "edit", "materials.edit(guidOrName) -> {guid, master, copied, editable}",
          "MAKE THIS MATERIAL EDITABLE IN THE OPEN PROJECT, and answer the guid every later edit "
          "must use. A shipped preset that a project holds is the project's to edit (the owner's "
          "rule, 2026-09-21: only the MASTER is locked), and THE FIRST EDIT IS WHAT MAKES THE "
          "COPY: this mints the project's own material bundle from the preset — keeping its NAME, "
          "sharing its member textures — moves the project's pin from the shared master to it, "
          "re-points every scene node that wore the master, and leaves the library master and "
          "every other project untouched. `copied` says whether THIS call did it; `guid` is the "
          "material to edit either way, so a script must read it back (THE GUID MOVES). "
          "Idempotent and free on anything else: an ordinary material, or a copy made earlier, is "
          "answered unchanged with no write at all. ONE UNDO STEP (the copy, the pin move and the "
          "use edges together; inside a script run it belongs to the run's own macro). Refused "
          "when no project is open — there is nowhere for the copy to live — and that is the only "
          "lock left on a preset.",
          Needs::Document },
        { "masterOf", "materials.masterOf(guidOrName) -> guid",
          "The SHIPPED PRESET behind a material: the preset's own reserved guid when `guidOrName` "
          "names one, the master a project's copy was made from (materials.edit) when it is one, "
          "and an empty string for an ordinary material. The link is recorded on the copy's row, "
          "so it survives every later edit of the copy.",
          Needs::Document },
        { "open", "materials.open(guidOrName, {scope}) -> {tab, guid, name, scope, readOnly, editable, master}",
          "Opens a material bundle in the Materials page's node editor as a TAB and makes it the active one — "
          "the drawer's double-click, as a verb. A material already open at that scope is activated, not opened "
          "twice. `scope` is 'library' or 'project' (the four-drawer rule: the project's pinned copy or the "
          "library original — two tabs if both are open); the default is 'project' when the open project pins "
          "the guid, else 'library'. A shipped preset (by name or reserved guid) opens EDITABLE when a "
          "project is open — `editable` true and `master` naming the preset — and its first edit "
          "makes that project's own copy (materials.edit); with NO project open it opens "
          "read-only, because there is nowhere for the copy to live. `tab` is the tab index. Refused when the library holds no material of that name or "
          "guid, and when the file cannot be opened at all (a graph written on a master this build "
          "no longer has) — a refusal opens no tab and changes nothing on the page.",
          Needs::Document },
        { "newMaterial", "materials.newMaterial(presetOrName?, {name}) -> {tab, guid, name, scope, readOnly}",
          "NEW MATERIAL — the Materials page's + button, as a verb, with the dialog's two answers as its "
          "arguments: which shipped preset the new material is based on (omit it for a blank graph: a "
          "master node on an empty canvas) and what to call it (the preset's name, numbered against the "
          "library's, when omitted). It is created in the LIBRARY — adding it to a project is a separate "
          "gesture — and it opens in a TAB OF ITS OWN and becomes the active one, so the material that was "
          "on screen stays open with its own graph, its own undo history and its own pending save.",
          Needs::Document },
        { "tabs", "materials.tabs() -> [{tab, guid, name, scope, active, readOnly, dirty}]",
          "The Materials page's open tabs in bar order. `dirty` = an autosave is pending on that document (it "
          "writes 1.5 s after the last edit, or on close). The anonymous new-material tab reports an empty guid.",
          Needs::Document },
        { "activate", "materials.activate(tabOrGuid) -> bool",
          "Makes a tab the active one (by index, or by guid — the first tab with that guid in bar order). The "
          "canvas, the properties panel, the material settings, the Members panel, the preview and Ctrl+Z all "
          "follow it.",
          Needs::Document },
        { "closeTab", "materials.closeTab(tabOrGuid) -> bool",
          "Closes a tab. A pending autosave is written FIRST; a read-only tab writes nothing. The document's "
          "graph, canvas and undo history are freed.",
          Needs::Document },
        { "activeTab", "materials.activeTab() -> {tab, guid, name, scope, readOnly, dirty} | null",
          "The active tab, or null with no page.",
          Needs::Document },
    };
}

// ---- the Materials page's tabs (MATERIALS_TABS_SPEC §3) ------------------

bool MaterialsApi::pageOrFail(const QString &verb)
{
    if (mPage.tabs) return true;
    fail(QStringLiteral("%1: no Materials page in this session").arg(verb));
    return false;
}

QString MaterialsApi::resolveMaterialGuid(const QString &guidOrName) const
{
    const QString wanted = guidOrName.trimmed();
    if (wanted.isEmpty()) return QString();
    // A ROW BY GUID FIRST — the cheap, common case, and it must stay cheap:
    // every open, every edit and every masterOf comes through here. A MATERIAL,
    // not any row that answers to the guid (fix round F4): a texture's or a
    // model's guid would be carried all the way to the page, where the read
    // finds no graph and the open is refused with a message about a master node.
    if (host.db) {
        const auto row = host.db->fetchAsset(wanted);
        if (!row.guid.isEmpty())
            return row.type == static_cast<int>(ModelTypes::Material) ? wanted : QString();
    }
    // THE OPEN PROJECT'S OWN MATERIAL WINS ON A NAME (PRESET-EDIT-1). A
    // project's copy of a preset keeps the PRESET'S NAME — the user sees one
    // material — so in a project that has edited "Wood PBR", that name means
    // the project's copy and not the shipped master. Only the rows this project
    // PINS, and only ones that record a master, so nothing changes for a project
    // that has copied nothing. (Everywhere else a preset name still means the
    // preset: `material.apply('Wood PBR')` is the shipped one, and every gesture
    // the user actually makes carries a guid in its drag payload.)
    if (host.db && host.isProjectOpen()) {
        const QString projectGuid = host.project->getProjectGuid();
        for (const auto &asset : host.db->fetchAssetsByViewFilter(AssetViewFilter::AssetsView)) {
            if (asset.type != static_cast<int>(ModelTypes::Material)) continue;
            if (asset.name.compare(wanted, Qt::CaseInsensitive) != 0) continue;
            // The row already carries its properties: no query per row.
            if (MaterialBundle::presetMasterOf(asset.properties).isEmpty()) continue;
            if (host.db->isAssetPinnedBy(projectGuid, asset.guid)) return asset.guid;
        }
    }
    // A SHIPPED PRESET BY NAME, or by a reserved guid whose row has not been
    // seeded yet (the drawer's own two spellings, and what every other
    // materials.* verb accepts).
    if (MaterialPresetAssets::isPreset(wanted)) return MaterialPresetAssets::guidFor(wanted);
    if (!host.db) return QString();
    // ...or a library material's NAME, which is what the user calls it.
    const auto assets = host.db->fetchAssetsByViewFilter(AssetViewFilter::AssetsView);
    for (const auto &asset : assets) {
        if (asset.type != static_cast<int>(ModelTypes::Material)) continue;
        if (asset.name.compare(wanted, Qt::CaseInsensitive) == 0) return asset.guid;
    }
    return QString();
}

QVariantMap MaterialsApi::edit(const QString &guidOrName)
{
    QVariantMap out;
    if (!host.db) { fail("materials.edit: not available in this session"); return out; }
    const QString guid = resolveMaterialGuid(guidOrName);
    if (guid.isEmpty()) {
        fail(QStringLiteral("materials.edit: no material '%1'").arg(guidOrName));
        return out;
    }
    // THE PROJECT ONLY WHEN ONE IS REALLY OPEN (presetedit.h): the live
    // Project instance keeps its guid after a close, so `isProjectOpen` is
    // the question, not the guid.
    const presetedit::Target target = presetedit::forEdit(
        host.db, host.isProjectOpen() ? host.project : nullptr, guid,
        host.services ? host.services->undo : nullptr,
        host.services ? host.services->sceneEdit : nullptr);
    if (!target.ok()) {
        fail(QStringLiteral("materials.edit: %1").arg(target.error));
        return out;
    }
    out["guid"] = target.guid;
    out["master"] = target.master;
    out["copied"] = target.copied;
    out["editable"] = true;
    // THE COPY IS A NEW TILE in the project drawer and the editor tray, and
    // the master has just left both — every drawer that lists one has to hear
    // about it (the four-drawer rule: one list, two windows).
    if (target.copied && host.services && host.services->sceneEdit)
        host.services->sceneEdit->requestAssetViewRefresh();
    return out;
}

QString MaterialsApi::masterOf(const QString &guidOrName)
{
    if (!host.db) { fail("materials.masterOf: not available in this session"); return QString(); }
    const QString guid = resolveMaterialGuid(guidOrName);
    if (guid.isEmpty()) {
        fail(QStringLiteral("materials.masterOf: no material '%1'").arg(guidOrName));
        return QString();
    }
    return presetedit::masterOf(host.db, guid);
}

QVariantMap MaterialsApi::open(const QString &guidOrName, const QVariantMap &options)
{
    QVariantMap out;
    if (!pageOrFail(QStringLiteral("materials.open"))) return out;
    static const QStringList knownOptions = { QStringLiteral("scope") };
    const QString refusal = refuseUnknownKeys(QStringLiteral("materials.open"),
                                              options, knownOptions);
    if (!refusal.isEmpty()) { fail(refusal); return out; }
    const QString scope = options.value(QStringLiteral("scope")).toString().trimmed().toLower();
    if (!scope.isEmpty() && scope != QLatin1String("library") && scope != QLatin1String("project")) {
        fail(QStringLiteral("materials.open: scope is 'library' or 'project', not '%1'").arg(scope));
        return out;
    }
    const QString guid = resolveMaterialGuid(guidOrName);
    if (guid.isEmpty()) {
        fail(QStringLiteral("materials.open: no material '%1'").arg(guidOrName));
        return out;
    }
    out = mPage.open(guid, scope);
    if (out.isEmpty()) {
        // WHAT ACTUALLY HAPPENED (TABS-SMALL-1). This used to say "no drawer
        // holds it", which is not a rule the page has: an open is not gated on
        // a tile — it refills the drawer and carries on (EffectsPage::
        // openDocument). The page answers with nothing for two reasons, and
        // the message names both rather than sending the reader to look for a
        // missing tile that was never the cause: there is no project open to
        // take a project-scope copy from, or the definition carries no graph
        // the node editor can draw (a values-only bundle, or a graph written
        // on a master this build no longer has — that one also raises a scene
        // issue, which `editor.issues()` reads).
        fail(QStringLiteral("materials.open: the page did not open '%1'%2 — either no project is "
                            "open (a project-scope copy needs one) or its definition carries no "
                            "graph the node editor can draw (see editor.issues())")
                 .arg(guidOrName, scope.isEmpty() ? QString()
                                                  : QStringLiteral(" at scope '%1'").arg(scope)));
    }
    return out;
}

QVariantMap MaterialsApi::previewEnvironment()
{
    return previewenv::describe();
}

QVariantMap MaterialsApi::newMaterial(const QString &presetOrName, const QVariantMap &options)
{
    QVariantMap out;
    if (!mPage.newMaterial) {
        fail(QStringLiteral("materials.newMaterial: no Materials page in this session"));
        return out;
    }
    static const QStringList knownOptions = { QStringLiteral("name") };
    const QString refusal = refuseUnknownKeys(QStringLiteral("materials.newMaterial"),
                                              options, knownOptions);
    if (!refusal.isEmpty()) { fail(refusal); return out; }
    out = mPage.newMaterial(presetOrName, options.value(QStringLiteral("name")).toString());
    if (out.isEmpty())
        fail(QStringLiteral("materials.newMaterial: no shipped preset '%1'").arg(presetOrName));
    return out;
}

QVariantList MaterialsApi::tabs()
{
    if (!pageOrFail(QStringLiteral("materials.tabs"))) return QVariantList();
    return mPage.tabs();
}

bool MaterialsApi::activate(const QVariant &tabOrGuid)
{
    if (!pageOrFail(QStringLiteral("materials.activate"))) return false;
    if (!mPage.activate(tabOrGuid)) {
        fail(QStringLiteral("materials.activate: no such tab (%1)").arg(tabOrGuid.toString()));
        return false;
    }
    return true;
}

bool MaterialsApi::closeTab(const QVariant &tabOrGuid)
{
    if (!pageOrFail(QStringLiteral("materials.closeTab"))) return false;
    if (!mPage.closeTab(tabOrGuid)) {
        fail(QStringLiteral("materials.closeTab: no such tab (%1)").arg(tabOrGuid.toString()));
        return false;
    }
    return true;
}

QVariant MaterialsApi::activeTab()
{
    if (!mPage.activeTab) return QVariant();   // no page: null, not a refusal
    const QVariantMap info = mPage.activeTab();
    return info.isEmpty() ? QVariant() : QVariant(info);
}

QString MaterialsApi::createFromImage(const QString &textureGuid, const QVariantMap &options)
{
    if (!host.db) { fail("materials: not available in this session"); return QString(); }

    if (options.value("graph").toBool())
        return createImageGraph(textureGuid);

    QString error;
    const QString materialGuid =
        ImageMaterial::createMaterialAsset(textureGuid, host.db, host.project, &error);
    if (materialGuid.isEmpty()) {
        fail(QStringLiteral("materials.createFromImage: %1").arg(error));
        return QString();
    }
    // Project context: pin it in (bin membership + session registration) —
    // the same path a direct image add-to-project takes for its companion.
    if (host.project && !host.project->getProjectGuid().isEmpty())
        ProjectAssets::addToProject(materialGuid, host.db, host.project, ProjectAssets::AddKind::Direct);
    // THE TILE IS A RENDER OF THE MATERIAL (THUMBS-1). The mint stores the
    // image as a fallback — correct and instant, and the only answer headless;
    // ONE gesture can afford ONE render, so ask for it here. A batch (a drop of
    // a hundred images) goes through the tray's one-per-turn backlog instead.
    materialtile::mint(host.db, host.project, materialGuid, "materials.createFromImage");
    return materialGuid;
}

int MaterialsApi::seedPresets()
{
    if (!host.db) { fail("materials: not available in this session"); return 0; }
    // Take the job over from the launch seeder rather than racing it (one
    // importer at a time — MaterialPresetSeeder::finishNow).
    MaterialPresetSeeder::instance().finishNow();
    QString error;
    const int seeded = MaterialPresetAssets::seedAll(host.db, &error);
    if (!error.isEmpty())
        fail(QStringLiteral("materials.seedPresets: %1").arg(error));
    return seeded;
}

QString MaterialsApi::createFromPreset(const QString &presetOrGuid, const QVariantMap &options)
{
    if (!host.db) { fail("materials: not available in this session"); return QString(); }
    static const QStringList knownOptions = { QStringLiteral("name") };
    const QString refusal = refuseUnknownKeys(QStringLiteral("materials.createFromPreset"),
                                              options, knownOptions);
    if (!refusal.isEmpty()) { fail(refusal); return QString(); }

    MaterialPresetSeeder::instance().finishNow();   // one importer at a time
    QString error;
    const QString copy = MaterialPresetAssets::customise(
        presetOrGuid, options.value(QStringLiteral("name")).toString(),
        host.db, host.project, &error);
    if (copy.isEmpty()) {
        fail(QStringLiteral("materials.createFromPreset: %1").arg(error));
        return QString();
    }
    auto *asset = new AssetMaterial;
    asset->fileName = host.db->fetchAsset(copy).name;
    asset->assetGuid = copy;
    AssetManager::addAsset(asset);
    // (The copy's TILE is rendered inside `customise` now — one place for all
    // three Customise doors, and a refused render is logged rather than
    // discarded; PREVIEWENV-2 item c, services/materialtile.h.)
    return copy;
}

QString MaterialsApi::createImageGraph(const QString &textureGuid)
{
    // IMAGE_PLANE_SPEC option B2: the graph twin — the spec's template
    // texture → textureSampler → PbrMaster.BaseColor. It is a MATERIAL BUNDLE
    // with a graph payload now, like every other graph material (spec 2.3).
    if (!host.db) { fail("materials: not available in this session"); return QString(); }

    const auto record = host.db->fetchAsset(textureGuid);
    if (record.guid.isEmpty()
        || static_cast<ModelTypes>(record.type) != ModelTypes::Texture) {
        fail(QStringLiteral("materials.createFromImage: '%1' is not a texture asset").arg(textureGuid));
        return QString();
    }

    const QString shaderName = QFileInfo(record.name).completeBaseName();

    // THE ONE NODE LIBRARY (fix round F9). Four verbs minted a LibraryV1 per
    // CALL — a registry of every node type, with its icons — and nothing
    // ever freed one, so a script that opened ten materials left ten behind.
    auto *lib = MaterialHelper::sharedNodeLibrary();
    auto *graph = new NodeGraph;
    graph->setNodeLibrary(lib);
    auto *master = new PbrMasterNode();
    graph->addNode(master);
    graph->setMasterNode(master);
    MaterialSettings settings;
    settings.name = shaderName;
    graph->setMaterialSettings(settings);

    auto *texNode = static_cast<TextureNode *>(lib->createNode("texture"));
    texNode->setTextureGuid(textureGuid);   // stored as the asset guid; resolvers go pin-first
    graph->addNode(texNode);
    auto *sampler = lib->createNode("textureSampler");
    graph->addNode(sampler);
    graph->addConnection(texNode, 0, sampler, 0);   // texture -> sampler.Texture
    graph->addConnection(sampler, 0, master, 0);    // sampler.RGBA -> Base Color

    // The row first (the bake's member textures need a parent), then the
    // definition: the bundle writer derives the Material→Texture edge from it,
    // so nobody writes one by hand any more.
    QJsonObject seed;
    seed["materialType"] = "pbr";
    seed["name"] = shaderName;
    QString error;
    const QString assetGuid = MaterialBundle::create(host.db, shaderName, seed,
                                                     QByteArray(), &error);
    if (assetGuid.isEmpty()) {
        delete graph;
        fail(QStringLiteral("materials.createFromImage: %1").arg(error));
        return QString();
    }
    const auto build = materials::buildDefinition(graph, assetGuid, host.db, host.project);
    if (!build.ok()) {
        fail(QStringLiteral("materials.createFromImage: %1").arg(build.error));
        return assetGuid;
    }
    const auto written = MaterialBundle::write(host.db, host.project, assetGuid,
                                               build.definition);
    if (!written.ok) fail(QStringLiteral("materials.createFromImage: %1").arg(written.error));

    auto assetShader = new AssetMaterial;
    assetShader->fileName = shaderName;
    assetShader->assetGuid = assetGuid;
    AssetManager::addAsset(assetShader);
    // THE MATERIAL'S TILE IS A RENDER OF IT ON THE STUDIO SPHERE (owner review
    // R9(a)). The graph twin of createFromImage minted its bundle with no tile
    // while its VALUES twin rendered one.
    materialtile::mint(host.db, host.project, assetGuid, "materials.createImageGraph");

    if (mGraphApi) mGraphApi->setCurrent(graph, assetGuid);
    return assetGuid;
}

bool MaterialsApi::regenerate(const QString &shaderGuid)
{
    if (!host.db) return fail("materials: not available in this session");
    if (!requireProject()) return false;

    const QJsonObject definition = MaterialBundle::read(host.db, shaderGuid, host.project);
    if (definition.isEmpty())
        return fail(QStringLiteral("materials.regenerate: no material '%1'").arg(shaderGuid));
    if (!definition.contains("shadergraph"))
        return fail("materials.regenerate: the asset has no 'shadergraph' object");

    QString refusedGraph;
    NodeGraph *graph = NodeGraph::deserialize(definition["shadergraph"].toObject(),
                                              MaterialHelper::sharedNodeLibrary(),
                                              &refusedGraph);
    if (!graph)
        return fail(QStringLiteral("materials.regenerate: %1")
                        .arg(refusedGraph.isEmpty()
                                 ? QStringLiteral("the graph could not be loaded")
                                 : refusedGraph));
    if (!graph->getMasterNode())
        return fail("materials.regenerate: the graph has no master node");

    const auto build = materials::buildDefinition(graph, shaderGuid, host.db, host.project);
    if (!build.ok()) return fail(QStringLiteral("materials.regenerate: %1").arg(build.error));
    QJsonObject rebaked = build.definition;
    // keep the identity keys the stored definition carried
    if (definition.contains("name")) rebaked["name"] = definition["name"];
    const bool projectOwns =
        host.project && !host.project->getProjectGuid().isEmpty()
        && host.db->isAssetPinnedBy(host.project->getProjectGuid(), shaderGuid);
    const auto written = MaterialBundle::write(
        host.db, host.project, shaderGuid, rebaked,
        projectOwns ? MaterialBundle::Scope::Project : MaterialBundle::Scope::Library);
    if (!written.ok) return fail(QStringLiteral("materials.regenerate: %1").arg(written.error));

    // AND THE SCENE FOLLOWS, through the ONE apply. The old walk here matched
    // materials by a "/BakedMaps/<guid>/" substring in a texture's source
    // path; a baked map is a store object named by its hash now, so the
    // catalog's own Object -> Material edges are what say who wears it.
    if (host.services && host.services->sceneEdit)
        host.services->sceneEdit->refreshMaterialUsers(shaderGuid);
    return true;
}

QVariantList MaterialsApi::presets()
{
    QVariantList out;
    for (const auto &preset : MaterialPresets::all()) {
        out.append(QVariantMap{
            { "name", preset.name },
            { "type", preset.type },
            { "guid", Constants::Reserved::DefaultMaterials.key(preset.name) } });
    }
    return out;
}

QString MaterialsApi::create(const QString &name, const QVariantMap &options)
{
    if (!host.db) { fail("materials: not available in this session"); return QString(); }
    if (name.trimmed().isEmpty()) { fail("materials.create: a name is required"); return QString(); }
    const QString materialName = name.trimmed();

    // A LIBRARY BUNDLE — no project needed, and nothing minted in one. Adding
    // it to a project is a separate, explicit gesture (`assets.addToProject`),
    // which is the four-drawer rule (OWNER_REVIEW 9).
    NodeGraph *graph = nullptr;
    QJsonObject definition;
    definition["materialType"] = "pbr";
    definition["name"] = materialName;

    if (options.value("graph").toBool()) {
        // A minimal graph with a PbrMaterial master — the current Effects
        // format (the old ShaderTemplate.shader predates the graph and cannot
        // be reopened by the graph loader; the .effect presets are the live
        // shape).
        graph = new NodeGraph;
        graph->setNodeLibrary(MaterialHelper::sharedNodeLibrary());
        auto *master = new PbrMasterNode();
        graph->addNode(master);
        graph->setMasterNode(master);
        MaterialSettings settings;
        settings.name = materialName;
        graph->setMaterialSettings(settings);
        // THE GRAPH IS A PAYLOAD of the ONE Material row (spec 2.3) — there is
        // no ModelTypes::Shader row any more.
        definition["shadergraph"] = graph->serialize();
        definition["values"] = PbrGraphEvaluator::evaluate(graph).values;
    }

    QString error;
    const QString assetGuid = MaterialBundle::create(host.db, materialName, definition,
                                                     QByteArray(), &error);
    if (assetGuid.isEmpty()) {
        delete graph;
        fail(QStringLiteral("materials.create: %1").arg(error));
        return QString();
    }

    auto *asset = new AssetMaterial;
    asset->fileName = materialName;
    asset->assetGuid = assetGuid;
    AssetManager::addAsset(asset);
    // THE MATERIAL'S TILE IS A RENDER OF IT ON THE STUDIO SPHERE (owner review
    // R9(a)). A minted bundle carried NO thumbnail at all, so a material made
    // this way was a grey tile until some sweep found it.
    materialtile::mint(host.db, host.project, assetGuid, "materials.create");

    // `folder` PUTS IT IN THE PROJECT, FILED (DRAWERS-1, the owner's "creating
    // in the project should add it to the project drawer in Materials
    // automatically"). Without the key this stays what it has always been: a
    // library bundle, and adding it to a project is the separate gesture
    // `assets.addToProject`. With it — the editor tray's right-click > Create
    // Material passes the folder the user is looking at, the project's own
    // guid at the root — the material is pinned into the open project and
    // filed there, and the ONE announcement repopulates both drawers.
    if (options.contains(QStringLiteral("folder"))) {
        if (!host.project || host.project->getProjectGuid().isEmpty()) {
            fail("materials.create: {folder} files the material in the OPEN project, and no "
                 "project is open");
            return QString();
        }
        const ProjectAssets::Result pinned = ProjectAssets::addToProject(
            assetGuid, host.db, host.project, ProjectAssets::AddKind::Direct);
        if (!pinned.ok()) {
            fail(QStringLiteral("materials.create: %1").arg(pinned.error));
            return QString();
        }
        const QString folder = options.value(QStringLiteral("folder")).toString();
        if (!folder.isEmpty() && folder != host.project->getProjectGuid()) {
            const projectfolders::Result filed = projectfolders::moveTo(
                host.db, host.project->getProjectGuid(), { assetGuid }, folder);
            if (!filed.ok) {
                fail(QStringLiteral("materials.create: %1").arg(filed.error));
                return QString();
            }
        }
        projectfolders::announce(host.project->getProjectGuid());
    }

    // Adopt the freshly built graph as the current one directly.
    if (graph && mGraphApi) mGraphApi->setCurrent(graph, assetGuid);
    return assetGuid;
}

QVariantList MaterialsApi::projectDrawer()
{
    // THE MODULE'S PROJECT DRAWER, AS THE WIDGET SHOWS IT (DRAWERS-1). It
    // reads the live drawer rather than re-deriving the listing, because the
    // point of the verb is to prove that the two drawers are one list: a
    // suite compares this with editor.trayAssets() filtered to materials.
    // Through the page delegate, like every other verb here — a process-wide
    // static pointing at the live widget was the first cut and is gone.
    if (!mPage.projectDrawer) {
        refuse("materials.projectDrawer: the Materials module's project drawer is not built "
               "in this session");
        return QVariantList();
    }
    return mPage.projectDrawer();
}

QVariantList MaterialsApi::customDrawer()
{
    if (!mPage.customDrawer) {
        refuse("materials.customDrawer: the Materials module's custom drawer is not built in this "
               "session");
        return QVariantList();
    }
    return mPage.customDrawer();
}

QString MaterialsApi::addTexture(const QString &materialGuid, const QString &pathOrGuid,
                                 const QVariantMap &options)
{
    if (!host.db) { fail("materials: not available in this session"); return QString(); }
    if (materialGuid.isEmpty() || pathOrGuid.isEmpty()) {
        fail("materials.addTexture: a material and an image are required");
        return QString();
    }
    const AssetRecord row = host.db->fetchAsset(materialGuid);
    if (row.type != static_cast<int>(ModelTypes::Material)) {
        fail(QStringLiteral("materials.addTexture: '%1' is not a material").arg(materialGuid));
        return QString();
    }
    // A SHIPPED PRESET IS READ-ONLY, AND THE ANSWER COMES BEFORE THE WORK
    // (PRESET-UNIFY-1, the same rule as the apply's F6 test). The definition
    // writer refuses a reserved guid at the end of this function anyway — but
    // by then a picture from disk has been imported into the library and
    // pinned into the project for an edit that was never going to land, which
    // is exactly the shape of defect "nothing is imported for an apply that
    // cannot happen" removed from the apply.
    const QString shipped = MaterialBundle::shippedPresetName(materialGuid);
    if (!shipped.isEmpty()) {
        // (AND THIS DOOR DOES NOT COPY ON WRITE, deliberately — PRESET-EDIT-1.
        // Every shipped preset is a GRAPH material, and a slot edit on a graph
        // material is refused by the next guard anyway ("that slot comes from
        // the graph"): copying first would mint the project a material it
        // never asked for and THEN refuse. The door that copies is
        // `materials.edit`, which the message names.)
        fail(QStringLiteral("materials.addTexture: '%1' is a material the app ships and its "
                            "library row is read-only — materials.edit('%1') gives you this "
                            "project's own copy of it to write to")
                 .arg(shipped));
        return QString();
    }

    // EITHER A GUID ALREADY IN THE LIBRARY, OR A PATH FROM ANYWHERE ON DISK —
    // and a path is IMPORTED at that moment (owner, spec 0/Q1: "adding a
    // texture to a material would import it into the project"). By CONTENT, so
    // picking the same image twice answers the same row and a duplicate is
    // impossible.
    QString textureGuid;
    if (!MaterialBundle::looksLikePath(pathOrGuid)) {
        const AssetRecord texRow = host.db->fetchAsset(pathOrGuid);
        if (texRow.type != static_cast<int>(ModelTypes::Texture)) {
            fail(QStringLiteral("materials.addTexture: '%1' is not a texture").arg(pathOrGuid));
            return QString();
        }
        textureGuid = pathOrGuid;
        // Already an asset: a project that holds the material should hold its
        // member too.
        if (host.project && !host.project->getProjectGuid().isEmpty()
            && host.db->isAssetPinnedBy(host.project->getProjectGuid(), materialGuid)
            && !host.db->isAssetPinnedBy(host.project->getProjectGuid(), textureGuid))
            ProjectAssets::addToProject(textureGuid, host.db, host.project,
                                        ProjectAssets::AddKind::Binding);
    } else {
        const ShippedAssets::Pinned imported = ShippedAssets::importTexture(
            pathOrGuid, QFileInfo(pathOrGuid).fileName(), host.db, host.project);
        if (!imported.ok() || imported.guid.isEmpty()) {
            fail(QStringLiteral("materials.addTexture: %1").arg(
                     imported.error.isEmpty() ? QStringLiteral("the import refused the image")
                                              : imported.error));
            return QString();
        }
        textureGuid = imported.guid;
        // THE MEMBER STAMP (V-2, owner Q4). A picture that arrived THROUGH a
        // material's picker folds into the bundle instead of standing as its
        // own library tile — and only a row this import MINTED gets the
        // stamp: if the bytes were already in the library the user imported
        // that image themselves, and their own texture is always a tile.
        if (imported.minted)
            memberstamp::stamp(host.db, textureGuid, materialGuid);
    }

    // THE SLOT. Naming one writes it into the definition (and the membership
    // edge follows, derived); naming none imports the image and pins it as a
    // member without changing what the material looks like — which is what a
    // texture NODE in the graph wants.
    const QString slot = options.value("slot").toString();
    if (!slot.isEmpty()) {
        if (!MaterialBundle::textureSlots().contains(slot)) {
            fail(QStringLiteral("materials.addTexture: '%1' is not a texture slot").arg(slot));
            return QString();
        }
        QJsonObject definition = MaterialBundle::read(host.db, materialGuid, host.project);
        // A GRAPH MATERIAL'S SLOTS BELONG TO ITS GRAPH, and writing one here
        // would be a value the next save silently overwrites: `graph.save`
        // rebuilds `values` from the graph, which is the whole point of a
        // graph material. Refuse, and say where the picture goes instead —
        // an edit that quietly disappears at the next autosave is worse than
        // one that does not happen. (The image IS imported and pinned by the
        // time we get here, which is what the caller asked for and what the
        // returned guid is for: hand it to graph.setValue on a texture node.)
        if (definition.contains(QStringLiteral("shadergraph"))) {
            fail(QStringLiteral(
                     "materials.addTexture: '%1' is a graph material, so its '%2' comes from "
                     "the graph — the image was imported and pinned (%3); put it on a texture "
                     "node with graph.addNode('texture') + graph.setValue(node, guid)")
                     .arg(row.name, slot, textureGuid));
            return QString();
        }
        QJsonObject values = definition["values"].toObject();
        values[slot] = textureGuid;
        definition["values"] = values;
        const bool projectOwns =
            host.project && !host.project->getProjectGuid().isEmpty()
            && host.db->isAssetPinnedBy(host.project->getProjectGuid(), materialGuid);
        const auto written = MaterialBundle::write(
            host.db, host.project, materialGuid, definition,
            projectOwns ? MaterialBundle::Scope::Project : MaterialBundle::Scope::Library);
        if (!written.ok) { fail(QStringLiteral("materials.addTexture: %1").arg(written.error)); return QString(); }
        if (host.services && host.services->sceneEdit)
            host.services->sceneEdit->refreshMaterialUsers(materialGuid);
    }
    return textureGuid;
}

QVariantList MaterialsApi::members(const QString &materialGuid)
{
    QVariantList out;
    if (!host.db) { fail("materials: not available in this session"); return out; }
    if (MaterialBundle::read(host.db, materialGuid, host.project).isEmpty()) {
        fail(QStringLiteral("materials.members: no material '%1'").arg(materialGuid));
        return out;
    }
    // ONE PROJECTION (services/materialmembers.h): the Members panel draws
    // exactly these rows, so the window and the verb cannot describe the
    // bundle differently.
    for (const materialmembers::Member &m :
         materialmembers::describe(host.db, host.project, materialGuid)) {
        out.append(QVariantMap{
            { "guid", m.guid },
            { "name", m.name },
            { "slot", m.slot },
            { "node", m.node },
            { "role", m.role },
            { "baked", m.role == QLatin1String("baked") },
            { "bytes", static_cast<qlonglong>(m.bytes) },
            { "usedBy", m.usedBy },
            { "pinned", m.pinned },
            { "member", m.member },
            { "hidden", m.hidden } });
    }
    return out;
}

QVariantList MaterialsApi::cleanUnused(const QString &materialGuid, const QVariantMap &options)
{
    QVariantList out;
    if (!host.db) { fail("materials: not available in this session"); return out; }
    static const QStringList knownOptions = { QStringLiteral("dryRun") };
    const QString refusal = refuseUnknownKeys(QStringLiteral("materials.cleanUnused"), options,
                                              knownOptions);
    if (!refusal.isEmpty()) { fail(refusal); return out; }

    // DRY RUN IS THE DEFAULT (the store's standing law, assetgc.h; owner Q6:
    // "list first"). A caller has to say `{dryRun:false}` to remove anything.
    const bool dryRun = options.value(QStringLiteral("dryRun"), true).toBool();
    if (!materialGuid.isEmpty()
        && MaterialBundle::read(host.db, materialGuid, host.project).isEmpty()) {
        fail(QStringLiteral("materials.cleanUnused: no material '%1'").arg(materialGuid));
        return out;
    }

    QString error;
    const QVector<materialmembers::Unused> entries =
        dryRun ? materialmembers::unused(host.db, host.project, materialGuid)
               : materialmembers::cleanUnused(host.db, host.project, materialGuid, &error);
    if (!error.isEmpty()) { fail(QStringLiteral("materials.cleanUnused: %1").arg(error)); return out; }
    for (const auto &entry : entries)
        out.append(QVariantMap{ { "guid", entry.guid },
                                { "name", entry.name },
                                { "bytes", static_cast<qlonglong>(entry.bytes) },
                                { "scope", entry.scope } });
    return out;
}

QString MaterialsApi::duplicate(const QString &materialGuid, const QVariantMap &options)
{
    if (!host.db) { fail("materials: not available in this session"); return QString(); }
    static const QStringList knownOptions = { QStringLiteral("name") };
    const QString refusal = refuseUnknownKeys(QStringLiteral("materials.duplicate"), options,
                                              knownOptions);
    if (!refusal.isEmpty()) { fail(refusal); return QString(); }
    QString error;
    const QString copy = materialmembers::duplicate(host.db, host.project, materialGuid,
                                                    options.value(QStringLiteral("name")).toString(),
                                                    &error);
    if (copy.isEmpty()) {
        fail(QStringLiteral("materials.duplicate: %1").arg(error));
        return QString();
    }
    auto *asset = new AssetMaterial;
    asset->fileName = host.db->fetchAsset(copy).name;
    asset->assetGuid = copy;
    AssetManager::addAsset(asset);
    // THE COPY'S TILE IS A RENDER OF THE COPY (owner review R9(a)): a duplicate
    // inherits the SOURCE row's tile, which is the right picture only while
    // the copy is untouched and the wrong one the moment the source's was an
    // icon. ONE implementation, and it says so when it cannot
    // (services/materialtile.h).
    materialtile::mint(host.db, host.project, copy, "materials.duplicate");
    return copy;
}

QString MaterialsApi::makeUnique(const QString &materialGuid, const QString &textureGuid)
{
    if (!host.db) { fail("materials: not available in this session"); return QString(); }
    QString error;
    const QString guid = materialmembers::makeUnique(host.db, host.project, materialGuid,
                                                     textureGuid, &error);
    if (guid.isEmpty()) {
        fail(QStringLiteral("materials.makeUnique: %1").arg(error));
        return QString();
    }
    // The picture on the mesh does not change (same bytes) — but the material
    // it wears now names a different row, and the ONE apply is what keeps the
    // scene and the definition in step.
    if (host.services && host.services->sceneEdit)
        host.services->sceneEdit->refreshMaterialUsers(materialGuid);
    return guid;
}

QVariantMap MaterialsApi::loadGraph(const QString &guidOrPath)
{
    QVariantMap out;
    if (!mGraphApi) { fail("materials.loadGraph: no graph module"); return out; }

    QJsonObject definition;
    QString assetGuid;
    if (QFileInfo::exists(guidOrPath)) {
        QFile file(guidOrPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            fail(QStringLiteral("materials.loadGraph: cannot open '%1'").arg(guidOrPath));
            return out;
        }
        definition = QJsonDocument::fromJson(file.readAll()).object();
    } else if (MaterialPresetAssets::isPreset(guidOrPath)) {
        // A SHIPPED PRESET'S GRAPH, WITHOUT SEEDING IT (PRESET-UNIFY-1). Every
        // preset is a graph now, and looking at one must not be the thing that
        // writes its row: a preset nobody has used yet has no row at all
        // (seeding is on first USE), so the shipped graph is read straight off
        // disk. A preset that HAS been seeded is read from its definition
        // instead — the same graph with its images named by the guids the
        // library gave them.
        assetGuid = MaterialPresetAssets::guidFor(guidOrPath);
        const QByteArray blob = host.db ? host.db->fetchAssetData(assetGuid) : QByteArray();
        definition = QJsonDocument::fromJson(blob).object();
        if (!definition.contains(QStringLiteral("shadergraph"))) {
            bool found = false;
            const MaterialPreset preset = MaterialPresets::find(assetGuid, &found);
            if (found) definition[QStringLiteral("shadergraph")] = preset.graph;
        }
    } else if (host.db) {
        const QByteArray blob = host.db->fetchAssetData(guidOrPath);
        if (blob.isEmpty()) {
            fail(QStringLiteral("materials.loadGraph: no shader asset or file '%1'").arg(guidOrPath));
            return out;
        }
        definition = QJsonDocument::fromJson(blob).object();
        assetGuid = guidOrPath;
    } else {
        fail("materials: not available in this session");
        return out;
    }

    if (!definition.contains("shadergraph")) {
        fail("materials.loadGraph: the definition has no 'shadergraph' object");
        return out;
    }
    // The real loader path (pixel-parity-tested): deserialize with LibraryV1.
    QString refused;
    NodeGraph *graph =
        NodeGraph::deserialize(definition["shadergraph"].toObject(),
                               MaterialHelper::sharedNodeLibrary(), &refused);
    // A REFUSED GRAPH IS AN ERROR, NOT A CRASH (LEGACY-MASTER-CRUD): this used
    // to dereference the result three lines down. A material written on the
    // deleted "Surface Material" master has nothing to load.
    if (graph == nullptr) {
        fail(QStringLiteral("materials.loadGraph: %1")
                 .arg(refused.isEmpty() ? QStringLiteral("the graph could not be loaded")
                                        : refused));
        return out;
    }

    // WHETHER THIS GRAPH CAN BE EDITED IS PART OF THE ANSWER (PRESET-UNIFY-1;
    // re-read by PRESET-EDIT-1). A shipped preset is no longer read-only
    // wherever it is found: with a project open it is that project's to edit,
    // and the SAVE is what makes the copy (materials.edit). So `readOnly` is
    // the one case where an edit cannot land at all — a preset with no project
    // — and `master` says which shipped material is behind this one either way.
    const bool preset = !MaterialBundle::shippedPresetName(assetGuid).isEmpty();
    const QString why = presetedit::refusal(
        host.db, host.isProjectOpen() ? host.project : nullptr, assetGuid);
    const bool readOnly = !why.isEmpty();

    // AND A READ WRITES NOTHING (fix round). Binding a graph's file-named
    // images through the content import is how they become library rows — and
    // it is a device wait on the calling thread for every picture the store
    // does not already hold, plus a pin into the open project. Doing that
    // because somebody LOOKED at a preset is the wrong answer twice over, and
    // it is what this verb did: opening the unseeded Brick preset imported and
    // pinned three PNGs. A read-only open binds the shipped FILE instead;
    // everything that draws the graph works from paths.
    //
    // THE KEY IS "IS THIS A PRESET", NOT "IS IT LOCKED" (PRESET-EDIT-1). A
    // preset a project could edit is still only being LOOKED at here — the
    // copy happens at the save — so its pictures stay bound as files until
    // then, and `graph.save` re-binds them as library assets the moment it
    // makes the copy.
    out["texturesResolved"] = MaterialHelper::resolveAppRelativeTextures(
        graph, preset ? MaterialHelper::TextureBinding::PathOnly
                      : MaterialHelper::TextureBinding::Import);
    out["texturesImported"] = preset ? 0 : out["texturesResolved"];
    mGraphApi->setCurrent(graph, assetGuid);

    out["nodes"] = graph->nodes.size();
    out["master"] = graph->masterNode ? graph->masterNode->typeName : QString();
    out["name"] = definition.value("name").toString();
    out["readOnly"] = readOnly;
    out["editable"] = !readOnly;
    // `presetMaster`, not `master`: this map's `master` has named the graph's
    // MASTER NODE type since the evaluator landed.
    out["presetMaster"] = presetedit::masterOf(host.db, assetGuid);
    return out;
}

// ----------------------------------------------------------------- material.*

QVector<VerbInfo> MaterialApi::verbs() const
{
    return {
        { "apply", "material.apply(nodeId, presetOrGuid) -> bool",
          "Applies a MATERIAL BUNDLE — a shipped preset (by name or by its reserved guid) or any "
          "library/project material asset (by guid) — to a node. A container node (an imported "
          "model's root) applies to every mesh under it, each with its own material instance. "
          "AN APPLY MINTS NOTHING: a preset is a read-only library bundle, seeded once with its "
          "reserved guid the first time anything uses it, and applying it PINS that bundle into "
          "the open project and dresses the meshes — three applies of one preset add zero rows. "
          "Undoable as ONE step, and the step carries everything the apply did: the material on "
          "each mesh, the project's pin when this apply created it, and the node→material use "
          "edges. Ends any live material.preview first, so the undo step captures the node's "
          "true original material.",
          Needs::Document },
        { "preview", "material.preview(nodeId, presetOrGuid) -> bool",
          "Shows a material on a node WITHOUT applying it — the editor's live hover preview, which "
          "is what a material dragged over the scene does. It takes the same three payloads "
          "material.apply takes (a preset name or reserved guid, a project material row, a "
          "Materials-module Shader row), resolves each into a PRIVATE instance, and puts the "
          "node's own material back the moment the preview moves, ends or anything writes the "
          "document. It pushes NO undo step, leaves the dirty flag alone, writes no database row, "
          "and — deliberately — costs no GI re-solve: the scene tells the renderer that what it "
          "is showing was never committed. Previewing a second node restores the first. False, "
          "with no preview left running, when the payload resolves to no material, the node is "
          "not a mesh, or the node is LOCKED (its drop would be refused, so its preview would be "
          "a promise the release cannot keep). A save taken while a preview is up writes the "
          "ORIGINAL material.",
          Needs::Document },
        { "endPreview", "material.endPreview() -> bool",
          "Ends the live material.preview, putting the previewed node's own material back. True "
          "when a preview was running. Harmless with none. Every path that commits the document "
          "calls it for you — this verb exists for the caller that started a preview and changed "
          "its mind.",
          Needs::Document },
        { "set", "material.set(nodeId, {baseColor, roughness, metallic, baseColorMap, textureScale, ...}) -> bool",
          "Sets material properties on a mesh node (PBR keys; *Map keys take texture paths or asset guids). Undoable per property. "
          "A texture ASSET guid on a map key pins that image into the open project as a binding "
          "(the scene uses it; no companion material is minted) and records the node -> texture "
          "dependency the material panel's texture row records — so it is a tile in the editor's "
          "asset tray (assets.list({scope: 'project', tray: true})), assets.dependencies(nodeId) "
          "names it, and a project export carries it. Recording a use never takes the image out "
          "of the LIBRARY: the Assets page hides an import's MEMBERS, not what a scene uses. "
          "THE UV TRANSFORM takes two spellings: `textureScale` and `textureOffset` accept a "
          "two-element array [u, v] for per-axis tiling/offset, or a plain number meaning both "
          "axes (which is what every script written before per-axis tiling says, and it keeps "
          "meaning that). `textureRotation` is degrees, counter-clockwise about the texture "
          "centre. They are stored as the scalar rows material.properties lists — textureScale, "
          "textureScaleV, textureOffsetU, textureOffsetV, textureRotation — and material.get "
          "reports those rows, so a read comes back as numbers, not as the pair.",
          Needs::Document },
        { "get", "material.get(nodeId) -> {property: value}",
          "Reads the node material's editor-facing properties. material.properties(nodeId) is the "
          "same values plus their types, ranges and the full writable-key list. A graph material "
          "that renders through a GENERATED SHADER PIECE (HLMS_ADOPTION P5) also reports "
          "`customPiecePixel` / `customPieceVertex` — read-only paths into the per-user piece "
          "cache, and the only way to tell from outside that the surface is live rather than "
          "baked.",
          Needs::Document },
        { "properties", "material.properties(nodeId) -> {class, rows:[{name, displayName, type, value, min?, max?}], writableKeys:[…]}",
          "What this node's material can be told, without guessing. 'class' is PbrMaterial for "
          "everything a scene can hold (the legacy shader-material class was retired and its "
          "materials convert at load). 'rows' are the DECLARED properties "
          "in panel order, with 'min'/'max' present only where a range is declared — the PBR "
          "material declares real ones (metallic and roughness are 0..1, emissiveIntensity 0..10), "
          "so this is where a scale actually means something. 'writableKeys' is the exact set "
          "material.set accepts — the row names plus, on a PbrMaterial, its texture slots "
          "(baseColorMap, metallicMap, roughnessMap, normalMap, emissiveMap, the detail-layer maps "
          "and reflectionMap), "
          "which take a file path, an image asset guid or a live texture guid. Read 'writableKeys' rather than "
          "deriving keys from 'rows': the two agree today but the slot list is what material.set "
          "actually consults. The legacy shader spellings (diffuseTexture, normalTexture, …) are "
          "NOT writable on a PBR material and are refused by name.",
          Needs::Document },
        { "reset", "material.reset(nodeId) -> bool",
          "RESETS the node's material to the node's OWN DEFAULT — the material panel's reset, as a "
          "verb (owner, 2026-09-12). A node has a default only when something provides one; the "
          "scene's DEFAULT FLOOR (node.properties(id).defaultFloor, the Ground every new scene and "
          "every shipped demo stands on) is the first and only provider today, and its default is "
          "the floor's own checker material (the shipped tile, textureScale 4, roughness 1, "
          "metallic 0) — never a shared library row. The reset CLEARS what the user applied from "
          "the NODE: it stops using an applied material asset (material.apply) and the textures "
          "its slots were bound to. Project membership is not touched: the applied material stays "
          "in the project (and the tray), exactly as when the material on any other mesh is "
          "replaced — assets.removeFromProject takes it out. ONE undo step (undo puts the user's "
          "material and its use back). A node with no default of its own answers false, changes "
          "nothing and records why (app.lastError).",
          Needs::Document },
        { "setDetail", "material.setDetail(nodeId, layer, {map, normalMap, blend, offset:{x,y}, scale:{x,y}, weight, normalWeight}) -> bool",
          "One DETAIL LAYER, ergonomically. A detail layer is a second diffuse map blended into "
          "the base colour by one of thirteen modes, optionally with its own normal map, its own "
          "UV offset/scale and its own weight — fields on the same renderer material, not a "
          "second material model. There are 2 layers (0 and 1); the renderer has four and the "
          "other two are reserved. `blend` takes a NAME (\"Overlay\", \"Multiply2x\", …, "
          "material.properties lists them on the detailNBlend row) or its index. This is a "
          "convenience over material.set and writes through exactly the SAME rows — "
          "detail0Map, detail0Blend, detail0OffsetU … — so the two can never disagree; "
          "material.set is the flat-key path every existing script idiom uses. "
          "DETAIL TILING IS PER LAYER: `scale` is the detail map's own tiling and is unrelated to "
          "the material's textureScale, which tiles the BASE maps only and deliberately does not "
          "reach detail UVs. An unauthored layer costs nothing at all — the renderer sets no "
          "shader property for it — so adding a layer to one material cannot move another's "
          "pixels. Undoable (one step per written row, inside the run's macro).",
          Needs::Document },
        { "detail", "material.detail(nodeId) -> [{map, normalMap, blend, blendName, offset, scale, weight, normalWeight}]",
          "All of the material's detail layers, in order — the read counterpart of "
          "material.setDetail. `blend` is the stored index and `blendName` its vocabulary entry.",
          Needs::Document },
        { "dumpDatablock", "material.dumpDatablock(nodeId) -> string",
          "DIAGNOSTIC: what the RENDERER's material for this node actually ends up holding, as "
          "text, read off the live datablock. The document says one thing, the mirror translates "
          "it, and the renderer then clamps, guards and reorders — 'what did the datablock "
          "actually end up with' has been the hardest question in every material bug, and until "
          "now it needed a debugger. Pairs with the JAHSHAKA_HLMS_DEBUG_DIR shader dump, which "
          "answers 'and what shader did that produce'. The format is the RENDERER'S and is NOT a "
          "material format: the document is the truth (asset guids, the node graph, baked maps, "
          "our alpha-mode vocabulary and roughness bounds have no home in a datablock). Read it, "
          "do not parse it. Needs a live renderer — it fails, by name, in a headless run.",
          Needs::Engine },
    };
}

iris::MeshNodePtr MaterialApi::meshNodeOrFail(const QString &nodeId, const QString &verb)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene() : iris::ScenePtr();
    if (!scene) {
        fail(QStringLiteral("%1: no scene is open").arg(verb));
        return iris::MeshNodePtr();
    }
    auto node = findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node || node->getSceneNodeType() != iris::SceneNodeType::Mesh) {
        fail(QStringLiteral("%1: '%2' is not a mesh node").arg(verb, nodeId));
        return iris::MeshNodePtr();
    }
    return node.staticCast<iris::MeshNode>();
}

bool MaterialApi::apply(const QString &nodeId, const QString &presetOrGuid)
{
    if (!host.mainWindow) return fail("material: not available in this session");
    if (!requireProject()) return false;   // the delegate registers DB rows

    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene() : iris::ScenePtr();
    if (!scene) return fail("material.apply: no scene is open");
    auto node = findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node) return fail(QStringLiteral("material.apply: no node '%1'").arg(nodeId));

    // A mesh applies directly; a container (an imported model's Empty root —
    // exactly what the viewport's click-selects-the-root rule selects) applies
    // to every mesh underneath. A target with no meshes at all is an error,
    // not a silent no-op — the silent path is how applied materials used to
    // vanish without ever reaching the document.
    std::function<bool(const iris::SceneNodePtr &)> hasMesh =
        [&hasMesh](const iris::SceneNodePtr &n) -> bool {
            if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) return true;
            for (const auto &child : n->children())
                if (hasMesh(child)) return true;
            return false;
        };
    if (!hasMesh(node))
        return fail(QStringLiteral("material.apply: '%1' has no mesh nodes to apply to").arg(nodeId));

    host.services->selection->select(node);

    // THE ONE APPLY (MATERIAL-PREVIEW-1). This verb used to carry its own copy
    // of the preset scan and its own fallback to the material-asset path, beside
    // a second copy in MainWindow::applyMaterialPreset that the viewport's drop
    // called; the two had already drifted in what they accepted.
    if (host.services->sceneEdit->applyMaterial(presetOrGuid, node)) return true;

    return fail(QStringLiteral("material.apply: no preset, material asset or effect graph '%1' (materials.presets() and assets.list list them)").arg(presetOrGuid));
}

bool MaterialApi::preview(const QString &nodeId, const QString &presetOrGuid)
{
    if (!host.services || !host.services->materialPreview)
        return fail("material.preview: not available in this session");
    auto scene = host.services->sceneEdit ? host.services->sceneEdit->scene() : iris::ScenePtr();
    if (!scene) return fail("material.preview: no scene is open");
    auto node = findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node) return fail(QStringLiteral("material.preview: no node '%1'").arg(nodeId));
    if (node->getSceneNodeType() != iris::SceneNodeType::Mesh)
        return fail(QStringLiteral("material.preview: '%1' is not a mesh node").arg(nodeId));

    if (host.services->materialPreview->begin(node, presetOrGuid)) {
        // Ends with the run that started it (MaterialPreviewService::markVerbOwned).
        host.services->materialPreview->markVerbOwned();
        return true;
    }

    // ONE refusal, two causes, and they are worth telling apart: a locked node
    // will refuse the drop too, and an unresolvable payload is not a material
    // at all.
    if (!node->isPickable())
        return fail(QStringLiteral("material.preview: '%1' is locked — unlock it in the hierarchy")
                        .arg(node->getName()));
    return fail(QStringLiteral("material.preview: no preset, material asset or effect graph '%1' (materials.presets() and assets.list list them)")
                    .arg(presetOrGuid));
}

bool MaterialApi::endPreview()
{
    if (!host.services || !host.services->materialPreview)
        return fail("material.endPreview: not available in this session");
    return host.services->materialPreview->end();
}

bool MaterialApi::set(const QString &nodeId, const QVariantMap &values)
{
    // A preview ends before this verb READS the node's material: bound to the
    // borrowed instance, the command below would write onto a material the
    // node stops wearing the moment it is pushed (code review, F1).
    if (host.services && host.services->materialPreview) host.services->materialPreview->end();
    auto meshNode = meshNodeOrFail(nodeId, QStringLiteral("material.set"));
    if (!meshNode) return false;
    auto material = meshNode->getMaterial();
    if (!material) return fail("material.set: the node has no material");

    // The vocabularies live at file scope (top of this file) so this function,
    // its refusal message and material.properties can never disagree.
    const QStringList &colorKeys = kColorKeys;
    const QStringList &legacyMapKeys = kLegacyMapKeys;
    const QStringList &mapKeys = kMapKeys;

    // `textureScale: [u, v]` / `textureOffset: [u, v]` become the scalar rows
    // the document actually stores; a bare number still means uniform.
    QString pairError;
    const QVariantMap expanded = expandUvPairKeys(values, &pairError);
    if (!pairError.isEmpty()) return fail(pairError);

    // Texture ASSETS this call binds to a slot (by guid) — pinned into the
    // project once every key has been accepted (below).
    QStringList boundTextures;

    for (auto it = expanded.constBegin(); it != expanded.constEnd(); ++it) {
        const QString &key = it.key();
        QVariant newValue = normalizeJs(it.value());

        // THE KEY IS CHECKED BEFORE ITS VALUE. Found building lane A: F7's
        // "that is a legacy shader texture name" message was UNREACHABLE for
        // the values people actually pass. A legacy key fell into the texture
        // branch below first, and any path that is not an existing file and not
        // an asset guid — i.e. every plausible typo — was refused with "no
        // texture file or asset 'x.png'", which sends the reader off hunting for
        // a missing file when the real problem is that this material has no such
        // slot at all. Only a legacy key whose value happened to resolve ever
        // reached the message written for it.
        if (legacyMapKeys.contains(key)) {
            static const QMap<QString, QString> replacement{
                { QStringLiteral("diffuseTexture"),    QStringLiteral("baseColorMap") },
                { QStringLiteral("normalTexture"),     QStringLiteral("normalMap") },
                { QStringLiteral("specularTexture"),   QString() },
                { QStringLiteral("reflectionTexture"), QString() },
            };
            const QString instead = replacement.value(key);
            return fail(QStringLiteral(
                            "material.set: '%1' is a legacy shader texture name and this "
                            "node's PBR material has no such slot — %2 (the PBR maps are "
                            "baseColorMap, metallicMap, roughnessMap, normalMap, "
                            "emissiveMap)")
                            .arg(key,
                                 instead.isEmpty()
                                     ? QStringLiteral("there is no PBR equivalent")
                                     : QStringLiteral("use '%1'").arg(instead)));
        }

        if (colorKeys.contains(key)) {
            // F8: a colour string the parser cannot read used to keep the old
            // value and still report success.
            bool colourOk = false;
            const QColor colour = colorFromJs(newValue, QColor(), &colourOk);
            if (!colourOk)
                return fail(QStringLiteral("material.set: %1 (property '%2')")
                                .arg(colorHelp(newValue), key));
            newValue = QVariant::fromValue(colour);
        } else if (mapKeys.contains(key)) {
            // texture: an absolute/existing path passes through, an asset guid
            // resolves through the CAS (pinned bytes in project context; the
            // flat projectFolder/name join pointed at an unpopulated folder),
            // empty clears
            const QString ref = newValue.toString();
            // A LIVE TEXTURE (MATERIAL_GAPS_SPEC A-1) resolves before anything
            // touches the database: it has no file, no store object and no
            // catalog row, and its whole purpose is that a script can bind
            // pixels it just made — including with NO PROJECT OPEN, which is
            // where the db branch below would refuse. The stored value is the
            // reference string ("live://<guid>"), which is what the document's
            // resolver looks up and what tells the scene writer to skip the row.
            if (!ref.isEmpty() && LiveTextureCatalog::exists(ref)) {
                if (key == QLatin1String("reflectionMap"))
                    return fail(QStringLiteral(
                        "material.set: 'reflectionMap' takes an equirect IMAGE, not a live "
                        "texture — a reflection cubemap is built once by projecting the image "
                        "onto six faces, so a live texture bound here would freeze at its first "
                        "frame instead of following its generation"));
                newValue = LiveTextureCatalog::refFor(ref);
            }
            else if (!ref.isEmpty() && !QFileInfo::exists(ref)) {
                if (!host.db || !host.isProjectOpen())
                    return fail(QStringLiteral("material.set: '%1' is not a file and no project is open to resolve it as an asset").arg(ref));
                const auto record = host.db->fetchAsset(ref);
                if (record.guid.isEmpty())
                    return fail(QStringLiteral("material.set: no texture file or asset '%1'").arg(ref));
                // The pinned bytes, else the library source (resolvePinned
                // falls back itself). No projectFolder + row-name join after
                // it (plan item 15c): nothing puts asset files there.
                newValue = AssetCas::resolvePinned(QSqlDatabase::database(), AssetStorePaths::root(),
                                                   host.project->getProjectGuid(), ref);
                if (record.type == static_cast<int>(ModelTypes::Texture) && !boundTextures.contains(ref))
                    boundTextures << ref;
            }
        }

        // Old value for undo: from the material's editor property list when the
        // key is declared there; texture/map keys fall back to empty.
        QVariant oldValue;
        QList<iris::Property *> props = declaredProperties(material);
        bool known = false;
        for (auto prop : props) {
            if (prop->name != key) continue;
            oldValue = prop->getValue(); known = true;
            // ENUM ROWS ACCEPT THEIR OWN VOCABULARY, not just the index.
            // ListProperty::setValue is `value.toInt()`, so a script that wrote
            // the label — `{workflow: "Specular"}`, `{alphaMode: "Glass"}` —
            // silently stored 0 and reported success. The labels ride the row
            // (that is the whole point of the type), so match them here and
            // REFUSE an unknown name rather than coerce it to zero. Applies to
            // every enum row on every material: workflow, alphaMode,
            // shadingModel, brdf.
            if (auto *list = dynamic_cast<iris::ListProperty *>(prop)) {
                if (newValue.typeId() == QMetaType::QString) {
                    const QString label = newValue.toString();
                    int index = -1;
                    for (int i = 0; i < list->labels.size(); ++i)
                        if (list->labels[i].compare(label, Qt::CaseInsensitive) == 0) { index = i; break; }
                    if (index < 0)
                        return fail(QStringLiteral("material.set: '%1' is not a value of '%2' "
                                                   "— accepted: %3")
                                        .arg(label, key, list->labels.join(QStringLiteral(", "))));
                    newValue = index;
                }
                // AN OUT-OF-RANGE ORDINAL IS DELIBERATELY STILL ACCEPTED, and
                // that is not an oversight. Enum rows are ints ON DISK, and the
                // documented rule is that a document written by a NEWER build
                // must still open: the document stores what it was told and the
                // engine boundary falls back to the safe value (brdfEngineName
                // returns "Default" for any unknown index). scripting.e2e.
                // pbs_knobs asserts exactly that, by name.
                //
                // A bad NAME is a different thing and is refused above: a label
                // can only come from a human or a script, never from an older
                // file, so there is no compatibility to preserve — only the
                // silent coercion to 0 that used to happen.
            }
            break;
        }
        if (!known) {
            // A PbrMaterial map key is legal even if its Property row were
            // ever dropped. The legacy *Texture spellings never reach here —
            // they are refused by name at the top of the loop (F7).
            if (!mapKeys.contains(key))
                // The key list is the ONE thing that turns this from a bare
                // rejection into a usable answer (AI_SURFACE_PROGRAM_SPEC §3.A
                // item #1): exactly the keys that survive the F7 fix. The legacy
                // spellings are deliberately NOT in it.
                return fail(QStringLiteral("material.set: unknown property '%1' — this "
                                           "material accepts: %2 (material.properties('%3') "
                                           "gives their types, values and ranges)")
                                .arg(key, writableMaterialKeys(material).join(QStringLiteral(", ")),
                                     nodeId));
        }

        host.services->undo->push(new ChangeMaterialPropertyCommand(material, key, oldValue, newValue));
    }

    // A MATERIAL SLOT BOUND BY GUID PINS THE IMAGE (lane L13): a scene now
    // uses it, so the project must carry it — as a BINDING, like the decal,
    // particle and light-profile bindings (it is referenced, not added: no
    // companion material is minted). It is then a tile in the editor's tray
    // and a project export carries it; before, the slot rendered the library
    // bytes and the project never knew it used them.
    //
    // AND IT RECORDS THE EDGE, exactly as the material panel's texture row does
    // (MaterialPropertyWidget::updateTextureDependency) — the verb and the
    // panel are one path or they are two rules. The edge was left out by L13
    // because the library grid hid every dependee, so recording what a slot
    // uses deleted the image from the user's library; the grid now hides
    // MEMBERSHIP instead (Database::memberSubquery), and a USE edge hides
    // nothing anywhere.
    //
    // Only a LIBRARY image (a project's own rows are members already — the view
    // filter tells them apart; project_guid does not, an import made with a
    // project open records it) that the project does not pin yet: addToProject
    // re-pins to the library's CURRENT version, which would silently upgrade an
    // older pin.
    if (!boundTextures.isEmpty() && host.db && host.isProjectOpen()) {
        const QString projectGuid = host.project->getProjectGuid();
        for (const QString &textureGuid : boundTextures) {
            const AssetRecord row = host.db->fetchAsset(textureGuid);
            // The panel writes ONE edge per node+texture: delete before create,
            // so re-binding the same image does not stack rows.
            host.db->deleteDependency(nodeId, textureGuid);
            host.db->createDependency(static_cast<int>(ModelTypes::Object),
                                      static_cast<int>(ModelTypes::Texture),
                                      nodeId, textureGuid, projectGuid);
            if (host.db->isAssetPinnedBy(projectGuid, textureGuid)) continue;
            if (row.view_filter != AssetViewFilter::AssetsView
                && row.view_filter != AssetViewFilter::Effects)
                continue;
            ProjectAssets::addToProject(textureGuid, host.db, host.project,
                                        ProjectAssets::AddKind::Binding);
        }
    }
    return true;
}

bool MaterialApi::reset(const QString &nodeId)
{
    auto meshNode = meshNodeOrFail(nodeId, QStringLiteral("material.reset"));
    if (!meshNode) return false;
    if (!host.services || !host.services->sceneEdit)
        return fail("material.reset: not available in this session");
    // THE PROVIDER QUESTION (services/materialdefaults.h): a node with no
    // default of its own is a plain "no", not a misuse.
    if (!materialdefaults::hasDefault(meshNode))
        return refuse(QStringLiteral("material.reset: '%1' has no default material of its own — "
                                     "only the scene's default floor provides one "
                                     "(material.apply puts a different material on any mesh)")
                          .arg(meshNode->getName()));
    return host.services->sceneEdit->resetMaterial(meshNode)
           || refuse(QStringLiteral("material.reset: the default material of '%1' could not be built")
                         .arg(meshNode->getName()));
}

// ONE LAYER, THROUGH THE SAME ROWS material.set writes (MATERIAL_GAPS_SPEC
// §3.4's verb shape). Deliberately not a second write path: this translates the
// ergonomic shape into flat keys and calls set(), so the undo command, the
// validation, the colour/texture coercion and the enum-name handling are all
// the ones material.set already has — the PUBLISH_AUDIT #4 lesson (one table,
// N consumers) applied to a verb rather than to a vocabulary.
bool MaterialApi::setDetail(const QString &nodeId, int layer, const QVariantMap &values)
{
    auto meshNode = meshNodeOrFail(nodeId, QStringLiteral("material.setDetail"));
    if (!meshNode) return false;
    if (layer < 0 || layer >= iris::PbrMaterial::kDetailLayers)
        return fail(QStringLiteral("material.setDetail: layer %1 does not exist — this material "
                                   "has %2 (0..%3)")
                        .arg(layer).arg(iris::PbrMaterial::kDetailLayers)
                        .arg(iris::PbrMaterial::kDetailLayers - 1));
    if (values.isEmpty())
        return fail(QStringLiteral("material.setDetail: expected an object with at least one of "
                                   "map, normalMap, blend, offset, scale, weight, normalWeight"));

    QVariantMap flat;
    const auto row = [layer](const char *suffix) {
        return iris::PbrMaterial::detailRow(layer, suffix);
    };
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const QString &k = it.key();
        if      (k == QLatin1String("map"))          flat[row("Map")] = it.value();
        else if (k == QLatin1String("normalMap"))    flat[row("NormalMap")] = it.value();
        else if (k == QLatin1String("blend"))        flat[row("Blend")] = it.value();
        else if (k == QLatin1String("weight"))       flat[row("Weight")] = it.value();
        else if (k == QLatin1String("normalWeight")) flat[row("NormalWeight")] = it.value();
        else if (k == QLatin1String("offset") || k == QLatin1String("scale")) {
            // {x, y} or [x, y] — the two shapes every vector argument in the
            // registry accepts.
            const QVariant v = normalizeJs(it.value());
            const bool isOffset = k == QLatin1String("offset");
            QVariant xv, yv;
            if (v.typeId() == QMetaType::QVariantList) {
                const QVariantList l = v.toList();
                if (l.size() >= 2) { xv = l.at(0); yv = l.at(1); }
            } else {
                const QVariantMap m = v.toMap();
                if (m.contains(QStringLiteral("x"))) xv = m.value(QStringLiteral("x"));
                if (m.contains(QStringLiteral("y"))) yv = m.value(QStringLiteral("y"));
            }
            if (!xv.isValid() && !yv.isValid())
                return fail(QStringLiteral("material.setDetail: '%1' expects {x, y} or [x, y]")
                                .arg(k));
            if (xv.isValid()) flat[row(isOffset ? "OffsetU" : "ScaleU")] = xv;
            if (yv.isValid()) flat[row(isOffset ? "OffsetV" : "ScaleV")] = yv;
        }
        else
            return fail(QStringLiteral("material.setDetail: unknown key '%1' — accepted: map, "
                                       "normalMap, blend, offset, scale, weight, normalWeight")
                            .arg(k));
    }
    return set(nodeId, flat);
}

QVariantList MaterialApi::detail(const QString &nodeId)
{
    QVariantList out;
    auto meshNode = meshNodeOrFail(nodeId, QStringLiteral("material.detail"));
    if (!meshNode) return out;
    auto pbr = meshNode->getMaterial().dynamicCast<iris::PbrMaterial>();
    if (!pbr) { fail("material.detail: the node has no PBR material"); return out; }
    const auto &names = iris::PbrMaterial::detailBlendNames();
    for (int i = 0; i < iris::PbrMaterial::kDetailLayers; ++i) {
        const auto &d = pbr->detail[i];
        out.append(QVariantMap{
            { "map", d.map }, { "normalMap", d.normalMap },
            { "blend", d.blend },
            { "blendName", QString::fromLatin1(names.value(d.blend, names.value(0))) },
            { "offset", QVariantMap{ { "x", d.offsetU }, { "y", d.offsetV } } },
            { "scale",  QVariantMap{ { "x", d.scaleU },  { "y", d.scaleV } } },
            { "weight", d.weight }, { "normalWeight", d.normalWeight },
        });
    }
    return out;
}

QVariantMap MaterialApi::properties(const QString &nodeId)
{
    QVariantMap out;
    auto meshNode = meshNodeOrFail(nodeId, QStringLiteral("material.properties"));
    if (!meshNode) return out;
    auto material = meshNode->getMaterial();
    if (!material) { fail("material.properties: the node has no material"); return out; }

    // Unlike SceneNode::getProperties(), a material's rows are OWNED by the
    // material (iris::Material holds the QList<Property*> for its lifetime) —
    // read them, never delete them.
    // "CustomMaterial" was a third answer here until HLMS_ADOPTION P4b. It
    // cannot come back: the class is gone and a legacy material converts at
    // load, so a script asking what it is holding gets PbrMaterial.
    out["class"] = !material.dynamicCast<iris::PbrMaterial>().isNull()
                       ? QStringLiteral("PbrMaterial")
                       : QStringLiteral("Material");

    QVariantList rows;
    for (auto *prop : declaredProperties(material)) rows.append(propertyRowToJs(prop));
    out["rows"] = rows;
    out["writableKeys"] = writableMaterialKeys(material);
    return out;
}

QVariantMap MaterialApi::get(const QString &nodeId)
{
    QVariantMap out;
    auto meshNode = meshNodeOrFail(nodeId, QStringLiteral("material.get"));
    if (!meshNode) return out;
    auto material = meshNode->getMaterial();
    if (!material) return out;

    for (auto prop : declaredProperties(material)) {
        const QVariant value = prop->getValue();
        if (value.typeId() == QMetaType::QColor) out[prop->name] = colorToJs(value.value<QColor>());
        else if (value.typeId() == QMetaType::QVector3D) out[prop->name] = vecToJs(iris::fromQt(value.value<QVector3D>()));
        else out[prop->name] = value;
    }
    // The generated shader pieces (HLMS_ADOPTION P5) are NOT declared
    // properties — they are a cache reference the emitter owns, not something
    // a user or a script sets — but they are the only way to see, from
    // outside, that a graph material actually reached the renderer as a live
    // surface rather than a baked one. Reported read-only, and absent when
    // there is none.
    if (auto *pbr = dynamic_cast<iris::PbrMaterial *>(material.data())) {
        if (!pbr->customPiecePixel.isEmpty()) out["customPiecePixel"] = pbr->customPiecePixel;
        if (!pbr->customPieceVertex.isEmpty()) out["customPieceVertex"] = pbr->customPieceVertex;
    }
    return out;
}

QString MaterialApi::dumpDatablock(const QString &nodeId)
{
    auto meshNode = meshNodeOrFail(nodeId, QStringLiteral("material.dumpDatablock"));
    if (!meshNode) return QString();
    if (!host.viewport) {
        fail("material.dumpDatablock: no viewport in this session (the renderer is "
             "what holds the datablock; there is nothing to dump without one)");
        return QString();
    }
    const QString dump = host.viewport->dumpMaterial(nodeId);
    if (dump.isEmpty()) {
        // Empty is never "the datablock is empty" — it means we could not
        // reach one, and saying which is the whole difference between a
        // diagnostic and a second mystery.
        fail(QStringLiteral("material.dumpDatablock: no renderer material for '%1' "
                            "(the node is not mirrored into the engine scene, or this "
                            "viewport has no mirror — a headless/document-only run)")
                 .arg(nodeId));
        return QString();
    }
    return dump;
}

// -------------------------------------------------------------------- graph.*

QVector<VerbInfo> GraphApi::verbs() const
{
    return {
        { "nodes", "graph.nodes() -> [{id, type, master}]",
          "The current graph's nodes.",
          Needs::Document },
        { "connections", "graph.connections() -> [{id, from, fromSocket, fromIndex, to, toSocket, toIndex}]",
          "Every pipe in the current graph: `from`/`to` are node ids, `fromSocket`/`toSocket` the "
          "socket NAMES (what graph.connect accepts), `fromIndex`/`toIndex` the same sockets as "
          "indices. `id` is what graph.disconnect takes. graph.nodes said what is in a graph; this "
          "says how it is wired — without it the only way to read a loaded graph's topology was to "
          "bake it and guess.",
          Needs::Document },
        { "nodeTypes", "graph.nodeTypes() -> [type]",
          "Every node type the library can create (plus the master type PbrMaterial).",
          Needs::Document },
        { "nodeInfo", "graph.nodeInfo(type) -> {type, inputs: [{name, index, type, value}], outputs: [...]}",
          "What a node type's sockets are, WITHOUT adding one to the graph: the input and output "
          "socket names in index order, their socket type name, and an input's default literal. "
          "graph.connect takes socket names, and this is the only way to learn them for a type you "
          "have not instantiated. Unknown types are refused with the same message graph.addNode "
          "gives.",
          Needs::Document },
        { "addNode", "graph.addNode(type) -> id",
          "Adds a node to the current graph ('PbrMaterial' adds and sets the master).",
          Needs::Document },
        { "removeNode", "graph.removeNode(id) -> bool",
          "Deletes a node and every pipe attached to it. On the MATERIALS PAGE this goes through "
          "the page's edit stack — the same command the canvas's Delete key pushes — so graph.undo "
          "takes it back; on a script graph opened by materials.loadGraph there is no command "
          "stack and the deletion is immediate. The MASTER node is refused: it is the graph's "
          "output, and a graph without one bakes nothing.",
          Needs::Document },
        { "connect", "graph.connect(fromId, fromSocket, toId, toSocket) -> bool",
          "Connects an output socket to an input socket; sockets by index or name (e.g. 'Base Color').",
          Needs::Document },
        { "disconnect", "graph.disconnect(connectionIdOr{to, toSocket}) -> bool",
          "Removes ONE pipe. Address it by the id graph.connections() reports, or — the shape a "
          "caller usually has — by the INPUT end, {to: nodeId, toSocket: nameOrIndex}, because an "
          "input socket holds at most one connection so that pair names exactly one pipe. Undo "
          "behaves exactly as graph.removeNode's does. False when there is no such pipe.",
          Needs::Document },
        { "setValue", "graph.setValue(nodeId, value) -> bool",
          "Sets a node's value through the same path the editor uses (numbers, {r,g,b,a} colors, {x,y,z} vectors).",
          Needs::Document },
        { "getValue", "graph.getValue(nodeId) -> value",
          "Reads a node's value back.",
          Needs::Document },
        { "evaluate", "graph.evaluate() -> {values, unsupported, approximated, animated}",
          "Folds the current graph to PBR material values (the evaluator is GL-free by design). Pure math chains fold; "
          "approximated lists nodes evaluated against the fake fragment context (worldNormal, fresnel, time at t=0, ...).",
          Needs::Document },
        { "bakeInfo", "graph.bakeInfo() -> {perSocket: {socketName: class}, fold, foldReason?, migrations?}",
          "Classifies each master input: 'uniform' | 'passthrough' | 'baked' | 'unsupported' | 'unconnected'. "
          "`fold` is THE UV TRANSFORM ROUTE (MATERIAL_UV_NODES_SPEC): when every texture in the "
          "graph reads the mesh UVs through the same constant tiling/offset/rotation, that "
          "transform is lifted onto the MATERIAL — `{scale:[u,v], offset:[u,v], rotation, "
          "samplers}` — the sources bind at full resolution and no map is baked. `fold: null` "
          "with `foldReason` means the textures are RESAMPLED into baked maps instead, which "
          "costs resolution (a 4x tiling into a 1024 bake keeps 256 px per tile), so the reason "
          "is worth reading. `migrations` lists what loading the graph had to change.",
          Needs::Document },
        { "emitInfo", "graph.emitInfo() -> {accepted, animated, emitted: [socket], fallback: {socket: reason}, "
          "ops: [opKey], pixelSource, vertexSource}",
          "What the SHADER-PIECE EMITTER makes of the current graph (HLMS_ADOPTION P5): which master "
          "sockets it lowers into generated GLSL that runs on the GPU per pixel (and per vertex), and "
          "— the half that matters when something did not animate — the REASON every other socket was "
          "left to the CPU baker, in words. `ops` is the emitter's whole op vocabulary. The two source "
          "strings are the generated pieces themselves, for eyeballing and for tests; nothing is "
          "written to disk by this verb.",
          Needs::Document },
        { "bake", "graph.bake({resolution?, time?}) -> {values, maps, passthrough, approximated, unsupported, animated, msElapsed}",
          "Full-quality synchronous bake of the current graph: UV-varying chains render per texel into "
          "hash-cached PNGs in the store's own disposable derived cache (headless-capable - CPU only; graph.save "
          "is what turns them into member textures), uniform chains fold, "
          "bare textures pass through. Map values are project-relative paths.",
          Needs::Document },
        { "toMaterial", "graph.toMaterial(nodeId) -> bool",
          "Evaluates the current graph and applies the resulting PBR material to a mesh node.",
          Needs::Document },
        { "save", "graph.save() -> bool",
          "Serializes the current graph back into its shader asset (only for graphs opened from an asset guid).",
          Needs::Document },
        { "selectNode", "graph.selectNode(id) -> bool",
          "Selects a node. When the Effects page has a node with this id its canvas selection (and the properties panel) follows; "
          "otherwise the id must belong to the current script graph.",
          Needs::Document },
        { "selectedNode", "graph.selectedNode() -> id|null",
          "The selected node's id: the Effects page's canvas selection when one exists, else the script-side selection.",
          Needs::Document },
        { "deselect", "graph.deselect() -> bool",
          "Clears the selection (canvas and script-side).",
          Needs::Document },
        { "settings", "graph.settings() -> {name, blendMode, bakeResolution}",
          "The current graph's material settings; blendMode is one of "
          "'Opaque' | 'Masked' | 'Translucent' | 'Additive' | 'Modulate' | 'Glass' | 'Refractive'.",
          Needs::Document },
        { "setBlendMode", "graph.setBlendMode(mode) -> bool",
          "Sets the master material's blend mode ('Opaque' | 'Masked' | 'Translucent' | 'Additive' | 'Modulate' | 'Glass' | 'Refractive' — "
          "the Unreal set; 'Blend' is accepted as the legacy name for 'Translucent'). Material state only: bakes are "
          "unaffected, the evaluated material's alphaMode changes.",
          Needs::Document },
        { "undo", "graph.undo() -> bool",
          "Undoes one edit on the Materials page's graph — the SAME stack the page's toolbar arrows and "
          "Ctrl+Z on that page drive (node adds and deletes, moves, connections, pastes, property edits). "
          "False when there is nothing to undo, or when this session has no Materials page (the API-local "
          "script graph keeps no command history). Scene edits are editor.undo's; the two stacks are separate.",
          Needs::Window },
        { "redo", "graph.redo() -> bool",
          "Redoes the last undone graph edit. False when there is nothing to redo, or with no Materials page.",
          Needs::Window },
        { "paletteTile", "graph.paletteTile(name) -> {tab, tabIndex, x, y, w, h, clickable, canvas:{x,y,w,h}, window:{w,h}} | null",
          "Where the node palette's tile called `name` is, in MAIN-WINDOW pixels — the coordinates "
          "a rig synthesising mouse input works in. SELECTS the tab that owns the tile and scrolls "
          "it into view first, so the returned rect is one a click can actually land in "
          "(`clickable` says whether it did). `canvas` is the graph view's rect, the only "
          "meaningful drop target for a palette drag, and `window` is the size of the window all "
          "of them are measured in (check it against the window you are clicking). Null with no "
          "Materials page, or when no "
          "tile carries that name (matched on the displayed name, case-insensitively). It exists "
          "because dragging a tile onto the canvas is the one graph edit no verb can make: the "
          "graph.* mutation verbs work on a script-local graph, never the page's.",
          Needs::Window },
        { "undoState", "graph.undoState() -> {available, canUndo, canRedo, undoCount, redoCount}",
          "The depth of the Materials page's graph edit stack. `available` is false in sessions with no "
          "Materials page, which is also why the counts are then zero.",
          Needs::Document },
    };
}

void GraphApi::setCurrent(NodeGraph *graph, const QString &assetGuid)
{
    // THE OLD GRAPH IS FREED (MATERIALS_TABS_SPEC §2.8). This used to read
    // "NodeGraph has no proper deep-delete; dropping the old pointer leaks a
    // little" — it has one now, so a script that opens ten materials no
    // longer leaves nine whole graphs behind. A script graph is never in a
    // canvas, so nothing else points into it.
    if (mGraph != graph) delete mGraph;
    mGraph = graph;
    mAssetGuid = assetGuid;
    mSelectedNodeId.clear();
}

NodeGraph *GraphApi::graphOrFail(const QString &verb)
{
    if (!mGraph) fail(QStringLiteral("%1: no graph is open — materials.loadGraph()/createGraph() first").arg(verb));
    return mGraph;
}

NodeGraph *GraphApi::editableGraphOrFail(const QString &verb)
{
    NodeGraph *graph = graphOrFail(verb);
    if (!graph) return nullptr;
    // A PRESET IS EDITABLE IN A PROJECT (PRESET-EDIT-1). The lock used to be
    // absolute — the definition writer refuses the reserved guid, so every
    // mutation was refused before it could reach a save that could not land.
    // The writer still refuses the MASTER; what changed is that an edit made
    // with a project open is not aimed at the master at all: `graph.save()`
    // copies on write first (materials.edit) and saves into the project's own
    // bundle. So the refusal is now exactly the one case where that cannot
    // happen — no project, nowhere for the copy to live.
    const QString why = presetedit::refusal(
        host.db, host.isProjectOpen() ? host.project : nullptr, mAssetGuid);
    if (why.isEmpty()) return graph;
    fail(QStringLiteral("%1: %2").arg(verb, why));
    return nullptr;
}

QVariantList GraphApi::nodes()
{
    QVariantList out;
    auto graph = graphOrFail(QStringLiteral("graph.nodes"));
    if (!graph) return out;
    for (auto it = graph->nodes.constBegin(); it != graph->nodes.constEnd(); ++it) {
        NodeModel *node = it.value();
        out.append(QVariantMap{ { "id", node->id },
                                { "type", node->typeName },
                                { "master", node == graph->masterNode } });
    }
    return out;
}

QVariantList GraphApi::connections()
{
    QVariantList out;
    auto graph = graphOrFail(QStringLiteral("graph.connections"));
    if (!graph) return out;
    for (auto it = graph->connections.constBegin(); it != graph->connections.constEnd(); ++it) {
        ConnectionModel *con = it.value();
        if (!con || !con->leftSocket || !con->rightSocket) continue;
        NodeModel *from = con->leftSocket->node;
        NodeModel *to = con->rightSocket->node;
        if (!from || !to) continue;
        out.append(QVariantMap{
            { "id", con->id },
            { "from", from->id },
            { "fromSocket", con->leftSocket->name },
            { "fromIndex", from->outSockets.indexOf(con->leftSocket) },
            { "to", to->id },
            { "toSocket", con->rightSocket->name },
            { "toIndex", to->inSockets.indexOf(con->rightSocket) } });
    }
    return out;
}

QVariantMap GraphApi::nodeInfo(const QString &type)
{
    QVariantMap out;
    // A THROWAWAY instance is the only honest source: sockets are added by the
    // node's own constructor (there is no static socket table anywhere), so
    // anything else here would be a second, drifting description of them.
    NodeModel *probe = nullptr;
    if (type == QLatin1String("PbrMaterial")) {
        probe = new PbrMasterNode();
    } else {
        LibraryV1 library;
        if (!library.hasNode(type)) {
            fail(QStringLiteral("graph.nodeInfo: unknown type '%1' (graph.nodeTypes() lists them)").arg(type));
            return out;
        }
        probe = library.createNode(type);
    }
    if (!probe) {
        fail(QStringLiteral("graph.nodeInfo: '%1' could not be instantiated").arg(type));
        return out;
    }

    const auto describe = [](const QVector<SocketModel *> &sockets, bool wantValue) {
        QVariantList rows;
        for (int i = 0; i < sockets.size(); ++i) {
            SocketModel *sock = sockets[i];
            if (!sock) continue;
            QVariantMap row{ { "name", sock->name }, { "index", i }, { "type", sock->typeName } };
            // The default LITERAL an unconnected input folds to — the thing a
            // caller wants to know before deciding whether to wire it at all.
            if (wantValue && !sock->value.isEmpty()) row["value"] = sock->value;
            rows.append(row);
        }
        return rows;
    };

    out["type"] = probe->typeName.isEmpty() ? type : probe->typeName;
    out["title"] = probe->title;
    out["inputs"] = describe(probe->inSockets, true);
    out["outputs"] = describe(probe->outSockets, false);
    delete probe;
    return out;
}

bool GraphApi::removeNode(const QString &nodeId)
{
    // The PAGE first (the selectNode pattern): if the Materials page's canvas
    // owns this node the deletion has to go through its undo stack.
    if (mEdit.removeNode && mEdit.removeNode(nodeId)) return true;

    auto graph = editableGraphOrFail(QStringLiteral("graph.removeNode"));
    if (!graph) return false;
    if (!graph->nodes.contains(nodeId))
        return fail(QStringLiteral("graph.removeNode: no node '%1'").arg(nodeId));
    if (graph->masterNode && graph->masterNode->id == nodeId)
        return fail("graph.removeNode: the master node is the graph's output and cannot be removed");
    graph->removeNode(nodeId);
    if (mSelectedNodeId == nodeId) mSelectedNodeId.clear();
    return true;
}

bool GraphApi::disconnect(const QVariant &connection)
{
    const QVariant value = normalizeJs(connection);

    // Shape 1: a connection id.
    if (value.typeId() == QMetaType::QString) {
        const QString id = value.toString();
        if (mEdit.removeConnection && mEdit.removeConnection(id)) return true;
        auto graph = editableGraphOrFail(QStringLiteral("graph.disconnect"));
        if (!graph) return false;
        if (!graph->connections.contains(id))
            return fail(QStringLiteral("graph.disconnect: no connection '%1' "
                                       "(graph.connections() lists them)").arg(id));
        graph->removeConnection(id);
        return true;
    }

    // Shape 2: the INPUT end, {to, toSocket} — an input socket holds at most
    // one connection, so the pair names exactly one pipe.
    if (value.typeId() != QMetaType::QVariantMap)
        return fail("graph.disconnect: pass a connection id or {to: nodeId, toSocket: nameOrIndex}");
    const QVariantMap m = value.toMap();
    auto graph = editableGraphOrFail(QStringLiteral("graph.disconnect"));
    if (!graph) return false;
    const QString toId = m.value(QStringLiteral("to")).toString();
    if (!graph->nodes.contains(toId))
        return fail(QStringLiteral("graph.disconnect: no node '%1'").arg(toId));
    NodeModel *to = graph->nodes[toId];

    const QVariant ref = m.value(QStringLiteral("toSocket"));
    int index = -1;
    bool isInt = false;
    const int asInt = ref.toInt(&isInt);
    if (isInt && ref.typeId() != QMetaType::QString) {
        index = asInt;
    } else {
        const QString name = ref.toString();
        for (int i = 0; i < to->inSockets.size(); ++i)
            if (to->inSockets[i]->name.compare(name, Qt::CaseInsensitive) == 0) { index = i; break; }
    }
    if (index < 0 || index >= to->inSockets.size())
        return fail(QStringLiteral("graph.disconnect: no input socket '%1' on '%2'")
                        .arg(ref.toString(), toId));

    ConnectionModel *con = to->inSockets[index]->connection;
    if (!con)
        return fail(QStringLiteral("graph.disconnect: nothing is connected to '%1' on '%2'")
                        .arg(to->inSockets[index]->name, toId));
    // No page offer here: the id was resolved against the SCRIPT graph, so it
    // is that graph's pipe by construction.
    graph->removeConnection(con->id);
    return true;
}

QVariantList GraphApi::nodeTypes()
{
    QVariantList out;
    LibraryV1 library;
    for (auto item : library.getItems()) out.append(item->name);
    // "PbrMaterial" is THE master node — the only one. ("Material", the
    // Blinn-Phong master, is not listed because it no longer EXISTS:
    // LEGACY-MASTER-CRUD deleted the class and converts old graphs at load.)
    out.append(QStringLiteral("PbrMaterial"));
    return out;
}

QString GraphApi::addNode(const QString &type)
{
    auto graph = editableGraphOrFail(QStringLiteral("graph.addNode"));
    if (!graph) return QString();

    NodeModel *node = nullptr;
    if (type == "PbrMaterial") {
        node = new PbrMasterNode();
    } else {
        // ("property" retired §3b — it is simply no longer a library type)
        LibraryV1 library;
        if (!library.hasNode(type)) {
            fail(QStringLiteral("graph.addNode: unknown type '%1' (graph.nodeTypes() lists them)").arg(type));
            return QString();
        }
        node = library.createNode(type);
    }
    graph->addNode(node);
    if (type == "PbrMaterial") graph->setMasterNode(node);
    return node->id;
}

bool GraphApi::connect(const QString &fromId, const QVariant &fromSocket,
                       const QString &toId, const QVariant &toSocket)
{
    auto graph = editableGraphOrFail(QStringLiteral("graph.connect"));
    if (!graph) return false;
    if (!graph->nodes.contains(fromId) || !graph->nodes.contains(toId))
        return fail("graph.connect: no such node id");
    NodeModel *from = graph->nodes[fromId];
    NodeModel *to = graph->nodes[toId];

    auto resolve = [this](const QVariant &ref, const QVector<SocketModel *> &sockets,
                          const char *side) -> int {
        const QVariant v = normalizeJs(ref);
        bool isInt = false;
        const int index = v.toInt(&isInt);
        if (isInt && v.typeId() != QMetaType::QString) {
            if (index >= 0 && index < sockets.size()) return index;
        } else {
            const QString name = v.toString();
            for (int i = 0; i < sockets.size(); ++i)
                if (sockets[i]->name.compare(name, Qt::CaseInsensitive) == 0) return i;
        }
        fail(QStringLiteral("graph.connect: no %1 socket '%2'").arg(side, v.toString()));
        return -1;
    };

    const int outIndex = resolve(fromSocket, from->outSockets, "output");
    if (outIndex < 0) return false;
    const int inIndex = resolve(toSocket, to->inSockets, "input");
    if (inIndex < 0) return false;

    graph->addConnection(from, outIndex, to, inIndex);
    return true;
}

bool GraphApi::setValue(const QString &nodeId, const QVariant &value)
{
    auto graph = editableGraphOrFail(QStringLiteral("graph.setValue"));
    if (!graph) return false;
    if (!graph->nodes.contains(nodeId)) return fail("graph.setValue: no such node id");
    // The NodeModel interface is the public route (some overrides are private).
    static_cast<NodeModel *>(graph->nodes[nodeId])
        ->deserializeWidgetValue(QJsonValue::fromVariant(normalizeJs(value)));
    return true;
}

QVariant GraphApi::getValue(const QString &nodeId)
{
    auto graph = graphOrFail(QStringLiteral("graph.getValue"));
    if (!graph) return QVariant();
    if (!graph->nodes.contains(nodeId)) { fail("graph.getValue: no such node id"); return QVariant(); }
    return static_cast<NodeModel *>(graph->nodes[nodeId])->serializeWidgetValue().toVariant();
}

QVariantMap GraphApi::evaluate()
{
    QVariantMap out;
    auto graph = graphOrFail(QStringLiteral("graph.evaluate"));
    if (!graph) return out;
    const auto result = PbrGraphEvaluator::evaluate(graph, MaterialHelper::textureResolver());
    out["values"] = result.values.toVariantMap();
    out["unsupported"] = QVariant(result.unsupportedNodes);
    out["approximated"] = QVariant(result.approximatedNodes);
    out["animated"] = result.animated;
    return out;
}

QVariantMap GraphApi::bakeInfo()
{
    QVariantMap out;
    auto graph = graphOrFail(QStringLiteral("graph.bakeInfo"));
    if (!graph) return out;
    return PbrGraphEvaluator::bakeInfo(graph, MaterialHelper::textureResolver()).toVariantMap();
}

QVariantMap GraphApi::emitInfo()
{
    QVariantMap out;
    auto graph = graphOrFail(QStringLiteral("graph.emitInfo"));
    if (!graph) return out;
    const auto result = materials::PieceEmitter::lower(graph, MaterialHelper::textureResolver());
    out["accepted"] = result.accepted;
    out["animated"] = result.animated;
    out["emitted"] = QVariant(result.emittedSockets);
    QVariantMap fallback;
    for (auto it = result.fallbackReasons.constBegin(); it != result.fallbackReasons.constEnd(); ++it)
        fallback[it.key().isEmpty() ? QStringLiteral("*") : it.key()] = it.value();
    out["fallback"] = fallback;
    out["ops"] = QVariant(materials::PieceEmitter::supportedOps());
    out["sockets"] = QVariant(materials::PieceEmitter::supportedSockets());
    out["pixelSource"] = result.pixelSource;
    out["vertexSource"] = result.vertexSource;
    return out;
}

QVariantMap GraphApi::bake(const QVariantMap &options)
{
    QVariantMap out;
    auto graph = editableGraphOrFail(QStringLiteral("graph.bake"));
    if (!graph) return out;
    QString guid = mAssetGuid.isEmpty() ? graph->materialGuid : mAssetGuid;
    if (guid.isEmpty()) guid = QStringLiteral("scratch");

    // A DIAGNOSTIC BAKE, into the store's own disposable derived cache — never
    // into a project folder (MATERIAL_BUNDLE_SPEC phase 1: nothing of a
    // material lives outside the store any more). It needs no project, which
    // is the point: a library material bakes and previews with none open.
    materials::GraphBaker::Options opts;
    opts.resolution = options.value(QStringLiteral("resolution"), 1024).toInt();
    opts.time = options.value(QStringLiteral("time"), 0.0).toDouble();
    opts.outputDir = AssetStorePaths::derivedPath(QStringLiteral("materialbake/") + guid);
    // AND THE EMITTED VALUE MUST NAME THE FILE. `relativePrefix` is what the
    // baker prepends to every map it reports; with it EMPTY the report is a
    // bare "roughnessMap-<hash>.png", which resolves against nothing and
    // renders as no texture at all. The old value was project-relative
    // ("BakedMaps/<guid>/") and a resolver put the project folder back in
    // front of it; there is no project folder any more, so the prefix is the
    // absolute directory itself. A path is legitimate here — this is the
    // BUILD side; only what is STORED must name guids.
    opts.relativePrefix = opts.outputDir + QLatin1Char('/');
    QDir().mkpath(opts.outputDir);

    const auto result = materials::GraphBaker::run(graph, opts, MaterialHelper::textureResolver());
    out["values"] = result.eval.values.toVariantMap();
    out["maps"] = result.maps.toVariantMap();
    out["passthrough"] = result.passthrough.toVariantMap();
    out["approximated"] = QVariant(result.eval.approximatedNodes);
    out["unsupported"] = QVariant(result.eval.unsupportedNodes);
    out["animated"] = result.eval.animated;
    out["msElapsed"] = double(result.msElapsed);
    return out;
}

bool GraphApi::toMaterial(const QString &nodeId)
{
    auto graph = graphOrFail(QStringLiteral("graph.toMaterial"));
    if (!graph) return false;
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene() : iris::ScenePtr();
    if (!scene) return fail("graph.toMaterial: no scene is open");
    auto node = findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node || node->getSceneNodeType() != iris::SceneNodeType::Mesh)
        return fail(QStringLiteral("graph.toMaterial: '%1' is not a mesh node").arg(nodeId));

    // APPLYING IS A FINAL-BAKE TRIGGER (spec section 2). The bake lands in the
    // store's derived cache and the material is built from the PATHS it
    // produced — this is the BUILD side of the bundle's lock 2, where paths
    // are legitimate; only what is STORED must name guids.
    iris::PbrMaterialPtr material;
    {
        QString guid = mAssetGuid.isEmpty() ? graph->materialGuid : mAssetGuid;
        if (guid.isEmpty()) guid = QStringLiteral("scratch");

        materials::GraphBaker::Options opts;
        opts.resolution = graph->settings.bakeResolution;
        opts.outputDir = AssetStorePaths::derivedPath(QStringLiteral("materialbake/") + guid);
        opts.relativePrefix = opts.outputDir + QLatin1Char('/');   // the map must NAME its file
        QDir().mkpath(opts.outputDir);
        // The emitter first, so the baker skips what the piece owns
        // (HLMS_ADOPTION P5).
        materials::PieceEmitter::Result emitted = materials::PieceEmitter::lower(
            graph, MaterialHelper::textureResolver());
        opts.emittedSockets = emitted.emittedSockets;
        const auto baked = materials::GraphBaker::run(graph, opts, MaterialHelper::textureResolver());
        material = PbrGraphEvaluator::materialFromValues(baked.eval.values, MaterialHelper::textureResolver());
        MaterialHelper::applyEmittedPieces(graph, material);
    }
    if (!material) return fail("graph.toMaterial: evaluation produced no material");
    // Stamp the SOURCE GRAPH's asset guid on the material. It costs nothing for
    // an ordinary material and it is what lets a reopened scene regenerate a
    // GENERATED SHADER PIECE (HLMS_ADOPTION P5) whose cache file this machine
    // does not have: the guid names the asset, the asset carries the graph, and
    // the graph re-emits byte-identical source.
    {
        const QString guid = mAssetGuid.isEmpty() ? graph->materialGuid : mAssetGuid;
        if (!guid.isEmpty()) material->setGuid(guid);
    }
    node.staticCast<iris::MeshNode>()->setMaterial(material);
    return true;
}

bool GraphApi::selectNode(const QString &nodeId)
{
    // the live Effects canvas wins when it knows the id (§3a: the panel
    // follows its selection)
    if (mSelection.select && mSelection.select(nodeId)) {
        mSelectedNodeId.clear();
        return true;
    }

    auto graph = graphOrFail(QStringLiteral("graph.selectNode"));
    if (!graph) return false;
    if (!graph->nodes.contains(nodeId))
        return fail(QStringLiteral("graph.selectNode: no node '%1' in the Effects page or the current graph").arg(nodeId));
    mSelectedNodeId = nodeId;
    return true;
}

QVariant GraphApi::selectedNode()
{
    if (mSelection.selected) {
        const QString pageId = mSelection.selected();
        if (!pageId.isEmpty()) return pageId;
    }
    if (!mSelectedNodeId.isEmpty() && mGraph && mGraph->nodes.contains(mSelectedNodeId))
        return mSelectedNodeId;
    return QVariant(); // null: nothing selected anywhere
}

bool GraphApi::deselect()
{
    if (mSelection.deselect) mSelection.deselect();
    mSelectedNodeId.clear();
    return true;
}

namespace {
// One name per BlendMode, matching the settings-view combo labels and the
// serialized strings (nodegraph.cpp keeps "Blend" on disk for Translucent).
const char *blendModeName(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Opaque:      return "Opaque";
    case BlendMode::Masked:      return "Masked";
    case BlendMode::Translucent: return "Translucent";
    case BlendMode::Additive:    return "Additive";
    case BlendMode::Modulate:    return "Modulate";
    case BlendMode::Glass:       return "Glass";
    case BlendMode::Refractive:  return "Refractive";
    }
    return "Opaque";
}
} // namespace

QVariantMap GraphApi::settings()
{
    QVariantMap out;
    auto graph = graphOrFail(QStringLiteral("graph.settings"));
    if (!graph) return out;
    out["name"] = graph->settings.name;
    out["blendMode"] = QString::fromLatin1(blendModeName(graph->settings.blendMode));
    out["bakeResolution"] = graph->settings.bakeResolution;
    return out;
}

bool GraphApi::setBlendMode(const QString &mode)
{
    auto graph = editableGraphOrFail(QStringLiteral("graph.setBlendMode"));
    if (!graph) return false;
    const QString m = mode.trimmed().toLower();
    BlendMode want;
    if      (m == "opaque")                       want = BlendMode::Opaque;
    else if (m == "masked")                       want = BlendMode::Masked;
    else if (m == "translucent" || m == "blend")  want = BlendMode::Translucent;
    else if (m == "additive")                     want = BlendMode::Additive;
    else if (m == "modulate")                     want = BlendMode::Modulate;
    else if (m == "glass")                        want = BlendMode::Glass;
    else if (m == "refractive")                   want = BlendMode::Refractive;
    else return fail(QStringLiteral("graph.setBlendMode: unknown mode '%1' "
                     "(Opaque | Masked | Translucent | Additive | Modulate | Glass | "
                     "Refractive)").arg(mode));
    MaterialSettings s = graph->settings;
    s.blendMode = want;
    graph->setMaterialSettings(s);
    return true;
}

// ---- the Materials page's edit stack ---------------------------------------
// The verbs and the shell's Ctrl+Z on that page are two callers of ONE entry
// point (EffectsPage::graphUndo/graphRedo), reached through the delegate
// MaterialsModule::registerApi installs. No page (headless slices, the
// document-only script host) = no stack: say so instead of silently doing
// nothing, because "graph.undo() returned true" is what a test believes.

bool GraphApi::undo()
{
    if (!mUndo.undo)
        return fail("graph.undo: no Materials page in this session (the script-local graph keeps no history)");
    return mUndo.undo();
}

bool GraphApi::redo()
{
    if (!mUndo.redo)
        return fail("graph.redo: no Materials page in this session (the script-local graph keeps no history)");
    return mUndo.redo();
}

QVariantMap GraphApi::undoState()
{
    QVariantMap out;
    const bool available = bool(mUndo.undoCount) && bool(mUndo.redoCount);
    const int undoCount = available ? mUndo.undoCount() : 0;
    const int redoCount = available ? mUndo.redoCount() : 0;
    out.insert("available", available);
    out.insert("canUndo", undoCount > 0);
    out.insert("canRedo", redoCount > 0);
    out.insert("undoCount", undoCount);
    out.insert("redoCount", redoCount);
    return out;
}

QVariant GraphApi::paletteTile(const QString &name)
{
    // Both "no page" and "no such tile" are REFUSALS: a caller asking where a
    // tile is can be told there isn't one without its script being aborted.
    if (!mPalette.tile) {
        refuse(QStringLiteral("graph.paletteTile: this session has no Materials page"));
        return jsNull();
    }
    const QVariantMap rect = mPalette.tile(name);
    if (rect.isEmpty()) {
        refuse(QStringLiteral("graph.paletteTile: no palette tile called '%1'").arg(name));
        return jsNull();
    }
    return rect;
}

bool GraphApi::save()
{
    auto graph = editableGraphOrFail(QStringLiteral("graph.save"));
    if (!graph) return false;
    if (!host.db) return fail("graph.save: not available in this session");
    if (mAssetGuid.isEmpty())
        return fail("graph.save: this graph was loaded from a file, not an asset — no destination");
    if (!graph->masterNode)
        return fail("graph.save: the graph has no master node (serialize would crash)");

    // THE FIRST EDIT OF A PRESET COPIES IT INTO THE PROJECT (PRESET-EDIT-1),
    // and this is an edit landing. BEFORE `buildDefinition`, which parents the
    // final bake's maps to the material being written: run on the master's
    // guid it would mint the preset's member rows all over again, under a
    // material the save is not going to write to.
    //
    // A script that needs the new guid calls `materials.edit()` itself — this
    // verb answers true, as it always has, and `mAssetGuid` is what moved.
    {
        const presetedit::Target target = presetedit::forEdit(
            host.db, host.isProjectOpen() ? host.project : nullptr, mAssetGuid,
            host.services ? host.services->undo : nullptr,
            host.services ? host.services->sceneEdit : nullptr);
        if (!target.ok()) return fail(QStringLiteral("graph.save: %1").arg(target.error));
        // ON AN IDENTITY CHANGE, NOT ON `copied` (the Fable read's item 1):
        // the guid also moves when this project ALREADY had its copy and the
        // graph was loaded from the master — and it is the graph's bindings
        // that make the difference, not who minted the row.
        if (target.guid != mAssetGuid) {
            // A READ BOUND THE SHIPPED FILES (looking at a preset writes
            // nothing); an EDIT binds library assets, because a definition may
            // never carry a path — lock 3, MaterialBundle::write. The content
            // import answers "I already have this" for every one of them
            // (seeding put the bytes in the store), so this costs a hash each.
            MaterialHelper::resolveAppRelativeTextures(
                graph, MaterialHelper::TextureBinding::Import);
            // Only a MINT is news to the drawers; adopting the copy that was
            // already there changes no tile.
            if (target.copied && host.services && host.services->sceneEdit)
                host.services->sceneEdit->requestAssetViewRefresh();
        }
        mAssetGuid = target.guid;
    }

    // SAVING IS THE DEFINITION WRITE (MATERIAL_BUNDLE_SPEC phase 1), and still
    // a final-bake trigger: the maps land as MEMBER textures in the store, not
    // as loose PNGs under the project folder, and the definition names them by
    // guid. It works with NO project open, which the old route could not.
    const auto build = materials::buildDefinition(graph, mAssetGuid, host.db, host.project);
    if (!build.ok()) return fail(QStringLiteral("graph.save: %1").arg(build.error));
    const bool projectOwns =
        host.project && !host.project->getProjectGuid().isEmpty()
        && host.db->isAssetPinnedBy(host.project->getProjectGuid(), mAssetGuid);
    const auto written = MaterialBundle::write(
        host.db, host.project, mAssetGuid, build.definition,
        projectOwns ? MaterialBundle::Scope::Project : MaterialBundle::Scope::Library);
    if (!written.ok) return fail(QStringLiteral("graph.save: %1").arg(written.error));
    if (host.services && host.services->sceneEdit)
        host.services->sceneEdit->refreshMaterialUsers(mAssetGuid);
    return true;
}
