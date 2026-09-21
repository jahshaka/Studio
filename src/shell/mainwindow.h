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

namespace Ui {
    class MainWindow;
}

class AssetView;
/// Only ever held as a weak_ptr here (mEngineWatch) — the shell includes the
/// engine header in the .cpp, never in this one.
namespace jahshaka { namespace engine { class Engine; } }
namespace materials { class EffectsPage; }
class StudioModule;
class MaterialsModule;
class PublishModule;
class AvatarModule;
class VrModule;
class PlayerModule;

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

enum WindowSpaces : int {
    DESKTOP,
    PLAYER,
    EDITOR,
	EFFECT,
    ASSETS,
    PUBLISH,
    // AVATAR is APPENDED, and its page is appended AFTER publishView: switchSpace
    // uses hard-coded stack indices, so inserting anywhere else switches every
    // space above it to the wrong widget (AVATAR_MODULE_SPEC R0.14).
    AVATAR
};

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

    void grabOpenGLContextHack();

    /// The editor viewport (engine-backed, or the headless stand-in).
    IEditorViewport *viewport() { return sceneView; }
    /// The outliner panel. Public because the OUTLINER is the authority on
    /// visible row order — editor.selectRange has to ask it what lies between
    /// two rows (EDITOR_MULTISELECT_SPEC §2.7); null in headless sessions.
    SceneHierarchyWidget *hierarchyPanel() const { return sceneHierarchyWidget; }
    /// --engine-selftest: show the editor page, build the default scene the way
    /// newScene() does and start the viewport. False (with a reason) if the engine
    /// viewport is not in use or has no view.
    bool beginEngineSelftest(QString &why);
    void endEngineSelftest();
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
    /// The ONE place the frame-stats readout is switched: F3, the View Options
    /// row, the Preferences checkbox and editor.setOverlays({stats}) all land
    /// here, and it persists `show_fps` (STATS_OVERLAY_SPEC.md §5.3).
    void setShowFrameStats(bool on);
    void setupProjectDB();
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

	/// Views dropdown / view.* shortcuts / editor.setView verb — ONE path:
	/// snaps the editor camera to a canonical view ("top", "bottom", "left",
	/// "right", "front", "back", "perspective"), switches projection (axis
	/// views are orthographic) and keeps the toolbar + dropdown checks in
	/// sync. Returns false for an unknown name.
	bool applyCameraView(const QString &name);
	/// The Views dropdown's label = the view it is currently in ("Perspective"
	/// until one is picked). Driven from applyCameraView, so a scripted
	/// editor.setView moves it exactly like a click does.
	void setViewsButtonLabel(const QString &view);
	/// Rebuilds the camera switcher's list from the live document
	/// (CAMERAS_SPEC D4). Connected to the menu's aboutToShow.
	void rebuildCamerasMenu();
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

    bool handleMousePress(QMouseEvent *event);
    bool handleMouseRelease(QMouseEvent *event);
    bool handleMouseMove(QMouseEvent *event);
    bool handleMouseWheel(QWheelEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);

    virtual void closeEvent(QCloseEvent *event);

    void setSettingsManager(SettingsManager* settings);
    SettingsManager* getSettingsManager();

    /// Panel-aware frame pacing (fps audit F1, services/framepacing.h): pushes
    /// the persisted pacing mode and this window's screen refresh rate into the
    /// render driver, and keeps the rate current across screen/mode changes.
    /// Called once, where the driver is started.
    void wireFramePacing();
    /// Re-resolves the screen and pushes its refresh rate. Also re-points the
    /// refresh-rate connection when the window has moved to another screen.
    void updateFramePacingScreen();
    /// Connects QWindow::screenChanged once the native window exists — it does
    /// not yet when wireFramePacing() runs (constructor work). Retries on the
    /// event loop and gives up silently in a session that shows no window.
    void hookFramePacingScreenSignal(int retriesLeft);

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
    AssetView *assetsPage() const { return _assetView; }
    /// The editor's ASSET TRAY panel (the Assets tab of the bottom tray), or
    /// null before the editor is built. editor.trayAssets reads it.
    AssetWidget *assetTray() const { return assetWidget; }

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
    struct ColumnMetrics {
        bool valid = false;
        int leftWidth = 0;      ///< 0 when the page has no left column
        int leftMin = 0;
        int rightWidth = 0;     ///< 0 when the page has no right column
        int rightMin = 0;
    };
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

    /// Orderly teardown of every background worker the window owns (import
    /// batch + tails, MCP server, Claude chat subprocess, thumbnails). Runs
    /// at most once; called from closeEvent and wired to aboutToQuit so the
    /// QApplication::exit/quit path is covered too. Bounded: a worker
    /// that will not die is abandoned (the process-level force-exit guard in
    /// main() has the final word).
    void shutdownBackgroundWork();

    /// Step 3 of the shutdown order (shell/shutdownorder.h): StudioModule::
    /// shutdown() on every registered module, while the Engine is still up.
    /// Tail of shutdownBackgroundWork(), which is itself run-once.
    void shutdownModules();

    /// Step 6 of the shutdown order (shell/shutdownorder.h): destroys the
    /// child widgets that hold the last shared_ptr<Engine>, so the engine dies
    /// with a name on it and BEFORE closeDatabase(). Called only from
    /// ~MainWindow.
    void destroyEngineViews();

    /// Parameterised node verbs for the scripting API: same behaviour as the
    /// deleteNode()/duplicateNode() context-menu slots but on an explicit node
    /// (and duplication is undoable via AddSceneNodeCommand).
    bool deleteSceneNode(iris::SceneNodePtr node);
    iris::SceneNodePtr duplicateSceneNode(iris::SceneNodePtr node);

    /// The scripting engine (created in the ctor; modules see the world through
    /// ScriptHost). Null only before the ctor finishes.
    class ScriptEngine *scripting() { return scriptEngine; }

    /// The MCP endpoint (CLAUDE_EDITOR_SPEC.md phase 1). Created in the ctor,
    /// OFF by default; started by the Preferences toggle or --mcp-port=N.
    class McpServer *mcp() { return mcpServer; }
    /// Starts the MCP server on 127.0.0.1:port and announces the connect line
    /// in the script console dock. False (with errorOut) when the bind fails.
    bool startMcpServer(quint16 port, QString *errorOut = nullptr);

    /// The floating Claude chat popup (CLAUDE_EDITOR_SPEC phase 2) — created
    /// lazily; toggled by the toolbar button and the claude.toggle shortcut.
    void toggleClaudeChat();
    /// Pushes the current project / MCP state into the chat window + host.
    void refreshClaudeChatContext();

    //void setGizmoTransformMode(GizmoTransformMode mode);

    /**
     * Applies material preset to active scene node and refreshes material property widget
     * @param preset
     */

    void favoriteItem(QListWidgetItem *item);
    void refreshThumbnail(const QString &guid);
    void refreshThumbnail(QListWidgetItem *item);

    /**
     * Returns absolute path of file copied as an asset
     * @param relToApp file path relative to application
     * @return
     */
    QString getAbsoluteAssetPath(QString pathRelativeToApp);
    QString originalTitle;

    void addNodeToActiveNode(QSharedPointer<iris::SceneNode> sceneNode);
    void addNodeToScene(QSharedPointer<iris::SceneNode> sceneNode, bool ignore = false);
    void repopulateSceneTree();

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

    //ui setup
    void setupLayerButtonMenu();
    void initLightLayerUi();
    void initTorusLayerUi();

    void setupPropertyUi();

    void setupLayerManager();

    void rebuildTree();
    void deselectTreeItems();

    void setupDefaultScene();

    QIcon getIconFromSceneNodeType(SceneNodeType type);

    void removeScene();
    void setScene(QSharedPointer<iris::Scene> scene);
    void updateGizmoTransform();    // @TODO - move this into updateSceneSettings

    /// IMMERSIVE FULLSCREEN IS TWO THINGS — a window state and a set of hidden
    /// docks — and the window state can be left without this class being asked
    /// (RR2, 2026-09-14): `app.resizeWindow()` calls showNormal() before it
    /// resizes, and a window manager's own control does the same. The flag then
    /// said "fullscreen" while the window was not, so the next F11 (and
    /// `editor.fullscreen(true)`, which is idempotent against the flag) did
    /// nothing at all — F11 was dead until it was pressed twice. This watches
    /// the state it does not own and puts the chrome back.
    void changeEvent(QEvent *event) override;


    void updateCurrentSceneThumbnail();

    // determines if file extension is that of a model (obj, fbx, 3ds)
    // bool isModelExtension(QString extension);

