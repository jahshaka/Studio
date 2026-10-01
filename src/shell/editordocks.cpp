/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/editordocks.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QDockWidget>
#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include "app/firstrun.h"
#include "irisgl/core/logger.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/assetshare.h"
#include "services/sceneeditservice.h"
#include "services/thumbnailservice.h"
#include "ui/dialogs/bundleexportdialog.h"
#include "data/constants.h"
#include "data/settingsmanager.h"
#include "services/materialpresetseeder.h"
#include "services/clipboardservice.h"
#include "services/selectioncost.h"
#include "services/undoservice.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "shell/dockstate.h"
#include "shell/mainwindow.h"
#include "ui/controls/propertiestabstrip.h"
#include "ui/panels/assetwidget.h"
#include "ui/panels/presets/assetmaterialpanel.h"
#include "ui/panels/presets/assetmodelpanel.h"
#include "ui/panels/presets/skypresets.h"
#include "ui/panels/propertywidgets/worldpropertywidget.h"
#include "ui/panels/scenehierarchywidget.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/panels/scriptconsole.h"
#include "ui/panels/timeline/animationwidget.h"
#include "ui/style/panelmetrics.h"
#include "ui/style/stylesheet.h"
#include "viewport/ieditorviewport.h"

/// Where the EDITOR docks' layout lives. Deliberately not "windowState": that
/// key is the OUTER window's, written by QMainWindow::saveState, and the two
/// blobs describe two different QMainWindows.
static const char *kViewportDockStateKey = "viewportDockState";

namespace {

// A DOCK BODY THAT *ASKS* FOR A WIDTH (smoke S1, F-X1).
//
// A dock area lays its docks out from their sizeHint and refuses to go below
// their minimumSizeHint. The right column used to get its 396 px from a
// MINIMUM — `presetsTabWidget->setMinimumWidth(396)` — which is why the column
// could never be dragged to the 300 px `rightColumnMinWidth` advertises: the
// default width was being expressed as a constraint. (And resizeDocks cannot
// fix it from outside: the right column is a vertically split PAIR, a nested
// dock layout, and Qt applies a horizontal resizeDocks to nested items only
// approximately — measured on the rig 2026-09-11, the request simply does not
// land, while the flat left column's does.)
//
// This is the same statement made the way Qt reads it: sizeHint = the column's
// default width, minimum untouched. The column OPENS at PanelMetrics::
// rightColumnWidth and drags down to rightColumnMinWidth, which is exactly what
// the two constants say.
class ColumnBody : public QWidget
{
public:
    explicit ColumnBody(int hintWidth, QWidget *parent = nullptr)
        : QWidget(parent), mHintWidth(hintWidth) {}

    QSize sizeHint() const override
    {
        const QSize base = QWidget::sizeHint();
        return QSize(qMax(base.width(), mHintWidth), base.height());
    }

private:
    int mHintWidth;
};

}   // namespace

EditorDocks::EditorDocks(QObject *parent) : QObject(parent)
{
	// WHICH EDITOR PANELS ARE OPEN. These are the defaults — all five panels
	// open, the script console closed — and a restored dock layout overwrites
	// them in build(). (The commented-out `widgets` QSettings key that used to
	// sit in the shell was dead for years: nothing ever wrote it. The dock
	// layout itself carries the answer now. Lane SPACE-1, CRUD.)
	widgetStates = QVector<bool>(6);
	widgetStates[static_cast<int>(Widget::HIERARCHY)]	= true;
	widgetStates[static_cast<int>(Widget::PROPERTIES)]	= true;
	widgetStates[static_cast<int>(Widget::ASSETS)]		= true;
	widgetStates[static_cast<int>(Widget::TIMELINE)]	= true;
	widgetStates[static_cast<int>(Widget::PRESETS)]		= true;
	// …and the console CLOSED: Ctrl+` is what opens it (lane SPACE-2).
	widgetStates[static_cast<int>(Widget::CONSOLE)]		= false;
}

void EditorDocks::setConsole(ScriptConsole *console)
{
    mConsole = console;
    if (scriptConsoleDock) scriptConsoleDock->setWidget(console);
}

void EditorDocks::setToolbar(QToolBar *bar)
{
    mToolbar = bar;
}

// THE EDITOR'S LAYOUT, NOT THE PAGE'S (lane SPACE-1, 2026-09-14), into the
// settings. Every space but the editor hides the editor's docks, and immersive
// fullscreen hides them inside it — so saving the live state was saving "no
// panels" for anyone who quit from the Player, from the Materials page or from
// F11, and that is what the next launch restored. captureLayout() takes the
// live layout when the editor is the page on screen and does nothing when it
// is not; the snapshot taken on the way out of the editor then stands. With
// neither — a session that never opened a scene — the stored layout is left
// exactly as it was, because this session has nothing better to say about it.
void EditorDocks::storeLayout()
{
    captureLayout();
    if (settings)
        DockState::store(settings->settings, QString::fromLatin1(kViewportDockStateKey),
                         editorDockState);
}

// Ctrl+Shift+P: the right column's World / Selection toggle (PROPERTY_FILTER_SPEC D2).
void EditorDocks::togglePropertiesTab()
{
    if (!sceneNodePropertiesWidget) return;
    sceneNodePropertiesWidget->setPropertiesTab(
        sceneNodePropertiesWidget->propertiesTab() == SceneNodePropertiesWidget::Tab::World
            ? SceneNodePropertiesWidget::Tab::Selection
            : SceneNodePropertiesWidget::Tab::World);
}

// Ctrl+F: the filter box of the tab ON SCREEN (PROPERTY_FILTER_SPEC D1).
void EditorDocks::focusPropertiesFilter()
{
    if (!propertiesTabStrip) return;
    // A dock tabbed BEHIND another is visible (shown, parked off-screen — the
    // SPACE-2 fact), so the test is "in front", not "visible": otherwise the
    // shortcut focused a filter box the user could not see (PROPS-SMALL-1).
    if (sceneNodePropertiesDock && !isFrontTab(sceneNodePropertiesDock))
        setPanelOpen(QStringLiteral("properties"), true);
    propertiesTabStrip->focusFilter();
}

