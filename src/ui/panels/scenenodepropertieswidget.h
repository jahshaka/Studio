/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENENODEPROPERTYWIDGET_H
#define SCENENODEPROPERTYWIDGET_H

#include <QWidget>
#include <QHash>
#include <QListWidgetItem>
#include <QPointer>
#include <QVBoxLayout>
#include <QSharedPointer>
#include <QVector>

#include "ui/panels/propertyrows.h"

namespace iris {
    class SceneNode;
    // Was missing: every signature below that names iris::Scene depended on the
    // INCLUDER having pulled scene.h in first, so this header could not be
    // included on its own.
    class Scene;
}

class AccordianBladeWidget;
class TransformEditor;
class MaterialPropertyWidget;
class WorldPropertyWidget;
class LightPropertyWidget;
class DecalPropertyWidget;
class FogPropertyWidget;
class EmitterPropertyWidget;
class CameraPostFxPropertyWidget;
class NodePropertyWidget;
class MeshPropertyWidget;
class MobilityPropertyWidget;
class ComponentsPropertyWidget;
class PhysicsPropertyWidget;
class IEditorViewport;
struct StudioServices;
class Database;
class Project;

// These are special and a kind of hack since this widget was never really designed to work with non scenenode types
class ShaderPropertyWidget;
class SkyPropertyWidget;
class WorldCloudsPropertyWidget;
class WorldGiPropertyWidget;
class WorldAaPropertyWidget;
class WorldModesPropertyWidget;
class WorldShadowPropertyWidget;

/**
 * This class shows the properties of selected nodes in the scene
 */
class SceneNodePropertiesWidget : public QWidget
{
    Q_OBJECT
public:
    /// THE RIGHT COLUMN HAS TWO TABS (PROPERTY_FILTER_SPEC §2). The panel does
    /// not change shape by what is selected any more: the World settings are
    /// always one tab away, and the selected object's rows are the other tab.
    /// The World row that used to have to exist in the Hierarchy for the world
    /// settings to be reachable is gone with it.
    enum class Tab { World, Selection };
    Q_ENUM(Tab)

    SceneNodePropertiesWidget(QWidget *parent = nullptr);

    /**
     * sets active scene node to show properties for
     * @param sceneNode
     */

    void setScene(QSharedPointer<iris::Scene> scene);
    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);

    /// Which tab is showing. A pick brings Selection to the front; the root
    /// (`editor.select(rootId)`, the scene open) brings World to the front; a
    /// deselect keeps whatever tab the user is on (D3).
    Tab propertiesTab() const { return currentTab; }
    /// Raise a tab. The tab decides WHICH BLADES THE ONE LAYOUT HOLDS — there
    /// is no second page widget and no reparenting, so the selection-cost law
    /// (blades are permanent children, see applyMountDiff) is untouched.
    void setPropertiesTab(Tab tab);
    /// HOW MANY TIMES THE COLUMN HAS BEEN (RE)MOUNTED — every applyTab, i.e.
    /// every mount of a blade set. It is here because "how much
    /// work does one pick cost" is an assertion the cost suites have to be able
    /// to make: a pick that crossed tabs used to mount TWICE (F2, second reader
    /// 2026-09-15) and nothing about that was visible from outside. Read by
    /// ui.properties_tabs; never used to make a decision.
    int mountCount() const { return mounts; }
    /// PAY AN OWED MOUNT NOW (ADD-1). A selection raises a debt that is settled
    /// at the end of the event-loop turn (see applyTab), and every question
    /// about the column settles it first — this is that call, public because a
    /// caller who is about to READ the column's widgets (a test, the row
    /// listing verb) is exactly such a question.
    void flushPendingMount();
    /// True while the column is out of date — a mount is owed (to this turn, to
    /// the moment the dock opens, or to the end of a batch). Reported by
    /// `editor.propertiesStats()` so the coalescing is pinnable.
    bool mountIsPending() const { return mountOwed; }

    /// WHAT THE COLUMN HAS BEEN DOING, for `editor.propertiesStats()`. The
    /// numbers a perf claim about a pick or an add has to be made from, so
    /// nobody has to read them off a stopwatch: how many times the column was
    /// mounted, how many material picks REFILLED the rows already there and how
    /// many had to rebuild them, how many rows the last mount holds, and
    /// whether a mount is owed right now (to this turn, or to the moment the
    /// dock opens).
    struct Stats {
        int mounts = 0;
        int refills = 0;
        int rebuilds = 0;
        int rows = 0;
        bool pending = false;
        bool deferredHidden = false;
        bool visible = false;
        int attached = 0;
    };
    Stats propertiesStats() const;
    /// The rows the named tab has MOUNTED, counted without building anything:
    /// unlike propertyRows() this never settles an owed mount, because a
    /// measurement must not change what it measures.
    int mountedRowCount(Tab tab) const;

    /// THE FILTER BOX BELONGS TO ITS TAB (PROPERTY_FILTER_SPEC, owner decision
    /// 2026-09-15): one box per tab, filtering that tab's rows only. World's
    /// text filters the world rows, Selection's the selected object's, each
    /// keeps its own text for the session (never persisted), and neither ever
    /// looks at the other's rows.
    QString propertiesFilter(Tab tab) const;
    /// Sets a tab's filter text. Applied immediately when it is the tab on
    /// screen; stored (and applied on the next mount) when it is not.
    void setPropertiesFilter(Tab tab, const QString &text);

    /// What the last apply of that tab's filter left on screen. `visible` +
    /// `hidden` count the ROWS the filter judged — rows the panel itself hides
    /// (a spot row on a point light) are in neither.
    struct FilterCounts { int visible = 0; int hidden = 0; };
    FilterCounts filterCounts(Tab tab) const;

    /// EVERY ROW THAT TAB HAS MOUNTED, in column order — what `editor.properties`
    /// answers with (PROPERTY_FILTER_SPEC §3.5). It is the column's own account
    /// of itself: section chain, name, key, keywords and both halves of the
    /// visibility law, so a script (or Claude) can ask what is on the Properties
    /// panel without a screenshot.
    QVector<PropertyRows::Registry::Listing> propertyRows(Tab tab) const;

    /// The name the verbs and the tab bar use ("world" / "selection").
    static QString tabName(Tab tab);
    static bool tabFromName(const QString &name, Tab &out);

