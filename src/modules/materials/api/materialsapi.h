/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_MATERIALSAPI_H
#define SCRIPTING_MATERIALSAPI_H

// materials.* / material.* / graph.* — the material system and the Effects
// module (SCRIPTING_SPEC §1.4).
//
// The graph module drives the shadergraph headlessly: NodeGraph +
// PbrGraphEvaluator were built script-first (the evaluator is GL-free by
// design) — the bridge here is glue. graph.* operates on ONE current graph,
// opened by materials.loadGraph or created by materials.createGraph.

#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

#include "scripting/apimodule.h"
#include "irisgl/irisglfwd.h"

class NodeGraph;
class GraphApi;

/// materials.* — presets and effect-graph assets.
class MaterialsApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("materials"); }
    QVector<VerbInfo> verbs() const override;

    /// loadGraph hands the deserialized graph to the graph.* module.
    void setGraphModule(GraphApi *graphApi) { mGraphApi = graphApi; }

    /// THE MATERIALS PAGE'S TABS (MATERIALS_TABS_SPEC §3). The five verbs
    /// below are the page's own entry points through this delegate, so a
    /// click on a tab and `materials.activate` are ONE gesture with one
    /// implementation. Unset in headless slices — there is no page there, and
    /// every one of the five refuses by name rather than pretending.
    struct PageDelegate {
        std::function<QVariantMap(const QString &guid, const QString &scope)> open;
        std::function<QVariantMap(const QString &presetOrName, const QString &name)> newMaterial;
        std::function<QVariantList()> tabs;
        std::function<bool(const QVariant &)> activate;
        std::function<bool(const QVariant &)> closeTab;
        std::function<QVariantMap()> activeTab;
    };
    void setPageDelegate(const PageDelegate &delegate) { mPage = delegate; }

    Q_INVOKABLE QVariantList presets();
    /// THE STUDIO EVERY MATERIAL IS SHOWN IN, as data (MATPREVIEW-ENV-1): the
    /// exposure the preview and the thumbnails are graded at, the generated
    /// environment's size, its mean and key radiance, the viewpoint it is
    /// authored for and its softboxes. A READ — the environment is one
    /// constant set, not a preference — and the one place a test or a picture
    /// sheet can quote it from.
    Q_INVOKABLE QVariantMap previewEnvironment();
    /// ONE mint (MATERIAL_BUNDLE_SPEC 6): a library material bundle, with a
    /// node graph as its payload when `{graph: true}`. `materials.createGraph`
    /// was this with the flag always on and a second ModelTypes::Shader row —
    /// one name survives (CRUD).
    Q_INVOKABLE QString create(const QString &name, const QVariantMap &options = QVariantMap());
    /// THE VERB THE TEXTURE PICKER CALLS. A path from anywhere on disk is
    /// imported by CONTENT at that moment; a guid already in the library is
    /// reused. Either way the image becomes a MEMBER of the material (the
    /// edge is derived from the definition) and is pinned into the project
    /// that holds the material. `{slot: "baseColorMap"}` also writes it into
    /// the definition; with no slot the image is imported and pinned without
    /// changing what the material looks like — what a graph texture node wants.
    Q_INVOKABLE QString addTexture(const QString &materialGuid, const QString &pathOrGuid,
                                   const QVariantMap &options = QVariantMap());
    /// The bundle's members: guid, name, slot/node, role, size, used-by
    /// count, pinned, hidden. THE Members panel's data (one projection,
    /// services/materialmembers.h).
    Q_INVOKABLE QVariantList members(const QString &materialGuid);
    /// Members nothing references any more. DRY RUN BY DEFAULT — with no
    /// guid the scope is the open PROJECT (pins), with one it is that
    /// bundle's own born-inside rows (library). Bytes are never removed
    /// here; that is `assets.gc`.
    Q_INVOKABLE QVariantList cleanUnused(const QString &materialGuid = QString(),
                                         const QVariantMap &options = QVariantMap());
    /// A second Texture row over the SAME bytes, swapped into this one
    /// material — "change this picture for this material only".
    Q_INVOKABLE QString makeUnique(const QString &materialGuid, const QString &textureGuid);
    /// ONE ROW, COPIED: a second library bundle on the same definition. The
    /// drawer's Duplicate is this verb's implementation.
    Q_INVOKABLE QString duplicate(const QString &materialGuid,
                                  const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap loadGraph(const QString &guidOrPath);
    Q_INVOKABLE bool regenerate(const QString &shaderGuid);
    Q_INVOKABLE QString createFromImage(const QString &textureGuid,
                                        const QVariantMap &options = QVariantMap());
    /// The first-run seed, on demand: every shipped preset as its read-only
    /// library bundle. Idempotent; answers how many exist afterwards.
    Q_INVOKABLE int seedPresets();
    /// R18 — CUSTOMISE A PRESET. A shipped preset is read-only (the writer
    /// refuses it by name), so the way to change one is to take a copy: this
    /// mints an ordinary, editable library bundle from the preset's own
    /// definition, named `<Preset>-1` with the suffix bumped against the
    /// names already there. THE SUFFIX RULE LIVES IN ONE PLACE
    /// (MaterialPresetAssets::customiseName) and both the editor's tray and
    /// the Materials module's Presets drawer call this verb.
    Q_INVOKABLE QString createFromPreset(const QString &presetOrGuid,
                                         const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap open(const QString &guidOrName,
                                 const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap newMaterial(const QString &presetOrName = QString(),
                                        const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantList tabs();
    Q_INVOKABLE bool activate(const QVariant &tabOrGuid);
    Q_INVOKABLE bool closeTab(const QVariant &tabOrGuid);
    Q_INVOKABLE QVariant activeTab();

private:
    /// The {graph: true} branch of createFromImage (IMAGE_PLANE_SPEC B2).
    QString createImageGraph(const QString &textureGuid);
    /// A guid, a shipped preset's NAME, or a library material's name — to
    /// the guid the page opens. Empty when it names nothing.
    QString resolveMaterialGuid(const QString &guidOrName) const;
    /// The page, or a refusal by name (headless).
    bool pageOrFail(const QString &verb);

    GraphApi *mGraphApi = nullptr;
    PageDelegate mPage;           // the Materials page's tabs, when wired
};

/// material.* — the material on one scene node.
class MaterialApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("material"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE bool apply(const QString &nodeId, const QString &presetOrGuid);
    Q_INVOKABLE bool preview(const QString &nodeId, const QString &presetOrGuid);
    Q_INVOKABLE bool endPreview();
    Q_INVOKABLE bool set(const QString &nodeId, const QVariantMap &values);
    Q_INVOKABLE bool reset(const QString &nodeId);
    Q_INVOKABLE bool setDetail(const QString &nodeId, int layer, const QVariantMap &values);
    Q_INVOKABLE QVariantList detail(const QString &nodeId);
    Q_INVOKABLE QVariantMap get(const QString &nodeId);
    Q_INVOKABLE QVariantMap properties(const QString &nodeId);
    Q_INVOKABLE QString dumpDatablock(const QString &nodeId);

private:
    iris::MeshNodePtr meshNodeOrFail(const QString &nodeId, const QString &verb);
};

/// graph.* — the current effect graph (see MaterialsApi::loadGraph).
class GraphApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("graph"); }
    QVector<VerbInfo> verbs() const override;

    /// Adopts a graph (called by MaterialsApi); takes ownership of the old one.
    void setCurrent(NodeGraph *graph, const QString &assetGuid);
    NodeGraph *current() const { return mGraph; }

    /// §3a: when the Effects page is alive its scene owns selection — the
    /// module wires these to EffectsPage::selectGraphNode & co. Verbs fall
    /// back to API-local selection on the current script graph when the
    /// page has no node with the id (or no delegate is set: headless slices).
    struct SelectionDelegate {
        std::function<bool(const QString &)> select;
        std::function<QString()> selected;
        std::function<void()> deselect;
    };
    void setSelectionDelegate(const SelectionDelegate &delegate) { mSelection = delegate; }

    /// The Effects page's edit stack, when a page exists. graph.undo/graph.redo
    /// are the API-first half of the owner's "on the Materials page the GRAPH
    /// undo wins" decision: the verbs and the shell's Ctrl+Z call the SAME
    /// EffectsPage entry points through this delegate. Headless slices leave it
    /// unset — the API-local script graph has no command stack, so the verbs
    /// fail cleanly rather than pretending.
    struct UndoDelegate {
        std::function<bool()> undo;
        std::function<bool()> redo;
        std::function<int()>  undoCount;
        std::function<int()>  redoCount;
    };
    void setUndoDelegate(const UndoDelegate &delegate) { mUndo = delegate; }

    /// The Effects page's DESTRUCTIVE edits (verb-coverage audit F2). Removing
    /// a node or a connection on the page must go through the page's undo
    /// commands or graph.undo would silently not cover a scripted deletion —
    /// so the verbs offer the id to the page FIRST (exactly the pattern
    /// selectNode uses) and only edit their own script graph when the page
    /// answers "I have no such node". Unset in headless slices.
    struct EditDelegate {
        std::function<bool(const QString &)> removeNode;
        std::function<bool(const QString &)> removeConnection;
    };
    void setEditDelegate(const EditDelegate &delegate) { mEdit = delegate; }

    /// The page's node PALETTE, in window pixels (hygiene lane, 2026-09-09).
    /// Dragging a tile out of the palette is the one graph edit no verb can
    /// make — the mutation verbs work on a script-local NodeGraph, never the
    /// page's — so the rig has to perform the real gesture, and it needs to be
    /// told where to aim instead of guessing window fractions. Unset in
    /// headless slices (there is no palette without a page).
    struct PaletteDelegate {
        std::function<QVariantMap(const QString &)> tile;
    };
    void setPaletteDelegate(const PaletteDelegate &delegate) { mPalette = delegate; }

    Q_INVOKABLE QVariantList nodes();
    Q_INVOKABLE QVariantList connections();
    Q_INVOKABLE QVariantMap nodeInfo(const QString &type);
    Q_INVOKABLE bool removeNode(const QString &nodeId);
    Q_INVOKABLE bool disconnect(const QVariant &connection);
    Q_INVOKABLE QVariantList nodeTypes();
    Q_INVOKABLE QString addNode(const QString &type);
    Q_INVOKABLE bool connect(const QString &fromId, const QVariant &fromSocket,
                             const QString &toId, const QVariant &toSocket);
    Q_INVOKABLE bool setValue(const QString &nodeId, const QVariant &value);
    Q_INVOKABLE QVariant getValue(const QString &nodeId);
    Q_INVOKABLE QVariantMap evaluate();
    Q_INVOKABLE QVariantMap bakeInfo();
    Q_INVOKABLE QVariantMap emitInfo();
    Q_INVOKABLE QVariantMap bake(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE bool toMaterial(const QString &nodeId);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool selectNode(const QString &nodeId);
    Q_INVOKABLE QVariant selectedNode();
    Q_INVOKABLE bool deselect();
    Q_INVOKABLE QVariantMap settings();
    Q_INVOKABLE bool setBlendMode(const QString &mode);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE QVariantMap undoState();
    Q_INVOKABLE QVariant paletteTile(const QString &name);

private:
    NodeGraph *graphOrFail(const QString &verb);
    /// THE GRAPH, AND ONLY IF IT MAY BE EDITED (PRESET-UNIFY-1 fix round).
    /// A graph opened from a SHIPPED PRESET is read-only in fact — the
    /// definition writer refuses its reserved guid — so every mutating verb
    /// asks through this rather than letting an edit land in a graph object
    /// nothing will ever store. The canvas refuses the same gestures
    /// (GraphNodeScene::setReadOnly); this is the same answer for a caller.
    NodeGraph *editableGraphOrFail(const QString &verb);

    NodeGraph *mGraph = nullptr;
    QString mAssetGuid;
    QString mSelectedNodeId;      // API-local selection (headless fallback)
    SelectionDelegate mSelection; // the Effects page, when wired
    UndoDelegate mUndo;           // the Effects page's edit stack, when wired
    EditDelegate mEdit;           // the Effects page's destructive edits
    PaletteDelegate mPalette;     // where the page's node tiles are on screen
};

#endif // SCRIPTING_MATERIALSAPI_H
