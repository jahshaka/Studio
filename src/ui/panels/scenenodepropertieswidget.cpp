/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <QWidget>
#include <QLabel>
#include <QLayout>
#include <QPointer>
#include <QResizeEvent>
#include <QShowEvent>
#include <QScrollArea>
#include <QTimer>

#include "irisgl/document/scenegraph/scenenode.h"


#include "services/services.h"
#include "services/playbackservice.h"

#include "ui/controls/accordionbladewidget.h"
#include "ui/panels/propertyrows.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/panels/transformeditor.h"
#include "ui/style/themeroles.h"

#include "data/database/database.h"
#include "ui/panels/propertywidgets/emitterpropertywidget.h"
#include "ui/panels/propertywidgets/fogpropertywidget.h"
#include "ui/panels/propertywidgets/lightpropertywidget.h"
#include "ui/panels/propertywidgets/decalpropertywidget.h"
#include "ui/panels/propertywidgets/materialpropertywidget.h"
#include "ui/panels/propertywidgets/meshpropertywidget.h"
#include "ui/panels/propertywidgets/mobilitypropertywidget.h"
#include "ui/panels/propertywidgets/shaderpropertywidget.h"
#include "ui/panels/propertywidgets/worldpropertywidget.h"
#include "ui/panels/propertywidgets/physicspropertywidget.h"
#include "ui/panels/propertywidgets/skypropertywidget.h"
#include "ui/panels/propertywidgets/worldgipropertywidget.h"
#include "ui/panels/propertywidgets/worldpostfxpropertywidget.h"
#include "ui/panels/propertywidgets/camerapostfxpropertywidget.h"
#include "ui/panels/propertywidgets/worldaapropertywidget.h"
#include "ui/panels/propertywidgets/worldmodespropertywidget.h"
#include "ui/panels/propertywidgets/worldshadowpropertywidget.h"