public slots:
    void exportSceneAsZip();

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
    void addMesh(const QString &path = "", bool ignore = false, iris::Vec3 position = iris::Vec3());
	void addMaterialMesh(const QString &path = "", bool ignore = false,
	                     iris::Vec3 position = iris::Vec3(), const QString &guid = QString(),
	                     const QString &name = QString(),
	                     surfaceplacement::Placement placement = surfaceplacement::Placement::Pivot);
    void addAssetParticleSystem(bool ignore, iris::Vec3 position, QString guid, QString assetName);
    void addDragPlaceholder();

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
    /// `empty` = the blank world (owner review R1b / Q1, 2026-09-18): the
    /// New Scene dialog's "Empty scene" checkbox and `project.create`'s
    /// `{empty: true}`. See createDefaultScene for what each of the two holds.
    void newScene(bool empty = false);

    void newProject(const QString&, const QString&, bool empty = false);
    /// THE SAME CREATE, WITHOUT THE DRAIN (OPEN_COVER_SPEC §2 C/§4,
    /// `project.createAsync`): the slices are queued and this returns at once.
    /// The caller polls `isOpeningProject()` — one runner serves both routes.
    void newProjectAsync(const QString&, const QString&, bool empty = false);
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

    /// THE NEW-SCENE TEMPLATE, and its blank twin (owner review R1b / Q1).
    ///
    /// `empty == false` is what a new scene has always been, plus the owner's
    /// Q1 answer: the default ground, the sun (a directional light), the Sky
    /// Light, shadows on, the Epic world mode — and, since 2026-09-19, the
    /// REALISTIC real-time sky with the sun following the atmosphere (which is
    /// LightNode::followsAtmosphere's own default, so nothing is set for it
    /// here; the template only chooses the sky the flag then means something
    /// under).
    ///
    /// `empty == true` is the blank world, and it holds EXACTLY: a root node,
    /// the Epic world mode, and the document's own constructor defaults. No
    /// ground, no lights (so nothing lights it — a Sky Light is a light and an
    /// empty scene has none), and no sky beyond the document's default flat
    /// 96-grey, which is what iris::Scene's constructor sets and what a scene
    /// built by a script has always come up with.
    iris::ScenePtr createDefaultScene(bool empty = false);

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

    /// Immersive fullscreen (F11 the KEY, editor.fullscreen the verb): the
    /// window goes fullscreen and the editor space hides its docks and
    /// toolbar. `setImmersiveFullscreen` is the idempotent form the verb needs
    /// — toggleImmersiveFullscreen() flips, this one lands on a state.
    bool isImmersiveFullscreen() const { return immersiveFullscreen; }
    void setImmersiveFullscreen(bool on);

    void updateSceneSettings();

    void undo();
    void updateWindowTitle();
    void redo();

    /// Ctrl+Z / Ctrl+Shift+Z, routed to whichever edit stack the ACTIVE SPACE
    /// owns: the Materials space owns the graph's (owner decision, deep audit
    /// 2026-09 area 1), every other space the editor's. The registry entries
    /// "edit.undo"/"edit.redo" and the Edit menu/toolbar actions all call these
    /// — never undo()/redo() directly — so there is exactly one claimant for
    /// the chord and one place the routing rule lives.
    void undoActiveSpace();
    /// The four edit chords, routed by the active space like undo/redo
    /// (EDITOR_MULTISELECT_SPEC §2.6): the editor's selection SET, or the
    /// Materials graph when that page is up.
    void deleteActiveSpace();
    void duplicateActiveSpace();
    void copyActiveSpace();
    void cutActiveSpace();
    void pasteActiveSpace();
    void selectAllActiveSpace();
    /// Space: node search on the Materials space, gizmo cycle elsewhere.
    void spaceKeyActiveSpace();
    void redoActiveSpace();

    void takeScreenshot();
    void toggleLightWires(bool state);
    void toggleGrid(bool state);
    void toggleImmersiveFullscreen();
    /// The LEAVE half of the toggle above, callable on its own. `restoreWindow`
    /// is false when the window state has already been changed by somebody else
    /// (changeEvent's case): the docks and the flag come back, the window is
    /// left exactly as it was found.
    void leaveImmersiveFullscreen(bool restoreWindow);
    void toggleDebugDrawer(bool state);
    void showProjectManagerInternal();