void EditorDocks::build(const Deps &deps)
{
    mShell = deps.shell;
    mWindow = deps.window;
    viewPort = deps.viewPort;
    sceneView = deps.viewport;
    db = deps.db;
    services = deps.services;
    project = deps.project;
    settings = deps.settings;
    mEditorOnScreen = deps.editorOnScreen;
    mEditorActive = deps.editorActive;
    QAction *gridCheckAction = deps.gridAction;
    QAction *groundPlaneCheckAction = deps.groundPlaneAction;

    // Hierarchy Dock
    sceneHierarchyDock = new QDockWidget("Hierarchy", viewPort);
    // The NAME restoreState matches this dock on. (It used to be overwritten
    // one line below with the WIDGET's name — a leftover that made every saved
    // layout call the left column `sceneHierarchyWidget`; DockState::kVersion
    // 3 retires those blobs. Lane SPACE-1.)
    sceneHierarchyDock->setObjectName(QStringLiteral("sceneHierarchyDock"));
    sceneHierarchyWidget = new SceneHierarchyWidget;
    sceneHierarchyDock->setWidget(sceneHierarchyWidget);
    // THE LEFT COLUMN IS ONE COLUMN, on every page (ui/style/panelmetrics.h).
    // The editor's left column is the one the other pages copy, so it is sized
    // from the constant rather than from whatever the tree's sizeHint asks for.
    sceneHierarchyWidget->setMinimumWidth(PanelMetrics::leftColumnMinWidth);
    sceneHierarchyWidget->setMainWindow(mShell);
    if (sceneView) sceneView->setHierarchyDragSource(sceneHierarchyWidget->getWidget());

    connect(sceneHierarchyWidget,   SIGNAL(sceneNodeSelected(iris::SceneNodePtr)),
            mShell,                 SLOT(sceneNodeSelected(iris::SceneNodePtr)));
    // The outliner's SET (EDITOR_MULTISELECT_SPEC §2.2): straight into the
    // service, primary first, the same way the single-node signal goes.
    connect(sceneHierarchyWidget, &SceneHierarchyWidget::sceneNodeSetSelected,
            this, [this](const QList<iris::SceneNodePtr> &nodes) {
        if (services && services->selection) services->selection->select(nodes);
    });

    // Scene Node Properties Dock
    // Since this widget can be longer than there is screen space, we need to add a QScrollArea
    // For this to also work, we need a "holder widget" that will have a layout and the scroll area
    sceneNodePropertiesDock = new QDockWidget("Properties", viewPort);
    sceneNodePropertiesDock->setObjectName(QStringLiteral("sceneNodePropertiesDock"));
    sceneNodePropertiesWidget = new SceneNodePropertiesWidget;
    sceneNodePropertiesWidget->setSceneView(sceneView);
    // World blade's "Show Grid" row is a second face of the View Options
    // Ground Grid action (the editor page's, built before the docks)
    sceneNodePropertiesWidget->getWorldPropertyWidget()->setGridAction(gridCheckAction);
    // ...and its "Ground Plane" row, of the Ground Plane action beside it.
    sceneNodePropertiesWidget->getWorldPropertyWidget()->setGroundPlaneAction(groundPlaneCheckAction);
    sceneNodePropertiesWidget->setDatabase(db);
    sceneNodePropertiesWidget->setServices(services);
    sceneNodePropertiesWidget->setProject(project);
    sceneNodePropertiesWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sceneNodePropertiesWidget->setObjectName(QStringLiteral("SceneNodePropertiesWidget"));
    sceneNodePropertiesDock->setStyleSheet(StyleSheet::MainWindowPropertiesDock());

    QWidget *sceneNodeDockWidgetContents = new ColumnBody(PanelMetrics::rightColumnWidth, viewPort);
    QScrollArea *sceneNodeScrollArea = new QScrollArea(sceneNodeDockWidgetContents);
    // THE RIGHT COLUMN IS ONE COLUMN (ui/style/panelmetrics.h): this dock and
    // the Presets panel below it are sized from the same number. The minimum is
    // a contract — there is no horizontal scrollbar below, so a panel that does
    // not fit here is CLIPPED (ui.properties_width).
    sceneNodeScrollArea->setMinimumWidth(PanelMetrics::rightColumnMinWidth);
    sceneNodeScrollArea->setStyleSheet(StyleSheet::BorderNone());
    sceneNodeScrollArea->setFrameShape(QFrame::NoFrame);
    sceneNodeScrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    sceneNodeScrollArea->setWidget(sceneNodePropertiesWidget);
    sceneNodeScrollArea->setWidgetResizable(true);
    sceneNodeScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QVBoxLayout *sceneNodeLayout = new QVBoxLayout(sceneNodeDockWidgetContents);
    sceneNodeLayout->setContentsMargins(0, 0, 0, 0);
    // THE TAB BAR SITS ABOVE THE SCROLL AREA (PROPERTY_FILTER_SPEC §2/§6.5):
    // World | Selection, pinned, so the rows scroll under it and the panel's
    // own minimum width — the column-width law — keeps measuring exactly the
    // rows it measured before.
    propertiesTabStrip = new PropertiesTabStrip(sceneNodePropertiesWidget,
                                                sceneNodeDockWidgetContents);
    sceneNodeLayout->addWidget(propertiesTabStrip);
    sceneNodeLayout->addWidget(sceneNodeScrollArea);
    sceneNodeDockWidgetContents->setLayout(sceneNodeLayout);
    sceneNodePropertiesDock->setWidget(sceneNodeDockWidgetContents);
    // THE COLUMN FILLS THE MOMENT THIS DOCK COMES FORWARD (TABS-HIDDEN-1). A
    // selection only raises a mount DEBT while nobody can see the column
    // (SceneNodePropertiesWidget::onScreen) — and "nobody can see it" includes
    // this dock sitting behind another tab of its group, which Qt SHOWS and
    // parks off-screen. Qt emits visibilityChanged(true) both when the dock is
    // opened and when its tab is raised (QMainWindowLayout::tabChanged), so
    // this is the one wire that settles the debt in the SAME turn as the click
    // — posted, not paid inline: the signal fires from the dock's own Show
    // event, and a mount frees retired rows (CREATE-CRASH-1; see
    // SceneNodePropertiesWidget::showEvent).
    connect(sceneNodePropertiesDock, &QDockWidget::visibilityChanged,
            this, [this](bool shown) {
                if (shown && sceneNodePropertiesWidget)
                    sceneNodePropertiesWidget->flushPendingMountAfterShow();
            });

    // Presets Dock
    presetsDock = new QDockWidget("Presets", viewPort);
    presetsDock->setObjectName(QStringLiteral("presetsDock"));

    QWidget *presetDockContents = new ColumnBody(PanelMetrics::presetsPanelWidth);
    presetDockContents->setStyleSheet(StyleSheet::MainWindowPresetsDock());
    SkyPresets *skyPresets = new SkyPresets;
    skyPresets->setMainWindow(mShell);
	skyPresets->setDatabase(db);
	skyPresets->setProject(project);

	connect(skyPresets, &SkyPresets::changeSceneCubemap,
			sceneNodePropertiesWidget, &SceneNodePropertiesWidget::acceptCubemapTexturesFromSkyPresets);

    assetModelPanel = new AssetModelPanel;
    assetModelPanel->setMainWindow(mShell);
    assetModelPanel->setDatabaseHandle(db);

    assetMaterialPanel = new AssetMaterialPanel;
    assetMaterialPanel->setMainWindow(mShell);
    assetMaterialPanel->setServices(services);
    assetMaterialPanel->setDatabaseHandle(db);

    // THE SHIPPED PRESETS, SEEDED AT FIRST RUN (MATERIAL_BUNDLE_SPEC §8 phase
    // 3). Their maps' bytes go into the store on a WORKER — a preset's maps
    // are copied and fsynced when the store is on a different filesystem from
    // the app tree, which is the owner's box, and an fsync belongs nowhere
    // near the thread that draws (FSYNC-2) — and the rows follow one preset
    // per event-loop turn, with no device wait left in them. A library that
    // already has all twenty starts no thread at all. Nothing waits for it:
    // an apply that beats the seeder seeds its own preset, as it always did.
    //
    // ONLY FOR A PERSON (app/firstrun.h, the one "is a machine driving this?"
    // predicate). A first run is a first run BY SOMEBODY; a driven session — a
    // suite, a script, an MCP client, the rig — gets a library that changes
    // only when its own verbs change it, because a background seed landing
    // between two `assets.list` calls is a row count that moves under the
    // caller's feet (it broke scripting.e2e.full_surface exactly that way).
    // Those sessions have the same seed on demand: `materials.seedPresets()`,
    // and any apply seeds the preset it needs. JAHSHAKA_SEED_PRESETS=1 forces
    // it on for measuring the shipped path.
    if (!FirstRun::isDrivenSession() || qEnvironmentVariableIsSet("JAHSHAKA_SEED_PRESETS"))
        MaterialPresetSeeder::instance().start(db);

    presetsTabWidget = new QTabWidget;
    presetsTabWidget->setObjectName("PresetsTabWidget");
    // F-X1 (platform audit, 2026-09-10): this used to be `presetsPanelWidth`
    // (396) — a MINIMUM 96 px wider than the column's own advertised minimum,
    // so the right column could never actually be dragged to
    // `rightColumnMinWidth` and the two constants contradicted each other.
    // The panel OPENS at the column's default width (applyColumnWidthsOnce
    // below); what it may be squeezed to is the column's minimum, one number
    // for the whole column.
    presetsTabWidget->setMinimumWidth(PanelMetrics::rightColumnMinWidth);
    presetsTabWidget->addTab(assetModelPanel, "Models");
    presetsTabWidget->addTab(assetMaterialPanel, "Materials");
    presetsTabWidget->addTab(skyPresets, "Skyboxes");
    presetDockContents->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    QGridLayout *presetsLayout = new QGridLayout(presetDockContents);
    presetsLayout->setContentsMargins(0, 0, 0, 0);
    presetsLayout->addWidget(presetsTabWidget);
    presetsDock->setWidget(presetDockContents);

    // Asset Dock — titled "Assets" again (lane SPACE-2). It was renamed "Tray"
    // at smoke L10 item 6 because the dock tab bar read "Timeline | Asset
    // Browser" under a tray whose OWN tabs already said "Assets | Console" —
    // one concept named twice. That second tab bar is gone: the dock's title is
    // now the only name the bottom area shows for the asset browser, and it is
    // what the owner calls it.
    assetDock = new QDockWidget(tr("Assets"), viewPort);
    assetDock->setObjectName(QStringLiteral("assetDock"));
    assetWidget = new AssetWidget(db, viewPort);
    assetWidget->setMainWindow(mShell);
    assetWidget->setEventBus(services->eventBus);
    assetWidget->setProject(project);
    assetWidget->setAcceptDrops(true);

	connect(assetWidget, SIGNAL(assetItemSelected(QListWidgetItem*)), mShell, SLOT(assetItemSelected(QListWidgetItem*)));
    assetWidget->setServices(services);
    // The drawer's avatar rows (AVATAR_ASSET_SPEC §5.5) come back here: the
    // panel decides WHAT it wants, the shell knows WHERE the modules are.
    connect(assetWidget, &AssetWidget::editAssetInModule, mShell, &MainWindow::openAssetInModule);
    MainWindow *shell = mShell;
    connect(assetWidget, &AssetWidget::spawnAvatarInScene, mShell,
            [shell](const QString &guid) { shell->spawnAvatarAsset(guid, iris::Vec3(), false); });
    // THE IMPORT DECISION (SPECS/IMPORT_DIALOG_SPEC.md §8), both halves. The
    // shell owns the dialog: it is the one place with the widget layer AND the
    // ScriptHost, so a reimport commits through the assets.reimport verb.
    connect(assetWidget, &AssetWidget::reimportAssetRequested, mShell,
            [shell](const QString &guid) { shell->openImportSettings(guid); });


	assetWidget->sceneView = sceneView;

    QWidget *assetDockContents = new QWidget(viewPort);
    QGridLayout *assetsLayout = new QGridLayout(assetDockContents);
    assetsLayout->addWidget(assetWidget);
    assetsLayout->setContentsMargins(0, 0, 0, 0);

    // THE BOTTOM AREA IS ONE TAB GROUP (owner, 2026-09-14, lane SPACE-2):
    // "Assets and Timeline as tabs, and the script Console (Ctrl+`) as a third
    // tab when it is turned on".
    //
    // It used to be TWO tab bars. The tray's widget was a QTabWidget carrying
    // "Assets" and "Console" at the top (smoke S1), while the Timeline — a dock
    // tabified with the tray since the phase-3 refactor — could only be reached
    // through Qt's OWN dock tab bar, which for a bottom dock area is drawn at
    // the very BOTTOM edge of the window, a ~20 px strip under the tray (rig
    // measurement 2026-09-14: the Timeline dock sat at x=-1239, Qt's off-screen
    // parking spot for a tab that is not in front, and its only handle was that
    // strip). The editor therefore read as a single "Assets" panel and the
    // Timeline as GONE — the owner's report.
    //
    // So: the nested QTabWidget is deleted (the tray holds the asset browser
    // directly), the console goes back to being a dock, and the ONE tab bar the
    // three share is moved to the TOP of the area, which is where the tray's
    // own bar used to be and where a user looks for tabs. Tabified docks do NOT
    // split the area, so the objection that retired the console dock at smoke
    // S1 (opening it shrank the viewport) does not apply to this shape.
    assetDock->setWidget(assetDockContents);
    // The Presets line follows the Tray (owner 2026-09-12): a Tray resize —
    // a drag of its top edge, a layout restore — re-aligns the right column.
    assetDock->installEventFilter(this);

    // Animation Dock
    animationDock = new QDockWidget("Timeline", viewPort);
    animationDock->setObjectName(QStringLiteral("animationDock"));
    animationWidget = new AnimationWidget;
    // F16: the Timeline's edits are undoable — the panel pushes the same
    // commands the anim.* verbs push (services/animationedits.h is the shared
    // edit, src/commands/animationcommands.h the shared record).
    animationWidget->setServices(services);

    QWidget *animationDockContents = new QWidget;
    QGridLayout *animationLayout = new QGridLayout(animationDockContents);
    animationLayout->setContentsMargins(0, 0, 0, 0);
    animationLayout->addWidget(animationWidget);

    animationDock->setWidget(animationDockContents);

    // Script Console Dock — the bottom area's third tab (lane SPACE-2). The
    // DOCK is built here, empty: it must exist before DockState::restore below,
    // because a layout blob that names a dock the window does not have leaves
    // Qt guessing at the whole area (the reason kVersion went to 2 when the
    // console STOPPED being a dock). Its widget is the ScriptConsole, which
    // needs the script engine and is handed over where that is built.
    scriptConsoleDock = new QDockWidget(tr("Console"), viewPort);
    scriptConsoleDock->setObjectName(QStringLiteral("scriptConsoleDock"));

    // THE DEFAULT LAYOUT. Presets lives in the RIGHT COLUMN, under Properties
    // (owner layout, 2026-09-08) — it used to open in the BOTTOM area beside
    // the Asset Browser and the Timeline, which is not where anybody uses it
    // and not the column PanelMetrics sizes it for: `presetsPanelWidth` IS
    // `rightColumnWidth`, and a bottom-area Presets panel forced that width
    // onto a dock that spans the whole window instead.
    //
    // splitDockWidget, not addDockWidget: it puts the two in ONE column split
    // vertically, which is the arrangement the shared width constant describes.
    viewPort->addDockWidget(Qt::LeftDockWidgetArea, sceneHierarchyDock);
    viewPort->addDockWidget(Qt::RightDockWidgetArea, sceneNodePropertiesDock);
    viewPort->splitDockWidget(sceneNodePropertiesDock, presetsDock, Qt::Vertical);
    viewPort->addDockWidget(Qt::BottomDockWidgetArea, assetDock);
    viewPort->addDockWidget(Qt::BottomDockWidgetArea, animationDock);
    viewPort->addDockWidget(Qt::BottomDockWidgetArea, scriptConsoleDock);
    // ONE GROUP, IN THE ORDER THE TABS READ (lane SPACE-2): Assets, Timeline,
    // Console. tabifyDockWidget(a, b) puts b AFTER a, so the pair of calls is
    // the tab order — the old single call read "Timeline | Tray", which put the
    // panel the user opens on the right of the panel they rarely open.
    viewPort->tabifyDockWidget(assetDock, animationDock);
    viewPort->tabifyDockWidget(animationDock, scriptConsoleDock);
    // AND THE BAR GOES AT THE TOP. Qt's default for a bottom dock area is
    // QTabWidget::South: a tab strip along the very bottom edge of the window,
    // which is where the Timeline's only handle was hiding (owner report,
    // 2026-09-14) and the first thing a window a few pixels too tall for the
    // screen loses. North puts it where the tray's own tab bar used to be.
    viewPort->setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
    // The console is CLOSED until Ctrl+` asks for it — a hidden dock has no
    // tab, which is exactly "a third tab when it is turned on". Hidden before
    // any layout is restored, so a blob that recorded it open can say so.
    scriptConsoleDock->hide();
    // AND THE EDITOR OPENS ON ASSETS. tabifyDockWidget leaves the dock it
    // inserted LAST in front, which would hand a fresh profile the Timeline —
    // a panel most sessions never touch — in front of the asset browser every
    // session starts in. (The restored layout below carries the user's LAST
    // front tab; it is raised again after that restore — see there.)
    raiseLaunchBottomTab();

    // ...and the USER's layout on top of it, if there is one. The docks belong
    // to this nested QMainWindow, so MainWindow's own restoreState (which the
    // constructor calls) never reached them: every move, resize, float, tab
    // and close was forgotten at exit. `restoredViewportDocks` is what tells
    // applyColumnWidthsOnce to keep its hands off — a remembered column
    // width must win over the compiled-in default (shell/dockstate.h).
    restoredViewportDocks =
        settings ? DockState::restore(viewPort, settings->settings, kViewportDockStateKey) : false;
    // A saved dock layout carries the dock-area CORNERS: restoring one saved
    // before the corner rule would put the default corner back. Re-assert it.
    viewPort->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    // EVERY SESSION STARTS IN THE ASSET BROWSER (owner, 2026-09-15, ledger
    // §351). The restored blob carries whichever bottom tab was in front when
    // the last session ended — the Timeline, or the Console after a Ctrl+` —
    // and which tab is in front is SESSION state, not a preference: within a
    // session it still persists across space switches (SPACE-2), but a launch
    // raises Assets over whatever the blob remembered. (It regressed because
    // the blob is restored a SECOND time from applyColumnWidthsOnce — that
    // restore is followed by the same call now; see raiseLaunchBottomTab.)
    raiseLaunchBottomTab();
    // A dock closed from its own title bar is a dock the user closed: the
    // Close event goes into `widgetStates` (see eventFilter), so the panel
    // stays closed across space switches and the Toggle Widgets dialog agrees.
    for (QDockWidget *dock : { sceneHierarchyDock, sceneNodePropertiesDock, presetsDock,
                               assetDock, animationDock, scriptConsoleDock })
        dock->installEventFilter(this);
    // WHICH PANELS ARE OPEN IS `widgetStates`, FROM NOW ON (lane SPACE-1).
    // A restored layout says which docks the user had closed, and until this
    // line that answer survived exactly until the first space switch, which
    // re-showed everything from the compiled-in defaults. Seeding the session's
    // own record from the blob is what makes a closed panel stay closed — and
    // it is the same record applyVisibility reads, so the layout
    // and the space can no longer disagree.
    //
    // `isHidden()`, AND IT HAS TO BE (lane SPACE-2, measured). A tabified dock
    // that is not the front tab is NOT hidden by Qt: it stays shown and is
    // parked off-screen (a negative x — the rig read the Timeline at x=-1239),
    // which is why isHidden() is the predicate that answers "did the user close
    // this panel" for a tab as well as for a lone dock. The obvious
    // alternatives are both WRONG here: `isVisible()` is false for every one of
    // these docks while their page is not on screen, and
    // `toggleViewAction()->isChecked()` is false for ALL of them until the
    // window is first shown — this runs in the constructor. A standalone Qt
    // 6.10 probe of the three readings is in the lane's spike directory.
    if (restoredViewportDocks) {
        widgetStates[(int) Widget::HIERARCHY]  = !sceneHierarchyDock->isHidden();
        widgetStates[(int) Widget::PROPERTIES] = !sceneNodePropertiesDock->isHidden();
        widgetStates[(int) Widget::PRESETS]    = !presetsDock->isHidden();
        widgetStates[(int) Widget::ASSETS]     = !assetDock->isHidden();
        widgetStates[(int) Widget::TIMELINE]   = !animationDock->isHidden();
        // The console comes back the way the user left it: a session that
        // quit with the Console tab open opens with it (owner, 2026-09-14).
        widgetStates[(int) Widget::CONSOLE]    = !scriptConsoleDock->isHidden();
    }

	viewPort->setStyleSheet(StyleSheet::QMenuFlat());
}