SceneNodePropertiesWidget::SceneNodePropertiesWidget(QWidget *parent) : QWidget(parent)
{
    widgetPropertyLayout = new QVBoxLayout(this);
    widgetPropertyLayout->setContentsMargins(0, 0, 0, 0);

    // THE PANEL TRACKS ITS DOCK, IT NEVER PUSHES BACK (owner report
    // 2026-09-08). The dock hosts this widget in a QScrollArea with
    // widgetResizable(true) and NO horizontal scrollbar, so its width is the
    // viewport's — unless its minimumSizeHint is bigger, in which case the
    // scroll area lays it out WIDER than the viewport and everything past the
    // right edge is silently unreachable. Nothing here may carry a minimum
    // width of its own; the blades' rows are fitted on the way in
    // (ui/controls/rowfit.h) and warnIfWiderThanDock() makes a regression
    // audible instead of invisible.
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    fogPropView = new FogPropertyWidget();
    fogPropView->setPanelTitle("Fog");

    worldPropView = new WorldPropertyWidget();
    worldPropView->setPanelTitle("World");
    worldPropView->expand();

	skyPropView = new SkyPropertyWidget();
	skyPropView->setPanelTitle("Sky");
	skyPropView->setDatabase(db);
	skyPropView->expand();

	// World Modes (POST_CHAIN_SPEC §9.6) sits FIRST among the quality sections:
	// it is the tier every one of them resolves through.
	worldModesPropView = new WorldModesPropertyWidget();
	worldModesPropView->setPanelTitle("World Mode");
	worldModesPropView->expand();
	// A tier writes THROUGH to the very fields the sibling World sections
	// display, and those sections read their fields ONLY when they are built.
	// Rebuild every one of them whenever a World Mode edit lands, or they keep
	// showing the pre-switch values until the node is reselected.
	//
	// DEFECT, owner-reported 2026-09-06 and fixed here: this list used to stop
	// at Anti-Aliasing / Shadows / Global Illumination — and the SKY section
	// displayed two World Mode rows of its own, "Sky Detail" and "Ambient From
	// Sky". Both were written through by setMode(), neither was refreshed, so
	// switching Epic -> High changed the document and left the Sky rows showing
	// the OLD tier. (Both rows are gone now — the ambient one with D14, the
	// detail one with the CPU sky bake — but the rule stands: anything that
	// grows a world-mode-backed row belongs in this list.)
	connect(worldModesPropView, &WorldModesPropertyWidget::worldSettingsChanged,
	        this, [this]() {
		auto sc = scene;
		if (!sc && !!sceneNode) sc = sceneNode->getScene();
		if (!sc) return;
		if (worldAaPropView)     worldAaPropView->setScene(sc);
		if (worldShadowPropView) worldShadowPropView->setScene(sc);
		if (worldGiPropView)     worldGiPropView->setScene(sc);
		if (worldPostFxPropView) worldPostFxPropView->setScene(sc);
		if (skyPropView)         skyPropView->setScene(sc);
	});

	worldGiPropView = new WorldGiPropertyWidget();
	// THE OTHER DIRECTION of the same rule (debt L6 item 4, found by the Photon
	// lane): the Photon section writes World Mode registry rows — the technique,
	// the quality, the irradiance field, the tier — and the World Mode section
	// lists every one of them with its pin mark. Without this, a Photon edit left
	// those rows showing the pre-edit values until the world was reselected.
	connect(worldGiPropView, &WorldGiPropertyWidget::worldSettingsChanged, this, [this]() {
		auto sc = scene;
		if (!sc && !!sceneNode) sc = sceneNode->getScene();
		if (!sc) return;
		if (worldModesPropView) worldModesPropView->setScene(sc);
		if (worldPostFxPropView) worldPostFxPropView->setScene(sc);
	});
	// PHOTON is the product name for realtime global illumination
	// (GI_UNIFIED_SPEC.md; the naming rule is that it is ALWAYS subtitled, so
	// nobody reads it as hardware ray tracing). The section is one switch, one
	// quality dial and the update budget, with everything they consume behind
	// its own Advanced disclosure.
	worldGiPropView->setPanelTitle("Photon — Realtime Global Illumination");
	worldGiPropView->expand();

	// POST PROCESS (fix wave 2026-09-07 item 8): every post effect and its
	// parameters in one section, generated from the same registry the World
	// Mode section is. It sits between GI and Anti-Aliasing because that is
	// where it sits in the frame — after the lighting solve, before the AA.
	worldPostFxPropView = new WorldPostFxPropertyWidget();
	worldPostFxPropView->setPanelTitle("Post Process");
	worldPostFxPropView->expand();
	// The two sections show the SAME on/off rows (this one groups the chain,
	// the World Mode one lists the whole tier), so each has to re-read after
	// the other writes.
	connect(worldPostFxPropView, &WorldPostFxPropertyWidget::worldSettingsChanged,
	        this, [this]() {
		auto sc = scene;
		if (!sc && !!sceneNode) sc = sceneNode->getScene();
		if (!sc) return;
		if (worldModesPropView) worldModesPropView->setScene(sc);
	});

	worldAaPropView = new WorldAaPropertyWidget();
	worldAaPropView->setPanelTitle("Anti-Aliasing");
	worldAaPropView->expand();

	worldShadowPropView = new WorldShadowPropertyWidget();
	worldShadowPropView->setPanelTitle("Shadows");
	worldShadowPropView->expand();

    transformPropView = new AccordianBladeWidget();
    transformPropView->setPanelTitle("Transformation");
    transformWidget = transformPropView->addTransformControls();
    transformPropView->expand();
    // THE TRANSFORM EDITOR IS ONE ROW holding a grid of fields (position,
    // rotation, scale, size, reset) — it has no label of its own, so it is
    // named here or it would be reachable only through its section.
    PropertyRows::nameRow(transformWidget, tr("Transform"),
                          { QStringLiteral("position"), QStringLiteral("rotation"),
                            QStringLiteral("scale"), QStringLiteral("size"),
                            QStringLiteral("move"), QStringLiteral("translate"),
                            QStringLiteral("xyz") });

    // MOVEMENT (REALTIME_REFLECTIONS_SPEC §3.3), right under Transformation:
    // "does this move?" is a property of the OBJECT, not of its mesh, so it is
    // its own blade and every node kind gets it.
    mobilityPropView = new MobilityPropertyWidget();
    mobilityPropView->setPanelTitle("Movement");
    mobilityPropView->expand();

    physicsPropView = new PhysicsPropertyWidget();
    physicsPropView->setPanelTitle("Physics Properties");

    meshPropView = new MeshPropertyWidget();
    meshPropView->setPanelTitle("Mesh Properties");
    meshPropView->expand();

    lightPropView = new LightPropertyWidget();
    lightPropView->setPanelTitle("Light");
    lightPropView->setDatabase(db);
    lightPropView->expand();

    decalPropView = new DecalPropertyWidget();
    decalPropView->setPanelTitle("Decal");
    decalPropView->expand();

    emitterPropView = new EmitterPropertyWidget();
    emitterPropView->setPanelTitle("Emitter");
    emitterPropView->setDatabase(db);
    emitterPropView->expand();

    // CAMERA_LENS_SPEC §4/§5: a selected scene camera grades its own shot.
    // This is the camera's first properties section — cameras had none before
    // (the transform editor was all a selected camera showed).
    cameraPostFxPropView = new CameraPostFxPropertyWidget();
    cameraPostFxPropView->setPanelTitle("Exposure & Post");
    cameraPostFxPropView->expand();

    shaderPropView = new ShaderPropertyWidget();
    shaderPropView->setPanelTitle("Shader Definitions");
    shaderPropView->setDatabase(db);
    shaderPropView->expand();

    // SELECTION COST (perf regression, owner session 2026-09-08: 1 fps and
    // 2-7 s UI stalls after an hour). Every blade above is a PERMANENT child of
    // this panel from here on, and selection only ever changes which of them
    // the layout holds — see clearLayout() for why that matters. Adopting them
    // here is the one and only reparent each of them will ever see, and it
    // happens before any of them has painted, i.e. before the style has
    // attached a single focus frame.
    for (QWidget *blade : bladeWidgets()) {
        adoptBlade(blade);
    }

    // THE SELECTION TAB'S EMPTY STATE (§2). A column with nothing in it reads
    // as a panel that failed to load; this one line says what to do instead.
    // Adopted like a blade so it obeys the same mount/hide rules and never
    // reparents.
    emptySelectionLabel = new QLabel(
        tr("Nothing selected — pick an object in the viewport or the Hierarchy."));
    emptySelectionLabel->setWordWrap(true);
    emptySelectionLabel->setContentsMargins(12, 16, 12, 16);
    emptySelectionLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    ThemeRoles::setTone(emptySelectionLabel, ThemeRoles::Tone::Muted);
    // The column has no horizontal scrollbar: a long sentence must be allowed
    // to wrap to nothing rather than set the panel's minimum width.
    emptySelectionLabel->setMinimumWidth(0);
    emptySelectionLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    adoptBlade(emptySelectionLabel);

    // ROWS THAT ARRIVE WITHOUT A MOUNT (§6.1). Five of the world panels rebuild
    // every row they own inside an edit, and the material blade refills on a
    // material pick — under a live filter those rows have to be judged too. The
    // registry coalesces its own signal to one per event-loop turn, so a Photon
    // edit that rebuilds five sections re-filters ONCE.
    connect(&PropertyRows::registry(), &PropertyRows::Registry::rowsChanged,
            this, [this]() {
        if (filterText[int(currentTab)].trimmed().isEmpty()) return;
        // A mount owed to this turn re-filters at its end anyway (applyTab), and
        // it would be filtering the blade set that is about to be replaced.
        if (mountOwed) return;
        applyRowFilter();
    });

    setLayout(widgetPropertyLayout);
}

