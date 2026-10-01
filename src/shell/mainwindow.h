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
#include "services/projectrunner.h"
#include "ui/ishellview.h"

namespace Ui {
    class MainWindow;
}

class AssetView;
/// Only ever held as a weak_ptr here (mEngineWatch) — the shell includes the
/// engine header in the .cpp, never in this one.
namespace jahshaka { namespace engine { class Engine; } }
class PageHost;
class ActionHost;
class ModuleHub;
class ShellLifecycle;
class ShellView;
class ViewController;
class ShellHeader;
class EditorPage;
class EditorToolbar;
class SceneIssueWatch;
class EditorDocks;

class QUndoStack;

class IEditorViewport;
class SceneHierarchyWidget;
class PlayerWidget;

class SettingsManager;
class ShortcutRegistry;
class PreferencesDialog;
class AboutDialog;

class ProjectManager;

// services (src/services/) — the shell constructs these and delegates to them
class MaterialPreviewService;
struct StudioServices;
class UndoService;
class SelectionService;
class PlaybackService;
class PlayerService;
class ProjectService;
class SceneEditService;
class ClipboardService;
class ThumbnailService;
class AssetService;

#include "ui/panels/scenenodepropertieswidget.h"

enum class SceneNodeType;

#include "shell/spaces.h"

#include <QJsonObject>
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "services/surfaceplacement.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/assets/texture2d.h"

class Database;
class Project;
class MainWindow : public QMainWindow, private ProjectRunner::Host
{

    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    /// The editor viewport (engine-backed, or the headless stand-in).
    IEditorViewport *viewport() { return sceneView; }
    /// The outliner panel. Public because the OUTLINER is the authority on
    /// visible row order — editor.selectRange has to ask it what lies between
    /// two rows (EDITOR_MULTISELECT_SPEC §2.7); null in headless sessions.
    SceneHierarchyWidget *hierarchyPanel() const;
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
    void setupUndoRedo();

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

    /// The window-centre notice (a page that cannot start, VR that did not).
    void showNotice(const QString &title, const QString &text);
    /// The Player page's VR button follows the session (the VR module's).
    void showPlayerVrState(bool available, bool active);

	/// The editor camera's controls: the canonical views, the projection
	/// toggle and the camera switcher (shell/viewcontroller.h).
	ViewController *views() const { return viewController; }
    /// THE OPEN, THE CREATE AND THE CLOSE (services/projectrunner.h).
    ProjectRunner *projectRunner() const { return projects; }
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

    virtual void closeEvent(QCloseEvent *event);

    SettingsManager* getSettingsManager();

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
    /// THE EDITOR'S PANELS (shell/editordocks.h): the docks, the bottom area,
    /// the properties column and the readings the verbs report.
    EditorDocks *editorDocks() const { return docks; }
    /// THE EDITOR PAGE and its toolbar (shell/editorpage.h, shell/editortoolbar.h).
    EditorPage *editorPage() const { return page; }
    EditorToolbar *editorToolbar() const { return toolbar; }
    /// The module loop (the edit chords' targets live there).
    ModuleHub *hub() const { return moduleHub; }
    /// The active space's name, as the hub and the action host key it.
    QString currentSpaceName() const { return spaces::id(currentSpace); }
    /// The space this window came FROM (Ctrl+Tab's "Previous Space").
    WindowSpaces previousWindowSpace() const { return previousSpace; }
    /// The Player page's widget, and the one live Project.
    PlayerWidget *playerPage() const { return playerView; }
    Project *currentProject() const { return project; }

    /// One toast, reused, for every transient viewport readout (snap size, fly
    /// speed) — and for a gesture the viewport has to REFUSE: a material or an
    /// image dropped on a LOCKED node (lane SPACE-2 item 5), which is why the
    /// viewport calls it and it lives out here with the rest of the shell's
    /// public surface.
    void showViewportToast(const QString &title, const QString &text);
    /// Re-reads the properties panel from the document (the edit gate's repaint:
    /// a refused row is showing a value the document does not hold).
    void refreshPropertiesFromDocument();