/// THE COLUMNS OPEN AT THEIR WIDTHS (owner, 2026-09-08: "make the presets
/// right column the same width as the presets panel on the main screen — a
/// little wider to match it"; extended to the LEFT column 2026-09-11, smoke
/// S1: "all right columns and left columns unify on the Editor's widths").
/// The right column used to open at whatever the dock's old 326 px minimum and
/// the viewport's stretch produced — 299 px measured on the rig, visibly
/// narrower than the Presets panel it shares the column with — and the left
/// column at whatever the tree's sizeHint asked for, which is the width every
/// other page is now told to copy, so it has to be a number we chose.
///
/// NOT in build: a dock that is made visible is re-laid-out from its
/// widget's sizeHint, and every entry to the editor page shows these docks, so
/// a resizeDocks from the constructor (queued or not) is simply undone —
/// measured twice on the rig before this landed. It has to run after the page
/// is up, which is why it is queued; enterEditorSpace runs it, which is the one
/// way the editor page appears (a user's switch, a load's reveal, and the
/// scripted/MCP boot — enterEditorOnNewScene).
///
/// ONCE per session. After that the user's drag is the answer — these are
/// starting sizes, not constraints.
void EditorDocks::applyColumnWidthsOnce()
{
    if (columnsSized) return;
    columnsSized = true;
    // A RESTORED LAYOUT ALREADY SAID HOW WIDE THE COLUMNS ARE: the widths below
    // are the compiled-in DEFAULTS, applied once per session, and overriding a
    // width the user dragged and this window just restored would make the dock
    // state look like it was not saved at all. (The restored case is not simply
    // skipped — see the queued block.)
    QTimer::singleShot(0, this, [this]() {
        if (!viewPort || !sceneNodePropertiesDock) return;
        // A RESTORED LAYOUT IS RESTORED AGAIN, HERE (smoke S1). The blob went
        // in from build — the constructor — where the nested
        // viewPort QMainWindow has no size yet, and Qt scales the saved dock
        // sizes down to whatever width it does have, clamping at the docks'
        // minimums; the window then grows and the slack all goes to the central
        // widget, so the columns come back NARROWER than the user left them,
        // every launch. Measured on the base build 2026-09-11: a Hierarchy dock
        // saved at 328 px came back at 288, and with the S1 minimums a column
        // saved at 396 came back at its 300 floor — which would have put the
        // editor's columns out of step with every other page's on the second
        // launch. Applying the SAME blob now, at the real width, lands the
        // sizes the user actually left.
        if (restoredViewportDocks) {
            if (settings) DockState::restore(viewPort, settings->settings, kViewportDockStateKey);
            viewPort->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);   // see build
            // AND THE LAUNCH TAB SURVIVES THE SECOND RESTORE (owner review R7).
            // This blob carries the last session's front tab, and re-applying
            // it here is what silently undid the constructor's raise one
            // event-loop turn after the editor opened — the rule was written in
            // build and lost here. Before applyVisibility
            // below, which READS the front tab back into bottomFrontTab.
            raiseLaunchBottomTab();
            // THE BLOB DOES NOT DECIDE WHICH PANELS ARE OPEN (lane SPACE-1).
            // It carries each dock's visibility, and applying it here — after
            // switchSpace(EDITOR) has just shown the panels — is what closed
            // them again one event-loop turn after the editor opened. The
            // space owns visibility; this pass owns sizes.
            applyVisibility();
            alignPresetsWithTray();
            return;
        }
        // BOTH docks in the right column, from the one constant. Presets sits
        // under Properties in the default layout now, and a horizontal
        // resizeDocks that names only one of a vertically split pair leaves the
        // other free to argue about the width. The Hierarchy dock is the left
        // column and rides the same call.
        QList<QDockWidget *> column{ sceneNodePropertiesDock };
        QList<int> widths{ PanelMetrics::rightColumnWidth };
        if (presetsDock) { column << presetsDock; widths << PanelMetrics::rightColumnWidth; }
        if (sceneHierarchyDock) {
            column << sceneHierarchyDock;
            widths << PanelMetrics::leftColumnWidth;
        }
        viewPort->resizeDocks(column, widths, Qt::Horizontal);
        alignPresetsWithTray();
    });
}