/// Every blade this panel owns for the lifetime of the window. The material
/// blade is NOT here: it is built on demand (the first mesh selection) and
/// adopted then.
QVector<QWidget *> SceneNodePropertiesWidget::bladeWidgets() const
{
    return {
        fogPropView, worldPropView, skyPropView,
        worldModesPropView, worldGiPropView, worldPostFxPropView,
        worldAaPropView, worldShadowPropView, transformPropView,
        mobilityPropView,
        physicsPropView, meshPropView, lightPropView, decalPropView,
        emitterPropView, cameraPostFxPropView, shaderPropView
    };
}

/// Makes a blade a permanent, hidden child. Hidden EXPLICITLY, so that adding
/// it to the layout later does not show it by accident and — more importantly —
/// so that Qt's layout machinery leaves its visibility to mount()/clearLayout().
void SceneNodePropertiesWidget::adoptBlade(QWidget *blade)
{
    if (!blade) return;
    if (blade->parentWidget() != this) blade->setParent(this);
    blade->hide();
}

/// Puts an already-adopted blade on screen. The ONLY two things a selection
/// change does to a blade are this and its inverse in clearLayout(); neither
/// touches the parent, so neither sends a single QEvent::ParentChange.
void SceneNodePropertiesWidget::mount(QWidget *blade)
{
    if (!blade) return;
    Q_ASSERT(blade->parentWidget() == this);
    widgetPropertyLayout->addWidget(blade);
    blade->show();
    // WHAT THIS TAB IS SHOWING, for the filter: the tab's own blade list, so a
    // filter applies to exactly the rows on screen and the other tab's rows are
    // never touched (the two boxes are independent by construction).
    mountedBlades[int(currentTab)].append(QPointer<QWidget>(blade));
}

// THE SCENE CHANGED — including to NOTHING.
//
// A null scene is a real state (MainWindow::removeScene, the close and the
// load-in-place path) and this used to ignore it: `if (!!scene)` kept the OLD
// scene, so the panel went on showing — and re-binding — a scene the viewport
// had already cleared. Now a close clears the panel, and the world blades are
// bound exactly ONCE per scene (F1/F3, second reader 2026-09-15).
void SceneNodePropertiesWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (this->scene == scene) return;
    this->scene = scene;
    // A CLOSED SCENE HAS NO SELECTION either: the node this panel was showing
    // belongs to the document that is going away.
    if (!scene) this->sceneNode.clear();
    bindScene(scene);
    applyTab();
}

/**
 * sets active scene node and determines which property ui should be shown
 * @param sceneNode
 */
// THE TAB IS THE SELECTOR, NOT THE SELECTION (PROPERTY_FILTER_SPEC §2).
//
// This used to be a MODE SWITCH on isRootNode(): selecting the World row in the
// Hierarchy was the only way to see the world settings, which is why that row
// had to exist and why a scene open selected the root. Now the column has two
// tabs; a selection raises Selection, the root (a verb, a scene open) raises
// World, and the panel's one layout holds whichever blade set the CURRENT tab
// calls for.
void SceneNodePropertiesWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    const bool isRoot = !!sceneNode && sceneNode->isRootNode();
    // The ROOT IS NOT A SELECTION in this column any more: it names the World
    // tab. The verbs still accept it (editor.select(rootId) is legal and
    // scene.root() unchanged) — this is the visual half only.
    this->sceneNode = isRoot ? iris::SceneNodePtr() : sceneNode;
    if (isRoot && !!sceneNode) this->scene = sceneNode->getScene();
    // A NODE and a library asset are exclusive — they are the same "what is
    // picked" slot.
    if (!!this->sceneNode) assetBinding = AssetBinding::None;

    // ONE MOUNT PER PICK (F2, second reader 2026-09-15). This used to call
    // setPropertiesTab() — which mounts when the tab moves — and then mount
    // again unconditionally, so every tab-crossing pick built its blades
    // twice and charged the retired-row ring two generations.
    const Tab wanted = isRoot ? Tab::World
                              : (!!sceneNode ? Tab::Selection : currentTab);
    const bool moved = wanted != currentTab;
    currentTab = wanted;
    applyTab();
    if (moved) emit propertiesTabChanged(currentTab);
}

QString SceneNodePropertiesWidget::tabName(Tab tab)
{
    return tab == Tab::World ? QStringLiteral("world") : QStringLiteral("selection");
}

bool SceneNodePropertiesWidget::tabFromName(const QString &name, Tab &out)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("world"))     { out = Tab::World;     return true; }
    if (n == QLatin1String("selection")) { out = Tab::Selection; return true; }
    return false;
}

void SceneNodePropertiesWidget::setPropertiesTab(Tab tab)
{
    if (tab == currentTab) return;
    currentTab = tab;
    applyTab();
    emit propertiesTabChanged(currentTab);
}

QSharedPointer<iris::Scene> SceneNodePropertiesWidget::worldScene() const
{
    if (!!scene) return scene;
    if (!!sceneNode) return sceneNode->getScene();
    return QSharedPointer<iris::Scene>();
}

