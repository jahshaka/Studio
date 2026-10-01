/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "irisgl/core/math/vec.h"
#include <QMainWindow>
#include <QActionGroup>
#include <QModelIndex>
#include <QDropEvent>
#include <QMimeData>
#include <QListWidgetItem>
#include <QDrag>
#include <QToolBar>
#include <QSharedPointer>
#include <QLabel>
#include <QCheckBox>
#include <QMenu>
#include <QHash>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include "irisgl/irisglfwd.h"
#include "irisgl/import/meshprewarm.h"
#include "thirdparty/qtawesome/QtAwesome.h"
#include "thirdparty/qtawesome/QtAwesomeAnim.h"
#include "ui/controls/fonticons.h"
#include "data/project.h"
#include "services/scenetemplate.h"
#include "modules/studiomodule.h"
#include "ui/ishellview.h"

namespace Ui {
    class MainWindow;
}

class AssetView;
/// Only ever held as a weak_ptr here (mEngineWatch) — the shell includes the
/// engine header in the .cpp, never in this one.
namespace jahshaka { namespace engine { class Engine; } }
class VrModule;
class PageHost;
class ActionHost;
class ModuleHub;
class ShellLifecycle;
class ShellView;
class ViewController;

class QPushButton;
class QStandardItem;
class QStandardItemModel;
class QTreeWidgetItem;
class QTreeWidget;
class QIcon;
class QUndoStack;
class QToolButton;
class QOffscreenSurface;

class TransformSlidersUi;
class LightLayerWidget;
class ModelLayerWidget;
class TorusLayerWidget;
class SphereLayerWidget;
class TimelineWidget;
class KeyFrameWidget;
class AnimationWidget;
class TexturedPlaneLayerWidget;
class WorldLayerWidget;
class EndlessPlaneLayerWidget;

class MaterialWidget;
class TransformGizmo;
class AdvancedTransformGizmo;
class TransformWidget;

class IEditorViewport;
class SceneHierarchyWidget;
class PlayerWidget;

class EditorCameraController;
class SettingsManager;
class ShortcutRegistry;
class PreferencesDialog;
class AboutDialog;

class JahRenderer;

class ProjectManager;

class GizmoHitData;
class AdvancedGizmoHandle;
class MaterialPreset;
class AssetWidget;

// services (src/services/) — the shell constructs these and delegates to them
class MaterialPreviewService;
struct StudioServices;
class UndoService;
class SelectionService;
class PlaybackService;
class SessionMarkers;
class PerfSampler;
class PlayerService;
class ProjectService;
class SceneEditService;
class ClipboardService;
class ThumbnailService;
class AssetService;
// class SceneNodePropertiesWidget;

class AssetModelPanel;
class AssetMaterialPanel;


#include "ui/panels/scenenodepropertieswidget.h"


enum class SceneNodeType;

#include "shell/spaces.h"


// The editor's panels, in `widgetStates` order. CONSOLE is APPENDED (lane
// SPACE-2, 2026-09-14): the script console is a dock of the bottom area again —
// the third tab beside Assets and the Timeline — so it needs the same record
// every other panel has (the space switch hides it with the rest, its title-bar
// X closes it for good, a restart brings back the tab the user left open). It
// is deliberately NOT one of the Toggle Widgets dialog's buttons: Ctrl+` is the
// console's switch, and "Restore All" restoring a console nobody asked for is
// not what that button means.
enum class Widget
{
	HIERARCHY,
	PROPERTIES,
	ASSETS,
	TIMELINE,
	PRESETS,
	CONSOLE
};

#include <QJsonObject>
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "services/surfaceplacement.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/assets/texture2d.h"

class Database;
class Project;
class MainWindow : public QMainWindow
{

    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void stopAnimWidget();

    /// The editor viewport (engine-backed, or the headless stand-in).
    IEditorViewport *viewport() { return sceneView; }
    /// The outliner panel. Public because the OUTLINER is the authority on
    /// visible row order — editor.selectRange has to ask it what lies between
    /// two rows (EDITOR_MULTISELECT_SPEC §2.7); null in headless sessions.
    SceneHierarchyWidget *hierarchyPanel() const { return sceneHierarchyWidget; }
    /// The engine-up boot of --engine-selftest, --script, --scripts and
    /// --mcp-port: a new default scene (newScene) shown through the PRODUCT's
    /// editor entry (enterEditorSpace) — there is no second way onto the page.
    /// False (with a reason) if the engine is not running or the viewport
    /// cannot draw. currentSpace is not moved: a scripted boot still reports
    /// the Desktop until a script switches space.
    bool enterEditorOnNewScene(QString &why);
    /// The product's editor leave (switchSpace's EDITOR case): the dock
    /// snapshot, then the viewport's end().
    void leaveEditorSpace();
    void goToDesktop();
    /// If the on-screen View could not be created, say so and land the user on
    /// a page that works. Returns true when it bounced — callers must then stop
    /// whatever they were doing (STATS_OVERLAY_SPEC.md §6.4).
    bool bounceIfViewportIsDead();
    /// WHY THE LAST SPACE SWITCH DID NOT HAPPEN, in the user's words — empty
    /// unless the most recent attempt was refused (SMOKE-FIX-1). Cleared at the
    /// start of every attempt, so it can only ever describe the last one; read
    /// by app.space() so a verb's refusal carries the same sentence as the
    /// toast the user saw.
    QString lastSpaceRefusal() const { return spaceRefusal; }
    /// THE EDITOR TOOLBAR'S CONTROLS, as state: one entry per action with its
    /// objectName (minus the `action` prefix, lower-cased), whether it is on
    /// screen and whether it can be used. Read by `editor.toolbar()`.
    ///
    /// The toolbar is a UI surface with no reading at all until now, which is
    /// why "the Save button is hidden on every default install" (owner,
    /// 2026-09-18) could be true for as long as it was: nothing could ask.
    QVariantList toolbarActions() const;
    /// What the View Options menu's checkmarks show ({grid, lightWires, stats,
    /// physicsDebug}) — editor.overlays().menu, the proof they follow the state.
    QVariantMap viewOptionChecks() const;
    /// The ONE place the frame-stats readout is switched: F3, the View Options
    /// row, the Preferences checkbox and editor.setOverlays({stats}) all land
    /// here, and it persists `show_fps` (STATS_OVERLAY_SPEC.md §5.3).
    void setShowFrameStats(bool on);
    void setupUndoRedo();

    /// THE size of the header's glyph icons (Publish / Help / Preferences) —
    /// one font for all three, so they cannot drift apart again.
    QFont headerGlyphFont() const;