signals:
	void projectionChangeRequested(bool val);

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

private slots:
    void translateGizmo();
    void rotateGizmo();
    void scaleGizmo();
    void cycleGizmoMode();

    void onPlaySceneButton();

	/// The PROJECTION TOGGLE, and it is a CANONICAL VIEW change (hygiene lane,
	/// 2026-09-09). `true` = perspective; `false` = the last orthographic axis
	/// view this window was in, "top" until there has been one. It routes
	/// through applyCameraView, which is the only path that also arms the
	/// axis-view rotation lock, updates the Views menu and relabels the button.
	///
	/// It used to flip `sceneView->getScene()->camera` — the SCENE's camera
	/// node, which is not the editor camera this viewport flies — leaving the
	/// explorer's projection untouched, the lock unarmed and the Views label
	/// reading "Perspective" over an orthographic picture.
	void changeProjection(bool val);

	/// Icon + tooltip only: what the projection button LOOKS like. Split out of
	/// changeProjection so the viewport can report a projection it changed
	/// itself without that report turning into a command.
	void syncProjectionButton(bool perspective);

	/// Shrinks the window to the screen it is about to appear on, keeping the
	/// authored .ui size as the preferred one. Called ONLY when there is no
	/// stored geometry to restore — a first run — so a saved size the user
	/// chose is never second-guessed.
	void fitToScreen();