// THE COLUMN IS OUT OF DATE — AND THAT IS ALL A SELECTION SAYS (ADD-1, 2026-09-15).
//
// THE MEASUREMENT THIS EXISTS FOR: `scene.addPrimitive` cost 44 ms of its 50 in
// here, because the add's undo command SELECTS the node it just made
// (AddSceneNodeCommand::redo -> SelectionService::select ->
// MainWindow::applySelectionToUi), and this column rebuilt itself for an object
// the user never asked to look at. A script adding 64 spheres rebuilt it 64
// times and only the LAST one was ever seen.
//
// So a selection raises a DEBT and says nothing about when it is paid. It is
// paid at the end of the event-loop turn — which is what makes a click one
// mount and unchanged in feel — UNLESS one of two things says "not yet", and
// both are the same kind of statement as PROPERTY_FILTER_SPEC's two-inputs law:
//
//   NOBODY CAN SEE IT. The properties dock is closed, or the panel is behind
//   another tabified dock. Building a widget tree that is not on screen cost
//   32 ms of a 50 ms add with the dock shut. The debt moves to the showEvent.
//
//   A BATCH IS IN FLIGHT. A script run is one gesture by the user, and the
//   sixty-four selections inside it are not sixty-four things to look at. The
//   script engine runs on its own thread now (SCRIPTING_SPEC §4), so the UI
//   thread's event loop DOES turn between verbs and the per-turn rule alone
//   would mount once per add again — measured: 64 adds became 64 mounts and
//   12.7 ms per add. The debt moves to the end of the run (MainWindow wires
//   ScriptEngine::runningChanged to setMountsHeld).
//
// AND EVERY QUESTION PAYS IT FIRST (flushPendingMount): a verb that lists the
// rows, the filter box, a test that asserts what is mounted. Deferred is never
// "not built" to anyone who asks — including during a script run.
void SceneNodePropertiesWidget::applyTab()
{
    mountOwed = true;
    scheduleMount();
}

/// Arranges for the owed mount to happen at the end of this turn — or does
/// nothing, because something is going to come back for it (showEvent,
/// setMountsHeld, or a question).
void SceneNodePropertiesWidget::scheduleMount()
{
    if (!mountOwed || mountScheduled) return;
    if (!isVisible() || mountsHeld) return;
    mountScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        mountScheduled = false;
        // The reasons can arrive between the selection and the turn's end: the
        // dock can close, a script can start.
        if (!mountOwed || !isVisible() || mountsHeld) return;
        mountOwed = false;
        mountNow();
    });
}

void SceneNodePropertiesWidget::flushPendingMount()
{
    if (!mountOwed) return;
    mountOwed = false;
    mountNow();
}

/// A BATCH — a script run, today — is one gesture, not one per verb.
void SceneNodePropertiesWidget::setMountsHeld(bool held)
{
    if (mountsHeld == held) return;
    mountsHeld = held;
    if (!held) scheduleMount();
}

/// THE DOCK OPENED (or the tabified dock came to the front, or the panel was
/// realised for the first time). Whatever the column owes, it owes now — in
/// this turn, so the dock is never seen holding the previous selection.
void SceneNodePropertiesWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    flushPendingMount();
}

void SceneNodePropertiesWidget::mountNow()
{
    ++mounts;                       // see mountCount()
    widgetPropertyLayout->setContentsMargins(0, 0, 0, 0);
    clearLayout(this->layout());
    mountedBlades[int(currentTab)].clear();

    if (currentTab == Tab::World) {
        const auto sc = worldScene();
        // With no scene there is nothing to show; the tab stays empty rather
        // than mounting blades pointing at nothing (a closed project, headless
        // and pre-open). bindScene is a no-op when the blades already point
        // here — the mount is the cheap half, the bind the expensive one.
        if (!!sc) {
            bindScene(sc);
            mountWorldBlades();
        }
    }
    else {
        mountSelectionBlades();
    }

    widgetPropertyLayout->addStretch();
    // THE FILTER IS RE-APPLIED ON EVERY MOUNT, before this returns: a pick, an
    // undo, a tab switch and a scene open all land with the box's text still in
    // force and no frame in between showing the unfiltered column.
    applyRowFilter();
    // ...and a filter CLEARED while this tab was off screen gets its sections
    // back here, which is the first moment they exist to put back.
    const int t = int(currentTab);
    if (restorePending[t] && filterText[t].isEmpty()) {
        restorePending[t] = false;
        restoreExpandState(currentTab);
    }
    warnIfWiderThanDock();
}

QString SceneNodePropertiesWidget::propertiesFilter(Tab tab) const
{
    return filterText[int(tab)];
}

SceneNodePropertiesWidget::FilterCounts
SceneNodePropertiesWidget::filterCounts(Tab tab) const
{
    // A QUESTION IS A REASON TO MOUNT (see applyTab): these counts describe the
    // rows the column HOLDS, and a mount owed to this turn has not built them
    // yet. const_cast because "answer truthfully" is the const contract here,
    // not "touch nothing".
    const_cast<SceneNodePropertiesWidget *>(this)->flushPendingMount();
    return counts[int(tab)];
}

int SceneNodePropertiesWidget::mountedRowCount(Tab tab) const
{
    int n = 0;
    for (const QPointer<QWidget> &blade : std::as_const(mountedBlades[int(tab)]))
        if (blade) n += PropertyRows::registry().list(blade.data()).size();
    return n;
}

SceneNodePropertiesWidget::Stats SceneNodePropertiesWidget::propertiesStats() const
{
    Stats out;
    out.mounts = mounts;
    out.refills = materialPropView ? materialPropView->refillCount() : 0;
    out.rebuilds = materialPropView ? materialPropView->rebuildCount() : 0;
    out.rows = mountedRowCount(currentTab);
    out.pending = mountOwed;
    out.deferredHidden = mountOwed && !isVisible();
    out.held = mountsHeld;
    out.visible = isVisible();
    return out;
}

QVector<PropertyRows::Registry::Listing>
SceneNodePropertiesWidget::propertyRows(Tab tab) const
{
    const_cast<SceneNodePropertiesWidget *>(this)->flushPendingMount();   // see filterCounts
    QVector<PropertyRows::Registry::Listing> out;
    for (const QPointer<QWidget> &blade : std::as_const(mountedBlades[int(tab)])) {
        if (!blade) continue;
        out += PropertyRows::registry().list(blade.data());
    }
    return out;
}