// THE PRESETS LINE (owner, 2026-09-12): "the bottom drawer with the materials
// and preset shapes needs to start at the same horizontal line as the asset
// module". The right column runs to the bottom (the corner rule), so the
// Presets panel's top is set to the Tray's top: Presets gets the Tray's height
// and Properties the rest of the column. Skipped when either panel is hidden or
// floating — there is no shared line to meet then.
void EditorDocks::alignPresetsWithTray(int retries)
{
    if (!viewPort || !assetDock || !presetsDock || !sceneNodePropertiesDock) return;
    if (!assetDock->isVisible() || !presetsDock->isVisible() || !sceneNodePropertiesDock->isVisible())
        return;
    if (assetDock->isFloating() || presetsDock->isFloating() || sceneNodePropertiesDock->isFloating())
        return;
    const int trayTop = bottomAreaTop();
    const int columnTop = sceneNodePropertiesDock->geometry().top();
    const int columnBottom = presetsDock->geometry().bottom();
    if (trayTop <= columnTop || columnBottom <= trayTop) return;
    // Two passes at most: the dock separators take a few pixels the first
    // request cannot know about, so measure what landed and correct once.
    int presetsH = columnBottom - trayTop + 1;
    for (int pass = 0; pass < 2; ++pass) {
        const int delta = presetsDock->geometry().top() - trayTop;
        if (qAbs(delta) <= 1) break;                        // on the line
        if (pass == 1) presetsH += delta;                   // the separator's share
        const int propsH = qMax(1, (columnBottom - columnTop + 1) - presetsH);
        viewPort->resizeDocks({ sceneNodePropertiesDock, presetsDock }, { propsH, presetsH }, Qt::Vertical);
        if (QLayout *l = viewPort->layout()) l->activate();
    }
    // AND THEN LOOK AGAIN, ONCE THE LAYOUT HAS SETTLED (lane SPACE-2). The two
    // passes above measure what the dock area reports in THIS turn, and that is
    // not always where things end up: the bottom area's tab bar appears a turn
    // later (a second panel opening there) and takes its height off the top of
    // the area, and the right column follows — so the pass that ran at boot
    // reported itself exactly on the line and the user saw it 15 px below.
    // Bounded, and a no-op the moment the two edges agree.
    if (retries <= 0) return;
    QTimer::singleShot(0, this, [this, retries]() {
        if (!presetsDock || !presetsDock->isVisible() || presetsDock->isFloating()) return;
        if (qAbs(presetsDock->geometry().top() - bottomAreaTop()) <= 1) return;
        alignPresetsWithTray(retries - 1);
    });
}

bool EditorDocks::setTrayHeight(int height)
{
    if (!viewPort || !assetDock || height < 40) return false;
    viewPort->resizeDocks({ bottomFrontDock() }, { height }, Qt::Vertical);
    QCoreApplication::processEvents();
    alignPresetsWithTray();
    return true;
}

// THE EDITOR'S DOCKS, MEASURED (lane SPACE-1, 2026-09-14). The panels the
// owner reported missing after a player -> editor switch were not always
// hidden: a restored layout can also bring them back at a degenerate WIDTH
// (the 2026-09-14 screenshot: a left column 20 px wide showing nothing but the
// hierarchy rows' lock icons), which looks exactly the same from the user's
// chair. "Is the panel there" is therefore two numbers, not one, and this is
// where a script reads both.
QVariantList EditorDocks::dockReport() const
{
    QVariantList out;
    const QDockWidget *docks[] = { sceneHierarchyDock, sceneNodePropertiesDock, presetsDock,
                                   assetDock, animationDock, scriptConsoleDock };
    for (const QDockWidget *d : docks) {
        if (!d) continue;
        QVariantMap m;
        m.insert("name", d->objectName());
        m.insert("title", d->windowTitle());
        // isVisible() is false for every dock while another page is on screen
        // (they are children of the editor page): the honest reading of "on
        // screen". isVisibleTo(viewPort) is the dock's OWN state — what the
        // editor will show when its page comes back — and the two together are
        // what tells a space-switch defect from a page-switch.
        m.insert("visible", d->isVisible());
        m.insert("shown", viewPort ? d->isVisibleTo(viewPort) : d->isVisible());
        m.insert("floating", d->isFloating());
        // TABS ARE A THIRD THING (lane SPACE-2, owner report: "the timeline
        // widget is gone"). A dock can be open AND unreachable: tabified docks
        // share one space and Qt parks the ones that are not in front
        // off-screen, so `shown` says true for a panel the user cannot see a
        // pixel of. `tabbed` is whether it shares a tab bar with anything, and
        // `current` whether it is the tab in FRONT — the two numbers that tell
        // "behind another tab" from "closed".
        m.insert("tabbed", viewPort ? !viewPort->tabifiedDockWidgets(
                                           const_cast<QDockWidget *>(d)).isEmpty()
                                    : false);
        m.insert("current", isFrontTab(d));
        m.insert("width", d->width());
        m.insert("height", d->height());
        // WHERE IT IS, in the window's own coordinates: what a rig driving
        // xdotool needs to put a pointer on a panel (its title bar's close
        // button, a row in it) without guessing at the dock layout.
        const QPoint topLeft = mWindow ? d->mapTo(mWindow, QPoint(0, 0)) : QPoint();
        m.insert("x", topLeft.x());
        m.insert("y", topLeft.y());
        m.insert("minWidth", qMax(d->minimumWidth(), d->minimumSizeHint().width()));
        m.insert("area", viewPort ? int(viewPort->dockWidgetArea(const_cast<QDockWidget *>(d)))
                                  : 0);
        out.append(m);
    }
    return out;
}

// WHICH TAB IS IN FRONT, without a QTabBar to ask (lane SPACE-2). Qt gives a
// tabified QDockWidget no "am I the current tab" accessor, and the tab bar
// itself is a private child of the dock area — but it gives the docks a
// reading that IS the answer and that QDockWidget's own code uses for exactly
// this: a tab that is not in front is shown and parked OFF-SCREEN, so
// `geometry().right() < 0` (qdockwidget.cpp emits visibilityChanged(geometry()
// .right() >= 0) on Show for this reason). Measured on the rig: the front tab
// at x=0, the other at x=-1239.
//
// A dock that is not tabbed at all trivially passes, which is what we want:
// with the Timeline and the Console closed, the Assets dock IS the front tab.
bool EditorDocks::isFrontTab(const QDockWidget *dock)
{
    return dock && !dock->isHidden() && dock->geometry().right() >= 0;
}

// The bottom area's docks in tab-bar order, and the name each answers to.
QVector<QPair<QString, QDockWidget *>> EditorDocks::bottomAreaTabs() const
{
    QVector<QPair<QString, QDockWidget *>> tabs;
    if (assetDock)        tabs.append({ QStringLiteral("assets"), assetDock });
    if (animationDock)    tabs.append({ QStringLiteral("timeline"), animationDock });
    if (scriptConsoleDock) tabs.append({ QStringLiteral("console"), scriptConsoleDock });
    return tabs;
}

// The bottom area's geometry belongs to whichever tab is in FRONT: the other
// two are parked off-screen, so reading the asset browser's own rectangle while
// the Timeline is up answers with Qt's parking spot (x=-836 on the rig) instead
// of the area every one of them fills. Everything that measures "the tray" —
// the Presets line, the height verb, trayState's geometry — reads it here.
QDockWidget *EditorDocks::bottomFrontDock() const
{
    for (const auto &tab : bottomAreaTabs())
        if (isFrontTab(tab.second)) return tab.second;
    return assetDock;
}