signals:
    /// The tab actually changed (the strip follows this; it never polls).
    void propertiesTabChanged(SceneNodePropertiesWidget::Tab tab);
    /// A tab's filter text changed (the strip follows this the same way).
    void propertiesFilterChanged(SceneNodePropertiesWidget::Tab tab, const QString &text);

public:
    void setAssetItem(QListWidgetItem *item);
	void setSceneView(IEditorViewport *sceneView);
	void setServices(StudioServices *services);

    /**
     * Updates material properties if active scene node is a mesh
     */
    void refreshMaterial();

	void refreshTransform();

    /// Re-reads the WHOLE selection's rows from the document.
    ///
    /// Every properties row is undoable now (debt L6), and an undo that changed
    /// the document while the panel kept showing the old numbers would be worse
    /// than no undo at all. MainWindow calls this after an undo or a redo; it is
    /// DEFERRED by one event-loop turn, because a panel that rebuilds its rows
    /// inside the signal that reached it destroys the control still on the
    /// stack (the Qt 6.10 + qlementine combo hazard CLAUDE.md records, and the
    /// reason the sky panel defers its own rebuild).
    void refreshFromDocument();

    void setDatabase(Database*);

    /// Forwards the one live Project to every property panel that reads it
    /// (Phase 4: was the Globals::project static). Panels created lazily
    /// (materialPropView) get it at construction.
    void setProject(Project*);

	WorldGiPropertyWidget *worldGiPropView;
	/// The World panel's VR section (lane VR-WORLD-1) — the project's own VR
	/// settings, generated from services/vrworld.h.
	class WorldVrPropertyWidget *worldVrPropView = nullptr;
	class WorldPostFxPropertyWidget *worldPostFxPropView = nullptr;
	WorldAaPropertyWidget *worldAaPropView;
	WorldModesPropertyWidget *worldModesPropView;
	WorldShadowPropertyWidget *worldShadowPropView;

public slots:
	void acceptCubemapTexturesFromSkyPresets(QStringList guids);