	WindowSpaces getWindowSpace();
	/// Opens a library asset in the module that owns its kind, switching to
	/// that module's space (AVATAR_ASSET_SPEC §5.5). Called by the Assets page
	/// and the editor's asset drawer; both go through the module's VERB, never
	/// through its widgets.
	void openAssetInModule(const QString &guid, const QString &moduleId, const QString &scope);
	/// Instantiates an avatar ASSET into the open scene through the module's
	/// `avatar.spawn` verb — the drawer's "Add to Scene" and the viewport's
	/// drop of an avatar row take the same path a script does.
	/// `hasPosition` false spawns in front of the editor camera.
	void spawnAvatarAsset(const QString &guid, const iris::Vec3 &position, bool hasPosition);
	/// Assigns an ANIMATION library row to a character already in the scene —
	/// the viewport's drop of a clip tile onto a body, through the module's
	/// `avatar.loadClip` verb (S9). A null node, or a node the verb refuses,
	/// says so in a viewport toast instead of doing nothing silently.
	void assignAnimationAsset(const QString &guid, const iris::SceneNodePtr &node);
	void deselectViewports();

    /// Enters or leaves the Player's VR mode — the toolbar icon, the Player
    /// page's button and the `vr.toggle()` verb all end up here, and all of
    /// them go through PlayerService so there is one implementation.
    void toggleVrMode();
    /// Icon + tooltip + enabled state of the VR actions, from the live session.
    void refreshVrUi();
    /// Says on screen why a VR toggle did not start (the reason is the verb's own).
    void showVrRefusal(const QString &reason);

	/// The editor camera's controls: the canonical views, the projection
	/// toggle and the camera switcher (shell/viewcontroller.h).
	ViewController *views() const { return viewController; }
    /// WHAT A CLOSE IS FOR (VIEW-REBUILD-1, 2026-09-21).
    ///
    /// `ToDesktop` is a close the user asked for: the world goes and the window
    /// lands on the Desktop, which is the only page left that means anything.
    ///
    /// `ReopenInPlace` is the FIRST HALF OF AN OPEN — every close-then-open
    /// caller (project.open, project.openAsync, a desktop tile, the
    /// import-and-open) tears the current world down through this same function
    /// before pointing the project at the next one. Measured on the rig
    /// (spikes/view-rebuild-1/): the space switch that ends a ToDesktop close
    /// hid the editor PAGE — a native X ancestor of the viewport's own window,
    /// because Qt gives every ancestor of a WA_NativeWindow widget a window of
    /// its own — so the viewport was UNVIEWABLE for 499-2,973 ms of an open in
    /// place, its rect walked five times as the docks came back, and nothing
    /// the engine drew (a cover, STALE-VIEW-1's background, the first frame of
    /// the new world) could reach a window that is not on screen. The user saw
    /// the app's watermark. The teardown is identical either way; only the page
    /// stays.
    enum class CloseIntent { ToDesktop, ReopenInPlace };
    void closeProject(CloseIntent intent);
    void switchSpace(WindowSpaces space, bool force = false);
    /// ENTERING THE EDITOR PAGE, the whole of it, in one place (VIEW-REBUILD-1).
    ///
    /// This is switchSpace's EDITOR case: the page, the docks, the toolbars,
    /// the edit mode, the views label and `sceneView->begin()`. It lives on its
    /// own because a load IN PLACE never leaves the editor — switchSpace
    /// returns at once when the space is already current — and the reveal still
    /// has to dress the editor for the world that just arrived. Two callers,
    /// one body, so they cannot drift.
    ///
    /// Returns false when the viewport cannot draw at all and the window has
    /// already been sent back to the Desktop (bounceIfViewportIsDead): the
    /// caller must not go on dressing a page nobody is on.
    bool enterEditorSpace();
	void updateTopMenuStates(WindowSpaces activeSpace);

    bool eventFilter(QObject *obj, QEvent *event);

    virtual void closeEvent(QCloseEvent *event);

    void setSettingsManager(SettingsManager* settings);
    SettingsManager* getSettingsManager();

    /// The VR icon follows the session (per frame, a cached bool unless the
    /// process can do VR at all).
    void followVrSession();

    iris::ScenePtr getScene();

    /// Selection read-back (SCRIPTING_SPEC §1.2) — delegates to SelectionService.
    iris::SceneNodePtr selectedSceneNode() const;

    /// The service layer (APP_ARCHITECTURE_AUDIT §3.3). Owned by the window;
    /// valid from the end of the constructor.
    StudioServices *studioServices() const { return services; }

    /// Blob-only save (SCRIPTING_SPEC §1.6.2): writes the scene into the
    /// project row — with a viewport thumbnail when one is available — and
    /// NEVER silently no-ops the way saveScene() does when the viewport is
    /// uninitialized (headless scripts must be able to save). False when no
    /// scene/project is open.
    bool saveProjectBlob();

    /// editor.importAssets verb: starts the interactive THREADED import (the
    /// project panel's ImportBatchRunner + progress dialog) for the given
    /// files. Returns false when no import UI exists or a batch is running.
    bool startInteractiveImport(const QStringList &files);

    /// The ASSETS PAGE, or null in a session without one (headless, a page-less
    /// host). The `assets.select`/`preview`/`fly` verbs drive it — the shell
    /// owns the widget, the verbs own the capability (SCRIPTING_SPEC §2.3).
    /// The Assets page, built on first use (D11-LIBRARY-SCALE): the verbs that
    /// drive it and the space switch both come through here.
    AssetView *assetsPage() { return ensureAssetsPage(); }
    AssetView *ensureAssetsPage();
    /// The editor's ASSET TRAY panel (the Assets tab of the bottom tray), or
    /// null before the editor is built. editor.trayAssets reads it.
    AssetWidget *assetTray() const { return assetWidget; }
    /// The LIBRARY materials tray (editor.activateMaterialTile).
    AssetMaterialPanel *materialTray() const { return assetMaterialPanel; }

    /// One toast, reused, for every transient viewport readout (snap size, fly
    /// speed) — and for a gesture the viewport has to REFUSE: a material or an
    /// image dropped on a LOCKED node (lane SPACE-2 item 5), which is why the
    /// viewport calls it and it lives out here with the rest of the shell's
    /// public surface.
    void showViewportToast(const QString &title, const QString &text);
    /// Re-reads the properties panel from the document (the edit gate's repaint:
    /// a refused row is showing a value the document does not hold).
    void refreshPropertiesFromDocument();

    // ---- the editor's BOTTOM AREA (owner, 2026-09-14, lane SPACE-2) --------
    // ONE tab bar along the bottom of the editor: "Assets" (the asset browser),
    // "Timeline" (the keyframe panel) and "Console" (the script console) when
    // it is turned on. All three are DOCKS of the editor's nested window,
    // tabified into one group whose tab bar sits at the TOP of the area
    // (setTabPosition(North) in setupDockWidgets) — the bar used to be Qt's
    // default SOUTH one, a 20 px strip along the very bottom edge of the
    // window under a tray that carried its own tab bar at the top, which is how
    // the Timeline came to read as "gone" (owner report, 2026-09-14).
    //
    // Ctrl+` and the `editor.tray` / `editor.trayState` verbs go through these
    // — the key and the verb are the same code path, which is what lets a
    // suite assert what the key did.