// WHERE THE BOTTOM AREA STARTS ON SCREEN — the line the Presets panel is
// supposed to meet (owner, 2026-09-12: "the bottom drawer … needs to start at
// the same horizontal line as the asset module").
//
// That is NOT the dock's own top any more: the group's tab bar sits ABOVE the
// dock (setTabPosition(North), lane SPACE-2) and is part of what the user sees
// as the bottom panel, so aligning to the dock left the Presets panel a tab
// bar's height (27 px, measured) below the line. Qt keeps that bar private —
// it is a QTabBar child of the dock area, not reachable through any dock API —
// so it is found by GEOMETRY: the visible tab bar sitting directly on top of
// the front dock, in the same horizontal span.
int EditorDocks::bottomAreaTop() const
{
    const QDockWidget *dock = bottomFrontDock();
    if (!dock || !viewPort) return 0;
    const QRect area = dock->geometry();
    int top = area.top();
    for (const QTabBar *bar : viewPort->findChildren<QTabBar *>()) {
        if (!bar->isVisible() || bar->parentWidget() == dock) continue;
        QRect g = bar->geometry();
        if (bar->parentWidget() && bar->parentWidget() != viewPort)
            g.moveTopLeft(bar->parentWidget()->mapTo(viewPort, g.topLeft()));
        if (g.bottom() > area.top() || g.bottom() < area.top() - 8) continue;   // not on top of it
        if (g.right() < area.left() || g.left() > area.right()) continue;       // not over it
        top = std::min(top, g.top());
    }
    return top;
}

QString EditorDocks::trayTab() const
{
    if (!assetDock) return QString();
    for (const auto &tab : bottomAreaTabs())
        if (isFrontTab(tab.second)) return tab.first;
    // Nothing in the bottom area is on screen (every panel there is closed, or
    // the editor page is not up): no tab is in front, but the area exists.
    return QStringLiteral("assets");
}

QStringList EditorDocks::trayTabs() const
{
    QStringList names;
    for (const auto &tab : bottomAreaTabs())
        if (!tab.second->isHidden()) names << tab.first;
    return names;
}

bool EditorDocks::isConsoleTabVisible() const
{
    return scriptConsoleDock && !scriptConsoleDock->isHidden();
}

bool EditorDocks::isTrayVisible() const
{
    return assetDock && assetDock->isVisible();
}

bool EditorDocks::isConsoleInputFocused() const
{
    return mConsole && mConsole->inputHasFocus();
}

// THE CONSOLE TAB IS THE CONSOLE DOCK (lane SPACE-2). Showing it adds a tab to
// the bottom area's one tab bar and brings it to the front; hiding it takes the
// tab away and leaves the other two exactly as they were. `widgetStates` is
// updated with it, like every other panel, so a space switch and a restart
// carry the answer (applyVisibility is the only other writer).
//
// The old implementation had to un-hide the TRAY to show the console, because
// the console lived inside the tray's widget — and then put it back, which is
// what `trayForcedVisible` was for. A dock of its own needs none of that: the
// console can be open with the asset browser closed.
void EditorDocks::setConsoleTabVisible(bool visible, bool focusInput)
{
    if (!scriptConsoleDock) return;
    if (visible) {
        // WHAT CTRL+` INTERRUPTED, so the second press can put it back. Qt
        // picks the NEIGHBOURING tab when the current one disappears, which
        // handed the bottom area to the Timeline every time the console was
        // closed (app.input_keys caught it) — the console is a visitor, and a
        // visitor leaves the room the way it found it.
        for (const auto &tab : bottomAreaTabs())
            if (tab.first != QLatin1String("console") && isFrontTab(tab.second)) {
                bottomReturnTab = tab.first;
                break;
            }
    }
    widgetStates[(int) Widget::CONSOLE] = visible;
    scriptConsoleDock->setVisible(visible);
    if (!visible) {
        bottomFrontTab = bottomReturnTab;
        raiseBottomFrontTab();
        return;
    }
    bottomFrontTab = QStringLiteral("console");
    scriptConsoleDock->raise();
    if (focusInput && mConsole) mConsole->focusInput();
}

bool EditorDocks::setTrayTab(const QString &tab, bool focusConsoleInput)
{
    if (!assetDock) return false;
    const QString wanted = tab.trimmed().toLower();
    if (wanted == QLatin1String("console")) {
        setConsoleTabVisible(true, focusConsoleInput);
        return true;
    }
    // Selecting a tab does NOT close any other: the tabs stay in the bar (that
    // is what a tab bar is for) — the title-bar X and Ctrl+` are what remove
    // one. A panel the user closed cannot be raised, though: naming a closed
    // tab OPENS it, which is what "show me this tab" means from a script.
    for (const auto &entry : bottomAreaTabs()) {
        if (entry.first != wanted) continue;
        if (entry.second->isHidden()) {
            entry.second->setVisible(true);
            if (entry.second == assetDock)     widgetStates[(int) Widget::ASSETS]   = true;
            if (entry.second == animationDock) widgetStates[(int) Widget::TIMELINE] = true;
        }
        entry.second->raise();
        return true;
    }
    return false;
}

void EditorDocks::toggleScriptConsole()
{
    if (!scriptConsoleDock) return;
    // "Showing" means both: the tab is in the bar AND it is the tab in front.
    // Anything less and Ctrl+` brings it forward rather than closing something
    // the user cannot see.
    setConsoleTabVisible(!(isConsoleTabVisible() && isFrontTab(scriptConsoleDock)));
}

void EditorDocks::openToggleDialog()
{
	QDialog *d = new QDialog(mWindow);
	d->setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::Popup);

	d->setStyleSheet(StyleSheet::DockToggleDialog());

	QVBoxLayout *dl = new QVBoxLayout;
	dl->setContentsMargins(20, 10, 20, 16);
	d->setLayout(dl);

	QPushButton *hierarchy = new QPushButton("Hierarchy");
	hierarchy->setAccessibleName(QStringLiteral("toggleAbles"));
	hierarchy->setCheckable(true);
	hierarchy->setChecked(widgetStates[(int) Widget::HIERARCHY]);

	QPushButton *properties = new QPushButton("Properties");
	properties->setAccessibleName(QStringLiteral("toggleAbles"));
	properties->setCheckable(true);
	properties->setChecked(widgetStates[(int) Widget::PROPERTIES]);

	QPushButton *presets = new QPushButton("Presets");
	presets->setAccessibleName(QStringLiteral("toggleAbles"));
	presets->setCheckable(true);
	presets->setChecked(widgetStates[(int) Widget::PRESETS]);

	QPushButton *timeline = new QPushButton("Timeline");
	timeline->setAccessibleName(QStringLiteral("toggleAbles"));
	timeline->setCheckable(true);
	timeline->setChecked(widgetStates[(int) Widget::TIMELINE]);

	QPushButton *assets = new QPushButton("Assets Browser");
	assets->setAccessibleName(QStringLiteral("toggleAbles"));
	assets->setCheckable(true);
	assets->setChecked(widgetStates[(int) Widget::ASSETS]);

	QPushButton *closeAll = new QPushButton("Close All");
	closeAll->setCheckable(true);

	QPushButton *restoreAll = new QPushButton("Restore All");
	restoreAll->setCheckable(true);

	QLabel *label = new QLabel("Toggle Widgets");
	label->setAlignment(Qt::AlignCenter);
	label->setContentsMargins(0, 0, 0, 6);
	dl->addWidget(label);

	dl->addWidget(hierarchy);
	dl->addWidget(properties);
	dl->addWidget(presets);
	dl->addWidget(timeline);
	dl->addWidget(assets);

	// (A dead "Save" button lived here — built, connected to a body that was
	// entirely commented out, and never added to the layout. The panels the
	// user leaves open are saved with the rest of the dock layout at exit now.
	// Lane SPACE-1, CRUD.)
	QWidget *cw = new QWidget;
	QHBoxLayout *cl = new QHBoxLayout;
    cl->setContentsMargins(0, 0, 0, 0);
	cw->setLayout(cl);
	cl->addWidget(closeAll);
	cl->addWidget(restoreAll);
	dl->addWidget(cw);

	// EVERY BUTTON IS `setPanelOpen` (lane SPACE-2). The five toggles used to
	// call setVisible and write `widgetStates` themselves, five copies of the
	// two lines, and Close All used close() instead — so "closed from the
	// dialog" and "closed from the X" were two different states of the same
	// panel. One function, one meaning, and the same one the `editor.panel`
	// verb and the tests drive. (The script console is deliberately not among
	// these: Ctrl+` is its switch, and "Restore All" is about the panels the
	// editor is made of.)
	static const QStringList kDialogPanels = { QStringLiteral("hierarchy"),
											   QStringLiteral("properties"),
											   QStringLiteral("presets"),
											   QStringLiteral("timeline"),
											   QStringLiteral("assets") };
	connect(hierarchy,  &QPushButton::toggled, [this](bool set) { setPanelOpen("hierarchy", set); });
	connect(properties, &QPushButton::toggled, [this](bool set) { setPanelOpen("properties", set); });
	connect(presets,    &QPushButton::toggled, [this](bool set) { setPanelOpen("presets", set); });
	connect(timeline,   &QPushButton::toggled, [this](bool set) { setPanelOpen("timeline", set); });
	connect(assets,     &QPushButton::toggled, [this](bool set) { setPanelOpen("assets", set); });

	connect(closeAll,	&QPushButton::pressed,	[&]() {
		for (const QString &panel : kDialogPanels) setPanelOpen(panel, false);

		hierarchy->setChecked(false);
		properties->setChecked(false);
		assets->setChecked(false);
		timeline->setChecked(false);
		presets->setChecked(false);
	});

	connect(restoreAll, &QPushButton::pressed,	[&]() {
		for (const QString &panel : kDialogPanels) setPanelOpen(panel, true);

		hierarchy->setChecked(true);
		properties->setChecked(true);
		assets->setChecked(true);
		timeline->setChecked(true);
		presets->setChecked(true);
	});

	d->exec();
}