void SceneNodePropertiesWidget::setPropertiesFilter(Tab tab, const QString &text)
{
    // The expand snapshot below is taken from the MOUNTED blades, so an owed
    // mount has to happen before it, not after (see applyTab).
    flushPendingMount();
    const int t = int(tab);
    const QString trimmed = text.trimmed();
    if (filterText[t] == trimmed) return;
    const bool was = !filterText[t].isEmpty();
    const bool now = !trimmed.isEmpty();
    // THE EXPAND SNAPSHOT (§3.4.5). A section with a match opens so the row can
    // be seen; clearing the box puts every section back the way the user had
    // it, not the way the filter left it.
    //
    // A TAB THAT IS NOT ON SCREEN can be filtered and cleared by a verb or the
    // other box's shortcut, and neither the apply nor the restore can run there
    // — the blades are not mounted. The clear therefore leaves a DEBT, paid at
    // that tab's next applyTab; without it the snapshot survived into the next
    // filter and was overwritten with the state the previous filter had left,
    // losing the user's open sections for good.
    if (!was && now) {
        if (restorePending[t]) restorePending[t] = false;   // the debt IS the snapshot
        else snapshotExpandState(tab);
    }
    filterText[t] = trimmed;
    if (tab == currentTab) {
        applyRowFilter();
        if (was && !now) restoreExpandState(tab);
    }
    else if (was && !now) {
        restorePending[t] = true;
    }
    emit propertiesFilterChanged(tab, filterText[t]);
}

// THE EXPAND SNAPSHOT SURVIVES A REBUILD, because the sections it names do not.
//
// A nested section is a ROW of its blade and dies like one: the material
// blade's "Detail Layers" is destroyed on every mesh pick (clearPanel →
// deleteLater, freed at the next event-loop turn or by the retired ring three
// clears later), and the Photon and sky panels rebuild theirs on every edit. A
// snapshot of raw pointers taken before such a rebuild and restored after it —
// filter on the Selection tab, pick a second mesh, clear the box — reads freed
// memory.
//
// So: guarded pointers, nulls skipped on restore, and only LIVE sections
// recorded. `findChildren` also returns the sections still sitting in the
// blade's retired-row ring (hidden, deleteLater pending, out of the layout),
// and "is this row still part of the panel" is a question the row registry
// already answers — a retired row leaves it at the moment it is retired.
void SceneNodePropertiesWidget::snapshotExpandState(Tab tab)
{
    const int t = int(tab);
    const auto &registry = PropertyRows::registry();
    expandSnapshot[t].clear();
    for (const QPointer<QWidget> &blade : std::as_const(mountedBlades[t])) {
        if (!blade) continue;
        if (auto *b = qobject_cast<AccordianBladeWidget *>(blade.data()))
            expandSnapshot[t].append({ QPointer<QWidget>(b), b->isExpanded() });
        for (AccordianBladeWidget *nested : blade->findChildren<AccordianBladeWidget *>()) {
            if (!registry.isRegistered(nested)) continue;   // retired, or on its way out
            expandSnapshot[t].append({ QPointer<QWidget>(nested), nested->isExpanded() });
        }
    }
}

void SceneNodePropertiesWidget::restoreExpandState(Tab tab)
{
    const int t = int(tab);
    for (const auto &entry : std::as_const(expandSnapshot[t])) {
        // Gone since the snapshot: a rebuild retired the section. There is
        // nothing to put back, and nothing to crash on either.
        auto *blade = qobject_cast<AccordianBladeWidget *>(entry.first.data());
        if (!blade) continue;
        entry.second ? blade->expand() : blade->collapse();
    }
    expandSnapshot[t].clear();
}

// THE FILTER, over the rows of the tab on screen and nothing else.
//
// Rows are HIDDEN AND SHOWN, never created or destroyed: a keystroke costs one
// walk of the registry's entries for the mounted blades (~250 at the widest
// selection) and the layout pass that follows, which is why the box can filter
// as the user types.
void SceneNodePropertiesWidget::applyRowFilter()
{
    const int t = int(currentTab);
    const QStringList terms = PropertyRows::Registry::termsFor(filterText[t]);
    auto &registry = PropertyRows::registry();

    // "ssr" means the row CALLED ssr: if anything in the column matches every
    // term at a word start, mid-word coincidences are dropped.
    bool strongOnly = false;
    if (!terms.isEmpty()) {
        for (const QPointer<QWidget> &blade : std::as_const(mountedBlades[t])) {
            if (blade && registry.hasStrongMatch(blade.data(), terms)) { strongOnly = true; break; }
        }
    }

    FilterCounts total;
    for (const QPointer<QWidget> &ptr : std::as_const(mountedBlades[t])) {
        QWidget *blade = ptr.data();
        if (!blade) continue;
        // NOT EVERYTHING ON THIS LAYOUT IS A SECTION. The Selection tab's
        // "Nothing selected — pick an object…" line is mounted like a blade and
        // holds no rows; it is the panel's own message about the SELECTION, not
        // a section the filter has an opinion about, so the filter leaves it
        // exactly as the panel mounted it. (It used to be hidden by any
        // non-empty Selection filter, which left the tab blank and wordless.)
        auto *b = qobject_cast<AccordianBladeWidget *>(blade);
        if (!b) continue;
        const PropertyRows::Result r = registry.apply(blade, terms, strongOnly);
        total.visible += r.visible;
        total.hidden  += r.hidden;
        if (terms.isEmpty()) {
            blade->show();
            b->setHeaderMuted(false);
            continue;
        }
        // A SECTION WITH A MATCH OPENS, so the row that matched is on screen
        // without a click. A SECTION WITHOUT ONE KEEPS ITS HEADER, greyed and
        // closed (PROPERTY_FILTER_SPEC D7a, the owner's pick): the column stays
        // legible — the user can see WHERE it went thin, and that the Sky
        // settings are still there and simply have nothing called "ssr" in
        // them. Hiding the header instead makes a filtered column read as a
        // panel that failed to load.
        const bool keep = r.anyVisible || r.titleMatch;
        blade->show();
        b->setHeaderMuted(!keep);
        keep ? b->expand() : b->collapse();
    }
    counts[t] = total;
}