    /// Which tab of the bottom area is in front: "assets", "timeline" or
    /// "console". Empty with no bottom area (a --headless run has no window).
    QString trayTab() const;
    /// The bottom area's tabs, in tab-bar order — the names trayTab() can
    /// return, for the panels that are open right now.
    QStringList trayTabs() const;
    /// Shows a tab by name ("assets" | "timeline" | "console"). Naming the
    /// console shows the dock if it was closed, raises it and
    /// (focusConsoleInput) puts the keyboard in the console's input line.
    /// False for an unknown name.
    bool setTrayTab(const QString &tab, bool focusConsoleInput = true);
    /// Whether the Console tab is in the tab bar at all.
    bool isConsoleTabVisible() const;
    /// Adds/removes the Console tab (shows/closes its dock). Showing it selects
    /// it; hiding it leaves the Assets and Timeline tabs as they were.
    void setConsoleTabVisible(bool visible, bool focusInput = true);
    /// Whether the console's INPUT line has the keyboard right now — the half
    /// of Ctrl+` that a console you still have to click does not deliver.
    bool isConsoleInputFocused() const;
    /// Whether the tray widget itself is on screen (the View menu can hide it).
    bool isTrayVisible() const;
    /// OPENS OR CLOSES AN EDITOR PANEL by its script name ("hierarchy",
    /// "properties", "presets", "assets", "timeline", "console"). The ONE
    /// implementation: the Toggle Widgets dialog's buttons, the `editor.panel`
    /// verb and the tests all call it, and closing goes through the dock's own
    /// close() — the title-bar X's gesture — so every route leaves the same
    /// state behind. False for a name that is not a panel.
    bool setPanelOpen(const QString &name, bool open);
    /// Whether that panel is open (it may still be behind another tab).
    bool isPanelOpen(const QString &name) const;
    /// BRINGS AN OPEN PANEL TO THE FRONT OF ITS TAB GROUP (`editor.panel`'s
    /// `raise`, lane STUDIO-SMALL-A; the gap SELECT-COST-1 found). Opening a
    /// panel already raises it, so this is the gesture for a panel that is
    /// OPEN but tabbed BEHIND another — the one thing a script could not do.
    /// A closed panel cannot be in front: raising one is a no-op (false), and
    /// `{open: true, raise: true}` is how a caller says "open it and show it".
    /// Also records the bottom group's front tab, so the next page switch
    /// brings back what the caller asked for rather than what it interrupted.
    bool raisePanel(const QString &name);
    /// That panel's dock, or null.
    QDockWidget *panelDock(const QString &name) const;
    /// THE RIGHT COLUMN'S TAB ("world" | "selection", PROPERTY_FILTER_SPEC §2).
    /// The one implementation behind the tab bar, the Ctrl+Shift+P toggle and
    /// the `editor.propertiesTab` verb.
    QString propertiesTab() const;
    /// The right column's per-tab FILTER (`editor.propertiesFilter`). An empty
    /// `tabName` means the tab on screen; an unknown one is refused (false /
    /// an empty string).
    QString propertiesFilter(const QString &tabName = QString()) const;
    /// Whether `tabName` names a tab ("world" / "selection"); an empty name is
    /// "the tab on screen" and is always valid.
    bool isPropertiesTab(const QString &tabName) const;
    bool setPropertiesFilter(const QString &tabName, const QString &text);
    /// {rows the filter kept, rows it removed} for that tab's last apply.
    QPair<int, int> propertiesFilterCounts(const QString &tabName = QString()) const;
    /// THE COLUMN'S OWN ACCOUNT OF ITSELF (`editor.properties`): one entry per
    /// row that tab has mounted, in column order.
    QVariantList propertyRows(const QString &tabName = QString()) const;
    /// ONE ROW BY ITS STABLE KEY (`editor.propertyRow`): its listing plus its
    /// control's reading (PropertyRows::readRow), after — when `drive` — the
    /// gesture a user makes on it (PropertyRows::driveRow). An empty map with
    /// `error` set when the tab, the key or the gesture is refused.
    QVariantMap propertyRow(const QString &tabName, const QString &key, bool drive,
                            const QVariant &value, QString *error);
    /// WHAT THE COLUMN HAS COST (`editor.propertiesStats`): mounts, material
    /// refills vs rebuilds, the mounted row count, and whether a mount is owed.
    /// Reads nothing into existence — it never settles a pending mount.
    QVariantMap propertiesStats() const;
    /// Raises a tab by name; false for a name that is not one.
    bool setPropertiesTab(const QString &name);
    /// Whether `dock` is the tab in FRONT of its group (and not closed). Qt
    /// offers no such accessor: a tabified dock that is not current is shown
    /// and parked off-screen, which is the reading this uses — the same one
    /// QDockWidget itself uses to emit visibilityChanged. A dock that shares a
    /// bar with nothing is trivially in front of its own group.
    static bool isFrontTab(const QDockWidget *dock);
    /// The bottom area's dock whose geometry IS the area: the tab in FRONT.
    /// The other tabs are parked off-screen by Qt, so this is the only honest
    /// answer to "where is the bottom area and how tall is it".
    QDockWidget *bottomFrontDock() const;
    /// The y of the bottom area's TOP EDGE as the user sees it — the group's
    /// tab bar, which sits above the dock, when there is one.
    int bottomAreaTop() const;
    /// The editor's bottom Tray height (owner 2026-09-12, editor.tray({height})).
    bool setTrayHeight(int height);
    /// THE PRESETS LINE: the right column's Presets panel starts on the SAME
    /// horizontal line as the bottom Tray (owner 2026-09-12). Re-run whenever the
    /// Tray is resized, so the two panels move together.
    void alignPresetsWithTray(int retries = 3);
    /// Ctrl+` : show + focus the Console tab, or hide it when it is already the
    /// tab in front. The ShortcutRegistry entry calls exactly this.
    void toggleScriptConsole();

    /// THE ACTIVE PAGE'S COLUMNS (smoke S1). Every page's left and right
    /// columns are sized from ui/style/panelmetrics.h; this reports what they
    /// actually came out as, per page, so `app.columns` can gate the law
    /// instead of trusting each page to have used the constant. `valid` is
    /// false for a space that has no columns (Desktop, Player).
    using ColumnMetrics = IShellView::ColumnMetrics;
    ColumnMetrics activeColumns() const;