// THE DOCKS FOLLOW THE PAGE ON SCREEN, FROM ONE PLACE (lane SPACE-1,
// 2026-09-14).
//
// The editor shows the panels `widgetStates` says are open; every other page
// shows none, because they are the editor page's and that page is not up. Both
// the space switch and the queued layout pass (applyColumnWidthsOnce, which
// re-applies the saved blob once the window has its real size) end by calling
// this, so a restored layout can no longer undo the visibility the page just
// asked for — which is exactly what made the editor open empty after a
// restart, and stay empty until the user visited another page and came back
// (owner report, 2026-09-14).
//
// THE PAGE, NOT `currentSpace` (round-2 review). They are the same thing for
// every path a user takes, and different for the one a SCRIPT takes:
// enterEditorOnNewScene runs enterEditorSpace with currentSpace still DESKTOP
// (the scripted/MCP boot never calls switchSpace), so keying on the space hid all five docks one loop turn into
// every scripted session that had a stored layout. `ui->stackedWidget`'s
// current index is what "the editor is what the user is looking at" actually
// means — it is the same reading app.docks() reports as `visible`.
//
// IMMERSIVE FULLSCREEN is the other way the editor page legitimately has no
// chrome (F11, EDITOR_SHORTCUTS_SPEC §3): without this term a space round trip
// inside fullscreen would put the docks back on top of it, and
// leaveImmersiveFullscreen would then restore a state nobody was in.
void EditorDocks::applyVisibility()
{
    if (!sceneHierarchyDock) return;
    const bool editor = mEditorOnScreen && mEditorOnScreen();
    // WHICH TAB IS IN FRONT SURVIVES THE ROUND TRIP (lane SPACE-2). Showing a
    // tabified dock RAISES it, so the loop below would hand the front tab to
    // whichever dock it shows last — a trip to the Player and back came home on
    // the Timeline no matter what the user was doing. Remember the front tab
    // while it is still readable, restore it once they are all back.
    for (const auto &tab : bottomAreaTabs()) {
        if (!isFrontTab(tab.second)) continue;
        bottomFrontTab = tab.first;
        break;
    }
    sceneHierarchyDock->setVisible(editor && widgetStates[(int) Widget::HIERARCHY]);
    sceneNodePropertiesDock->setVisible(editor && widgetStates[(int) Widget::PROPERTIES]);
    presetsDock->setVisible(editor && widgetStates[(int) Widget::PRESETS]);
    // THE BOTTOM AREA'S THREE, AND THE FRONT TAB GOES LAST (lane SPACE-2).
    // Showing a tabified dock RAISES it, so the order these are shown in IS
    // which tab comes up in front — and a raise() afterwards does not stick:
    // the dock area's layout pass runs later and leaves the last dock it
    // inserted in front (measured on the rig — every trip home landed on the
    // Timeline, and so did the boot). Ordering the calls needs no timer and
    // cannot flip a tab in front of the user.
    auto wanted = [&](const QDockWidget *dock) {
        if (dock == assetDock)     return editor && widgetStates[(int) Widget::ASSETS];
        if (dock == animationDock) return editor && widgetStates[(int) Widget::TIMELINE];
        return editor && widgetStates[(int) Widget::CONSOLE];
    };
    const QVector<QPair<QString, QDockWidget *>> bottom = bottomAreaTabs();
    for (const auto &tab : bottom)
        if (tab.first != bottomFrontTab) tab.second->setVisible(wanted(tab.second));
    for (const auto &tab : bottom)
        if (tab.first == bottomFrontTab) tab.second->setVisible(wanted(tab.second));
    // A dock that was ALREADY visible is not re-shown by the line above (Qt
    // returns early), so an ordinary raise covers the case where nothing about
    // the bottom area's visibility changed and only the front tab is wrong.
    if (editor) raiseBottomFrontTab();
}

// OPEN OR CLOSE AN EDITOR PANEL, IN ONE PLACE (lane SPACE-2, API-first).
//
// Closing is `close()` and not `setVisible(false)` ON PURPOSE: the title-bar X
// is close(), the Close event is what writes `widgetStates` (see eventFilter),
// and a panel that two gestures close by two different routes is how the Toggle
// Widgets dialog and the X came to disagree in the first place. Opening shows
// the dock AND raises it — in the bottom area's tab group, a panel the user
// asked for that comes back behind another tab has not come back.
//
// `name` is the panel's script name; the empty QString answer means "no such
// panel", which is what the verb reports back.
QDockWidget *EditorDocks::panelDock(const QString &name) const
{
    const QString wanted = name.trimmed().toLower();
    if (wanted == QLatin1String("hierarchy"))  return sceneHierarchyDock;
    if (wanted == QLatin1String("properties")) return sceneNodePropertiesDock;
    if (wanted == QLatin1String("presets"))    return presetsDock;
    if (wanted == QLatin1String("assets"))     return assetDock;
    if (wanted == QLatin1String("timeline"))   return animationDock;
    if (wanted == QLatin1String("console"))    return scriptConsoleDock;
    return nullptr;
}

QVariantList EditorDocks::propertyRows(const QString &tabName) const
{
    QVariantList out;
    if (!sceneNodePropertiesWidget) return out;
    SceneNodePropertiesWidget::Tab tab = sceneNodePropertiesWidget->propertiesTab();
    if (!tabName.trimmed().isEmpty()
        && !SceneNodePropertiesWidget::tabFromName(tabName, tab)) return out;
    const QString name = SceneNodePropertiesWidget::tabName(tab);
    for (const auto &row : sceneNodePropertiesWidget->propertyRows(tab)) {
        QVariantMap entry;
        entry[QStringLiteral("tab")] = name;
        entry[QStringLiteral("section")] = row.sections;
        entry[QStringLiteral("label")] = row.label;
        entry[QStringLiteral("key")] = row.key;
        entry[QStringLiteral("keywords")] = row.keywords;
        entry[QStringLiteral("panelVisible")] = row.panelVisible;
        entry[QStringLiteral("filteredOut")] = row.filteredOut;
        entry[QStringLiteral("visible")] = row.visible;
        out.append(entry);
    }
    return out;
}

QVariantMap EditorDocks::propertyRow(const QString &tabName, const QString &key, bool drive,
                                    const QVariant &value, QString *error)
{
    auto refuse = [error](const QString &why) { if (error) *error = why; return QVariantMap(); };
    if (!sceneNodePropertiesWidget) return refuse(QStringLiteral("there is no properties column"));
    SceneNodePropertiesWidget::Tab tab = sceneNodePropertiesWidget->propertiesTab();
    if (!tabName.trimmed().isEmpty() && !SceneNodePropertiesWidget::tabFromName(tabName, tab))
        return refuse(QStringLiteral("unknown tab '%1' (world|selection)").arg(tabName));
    // The same listing editor.properties reports, so "which row" is answered
    // the way the column answers it — and a mount owed to this turn happens.
    QVector<PropertyRows::Registry::Listing> hits;
    for (const auto &row : sceneNodePropertiesWidget->propertyRows(tab))
        if (row.key == key && row.widget) hits.append(row);
    const QString name = SceneNodePropertiesWidget::tabName(tab);
    if (hits.isEmpty())
        return refuse(QStringLiteral("the %1 tab has no row with the key '%2' mounted")
                          .arg(name, key));
    if (hits.size() > 1)
        return refuse(QStringLiteral("%1 rows on the %2 tab share the key '%3'")
                          .arg(hits.size()).arg(name, key));
    const PropertyRows::Registry::Listing &row = hits.first();
    if (drive) {
        // A ROW THE PANEL HIDES TAKES NO GESTURE (a filtered-out row is still
        // the panel's and is driven: the filter is a view, not a lock).
        if (!row.panelVisible)
            return refuse(QStringLiteral("the row '%1' is hidden by its panel").arg(key));
        QString why;
        if (!PropertyRows::driveRow(row.widget, value, &why))
            return refuse(QStringLiteral("the row '%1' refused: %2").arg(key, why));
    }
    // Read AFTER the gesture: what the row shows once its panel has answered.
    QVariantMap out = PropertyRows::readRow(row.widget);
    out[QStringLiteral("tab")] = name;
    out[QStringLiteral("key")] = row.key;
    out[QStringLiteral("label")] = row.label;
    out[QStringLiteral("section")] = row.sections;
    out[QStringLiteral("panelVisible")] = row.panelVisible;
    out[QStringLiteral("filteredOut")] = row.filteredOut;
    out[QStringLiteral("visible")] = row.visible;
    return out;
}

bool EditorDocks::isPropertiesTab(const QString &tabName) const
{
    if (tabName.trimmed().isEmpty()) return true;
    SceneNodePropertiesWidget::Tab tab;
    return SceneNodePropertiesWidget::tabFromName(tabName, tab);
}

QString EditorDocks::propertiesFilter(const QString &tabName) const
{
    if (!sceneNodePropertiesWidget) return QString();
    SceneNodePropertiesWidget::Tab tab = sceneNodePropertiesWidget->propertiesTab();
    if (!tabName.isEmpty() && !SceneNodePropertiesWidget::tabFromName(tabName, tab)) return QString();
    return sceneNodePropertiesWidget->propertiesFilter(tab);
}