// THE EXPENSIVE HALF, run once per scene (see the header). Five of these
// panels rebuild every row they own from the scene inside their setScene.
void SceneNodePropertiesWidget::bindScene(const QSharedPointer<iris::Scene> &scene)
{
    if (worldBoundScene == scene) return;
    worldBoundScene = scene;

    fogPropView->setScene(scene);
    worldPropView->setScene(scene);
    worldModesPropView->setSceneView(sceneView);
    worldModesPropView->setScene(scene);
    worldGiPropView->setSceneView(sceneView);
    worldGiPropView->setScene(scene);
    worldPostFxPropView->setSceneView(sceneView);
    worldPostFxPropView->setScene(scene);
    worldAaPropView->setSceneView(sceneView);
    worldAaPropView->setScene(scene);
    worldShadowPropView->setSceneView(sceneView);
    worldShadowPropView->setScene(scene);
    // The world's sky is bound here too, because the same panel may have been
    // showing a LIBRARY sky asset since the last time the world was shown (one
    // implementation, two bindings).
    skyPropView->setScene(scene);
}

// THE CHEAP HALF: the blades are permanent children and already bound, so a
// mount is a layout move and a show (see clearLayout).
void SceneNodePropertiesWidget::mountWorldBlades()
{
    mount(worldPropView);
    mount(skyPropView);
    mount(worldModesPropView);
    mount(worldGiPropView);
    mount(worldPostFxPropView);
    mount(worldAaPropView);
    mount(worldShadowPropView);
    mount(fogPropView);
}

void SceneNodePropertiesWidget::mountSelectionBlades()
{
    // A LIBRARY ASSET is what the Selection tab shows when one is picked — the
    // same slot as a scene node, and exclusive with it. Re-mounted from STATE
    // (not just at the moment of the pick) so an undo, a tab toggle or any
    // other re-apply does not replace it with the "nothing selected" line.
    if (assetBinding == AssetBinding::Shader) {
        shaderPropView->setShaderGuid(assetGuid);
        mount(shaderPropView);
        return;
    }
    if (assetBinding == AssetBinding::Sky) {
        skyPropView->setSkyAlongWithProperties(assetGuid,
                                               static_cast<iris::SkyType>(assetSkyType));
        mount(skyPropView);
        return;
    }

    // NOTHING SELECTED IS AN ANSWER, and it says so (§2): an empty column
    // reads as a broken panel.
    if (!sceneNode) {
        if (emptySelectionLabel) mount(emptySelectionLabel);
        return;
    }
    {
        const auto sceneNode = this->sceneNode;
        transformWidget->setSceneNode(sceneNode);
        mount(transformPropView);
        // EVERY node kind: the resolution has an answer for all of them,
        // and a light or an emitter is exactly the kind of thing an author
        // needs to pin by hand.
        mobilityPropView->setSceneNode(sceneNode);
        mount(mobilityPropView);

        switch (sceneNode->getSceneNodeType()) {
            case iris::SceneNodeType::Light: {
                lightPropView->setSceneNode(sceneNode);
                mount(lightPropView);
                break;
            }

            case iris::SceneNodeType::Decal: {
                decalPropView->setDatabase(db);
                decalPropView->setProject(project);
                decalPropView->setServices(services);
                decalPropView->setSceneNode(sceneNode);
                mount(decalPropView);
                break;
            }

            case iris::SceneNodeType::Empty: {
                physicsPropView->setSceneNode(sceneNode);
                physicsPropView->setSceneView(sceneView);
                mount(physicsPropView);
                break;
            }

            case iris::SceneNodeType::Mesh: {
                // THE LEAK behind the session-long slowdown: this panel was
                // built fresh for EVERY mesh selection and the old one was
                // handed to clearLayout(), which orphaned it with
                // setParent(nullptr) — a live, parentless widget tree that
                // nothing ever deleted. An hour of clicking around left
                // hundreds of them (and their focus frames, their event
                // filters and their property listeners) alive in the
                // process. Build it ONCE and refill it; the rebuild-per-node
                // that made it look disposable is what clearPanel() does,
                // and materialChanged() has always used exactly that path on
                // the live panel.
                if (!materialPropView) {
                    materialPropView = new MaterialPropertyWidget();
                    materialPropView->setPanelTitle("Material");
                    materialPropView->expand();
                    adoptBlade(materialPropView);
                }
                materialPropView->setDatabase(db);
                materialPropView->setProject(project);
                materialPropView->setServices(services);
                // THE BLADE CLEARS ITSELF, when it has to (ADD-1). This used to
                // clearPanel() here, unconditionally, before every mesh pick —
                // which is why showing another cube's material meant destroying
                // twenty-five rows and a forty-item combo and building them
                // again. setSceneNode decides: the same shape is a refill, a
                // different one is the rebuild this line used to force.

                physicsPropView->setSceneNode(sceneNode);
                physicsPropView->setSceneView(sceneView);
                meshPropView->setSceneView(sceneView);
                meshPropView->setSceneNode(sceneNode);
                materialPropView->setSceneNode(sceneNode);

                if (!(services && services->playback && services->playback->isSimulationRunning())) {
                    mount(physicsPropView);
                }

                mount(meshPropView);
                mount(materialPropView);
                break;
            }

            case iris::SceneNodeType::Camera: {
                cameraPostFxPropView->setSceneView(sceneView);
                cameraPostFxPropView->setSceneNode(sceneNode);
                mount(cameraPostFxPropView);
                break;
            }

            case iris::SceneNodeType::ParticleSystem: {
                emitterPropView->setSceneNode(sceneNode);
                mount(emitterPropView);
                break;
            }

            default: break;
        }
    }
}