    /// THE EDITOR'S DOCKS, MEASURED (lane SPACE-1, 2026-09-14). One entry per
    /// dock of the nested `viewPort` QMainWindow — the name restoreState
    /// matches on, whether it is on screen, and the rectangle it occupies —
    /// so `app.docks` can assert "the editor came back with its panels"
    /// instead of a human looking at the window. A dock reports visible only
    /// while the editor page is the page on screen, which is the honest
    /// reading: a dock on a hidden page is not on screen.
    QVariantList dockReport() const;

    /// THE APP'S DIALOGS, BY NAME (theme sweep, lane 16 — shell/mainwindowdialogs.cpp):
    /// what app.dialogs / app.dialog open for a script. The theme walk
    /// (app.styleSheets) only sees widgets that exist, and a dialog built on
    /// demand exists only while it is open. Every entry opens NON-modally (a
    /// verb cannot sit in exec()); openDialog returns nullptr for an unknown
    /// name, closeDialog false when that dialog was not open.
    ///
    /// `options` is per-dialog and almost always empty; `importSettings` reads
    /// {guid, settings, accept} from it (SPECS/IMPORT_DIALOG_SPEC.md §8) so a
    /// script and an MCP client can drive the import decision the way a person
    /// does, through the dialog. Anything a dialog wants to REPORT back —
    /// `importSettings` answers with the record it holds — lands in `extra`.
    QStringList dialogNames() const;
    QWidget *openDialog(const QString &name, const QVariantMap &options = QVariantMap(),
                        QVariantMap *extra = nullptr);
    bool closeDialog(const QString &name);
    bool isDialogOpen(const QString &name) const;

    /// THE IMPORT DECISION, REOPENED (SPECS/IMPORT_DIALOG_SPEC.md §8/§5): the
    /// import-settings dialog for a library asset, pre-filled from
    /// `assets.importSettings(guid)` and committing through `assets.reimport`.
    /// The ONE entry point behind the Assets page's "Import settings…" button,
    /// the tray's and the page's "Reimport…" rows and
    /// `app.dialog('importSettings', {guid})` — pages never reach for the
    /// scripting layer themselves. Returns null when the asset has no import
    /// record to reopen.
    ///
    /// `errorOut` decides HOW that refusal is delivered, and it matters: a
    /// button press gets a message box (errorOut null), a VERB gets the string
    /// (errorOut set) — a verb that stopped on a modal box would hang the run
    /// and everything queued behind it, which is exactly the defect
    /// import.shutdown caught on the interactive-import path.
    class ImportSettingsDialog *openImportSettings(const QString &guid,
                                                   QString *errorOut = nullptr);

    /// Parameterised node verbs for the scripting API: same behaviour as the
    /// deleteNode()/duplicateNode() context-menu slots but on an explicit node
    /// (and duplication is undoable via AddSceneNodeCommand).
    bool deleteSceneNode(iris::SceneNodePtr node);
    iris::SceneNodePtr duplicateSceneNode(iris::SceneNodePtr node);

    /// The scripting engine (created in the ctor; modules see the world through
    /// ScriptHost). Null only before the ctor finishes.
    class ScriptEngine *scripting() { return scriptEngine; }

    /// The MCP endpoint and the Claude chat (scripting/claude/claudeassistant.h):
    /// created in the ctor, the endpoint OFF by default; --mcp-port=N starts it.
    class ClaudeAssistant *claudeAssistant() const { return assistant; }

    /**
     * Applies material preset to active scene node and refreshes material property widget
     * @param preset
     */

    void favoriteItem(QListWidgetItem *item);
    void refreshThumbnail(const QString &guid);
    void refreshThumbnail(QListWidgetItem *item);

    QString originalTitle;

    void addNodeToActiveNode(QSharedPointer<iris::SceneNode> sceneNode);
    void addNodeToScene(QSharedPointer<iris::SceneNode> sceneNode, bool ignore = false);

    // (evalShadowMapType / getLightTypeFromName / createLight — a SECOND scene
    // reader that lived here, knew neither Area nor Sky nor the sun rows, and
    // had no caller anywhere in the tree — are DELETED, SKY_LIGHT_SPEC.md §5
    // item 5. SceneReader is the one reader.)

    /// Rewrites the four read-only Gameplay rows in the Shortcut Registry from
    /// the live InputMap (AVATAR_LOCOMOTION_SPEC §8.2). Called at setup and
    /// again whenever `input.bind` changes a binding — public for that one
    /// caller; everything else goes through the registry's own API.
    void refreshGameplayShortcutRows();

    /// One pass of the scene-issue scanner plus the show/hide decision for the
    /// viewport's error bar. Driven by the 1 Hz timer and by every space switch;
    /// PUBLIC because `editor.issueBar()` runs it before reporting, so a script
    /// reads a settled answer instead of racing the timer.
    void updateSceneIssues();
    /// What the error bar is currently showing, for that verb: whether it
    /// exists, whether the editor is the active space, whether it is on screen
    /// and how many rows it has.
    QVariantMap sceneIssueBarState() const;

private:

    // menus
    void setupFileMenu();

    void removeScene();
    void setScene(QSharedPointer<iris::Scene> scene);

    /// The window state changed: immersive fullscreen (ViewController) watches
    /// the state it does not own and puts the chrome back.
    void changeEvent(QEvent *event) override;


    void updateCurrentSceneThumbnail();

public slots:
    /// File > Export: the OPEN project, through a save dialog.
    void exportSceneAsZip();
    /// A desktop tile's Export: project `guid`, through a save dialog. The
    /// current project is never re-pointed (CREATE-GAP-1's fix round).
    void exportProjectWithDialog(const QString &guid, const QString &name);

public:
    /// THE ONE EXPORT OF A PROJECT BY GUID (threaded, the window's archiver):
    /// what the tile's Export and `desktop.exportTile` both run. Reads the
    /// project's ROW; the current project, the open world and its autosave are
    /// untouched — except that exporting the project that IS open first saves
    /// it, so the archive carries what is on screen. False (and `why`) when an
    /// archive operation is already running or the project is unknown.
    bool startProjectExport(const QString &guid, const QString &zipPath, QString *why = nullptr);

public slots:

    void setupDockWidgets();
    void setupViewPort();
    void setupDesktop();
    void setupToolBar();
    void setupShortcuts();

    //scenegraph
    /// Adds the primitive the sender QAction names (its `data()` is the row's name
    /// in src/data/primitives.h). It replaced thirteen one-line slots — owner
    /// review R6; the Add > Primitive menu is built from the table.
    void addPrimitiveFromAction();
    void addEmpty();
    void addCamera();
	void addMaterialMesh(const QString &path = "", bool ignore = false,
	                     iris::Vec3 position = iris::Vec3(), const QString &guid = QString(),
	                     const QString &name = QString(),
	                     surfaceplacement::Placement placement = surfaceplacement::Placement::Pivot);
    void addAssetParticleSystem(bool ignore, iris::Vec3 position, QString guid, QString assetName);