protected:
    void resizeEvent(QResizeEvent *event) override;
    /// Pays an owed mount when the dock opens (see applyTab) — unless the dock
    /// opened behind another tab, where nobody can see it yet (onScreen()).
    void showEvent(QShowEvent *event) override;
    /// Watches the ancestor dock for the tab raise Qt sends this panel no event
    /// for (see watchDock()).
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// Logs (once per offender) when the panel's minimum width does not fit the
    /// dock it is scrolled in — the shape of failure that made the World
    /// sections look empty in 2026-09-08: rows laid out past the right edge of
    /// a scroll area with no horizontal bar, present and unreachable.
    void warnIfWiderThanDock();
    QString lastWidthWarning;

	StudioServices *services = nullptr;
	Project *project = nullptr;

    /// The blades this panel owns permanently (everything built in the
    /// constructor). SELECTION COST, 2026-09-08: a selection change moves
    /// blades in and out of the LAYOUT and never in and out of the widget
    /// HIERARCHY — see applyMountDiff()'s comment for the regression that shape
    /// fixes.
    QVector<QWidget *> bladeWidgets() const;
    /// Makes a blade a permanent hidden child of this panel. Called once per
    /// blade, ever.
    void adoptBlade(QWidget *blade);
    /// NAMES a blade as part of the set this mount wants, in order. It does
    /// NOT touch the layout: mountNow() collects the whole wanted list and
    /// then applies the DIFFERENCE against what is already mounted
    /// (applyMountDiff), because taking every blade off the layout and showing
    /// it again was 4.2 of the 4.9 ms a pick cost (SELECT-COST-1).
    void mount(QWidget *blade);

private:
    /// Mounts the blade set the current tab calls for. Every path that used to
    /// end in "mount the world blades" or "mount the node blades" ends here.
    /// MOUNTING IS NOT BINDING: this only moves blades on and off the layout.
    void applyTab();
    /// Posts the end-of-turn settlement, when nothing says "not yet".
    void scheduleMount();
    /// CAN ANYBODY SEE THIS COLUMN — `isVisible()` AND, when the panel lives in
    /// a dock, that dock being the tab in FRONT of its group (a tab behind
    /// another is shown and parked off-screen). See the definition.
    bool onScreen() const;
    /// The QDockWidget this panel is hosted in, or null (a test rig, a preview).
    const class QDockWidget *ancestorDock() const;
    /// Points the event filter at the dock the panel is currently under.
    void watchDock();
    /// The dock watchDock() last installed the filter on.
    QPointer<class QDockWidget> watchedDock;
    /// The mount itself — the tab's blade set, the layout diff, the filter. Never
    /// called directly by a selection: applyTab() coalesces the calls and this
    /// runs once per turn.
    void mountNow();
    /// Points the eight world blades at `scene` — which REBUILDS the rows of
    /// the five that build from the scene (World Mode, Photon, Post Process,
    /// Anti-Aliasing, Shadows). Idempotent by `worldBoundScene`, so it costs
    /// that rebuild ONCE per scene, not once per mount: a scene open used to
    /// run it three times (once against the scene being closed) because every
    /// mount re-bound (F1, second reader 2026-09-15). A bound blade is kept
    /// live afterwards by the worldSettingsChanged fan-out and
    /// refreshFromDocument, never by re-binding.
    void bindScene(const QSharedPointer<iris::Scene> &scene);
    /// Drops the memo so the next mount re-binds — for the setters that feed
    /// the world blades something other than the scene (the viewport, the
    /// library, the project, the undo stack).
    void invalidateWorldBinding() { worldBoundScene.clear(); }
    void mountWorldBlades();
    void mountSelectionBlades();
    /// Makes the layout hold exactly `wantedBlades`, in order, moving only
    /// what differs from what it holds now (see mount()).
    void applyMountDiff();
    /// The scene the World tab binds to, whatever is selected.
    QSharedPointer<iris::Scene> worldScene() const;

    /// Re-runs the CURRENT tab's filter over the blades that tab has mounted.
    /// Called at the end of every applyTab (so a pick, an undo and a tab switch
    /// all re-filter without a flash) and, coalesced, whenever the registry
    /// reports new rows (the Photon and sky panels rebuild theirs on an edit).
    void applyRowFilter();
    /// The expand state of every section of `tab`'s blades, taken when its
    /// filter goes non-empty and put back when it is cleared (§3.4.5).
    void snapshotExpandState(Tab tab);
    void restoreExpandState(Tab tab);

    /// Per TAB, never shared: the filter text, what its last apply counted, the
    /// expand snapshot, and the blades that tab has on the layout.
    QString filterText[2];
    FilterCounts counts[2];
    /// THE SNAPSHOT HOLDS GUARDED POINTERS, NOT RAW ONES. A nested section is
    /// destroyed and rebuilt under the panel all the time — the material
    /// blade's "Detail Layers" goes with every mesh pick (clearPanel →
    /// deleteLater) — so a snapshot taken before a pick and restored after one
    /// would qobject_cast freed memory. QPointer<QWidget> (rather than of the
    /// blade type) keeps this header free of the accordion's; the restore
    /// casts what is still alive.
    QVector<QPair<QPointer<QWidget>, bool>> expandSnapshot[2];
    /// A TAB WHOSE FILTER WAS CLEARED WHILE IT WAS NOT ON SCREEN still owes its
    /// sections their expand state back — the restore can only run where the
    /// blades are mounted. Paid at that tab's next applyTab.
    bool restorePending[2] = { false, false };
    QVector<QPointer<QWidget>> mountedBlades[2];
    /// The set the mount in progress has asked for, in order (mount() appends;
    /// applyMountDiff consumes). A member rather than a local because every
    /// blade branch calls mount() through several frames of call stack.
    QVector<QWidget *> wantedBlades;
    /// How many blades the last mount actually attached to the layout and
    /// showed — zero when the set did not change, which is the whole point.
    /// Reported by `editor.propertiesStats().attached`.
    int lastAttached = 0;

    /// See mountCount().
    int mounts = 0;
    /// THE DEBT: the column does not match the selection it claims to show.
    /// Cleared only by a mount (see applyTab for when one happens).
    bool mountOwed = false;
    /// A zero-timer is already posted to settle it at the end of this turn.
    bool mountScheduled = false;
    /// The scene the world blades are currently pointed at (see bindScene).
    QSharedPointer<iris::Scene> worldBoundScene;

    /// WHAT THE SELECTION TAB IS SHOWING when it is not a scene node: a
    /// library asset picked in a drawer (setAssetItem). It has to be STATE and
    /// not just a mount, because the tab is re-applied for reasons that have
    /// nothing to do with the pick — an undo (refreshFromDocument), a tab
    /// toggle and back — and those used to replace the asset's rows with the
    /// "nothing selected" line (F6, second reader 2026-09-15).
    enum class AssetBinding { None, Shader, Sky };
    AssetBinding assetBinding = AssetBinding::None;
    QString assetGuid;
    int assetSkyType = 0;

    Tab currentTab = Tab::World;
    /// The "nothing selected" line the Selection tab shows (a permanent child,
    /// adopted like a blade).
    class QLabel *emptySelectionLabel = nullptr;
    QSharedPointer<iris::SceneNode> sceneNode;