/// THE FAILURE IS NEVER SILENT AGAIN. A panel wider than its dock is invisible
/// by construction: the rows are laid out past the right edge of a scroll area
/// that has no horizontal bar, so they are on screen, not hidden, and not
/// reachable (root cause of "the World blades show no controls", 2026-09-08 —
/// one label row asked for 3674 px of a 315 px dock and took every other row
/// with it). Nothing about that shows up in a screenshot, a visibility dump or
/// a widget count, so it says so in the log, naming the blade that did it.
void SceneNodePropertiesWidget::warnIfWiderThanDock()
{
    QWidget *viewport = nullptr;
    for (QWidget *p = parentWidget(); p; p = p->parentWidget()) {
        if (auto *area = qobject_cast<QScrollArea *>(p)) { viewport = area->viewport(); break; }
    }
    const int budget = viewport ? viewport->width() : width();
    if (budget <= 0) return;

    const int need = minimumSizeHint().width();
    if (need <= budget) { lastWidthWarning.clear(); return; }

    QString worst;
    int worstWidth = 0;
    for (QWidget *blade : bladeWidgets()) {
        if (!blade || !blade->isVisibleTo(this)) continue;
        const int w = blade->minimumSizeHint().width();
        if (w > worstWidth) { worstWidth = w; worst = blade->metaObject()->className(); }
    }
    const QString key = QStringLiteral("%1/%2/%3").arg(worst).arg(need).arg(budget);
    if (key == lastWidthWarning) return;   // once per offender, not once per resize
    lastWidthWarning = key;
    qWarning("Properties panel does not fit its dock: needs %d px, has %d px "
             "(widest section: %s, %d px). Rows past the right edge are unreachable.",
             need, budget, qPrintable(worst), worstWidth);
}

void SceneNodePropertiesWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    warnIfWiderThanDock();
}

// An ASSET binding (a shader definition, a library sky) is what the Selection
// tab shows while a library row is picked — the same "what is picked" slot as a
// scene node, so it raises the same tab.
void SceneNodePropertiesWidget::setAssetItem(QListWidgetItem *item)
{
    if (!item) return;
    const int type = item->data(MODEL_TYPE_ROLE).toInt();
    if (type == static_cast<int>(ModelTypes::Shader)) {
        assetBinding = AssetBinding::Shader;
        assetGuid = item->data(MODEL_GUID_ROLE).toString();
    }
    else if (type == static_cast<int>(ModelTypes::Sky)) {
        assetBinding = AssetBinding::Sky;
        assetGuid = item->data(MODEL_GUID_ROLE).toString();
        assetSkyType = item->data(SKY_TYPE_ROLE).toInt();
    }
    else {
        return;                       // not an asset this column edits
    }
    // The asset is what is picked: it owns the Selection tab until a node is.
    this->sceneNode.clear();
    const bool moved = currentTab != Tab::Selection;
    currentTab = Tab::Selection;
    applyTab();
    if (moved) emit propertiesTabChanged(currentTab);
}

void SceneNodePropertiesWidget::refreshMaterial(const QString &matName)
{
    if (!!sceneNode && sceneNode->sceneNodeType == iris::SceneNodeType::Mesh
        && materialPropView) {
        materialPropView->forceShaderRefresh(matName);
    }
}

void SceneNodePropertiesWidget::refreshFromDocument()
{
    // Deferred: an undo can arrive from inside a control's own signal (a
    // shortcut handled while a combo popup is closing), and rebuilding a blade
    // there is the crash the sky panel's queued rebuild exists to avoid.
    QPointer<SceneNodePropertiesWidget> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (!self) return;
        // RE-READ WHATEVER THE CURRENT TAB IS SHOWING — that is this function's
        // whole contract after an undo, so the bind memo is dropped first: the
        // five world panels build their rows from the scene INSIDE setScene,
        // and skipping that here would leave the World tab showing the numbers
        // the undo just took back. It is off the open path (one deferred call
        // per undo/redo), which is why the memo can be strict everywhere else.
        self->invalidateWorldBinding();
        self->applyTab();
    });
}

void SceneNodePropertiesWidget::refreshTransform()
{
	if (transformWidget) {
		transformWidget->refreshUi();
	}
}

void SceneNodePropertiesWidget::setSceneView(IEditorViewport *sceneView)
{
    this->sceneView = sceneView;
    // bindScene pushes this into five of the world blades: a new viewport has
    // to reach them, so the memo that says "already bound" is dropped.
    invalidateWorldBinding();
    if (skyPropView) skyPropView->wireViewportEvents(sceneView);
}