    //context menu functions
    void duplicateNode();
	void createMaterial();
	void exportNode(const iris::SceneNodePtr &node, ModelTypes modelType);
    void deleteNode();

    void addPointLight();
    void addSpotLight();
    void addDirectionalLight();
    void addAreaLight();
    void addSkyLight();

    /// Adds an image-less decal (DECALS_SPEC): the node draws its wire box until
    /// an image is picked in the Decal panel or dropped on it from the bin.
    void addDecal();

    void addParticleSystem();


    void sceneTreeCustomContextMenu(const QPoint&);
    void sceneTreeItemChanged(QTreeWidgetItem* item,int column);

    void sceneNodeSelected(QTreeWidgetItem *item);
    void assetItemSelected(QListWidgetItem *item);
    void sceneNodeSelected(iris::SceneNodePtr sceneNode);

	void saveScene(const QString &filename, const QString &projectPath);
    void saveScene();

	void toggleDockWidgets();
    void showPreferences();
    /// `kind` = the New Scene dialog's Template drop-down and
    /// `project.create`'s `{template}` (services/scenetemplate.h says what
    /// each template holds).
    void newScene(SceneTemplate kind = SceneTemplate::Basic);
    /// The grid, light-wire and physics-debug overlays back to EditorData's
    /// defaults — newScene and the create run, one body.
    void resetOverlaysToDefaults();

    /// Creates the world of the project `guid` (a row createProjectShell has
    /// just made) named `filename` in `projectPath`: closes the world that is
    /// open — its autosave lands in ITS OWN row — then points the current
    /// project at `guid` and runs the create.
    void newProject(const QString &guid, const QString &filename, const QString &projectPath,
                    SceneTemplate kind = SceneTemplate::Basic);
    /// THE SAME CREATE, WITHOUT THE DRAIN (OPEN_COVER_SPEC §2 C/§4,
    /// `project.createAsync`): the slices are queued and this returns at once.
    /// The caller polls `isOpeningProject()` — one runner serves both routes.
    void newProjectAsync(const QString &guid, const QString &filename,
                         const QString &projectPath, SceneTemplate kind = SceneTemplate::Basic);
    /// The BLOCKING open: returns with the world open, which is the contract
    /// `project.open()` and every headless script are written against.
    ///
    /// Its MODEL PARSES run on a worker while this thread pumps
    /// (prewarmModelsPumped; OPEN-ASSIMP-1) — measured 1 086 ms of assimp for
    /// the Matcaps sample, 986 ms for World Background, all of it on the UI
    /// thread before this — and the install stages then run back to back as
    /// they always have.
    void openProject(bool playMode = false);
    /// The RESPONSIVE open (services/sceneopenrunner.h): the same worker parse,
    /// and the install run one slice per event-loop turn so the window keeps
    /// pumping. Returns immediately; the open completes through the event loop.
    /// What a tile click uses.
    void openProjectAsync(bool playMode = false);
    /// THE DESKTOP PAGE, for the verbs that drive what it owns — today the
    /// sample browser's open (project.openSample). Borrowed, never null in a
    /// windowed session, and owned by this window.
    ProjectManager *projectPage() const { return pmContainer; }
    /// True while an asynchronous open is in flight.
    bool isOpeningProject() const;
    /// THE OPEN'S SLICE-BOUNDARY COUNTERS (lane OPEN-FRAMES-1), reported by
    /// app.openStats(). `openSliceBoundaries()` is how many times the install
    /// crossed a slice boundary in this window's life — where the renderer is
    /// driven so that the open never depends on the app's render tick — and
    /// `openSliceBoundaryFrames()` how many of those crossings really rendered
    /// a frame (the rest made the engine's bare resource advance, which is what
    /// a session with no viewport can do). Monotonic: only differences mean
    /// anything.
    unsigned openSliceBoundaries() const;
    unsigned openSliceBoundaryFrames() const { return openSliceBoundaryFrameCount; }
    /// Runs an in-flight threaded open TO COMPLETION before the caller does
    /// anything else, with the event loop pumped (user input excluded).
    ///
    /// A verb that is about to close the project and point it at another world
    /// MUST call this FIRST: the runner's remaining slices read the project at
    /// SLICE time (readProjectScene asks the Database for
    /// project->getProjectGuid()'s blob), so an open finished after the
    /// pointers moved would install a hybrid — the old session's assets, the
    /// new world's blob, and a prewarm for neither, every mesh of it parsed on
    /// this thread. Returns true when nothing is (or is still) in flight.
    bool waitForOpen();

    void closeProject();

    /// Takes the editor's panels down for a page that is not the editor.
    void hideEditorPanels();

    /// THE NEW-SCENE TEMPLATES (WORLD-MODEL-1; services/scenetemplate.h).
    ///
    /// Basic and World: the sun (a directional light), the Sky Light, shadows
    /// on, the Epic world mode and the REALISTIC real-time sky with the sun
    /// following the atmosphere (LightNode::followsAtmosphere's own default),
    /// standing on ordinary cube floors — one "Floor" for Basic, a 5 x 5
    /// "World Floor" group for World.
    ///
    /// Empty is NOTHING: a root node, the Epic world mode, a black
    /// single-colour sky (the document's "no sky") and no lights.
    iris::ScenePtr createDefaultScene(SceneTemplate kind = SceneTemplate::Basic);

    void useFreeCamera();
    void useArcballCam();

    void useLocalTransform();
    void useGlobalTransform();

    /// The gizmo transform space as the verb surface spells it: "local" |
    /// "global" (editor.gizmoSpace / editor.setGizmoSpace, 2026-09-06
    /// verb-coverage audit F12). Reading goes to the viewport — the gizmos own
    /// the state — and writing goes through the two slots above so the
    /// toolbar's Global/Local buttons follow a scripted switch.
    QString gizmoTransformSpace() const;
    bool applyGizmoTransformSpace(const QString &space);

    /// The physics debug drawer with the menu's checkmark kept in sync
    /// (editor.setOverlays({physicsDebug}) — F11). The action's toggled()
    /// signal drives toggleDebugDrawer, so this is one path, not two.
    void setPhysicsDebugOverlay(bool on);


    void updateSceneSettings();

    void undo();
    void updateWindowTitle();
    void redo();

    /// Ctrl+Z / Ctrl+Shift+Z, routed to whichever edit stack the ACTIVE SPACE
    /// owns (the ModuleHub's edit targets): the Materials space owns the
    /// graph's (owner decision, deep audit 2026-09 area 1), every other space
    /// the editor's. The registry entries "edit.undo"/"edit.redo" and the Edit
    /// menu/toolbar actions all call these — never undo()/redo() directly — so
    /// there is exactly one claimant for the chord and one place the routing
    /// rule lives.
    void undoActiveSpace();
    void redoActiveSpace();
    /// Ctrl+A: a focused text entry owns the chord; otherwise the active
    /// space's edit target (EDITOR_MULTISELECT_SPEC §8.7).
    void selectAllActiveSpace();