private:
    void setupServices();

    // ---- the open, in stages (shared by the synchronous and threaded paths) --
    /// Cover up + tear the previous world down. Always first.
    /// The sliced CREATE (OPEN_COVER_SPEC §2 C) — the same runner, the same
    /// stage order. `newProject` is this plus the pumped drain.
    void startCreateRun(const QString &filename, const QString &projectPath, bool empty);
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

    QPoint mousePressPos;
    QPoint mouseReleasePos;
    QPoint mousePos;
    iris::Vec3 dragScenePos;

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
    QPointer<class ProgressDialog> archiveProgress;

    /// A NON-owning watch on the process's Engine, taken when the viewport is
    /// created. Step 5 of the shutdown order (shell/shutdownorder.h) uses it to
    /// prove the engine really died with the viewports — if a new
    /// shared_ptr<Engine> holder ever appears outside this window's widget
    /// tree, this is what notices.
    std::weak_ptr<jahshaka::engine::Engine> mEngineWatch;

    /// The screen the render loop is currently paced against (fps audit F1),
    /// and the connection to its refresh-rate signal. Non-owning; both are
    /// remade whenever the window changes screen.
    class QScreen *mPacingScreen = nullptr;
    QMetaObject::Connection mPacingRefreshConnection;

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
	QWidget *publishView = nullptr;   // stacked page 5: publishing stub
	QWidget *avatarView = nullptr;    // stacked page 6: the avatar module
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
    QToolButton *viewsButton = nullptr;
    QMenu *viewsMenu = nullptr;
    QVector<QAction *> viewsActions;   // checkable, ordered as built
    /// Where the projection toggle goes when it is asked for "orthographic":
    /// the last axis view this window was in, so Perspective -> Front ->
    /// Perspective -> (toggle) returns to Front rather than jumping to Top.
    QString lastOrthographicView = QStringLiteral("top");
    /// The CAMERA SWITCHER (CAMERAS_SPEC D4), beside Views: "Viewport" (the
    /// free explorer) plus every scene camera by name. Rebuilt from the
    /// document each time it opens — cameras are added, renamed and deleted
    /// while the menu exists, and a stale list would pilot a dead node.
    QToolButton *camerasButton = nullptr;
    QMenu *camerasMenu = nullptr;
    QPushButton *restartBtn = nullptr;
    QPushButton *playBtn = nullptr;
    QPushButton *stopBtn = nullptr;

    QToolBar *toolBar = nullptr;
    AssetView *_assetView = nullptr;
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
    QAction *statsCheckAction = nullptr;   // F3 frame-stats readout (persisted)
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
    // F11 immersive fullscreen restore state (EDITOR_SHORTCUTS_SPEC §3)
    bool immersiveFullscreen = false;
    /// ENTERING, and not there yet (round-2 review, item 5). `showFullScreen()`
    /// is a REQUEST: a window manager answers it with its own sequence, and a
    /// maximized window can be handed an intermediate state that does not carry
    /// the fullscreen flag — which changeEvent would read as "somebody took us
    /// out of fullscreen" and restore every dock INSIDE the fullscreen window.
    /// The latch is set when the toggle asks and cleared by the first state
    /// change that reports fullscreen; until then a non-fullscreen state is the
    /// transition, not a departure.
    bool enteringFullscreen = false;

    bool preFullscreenMaximized = false;
    QVector<bool> preFullscreenWidgets;

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

	QPushButton *cameraView = nullptr;
	QtAwesome *fontIcons;

	materials::EffectsPage *shaderGraph = nullptr;   // the materials module's page
	QVector<StudioModule*> modules;                  // audit §6.2: the shell's module list
	MaterialsModule *materialsModule = nullptr;
	PublishModule *publishModule = nullptr;
	AvatarModule *avatarModule = nullptr;
	VrModule *vrModule = nullptr;
	PlayerModule *playerModule = nullptr;

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
    class McpServer *mcpServer = nullptr;
    class ClaudeChatHost *claudeChatHost = nullptr;
    class ClaudeChatWindow *claudeChatWindow = nullptr;

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