bool EditorDocks::setPropertiesFilter(const QString &tabName, const QString &text)
{
    if (!sceneNodePropertiesWidget) return false;
    SceneNodePropertiesWidget::Tab tab = sceneNodePropertiesWidget->propertiesTab();
    if (!tabName.isEmpty() && !SceneNodePropertiesWidget::tabFromName(tabName, tab)) return false;
    sceneNodePropertiesWidget->setPropertiesFilter(tab, text);
    return true;
}

QPair<int, int> EditorDocks::propertiesFilterCounts(const QString &tabName) const
{
    if (!sceneNodePropertiesWidget) return { 0, 0 };
    SceneNodePropertiesWidget::Tab tab = sceneNodePropertiesWidget->propertiesTab();
    if (!tabName.isEmpty() && !SceneNodePropertiesWidget::tabFromName(tabName, tab)) return { 0, 0 };
    const auto c = sceneNodePropertiesWidget->filterCounts(tab);
    return { c.visible, c.hidden };
}

QVariantMap EditorDocks::propertiesStats() const
{
    QVariantMap out;
    if (!sceneNodePropertiesWidget) return out;
    const auto s = sceneNodePropertiesWidget->propertiesStats();
    out[QStringLiteral("mounts")] = s.mounts;
    out[QStringLiteral("refills")] = s.refills;
    out[QStringLiteral("rebuilds")] = s.rebuilds;
    out[QStringLiteral("rows")] = s.rows;
    out[QStringLiteral("pending")] = s.pending;
    out[QStringLiteral("deferredHidden")] = s.deferredHidden;
    out[QStringLiteral("visible")] = s.visible;
    out[QStringLiteral("attached")] = s.attached;
    return out;
}

QString EditorDocks::propertiesTab() const
{
    return sceneNodePropertiesWidget
        ? SceneNodePropertiesWidget::tabName(sceneNodePropertiesWidget->propertiesTab())
        : QString();
}

bool EditorDocks::setPropertiesTab(const QString &name)
{
    if (!sceneNodePropertiesWidget) return false;
    SceneNodePropertiesWidget::Tab tab;
    if (!SceneNodePropertiesWidget::tabFromName(name, tab)) return false;
    sceneNodePropertiesWidget->setPropertiesTab(tab);
    return true;
}

bool EditorDocks::setPanelOpen(const QString &name, bool open)
{
    QDockWidget *dock = panelDock(name);
    if (!dock) return false;
    if (!open) {
        dock->close();                       // the X's own gesture: see eventFilter
        return true;
    }
    // The record first: showing a dock whose page is not up would be undone by
    // the next applyVisibility, and the user's answer is the bit.
    if      (dock == sceneHierarchyDock)      widgetStates[(int) Widget::HIERARCHY]  = true;
    else if (dock == sceneNodePropertiesDock) widgetStates[(int) Widget::PROPERTIES] = true;
    else if (dock == presetsDock)             widgetStates[(int) Widget::PRESETS]    = true;
    else if (dock == assetDock)               widgetStates[(int) Widget::ASSETS]     = true;
    else if (dock == animationDock)           widgetStates[(int) Widget::TIMELINE]   = true;
    else if (dock == scriptConsoleDock) {
        // ONE OPENER FOR THE CONSOLE (round 2): setConsoleTabVisible is where
        // the tab it interrupts is recorded, so a console opened through this
        // verb and one opened with Ctrl+` return to the same tab when they
        // close. `false`: opening a panel is not a request for the keyboard.
        setConsoleTabVisible(true, false);
        return true;
    }
    dock->show();
    dock->raise();
    for (const auto &tab : bottomAreaTabs())
        if (tab.second == dock) bottomFrontTab = tab.first;
    return true;
}

bool EditorDocks::isPanelOpen(const QString &name) const
{
    const QDockWidget *dock = panelDock(name);
    return dock && !dock->isHidden();
}

bool EditorDocks::raisePanel(const QString &name)
{
    QDockWidget *dock = panelDock(name);
    if (!dock || dock->isHidden()) return false;   // a closed panel has no front
    // The console's tab is bookkept by setConsoleTabVisible (it remembers the
    // tab it interrupted), so raising it goes through that one opener for the
    // same reason opening it does — otherwise the tab it interrupted is lost
    // and closing the console returns to the wrong one.
    if (dock == scriptConsoleDock) { setConsoleTabVisible(true, false); return true; }
    dock->raise();
    for (const auto &tab : bottomAreaTabs())
        if (tab.second == dock) bottomFrontTab = tab.first;
    return true;
}

void EditorDocks::raiseBottomFrontTab()
{
    for (const auto &tab : bottomAreaTabs())
        if (tab.first == bottomFrontTab && !tab.second->isHidden()) { tab.second->raise(); return; }
}

// THE LAUNCH TAB IS ASSETS — see the header for the rule and for why this is a
// function and not two lines at each restore.
void EditorDocks::raiseLaunchBottomTab()
{
    if (!assetDock) return;
    bottomFrontTab = QStringLiteral("assets");
    // Where the console would return to, too: a blob that recorded the console
    // open leaves it open (the user's panel set is theirs), but closing it must
    // land on Assets and not on whatever the last session interrupted.
    bottomReturnTab = QStringLiteral("assets");
    assetDock->raise();
}

// THE LAYOUT THE EDITOR LAST HAD (lane SPACE-1). Called on the way out of the
// editor space and before immersive fullscreen hides the chrome; a no-op once
// the docks are down, so the caller never has to think about ordering. What it
// holds is what closeEvent writes — never the Player's empty one.
void EditorDocks::captureLayout()
{
    if (!viewPort || !DockState::hasVisibleDock(viewPort)) return;
    editorDockState = DockState::snapshot(viewPort);
}

void EditorDocks::favoriteItem(QListWidgetItem *item)
{
    if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Material)) {
        assetMaterialPanel->addNewItem(item);
        presetsTabWidget->setCurrentIndex(1);
    }
    else if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Object)) {
        assetModelPanel->addNewItem(item);
        presetsTabWidget->setCurrentIndex(0);
    }
}

// THE PANEL RE-READS THE DOCUMENT (round 2, item 3). Used by the edit gate:
// a row whose write was refused is still showing the refused value, and the
// document is the only thing that knows better. Both halves are deferred —
// refreshFromDocument defers its own rebuild (a blade rebuilt inside a
// control's signal handler is the sky panel's crash), and the transform rows
// are refreshed on the same turn for symmetry.
void EditorDocks::refreshPropertiesFromDocument()
{
    if (!sceneNodePropertiesWidget) return;
    sceneNodePropertiesWidget->refreshFromDocument();
    QPointer<EditorDocks> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (self && self->sceneNodePropertiesWidget) self->sceneNodePropertiesWidget->refreshTransform();
    });
}

// THE TITLE-BAR X AND THE PRESETS LINE: the docks' own events.
bool EditorDocks::eventFilter(QObject *obj, QEvent *event)
{
    // THE TITLE-BAR X IS A DOCK TOGGLE (lane SPACE-1 round 2). `widgetStates`
    // is the session's record of which editor panels are open — it is what the
    // space switch, the queued layout pass and the Toggle Widgets dialog all
    // read — and closing a dock from its own title bar never reached it: the
    // panel came back at the next space round trip, and the dialog showed it
    // ticked in the meantime. A QDockWidget's X calls close() on the dock, so
    // the Close event is exactly that gesture and nothing else (hiding a page
    // hides its docks without closing them).
    if (event->type() == QEvent::Close) {
        if      (obj == sceneHierarchyDock)      widgetStates[(int) Widget::HIERARCHY]  = false;
        else if (obj == sceneNodePropertiesDock) widgetStates[(int) Widget::PROPERTIES] = false;
        else if (obj == presetsDock)             widgetStates[(int) Widget::PRESETS]    = false;
        else if (obj == assetDock)               widgetStates[(int) Widget::ASSETS]     = false;
        else if (obj == animationDock)           widgetStates[(int) Widget::TIMELINE]   = false;
        else if (obj == scriptConsoleDock)       widgetStates[(int) Widget::CONSOLE]    = false;
    }
    // THE PRESETS LINE FOLLOWS THE BOTTOM AREA, however it moves (lane
    // SPACE-2). A RESIZE of the tray was the only trigger, and the area's top
    // also moves without one: the group's tab bar appears when a second panel
    // opens there and disappears when the last one closes, which shifts the
    // whole area's top edge by the bar's height (15 px, measured) — and it
    // happens AFTER the queued alignment pass has run, so the boot layout was
    // left a tab bar's height out of line. A MOVE of any of the three is the
    // same event for this purpose.
    if ((obj == assetDock || obj == animationDock || obj == scriptConsoleDock)
        && (event->type() == QEvent::Resize || event->type() == QEvent::Move)
        && !presetsAlignQueued) {
        presetsAlignQueued = true;
        QTimer::singleShot(0, this, [this]() { presetsAlignQueued = false; alignPresetsWithTray(); });
    }
    return false;
}

// THE EDITOR'S CHROME UNDER IMMERSIVE FULLSCREEN (the ViewController owns the
// toggle and the window state; the docks and the toolbar are the editor's).
namespace {
/// The widgets immersive fullscreen hides, in one place: the hide and the
/// restore must touch exactly the same list. The script console is a dock of
/// the bottom area again (lane SPACE-2), so it is back on the list — hiding the
/// tray no longer hides it.
constexpr int kImmersiveDockCount = 7;
}   // namespace