    void takeScreenshot();
    void toggleLightWires(bool state);
    void toggleGrid(bool state);
    /// The View Options checkmarks := the viewport's overlay state (the one
    /// owner; driven by EditorViewportEvents::overlaysChanged).
    void syncOverlayChecks();
    void toggleDebugDrawer(bool state);

signals:
	/// An asset was REIMPORTED through the import-settings dialog: its bake,
	/// its metadata block and every placed instance of it have changed
	/// (SPECS/IMPORT_DIALOG_SPEC.md §5). The Assets page and the project tray
	/// re-read the row's size line and thumbnail on it.
	void assetReimported(const QString &guid);

public slots:
    // public for the scripting API (editor.play()/stop() set the mode
    // explicitly instead of toggling the play button)
    void enterEditMode();
    void enterPlayMode();
    /// Re-reads CameraSpeed into the toolbar's speed button and its popover.
    /// Public and a SLOT because editor.cameraSpeed invokes it by name (the
    /// verb owns the value, the toolbar is only a view of it) and because the
    /// viewport's wheel gesture routes here through
    /// EditorViewportEvents::cameraSpeedChanged.
    void syncCameraSpeedUi();

    /// The three gizmo modes with the toolbar following (the keys, the buttons
    /// and IShellView::applyGizmoMode — editor.setGizmoMode and the headset).
    void translateGizmo();
    void rotateGizmo();
    void scaleGizmo();

private slots:
    void cycleGizmoMode();

    void onPlaySceneButton();

	/// Shrinks the window to the screen it is about to appear on, keeping the
	/// authored .ui size as the preferred one. Called ONLY when there is no
	/// stored geometry to restore — a first run — so a saved size the user
	/// chose is never second-guessed.
	void fitToScreen();

private:
    void setupServices();
    /// What the edit chords mean on the EDITOR space (the ModuleHub asks for
    /// it each time one fires): the selection SET, the clipboard, the scene's
    /// undo (EDITOR_MULTISELECT_SPEC §2.6).
    EditTarget editorEditTarget();
    void copyEditorSelection();
    void cutEditorSelection();
    void pasteIntoEditor();
    /// The active space's name, as the hub and the action host key it.
    QString currentSpaceId() const { return spaces::id(currentSpace); }

    // ---- the open, in stages (shared by the synchronous and threaded paths) --
    /// Cover up + tear the previous world down. Always first.
    /// The sliced CREATE (OPEN_COVER_SPEC §2 C) — the same runner, the same
    /// stage order. `newProject` is this plus the pumped drain.
    void startCreateRun(const QString &guid, const QString &filename, const QString &projectPath,
                        SceneTemplate kind);
    /// Builds the open/create runner and its slice boundary, once per window.
    void startOpenRunnerIfNeeded();
    void openStageBegin();
    /// Read the document (optionally out of a worker's prewarm), bind it to
    /// the viewports and the panels that follow the scene. The threaded open
    /// runs the two halves on separate turns — reading is the biggest slice
    /// left once the model parses are on the worker.
    void openStageReadDocument(bool playMode, const iris::MeshPrewarmPtr &prewarm);
    void openStageRead(const iris::MeshPrewarmPtr &prewarm);
    void openStageBind(bool playMode);

    iris::ScenePtr openPendingScene;
    class EditorData *openPendingEditorData = nullptr;
    /// The asset panel rebuild + undo bookkeeping.
    void openStagePanels();
    /// The page switch, the root selection and autoplay. Always last.
    void openStageReveal(bool playMode);

    /// Plans and STARTS the THREADED open (the worker parse + the install
    /// slices, one per event-loop turn). openProjectAsync's whole body.
    void startOpenRun(bool playMode);

    /// Every model file the opening project needs, resolved on the thread that
    /// owns the database connection. Shared by both open paths.
    QStringList plannedOpenModelPaths();

    /// The synchronous open's parse: the plan resolved here, the FILES read on
    /// a worker, this thread pumping (user input excluded) until it is done.
    /// Never returns null; an empty prewarm simply means nothing to parse.
    iris::MeshPrewarmPtr prewarmModelsPumped();

    class SceneOpenRunner *openRunner = nullptr;
    /// See openSliceBoundaryFrames(). Counted here rather than in the runner
    /// because only the shell knows whether its viewport drew.
    unsigned openSliceBoundaryFrameCount = 0;

    void applySelectionToUi(iris::SceneNodePtr sceneNode);
    /// The primary this fan-out last applied — for the honest "did the
    /// selection really change" count behind `editor.selectionCost()`. Weak:
    /// it is an identity, never dereferenced, and a deleted node must not be
    /// kept alive (or confused with a new one at the same address).
    QWeakPointer<iris::SceneNode> lastAppliedSelection;
    void applySelectionSetToUi(const QList<iris::SceneNodePtr> &nodes);
    /// The widget fan-out for a selection change (viewport, properties,
    /// hierarchy, timeline) — driven by SelectionService::selectionChanged.
    /// The play-button chrome halves of the old enterEditMode/enterPlayMode —
    /// driven by PlaybackService's mode signals.
    void applyEditModeUi();
    void applyPlayModeUi();


    Ui::MainWindow *ui;
    IEditorViewport* sceneView = nullptr;
	PlayerWidget* playerView = nullptr;
	/// The player's engine backend, or null in headless runs. Held so
	/// setupServices can hand it to PlayerService (verb-coverage audit F1).
	class EnginePlayerView* playerBackend = nullptr;

    QWidget *container = nullptr;
    EditorCameraController* camControl;

    QSharedPointer<iris::Scene> scene;



    AnimationWidget* animWidget = nullptr;


    SettingsManager* settings;
    PreferencesDialog* prefsDialog;
    ShortcutRegistry* shortcutRegistry = nullptr;
    AboutDialog* aboutDialog;

    QActionGroup* transformGroup = nullptr;
    QActionGroup* transformSpaceGroup = nullptr;
    QActionGroup* cameraGroup = nullptr;

    Database *db = nullptr;

    /// The THREADED project-archive export (STABILITY_PROGRAM_SPEC Lane 4).
    /// Created on first use, parented here; shutdownBackgroundWork cancels and
    /// joins it (ProjectArchiver::shutdownArchives).
    class ProjectArchiver *archiver = nullptr;
    /// The archiver's export target: a Project naming the row being exported,
    /// never the live one (an export used to re-point the live project).
    std::unique_ptr<Project> exportTarget;
    QPointer<class ProgressDialog> archiveProgress;


    /// The one live Project instance, owned by the shell and injected into
    /// everything that needs it (Phase 4: was the Globals::project static).
    /// Created first thing in the constructor, then mutated in place
    /// (setProjectPath/setProjectGuid) for the process lifetime.
    Project *project = nullptr;