    /// THE ACTIVE PAGE'S COLUMNS (smoke S1). Every page's left and right
    /// columns are sized from ui/style/panelmetrics.h; this reports what they
    /// actually came out as, per page, so `app.columns` can gate the law
    /// instead of trusting each page to have used the constant. `valid` is
    /// false for a space that has no columns (Desktop, Player).
    using ColumnMetrics = IShellView::ColumnMetrics;
    ColumnMetrics activeColumns() const;

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

    // (evalShadowMapType / getLightTypeFromName / createLight — a SECOND scene
    // reader that lived here, knew neither Area nor Sky nor the sun rows, and
    // had no caller anywhere in the tree — are DELETED, SKY_LIGHT_SPEC.md §5
    // item 5. SceneReader is the one reader.)

    /// Rewrites the four read-only Gameplay rows in the Shortcut Registry from
    /// the live InputMap (AVATAR_LOCOMOTION_SPEC §8.2). Called at setup and
    /// again whenever `input.bind` changes a binding — public for that one
    /// caller; everything else goes through the registry's own API.
    void refreshGameplayShortcutRows();

    /// The scene-error area (shell/sceneissuewatch.h).
    SceneIssueWatch *sceneIssues() const { return issueWatch; }

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

public slots:

    void setupViewPort();
    void setupDesktop();
    void setupShortcuts();

    //context menu functions
    void duplicateNode();
	void createMaterial();
	void exportNode(const iris::SceneNodePtr &node, ModelTypes modelType);
    void deleteNode();

    void assetItemSelected(QListWidgetItem *item);
    void sceneNodeSelected(iris::SceneNodePtr sceneNode);

	void saveScene(const QString &filename, const QString &projectPath);
    void saveScene();

    void showPreferences();
    /// `kind` = the New Scene dialog's Template drop-down and
    /// `project.create`'s `{template}` (services/scenetemplate.h says what
    /// each template holds).
    void newScene(SceneTemplate kind = SceneTemplate::Basic);

    /// The threaded open (a desktop tile's), through the runner.
    void openProjectAsync(bool playMode = false);
    /// THE DESKTOP PAGE, for the verbs that drive what it owns — today the
    /// sample browser's open (project.openSample). Borrowed, never null in a
    /// windowed session, and owned by this window.
    ProjectManager *projectPage() const { return pmContainer; }
    /// The close a user asked for (the runner drains an open in flight first).
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

signals:
	/// An asset was REIMPORTED through the import-settings dialog: its bake,
	/// its metadata block and every placed instance of it have changed
	/// (SPECS/IMPORT_DIALOG_SPEC.md §5). The Assets page and the project tray
	/// re-read the row's size line and thumbnail on it.
	void assetReimported(const QString &guid);

public slots:

private slots:

	/// Shrinks the window to the screen it is about to appear on, keeping the
	/// authored .ui size as the preferred one. Called ONLY when there is no
	/// stored geometry to restore — a first run — so a saved size the user
	/// chose is never second-guessed.
	void fitToScreen();

private:
    void setupServices();
    /// The active space's name, as the hub and the action host key it.
    QString currentSpaceId() const { return spaces::id(currentSpace); }

    // ---- ProjectRunner::Host: the stage bodies only the window can run ----
    void teardownWorld() override;
    void bindWorld(const iris::ScenePtr &scene, EditorData *editorData, bool playMode) override;
    void bindNewWorld(const iris::ScenePtr &scene) override;
    iris::ScenePtr createWorld(SceneTemplate kind) override;
    void buildPanels(bool fresh) override;
    bool revealWorld(bool playMode) override;
    void prepareClose() override;
    void closeWorld(bool reopenInPlace) override;
    QStringList plannedSessionModelPaths() override;
    QStringList sessionAssetGuids() override;
    void registerSessionAssets(const iris::MeshPrewarmPtr &prewarm) override;
    void registerSessionAssetGuids(const QStringList &guids,
                                   const iris::MeshPrewarmPtr &prewarm) override;
    void showOpenProgress(int percent, const QString &text) override;
    void hideOpenProgress() override;
    void saveOpenWorld() override;
    iris::ScenePtr openWorld() const override;
    ProjectRunner *projects = nullptr;