public:
    WorldPropertyWidget *getWorldPropertyWidget() const { return worldPropView; }

private:
    AccordianBladeWidget* transformPropView;
    TransformEditor* transformWidget;

    /// Built on the first mesh selection and REUSED (it used to be rebuilt per
    /// selection and orphaned, which leaked a whole widget tree every time).
    MaterialPropertyWidget* materialPropView = nullptr;
    EmitterPropertyWidget* emitterPropView;
    /// The camera panel's "Exposure & Post" section (CAMERA_LENS_SPEC §4/§5).
    CameraPostFxPropertyWidget* cameraPostFxPropView;
    // NodePropertyWidget* nodePropView;
    LightPropertyWidget* lightPropView;
    DecalPropertyWidget* decalPropView;
    WorldPropertyWidget* worldPropView;
    FogPropertyWidget*  fogPropView;
	/// ONE sky panel (debt L6 / N3: the twins are one implementation now). It
	/// serves both bindings — the world's sky while the world is selected, a
	/// library sky asset while one is — because a selection is exclusive.
	SkyPropertyWidget *skyPropView;
	/// The Clouds blade, under the Sky blade (CLOUDS-2D-1).
	WorldCloudsPropertyWidget *cloudsPropView = nullptr;
	MeshPropertyWidget* meshPropView;
    /// MOVEMENT (REALTIME_REFLECTIONS_SPEC §3.3): the per-object mobility row,
    /// mounted for EVERY node kind — a lamp on a swinging arm needs it as much
    /// as a prop does.
    MobilityPropertyWidget* mobilityPropView;
    /// THE PARTS OF A GROUPED NODE (owner review R14). Mounted only when the
    /// selection's group has children; built once, like every other blade.
    ComponentsPropertyWidget* componentsPropView = nullptr;
    PhysicsPropertyWidget *physicsPropView;

    QSharedPointer<iris::Scene> scene;

    Database *db = nullptr;   // was uninitialized: the ctor forwards it to panels before setDatabase()
	ShaderPropertyWidget *shaderPropView;
    IEditorViewport *sceneView = nullptr;   // was uninitialized: read before setSceneView() on some paths

    QVBoxLayout *widgetPropertyLayout;
};

#endif // PROPERTYWIDGET_H