    ProjectManager *pmContainer = nullptr;

    QUndoStack* undoStack = nullptr;

	QPushButton *worlds_menu = nullptr;
	QPushButton *player_menu = nullptr;
	QPushButton *editor_menu = nullptr;
	QPushButton *effect_menu = nullptr;
	QPushButton *assets_menu = nullptr;
	QPushButton *publish_menu = nullptr;
	QPushButton *avatar_menu = nullptr;
	QWidget *assets_panel = nullptr;
	QLabel *jlogo = nullptr;
	QPushButton *help = nullptr;
	QPushButton *prefs = nullptr;

    QMainWindow *dialog = nullptr;

    QDockWidget *sceneHierarchyDock = nullptr;
    SceneHierarchyWidget *sceneHierarchyWidget = nullptr;

    QDockWidget *sceneNodePropertiesDock = nullptr;
    SceneNodePropertiesWidget *sceneNodePropertiesWidget = nullptr;
    /// The right column's World | Selection tab bar (above the scroll area).
    class PropertiesTabStrip *propertiesTabStrip = nullptr;

    QDockWidget *presetsDock = nullptr;
    /// Gives the editor's two COLUMNS their default widths, once per session
    /// (ui/style/panelmetrics.h): the Properties/Presets column on the right
    /// and the Hierarchy column on the left, which every other page copies.
    /// Called from both ways the editor page opens.
    void applyColumnWidthsOnce();
    bool presetsAlignQueued = false;
    /// Whether that has happened — after it has, a user's drag wins.
    bool columnsSized = false;
    /// True when the nested `viewPort` QMainWindow's dock layout came back from
    /// settings — the once-per-session default column width then stands down
    /// (shell/dockstate.h).
    bool restoredViewportDocks = false;
    /// THE EDITOR'S DOCKS, AS THE EDITOR LAST HAD THEM (lane SPACE-1). Every
    /// space but the editor hides them, so the layout live at exit is the
    /// layout of whatever page the user quit from — an editor with no panels,
    /// stored as the editor's own. This is the last one the EDITOR had, taken
    /// on the way out of that space (and before immersive fullscreen hides the
    /// chrome), and it is what closeEvent writes.
    QByteArray editorDockState;
    /// Takes that snapshot. A no-op while the docks are hidden, which is what
    /// makes it safe to call from anywhere on the way out.
    void captureEditorDockState();
    /// Shows or hides the editor's five docks for the space that is on screen:
    /// the editor shows the ones `widgetStates` says are open, every other
    /// space shows none. The ONE place dock visibility follows a space, so the
    /// space switch and the queued layout pass cannot disagree about it.
    void applyDockVisibilityForSpace();

    /// The bottom area's docks in tab-bar order, each with the name the
    /// `editor.tray` verbs call it by ("assets" | "timeline" | "console").
    QVector<QPair<QString, QDockWidget *>> bottomAreaTabs() const;
    /// The tab that was in front the last time the bottom area was on screen —
    /// what a space round trip puts back (lane SPACE-2).
    QString bottomFrontTab = QStringLiteral("assets");
    /// The tab Ctrl+` interrupted — where the bottom area goes when the
    /// console tab is taken away again.
    QString bottomReturnTab = QStringLiteral("assets");
    /// Brings `bottomFrontTab` back to the front of the bottom area.
    void raiseBottomFrontTab();

    /// THE LAUNCH TAB (owner review R7, 2026-09-18; the rule of 2026-09-15).
    ///
    /// EVERY SESSION OPENS ON ASSETS. Which bottom tab is in front is SESSION
    /// state, not a preference: inside a session it follows the user and
    /// survives space switches, fullscreen and the console's visit — but a
    /// LAUNCH always starts on the asset browser, whatever the saved DockState
    /// blob remembers, because that blob records where the last session HAPPENED
    /// to stop (often the Timeline, or the Console after a Ctrl+`).
    ///
    /// It is a function because the blob is restored TWICE — once in the
    /// constructor, and again from applyColumnWidthsOnce at the window's real
    /// size (the columns come back too narrow otherwise, smoke S1) — and the
    /// second restore silently re-applied the blob's front tab: the raise in
    /// the constructor was undone one event-loop turn after the editor opened,
    /// which is how the rule regressed without anybody touching it. Both
    /// restores are followed by this call.
    ///
    /// It sets `bottomFrontTab` as well as raising the dock: the very next
    /// thing applyDockVisibilityForSpace does is READ the current front tab
    /// into that field, so a raise alone would be read straight back out again.
    void raiseLaunchBottomTab();

    QTabWidget *presetsTabWidget = nullptr;

    /// The bottom area's three docks — ONE tab group, one tab bar (lane
    /// SPACE-2). `assetDock` holds the asset browser directly (the QTabWidget
    /// that used to nest a second tab bar inside it is gone), `animationDock`
    /// the Timeline, `scriptConsoleDock` the script console, which is hidden
    /// until Ctrl+` (or editor.tray) asks for it — a hidden dock has no tab, so
    /// "the Console tab is in the bar" and "the console dock is open" are the
    /// same statement.
    QDockWidget *assetDock = nullptr;
    AssetWidget *assetWidget = nullptr;

    QDockWidget *animationDock = nullptr;
    AnimationWidget *animationWidget = nullptr;
    QDockWidget *scriptConsoleDock = nullptr;

    QMainWindow *viewPort = nullptr;
    QWidget *sceneContainer = nullptr;

    QWidget *controlBar = nullptr;
    QWidget *playerControls = nullptr;
    QPushButton *playSceneBtn = nullptr;
    QMenu *wireFramesMenu = nullptr;
    QToolButton *wireFramesButton = nullptr;
    QPushButton *restartBtn = nullptr;
    QPushButton *playBtn = nullptr;
    QPushButton *stopBtn = nullptr;

    QToolBar *toolBar = nullptr;
    AssetView *_assetView = nullptr;
    QWidget *assetsPlaceholder = nullptr;   // the "assets" page until the real one is built
    class IAssetViewer *assetsPreviewViewer = nullptr;   // made at boot, handed to the page
	QAction *actionSaveScene = nullptr;

    /// THE VR TOGGLE (SPECS/VR_SPEC.md §4.5, phase 3) — the editor toolbar's
    /// half. The Player page has its own button and both make the same call
    /// (PlayerService::toggleVr, which is what `vr.toggle()` calls); this is
    /// held so its icon and its tooltip can follow the session.
    QAction *actionVr = nullptr;
    /// What the icon is currently showing, so the per-frame refresh only
    /// rebuilds a QIcon when the answer moves.
    bool mVrIconActive = false;
    /// Can this PROCESS do VR at all? Fixed at boot (the engine asks the
    /// runtime once, and only under `--vr`), so the per-frame follower reads
    /// this cached bool first and an ordinary launch pays nothing.
    bool mVrCapable = false;