    Ui::MainWindow *ui;
    IEditorViewport* sceneView = nullptr;
	PlayerWidget* playerView = nullptr;
	/// The player's engine backend, or null in headless runs. Held so
	/// setupServices can hand it to PlayerService (verb-coverage audit F1).
	class EnginePlayerView* playerBackend = nullptr;

    QSharedPointer<iris::Scene> scene;

    SettingsManager* settings;
    PreferencesDialog* prefsDialog;
    ShortcutRegistry* shortcutRegistry = nullptr;
    AboutDialog* aboutDialog;

    Database *db = nullptr;

    /// The export's progress dialog (the archive is the ProjectRunner's).
    QPointer<class ProgressDialog> archiveProgress;

    /// The one live Project instance, owned by the shell and injected into
    /// everything that needs it (Phase 4: was the Globals::project static).
    /// Created first thing in the constructor, then mutated in place
    /// (setProjectPath/setProjectGuid) for the process lifetime.
    Project *project = nullptr;

    ProjectManager *pmContainer = nullptr;

    QUndoStack* undoStack = nullptr;

    QMainWindow *viewPort = nullptr;

    QToolBar *toolBar = nullptr;
    AssetView *_assetView = nullptr;
    QWidget *assetsPlaceholder = nullptr;   // the "assets" page until the real one is built
    class IAssetViewer *assetsPreviewViewer = nullptr;   // made at boot, handed to the page
	QAction *actionSaveScene = nullptr;

    class Toast *snapToast = nullptr;   // [ / ] snap-size feedback
    /// "The 3D view could not be created" — the respecced Failed state
    /// (STATS_OVERLAY_SPEC.md §6.4), which used to be a ViewportCover state.
    class Toast *viewErrorToast = nullptr;
    /// Why the last space switch was refused (lastSpaceRefusal).
    QString spaceRefusal;
    /// The Player page could not start: say why and go back (SMOKE-FIX-1).
    void bounceFromPlayer(const QString &why);

    /// The space this window came FROM and the one it is on. BOTH initialised:
    /// `previousSpace` is read by the Ctrl+Tab "Previous Space" shortcut (its
    /// ONLY reader) and was uninitialised until the first switch wrote it, so
    /// the first press of that chord in a session read a garbage space
    /// (SMOKE-FIX-1's audit — the same class of defect as the play-mode flag,
    /// two members down from it).
    WindowSpaces previousSpace = WindowSpaces::DESKTOP;
    WindowSpaces currentSpace = WindowSpaces::DESKTOP;

	QtAwesome *fontIcons;

	/// THE SHELL'S PARTS (D10-SHELL-MODULES): the pages by id, the actions /
	/// menus / toolbar slots, the module loop, and the one teardown path.
	PageHost *pageHost = nullptr;
	ActionHost *actionHost = nullptr;
	ModuleHub *moduleHub = nullptr;
	ShellLifecycle *lifecycle = nullptr;
	/// The window as the layers below see it (ui/ishellview.h); owned here.
	std::unique_ptr<ShellView> shellView;
	ViewController *viewController = nullptr;
	ShellHeader *header = nullptr;
	EditorPage *page = nullptr;
	EditorToolbar *toolbar = nullptr;
	SceneIssueWatch *issueWatch = nullptr;
	EditorDocks *docks = nullptr;

    // services (APP_ARCHITECTURE_AUDIT §3.3): constructed in setupServices(),
    // deleted in the dtor. The QObject services are parented to the window.
    StudioServices *services = nullptr;
    class ShellServices *serviceLayer = nullptr;
    UndoService *undoService = nullptr;
    SelectionService *selectionService = nullptr;
    PlaybackService *playbackService = nullptr;
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
    class ShellScripting *scripting_ = nullptr;
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