void SceneNodePropertiesWidget::setServices(StudioServices *services)
{
    // A world blade built before the undo stack arrived was built without it.
    invalidateWorldBinding();
    this->services = services;
    if (transformWidget) transformWidget->setServices(services);
    // World Mode edits are undoable (WorldModeCommand) — the section needs the
    // undo stack, and like every other panel here it is built in the CONSTRUCTOR,
    // which runs before this setter.
    if (worldModesPropView) worldModesPropView->setServices(services);
    // EVERY properties row is undoable (debt L6 / N5), so every panel that owns
    // rows needs the stack — not just the two that had it.
    if (worldGiPropView) worldGiPropView->setServices(services);
    if (worldPropView) worldPropView->setServices(services);
    if (fogPropView) fogPropView->setServices(services);
    if (worldAaPropView) worldAaPropView->setServices(services);
    if (worldShadowPropView) worldShadowPropView->setServices(services);
    if (worldPostFxPropView) worldPostFxPropView->setServices(services);
    if (lightPropView) lightPropView->setServices(services);
    if (meshPropView) meshPropView->setServices(services);
    if (mobilityPropView) mobilityPropView->setServices(services);
    if (physicsPropView) physicsPropView->setServices(services);
    if (emitterPropView) emitterPropView->setServices(services);
    if (cameraPostFxPropView) cameraPostFxPropView->setServices(services);
    if (skyPropView) skyPropView->eventBus = services ? services->eventBus : nullptr;
    // Sun coupling (re-audit F5): both sky panels carry the "drive a
    // directional light" row, which needs the selection and the undo stack.
    if (skyPropView) skyPropView->setServices(services);
}

void SceneNodePropertiesWidget::setDatabase(Database *db)
{
    // A world blade built before the library arrived was built without it.
    invalidateWorldBinding();
    this->db = db;
    // FORWARD, DO NOT JUST STORE. Every panel here is built in the CONSTRUCTOR,
    // which runs before this setter — so the ctor's `setDatabase(db)` calls
    // hand them the member while it is still NULL, and a panel that only got
    // it there never has a library at all. That was silent damage, not a
    // theoretical one (code review F-P1): the sky section's equirect pick and
    // its cubemap slots returned early on `!db`, the Sky Presets apply did
    // nothing, a sky ASSET could not be edited, and the emitter's image row and
    // the shader panel dereferenced the null outright.
    //
    // The rule for this panel from here on: a child that needs the library is
    // re-pushed HERE, and the ctor's call stays only as the "born with
    // whatever we have" case.
    if (lightPropView) lightPropView->setDatabase(db);
    // The World blade was MISSING from this list (lane DBPTR-1) and nothing
    // else in the tree called WorldPropertyWidget::setDatabase — so its `db`
    // was uninitialised for the life of the window, refreshRows() tested a
    // wild pointer, and its "Background Ambience" row (which is shown only
    // when the project HAS music assets) could never appear.
    if (worldPropView) worldPropView->setDatabase(db);
    if (skyPropView) skyPropView->setDatabase(db);
    if (emitterPropView) emitterPropView->setDatabase(db);
    if (shaderPropView) shaderPropView->setDatabase(db);
    if (decalPropView) decalPropView->setDatabase(db);
    if (materialPropView) materialPropView->setDatabase(db);
}

void SceneNodePropertiesWidget::setProject(Project *project)
{
    // A world blade built before the project arrived was built without it.
    invalidateWorldBinding();
    // Phase 4: every panel that used to read the Globals::project static now
    // carries the pointer (AccordianBladeWidget::project, which its add*()
    // helpers forward to the controls they build).
    this->project = project;
    if (worldPropView)    worldPropView->setProject(project);
    if (skyPropView)      skyPropView->setProject(project);
    if (emitterPropView)  emitterPropView->setProject(project);
    if (shaderPropView)   shaderPropView->setProject(project);
    if (lightPropView)    lightPropView->setProject(project);
    // materialPropView is created on demand in setSceneNode() and gets the
    // pointer there (the member is not null-initialised).
}

void SceneNodePropertiesWidget::acceptCubemapTexturesFromSkyPresets(QStringList guids)
{
	QStringList fileNames;

	for (auto guid : guids) {
		fileNames.append(db->fetchAsset(guid).name);
	}

	db->removeDependenciesByType(scene->skyGuid, ModelTypes::Texture);

	skyPropView->skyTypeChanged(static_cast<int>(iris::SkyType::CUBEMAP));
	for (int i = 0; i < 6; i++) {
		skyPropView->onSlotChanged(fileNames[i], guids[i], i);
	}
}

/**
 * Takes every blade back off the layout, WITHOUT reparenting any of them.
 *
 * THE PERF REGRESSION (owner session 2026-09-08 — 1 fps, 2-7 s UI stalls after
 * an hour of use; watchdog backtraces all in
 * WidgetWithFocusFrameEventFilter::refreshFocusFrame under this function):
 * this used to call `widget->setParent(nullptr)` on each blade, i.e. it
 * DETACHED a live widget subtree on every single selection change and
 * re-attached it a few lines later in setSceneNode(). Each of those two
 * reparents sends a QEvent::ParentChange to the blade, and Qlementine installs
 * one WidgetWithFocusFrameEventFilter per focusable descendant, every one of
 * which watches its ancestors — so ONE selection change fired the ancestor
 * handler dozens of times, twice, and each firing re-derived and re-parented a
 * QFocusFrame and re-installed its event filters along the whole chain. It also
 * left the frames stranded in a hierarchy their widget had left, which is the
 * "QWidget::mapTo(): parent must be in parent hierarchy" flood (164,651
 * warnings in 13 minutes) the qlementine fork was pulled in to fix.
 *
 * Hiding instead of orphaning is the honest shape: the blades are this panel's
 * permanent children (adopted in the constructor / at first use), a selection
 * change only decides which of them the layout holds, and the widget hierarchy
 * never changes at all. Zero ParentChange events, zero focus-frame work, no
 * growth.
 *
 * @param layout
 */
void SceneNodePropertiesWidget::clearLayout(QLayout *layout)
{
    if (layout == nullptr) return;

    while (auto item = layout->takeAt(0)) {
        if (auto widget = item->widget()) {
            // NOT setParent(nullptr) — see above. Explicitly hidden, so the
            // next mount() has to show it deliberately.
            widget->hide();
        }

        if (auto childLayout = item->layout()) this->clearLayout(childLayout);
        delete item;
    }

    //delete layout;
}