    QAction *wireCheckAction = nullptr;
    QAction *physicsCheckAction = nullptr;
    QAction *gridCheckAction = nullptr;
    QAction *groundPlaneCheckAction = nullptr;   ///< View Options "Ground Plane" (WORLD-MODEL-1)
    QAction *statsCheckAction = nullptr;   // F3 frame-stats readout (persisted)
    /// THE ATOM VIEW sub-menu of View Options (D0-ATOM-VIEW): Off, Triangles,
    /// Levels, Buckets, Objects — exclusive, in AtomView's order; F6 cycles it.
    /// Both call the scene's setAtomView, the one path world.setAtomView takes.
    QVector<QAction *> atomViewActions;
    void setAtomViewMode(int mode);
    int atomViewMode();
    /// View Options -> Photon View (PHOTON-VIEW-1): Off, Voxels, Probes, Cards,
    /// Screen Probes, Diffuse GI Only, Reflections Only, Ray Hits — exclusive, in
    /// PhotonView's order; F7 cycles through the ones that can paint. Both call the
    /// scene's setPhotonView behind its photonViewRefusal, world.setPhotonView's path.
    QVector<QAction *> photonViewActions;
    void setPhotonViewMode(int mode);
    int photonViewMode();
    class Toast *snapToast = nullptr;   // [ / ] snap-size feedback
    /// THE CAMERA-SPEED BUTTON and the two controls in its popover (owner
    /// R15). Owned by the toolbar and the popover menu; held to keep all three
    /// in sync with CameraSpeed, which the verb and the scroll wheel can both
    /// change behind their backs.
    class QToolButton *cameraSpeedButton = nullptr;
    class QSlider *cameraSpeedSlider = nullptr;
    class QSpinBox *cameraSpeedSpin = nullptr;
    /// "The 3D view could not be created" — the respecced Failed state
    /// (STATS_OVERLAY_SPEC.md §6.4), which used to be a ViewportCover state.
    class Toast *viewErrorToast = nullptr;
    /// Why the last space switch was refused (lastSpaceRefusal).
    QString spaceRefusal;
    /// The Player page could not start: say why and go back (SMOKE-FIX-1).
    void bounceFromPlayer(const QString &why);
    /// THE SCENE-ERROR AREA (services/sceneissues.h): a dismissible list of the
    /// scene problems the user can fix, over the viewport beside the frame-rate
    /// readout. Built on the first issue and kept; the timer runs the scanner.
    class SceneIssueBar *sceneIssueBar = nullptr;
    class QTimer *sceneIssueTimer = nullptr;
    void wireSceneIssues();
    void stepSnapSize(int direction);
    /// What the editor's chrome looked like before immersive fullscreen hid it.
    QVector<bool> preFullscreenWidgets;
    void hideChromeForFullscreen();
    void restoreChromeAfterFullscreen();

	QVector<bool> widgetStates;	// use the order in the enum

    /// The space this window came FROM and the one it is on. BOTH initialised:
    /// `previousSpace` is read by the Ctrl+Tab "Previous Space" shortcut (its
    /// ONLY reader) and was uninitialised until the first switch wrote it, so
    /// the first press of that chord in a session read a garbage space
    /// (SMOKE-FIX-1's audit — the same class of defect as the play-mode flag,
    /// two members down from it).
    WindowSpaces previousSpace = WindowSpaces::DESKTOP;
    WindowSpaces currentSpace = WindowSpaces::DESKTOP;
	QPushButton *playSimBtn = nullptr;

    QAction *actionTranslate = nullptr;
    QAction *actionRotate = nullptr;
    QAction *actionScale = nullptr;
    /// The toolbar's transform-space pair. Members (they were locals) so a
    /// scripted editor.setGizmoSpace can leave the buttons telling the truth,
    /// exactly as actionTranslate/Rotate/Scale do for the gizmo mode.
    QAction *actionGlobalSpace = nullptr;
    QAction *actionLocalSpace = nullptr;

    AssetModelPanel *assetModelPanel = nullptr;
    AssetMaterialPanel *assetMaterialPanel = nullptr;

	QtAwesome *fontIcons;

	VrModule *vrModule = nullptr;

	/// THE SHELL'S PARTS (D10-SHELL-MODULES): the pages by id, the actions /
	/// menus / toolbar slots, the module loop, and the one teardown path.
	PageHost *pageHost = nullptr;
	ActionHost *actionHost = nullptr;
	ModuleHub *moduleHub = nullptr;
	ShellLifecycle *lifecycle = nullptr;
	ShellView *shellView = nullptr;
	ViewController *viewController = nullptr;

    // services (APP_ARCHITECTURE_AUDIT §3.3): constructed in setupServices(),
    // deleted in the dtor. The QObject services are parented to the window.
    StudioServices *services = nullptr;
    UndoService *undoService = nullptr;
    SelectionService *selectionService = nullptr;
    PlaybackService *playbackService = nullptr;
    /// The session log's shell-side event markers (SESSION_LOG_SPEC §5):
    /// play/stop brackets, space switches, the quit summary and the scene
    /// stats appended to the open block. Parented, so it dies with the window.
    SessionMarkers *sessionMarkers = nullptr;
    /// The session log's periodic perf sampler (SESSION_LOG_SPEC §8-R3).
    /// Parented; also published through StudioServices for log.perf/log.sample.
    PerfSampler *perfSampler = nullptr;
    PlayerService *playerService = nullptr;
    ProjectService *projectService = nullptr;
    SceneEditService *sceneEditService = nullptr;
    /// The material hover preview (MATERIAL-PREVIEW-1). Owned here; QObject-free,
    /// so it is deleted by hand in the destructor.
    MaterialPreviewService *materialPreviewService = nullptr;
    ClipboardService *clipboardService = nullptr;
    ThumbnailService *thumbnailService = nullptr;
    AssetService *assetService = nullptr;

    // scripting (SCRIPTING_SPEC §2): the host struct must outlive the engine
    struct ScriptHost *scriptHost = nullptr;
    class ScriptEngine *scriptEngine = nullptr;
    class ScriptConsole *scriptConsole = nullptr;
    class ClaudeAssistant *assistant = nullptr;

    /// Per-dialog options and answers for openDialog (only importSettings has
    /// any — see mainwindowdialogs.cpp).
    void applyDialogOptions(const QString &name, QWidget *widget, const QVariantMap &options,
                            QVariantMap *extra);
    /// The message a dialog's own OK handler failed with, for the verb that
    /// pressed it.
    QString mDialogError;

    // dialogs opened by name (openDialog); QPointer: owned entries delete
    // themselves on close
    QHash<QString, QPointer<QWidget>> scriptDialogs;
};

#endif // MAINWINDOW_H