void EditorDocks::hideForFullscreen()
{
    QWidget *editorDocks[kImmersiveDockCount] = { sceneHierarchyDock, sceneNodePropertiesDock,
                                                  presetsDock, assetDock, animationDock,
                                                  scriptConsoleDock, mToolbar };
    preFullscreenWidgets.clear();
    if (!mEditorActive || !mEditorActive()) return;
    // WHICH TAB WAS IN FRONT, before the chrome goes away (round 2). F11 hides
    // these docks itself rather than going through applyVisibility,
    // so nothing else records it — and re-showing them in list order hands the
    // front tab to the last one shown, which is the Console if it is open and
    // the Timeline if it is not. Same mechanism, same remedy as the space switch.
    for (const auto &tab : bottomAreaTabs())
        if (isFrontTab(tab.second)) { bottomFrontTab = tab.first; break; }
    for (QWidget *w : editorDocks) {
        preFullscreenWidgets.append(w && w->isVisible());
        if (w) w->hide();
    }
}

void EditorDocks::restoreAfterFullscreen()
{
    QWidget *editorDocks[kImmersiveDockCount] = { sceneHierarchyDock, sceneNodePropertiesDock,
                                                  presetsDock, assetDock, animationDock,
                                                  scriptConsoleDock, mToolbar };
    if (preFullscreenWidgets.size() == kImmersiveDockCount) {
        // THE FRONT TAB GOES LAST, because showing a tabified dock raises it
        // (round 2) — the same two-pass order applyVisibility
        // uses, so leaving fullscreen comes back to the tab F11 interrupted
        // instead of to whichever dock happens to sit last in this list.
        QDockWidget *front = nullptr;
        for (const auto &tab : bottomAreaTabs())
            if (tab.first == bottomFrontTab) { front = tab.second; break; }
        for (int i = 0; i < preFullscreenWidgets.size(); ++i)
            if (editorDocks[i] && editorDocks[i] != front)
                editorDocks[i]->setVisible(preFullscreenWidgets[i]);
        for (int i = 0; i < preFullscreenWidgets.size(); ++i)
            if (editorDocks[i] && editorDocks[i] == front)
                editorDocks[i]->setVisible(preFullscreenWidgets[i]);
        raiseBottomFrontTab();
    }
    preFullscreenWidgets.clear();
}

// WHAT A SELECTION COSTS (ADD-1, 2026-09-15). Three of these four are cheap and
// IMMEDIATE — the outline and gizmo in the viewport, the highlighted row in the
// Hierarchy, the timeline's subject. The fourth, the Properties column, is the
// expensive one (44 ms of a scripted add's 50 before this lane), and it is the
// only one nobody can see until the frame paints: it settles its rebuild at the
// end of the event-loop turn instead, coalescing repeated selections into one
// mount (SceneNodePropertiesWidget::applyTab). A click is one turn, so the pick
// is unchanged in feel; an undo of a 64-object macro selects 64 times and mounts
// once. A scripted add is a turn of its own (every verb hops to this thread), so
// a script still mounts per add — the win there is the material blade's REFILL
// and the mesh cache (~3 ms per add, not 44).
void EditorDocks::showSelection(iris::SceneNodePtr sceneNode)
{
    // WHAT THIS COSTS, PER CONSUMER (SELECT-COST-1, 2026-09-18): `vr.select()`
    // measured 16-17 ms per call and a desktop click paid the same, which at
    // 90 Hz is more than a frame for a trigger press. The four calls below are
    // charged separately — plus the Properties column's DEFERRED mount, which
    // lands in a later turn and no timer around this function can see — and
    // `editor.selectionCost()` reads them back.
    //
    // A RE-SELECTION IS NOT A NO-OP HERE, deliberately: three callers
    // re-select the node they already have precisely to REFRESH the panels
    // after changing the document under them (ReparentSceneNodeCommand's
    // undo and redo, material.apply), and the service's own contract says a
    // replace always re-emits. What makes it cheap is that the consumers
    // themselves build nothing when nothing changed — the column re-points
    // its blades (0.26 ms) instead of re-showing them — so the counter below
    // records honestly how many of these fan-outs really moved the primary.
    const bool primaryChanged = lastAppliedSelection.toStrongRef() != sceneNode;
    lastAppliedSelection = sceneNode.toWeakRef();
    selcost::noteSelection(primaryChanged);
    { selcost::Scope s(selcost::Viewport);   sceneView->setSelectedNode(sceneNode); }
    { selcost::Scope s(selcost::Properties); sceneNodePropertiesWidget->setSceneNode(sceneNode); }
    { selcost::Scope s(selcost::Hierarchy);  sceneHierarchyWidget->setSelectedNode(sceneNode); }
    { selcost::Scope s(selcost::Timeline);   animationWidget->setSceneNode(sceneNode); }
}

// The consumers that understand a SET: the outliner's selected rows and the
// viewport (outline, gizmo group, focus/orbit/floor). The properties panel and
// the timeline stay on the primary — multi-edit is out of scope for v1
// (EDITOR_MULTISELECT_SPEC §4).
//
// THIS RUNS ON EVERY SINGLE PICK TOO, which is why its two calls are charged
// like the four above (SELECT-COST-1's second read): `SelectionService::select`
// emits selectionChanged AND selectionSetChanged, so a plain click, a verb and
// a `vr.select` all write the viewport and the outliner twice — once with the
// primary, once with the set of one. `editor.selectionCost()` would otherwise
// call four consumers "the whole cost as the user pays it".
void EditorDocks::showSelectionSet(const QList<iris::SceneNodePtr> &nodes)
{
    { selcost::Scope s(selcost::SetViewport);
      if (sceneView) sceneView->setSelectedSet(nodes); }
    { selcost::Scope s(selcost::SetHierarchy);
      if (sceneHierarchyWidget) sceneHierarchyWidget->setSelectedSet(nodes); }
}

void EditorDocks::exportNode(const iris::SceneNodePtr &node, ModelTypes modelType)
{
    if (!node) return;

    // Dispatch a thumbnail request regardless of what happens,
    // This should finish in the time it takes to spawn a dialog and save
    // Since the object is already loaded in memory
    if (services && services->thumbnails) services->thumbnails->refreshObjectThumbnail(node->getGUID());

    QDateTime currentDateTime = QDateTime::currentDateTimeUtc();

    // The export is titled the name of the node + the current date time in UTC
    auto filePath = QFileDialog::getSaveFileName(
        mWindow,
        "Choose export path",
        QStringLiteral("%1_%2.%3").arg(node->getName(),
                                      QString::number(static_cast<time_t>(currentDateTime.toSecsSinceEpoch())),
                                      QLatin1String(assetshare::extension())),
        assetshare::fileFilter()
    );

    if (filePath.isEmpty() || filePath.isNull()) return;

    // THE VERB'S STAGE (node.exportArchive stages the same way) and the same
    // worker job, behind the progress dialog (EXPORT-THREAD-1).
    const auto result = bundleexportdialog::run(
        mWindow, services->sceneEdit->stageNodeExport(node, modelType), filePath, tr("Export"));
    if (result.canceled) return;
    if (!result.ok()) {
        // TOLD, not only logged — this was a silent void (the project export's
        // shape, exportSceneAsZip).
        irisLog(QStringLiteral("Export failed: %1").arg(result.error));
        if (!FirstRun::isDrivenSession())
            QMessageBox::warning(mWindow, tr("Export failed"),
                                 tr("%1 could not be exported: %2").arg(node->getName(), result.error));
    }
}

// THE PANELS FOLLOW THE SERVICES: the undo commands' refresh notifications, a
// paste's library import, an undo's repaint of the properties column.
void EditorDocks::followServices(StudioServices *svc)
{
    // The undo commands' refresh notifications (Phase 4: was
    // UiManager::sceneHierarchyWidget / ::propertyWidget reach-ins).
    connect(svc->sceneEdit, &SceneEditService::hierarchyChanged, this, [this]() {
        sceneHierarchyWidget->repopulateTree();
    });
    connect(svc->sceneEdit, &SceneEditService::nodeInserted, this,
            [this](const iris::SceneNodePtr &node) {
        if (sceneHierarchyWidget) sceneHierarchyWidget->insertChild(node);
    });
    connect(svc->sceneEdit, &SceneEditService::nodeRemoved, this,
            [this, svc](const iris::SceneNodePtr &node) {
        if (sceneHierarchyWidget) sceneHierarchyWidget->removeChild(node);
        // A node that has left the document cannot stay in the selection SET
        // (EDITOR_MULTISELECT_SPEC §2.1). The single selection was pruned by
        // the delete command's select(null); a set member three rows down was
        // not, and a stale member would keep an outline shell alive and feed a
        // dead node to the next group transform.
        if (svc->selection) svc->selection->remove(node);
    });
    connect(svc->sceneEdit, &SceneEditService::transformRefreshRequested, this, [this]() {
        if (sceneNodePropertiesWidget) sceneNodePropertiesWidget->refreshTransform();
    });
    connect(svc->sceneEdit, &SceneEditService::assetViewRefreshRequested, this, [this]() {
        assetWidget->updateAssetView(assetWidget->assetItem.selectedGuid);
    });
    connect(svc->sceneEdit, &SceneEditService::materialApplied, this, [this](const QString &) {
        sceneNodePropertiesWidget->refreshMaterial();
    });
    connect(svc->clipboard, &ClipboardService::assetsImported, this,
            [this](const QStringList &) {
        // A paste that imported library assets has changed the library.
        if (assetWidget)
            assetWidget->updateAssetView(assetWidget->assetItem.selectedGuid);
    });

    // AN UNDO REPAINTS THE PANEL (debt L6): every properties row is undoable
    // now, and the rows are the document's state on screen. One hook, deferred
    // by the panel itself, rather than a refresh callback on every command.
    svc->undo->setStackMovedHook([this]() {
        if (sceneNodePropertiesWidget) sceneNodePropertiesWidget->refreshFromDocument();
    });
}
