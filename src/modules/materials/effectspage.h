#pragma once

#include <functional>

#include <QPointer>
#include <QSet>

#include "ui/style/columnedpage.h"
#include "services/presetedit.h"
#include <QListWidget>
#include <QVariantMap>
#include <QMainWindow>
#include <QWidget>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QDockWidget>
#include <QSplitter>
#include <QToolBar>
#include <QUndoStack>

#include "widgets/nodepropertiespanel.h"
//#include "nodemodel.h"
#include "widgets/graphicsview.h"
#include "widgets/materialsettingswidget.h"
#include "dialogs/createnewdialog.h"
#include "widgets/listwidget.h"
#include "thirdparty/qtawesome/QtAwesome.h"
#include "thirdparty/qtawesome/QtAwesomeAnim.h"
#include "ui/controls/fonticons.h"

#if(EFFECT_BUILD_AS_LIB)
#include "widgets/shaderassetwidget.h"
#endif

class Toast;
class QLabel;
class QMenuBar;
class GraphNodeScene;
class NodeGraph;
class NodeLibraryItem;
class Database;
class Project;
class SettingsManager;
class TexturePropertyWidget;
class UndoRedo;
class AssetView;
struct ThumbnailResult;

class QTabBar;
class QTimer;
class NodeLibrary;

namespace materials
{
Q_NAMESPACE

class IMaterialPreviewWidget;
class MaterialDocument;
class MembersPanel;


struct nodeListModel {
	QString name;
	//NodeType type;
	int inSockets = 0;
	int outSockets = 0;

};

class EffectsPage : public QMainWindow, public ColumnedPage
{
    Q_OBJECT

public:
    /// ColumnedPage (ui/style/columnedpage.h): the assets/settings docks are
    /// the left column, the Display/Properties docks the right one. Both are
    /// sized from PanelMetrics, like every other page's.
    QWidget *leftColumn() const override;
    QWidget *rightColumn() const override;

protected:
    /// The page's columns get their widths the FIRST time it is shown, not in
    /// the constructor: a dock is re-laid-out from its widget's sizeHint when it
    /// becomes visible, so a resizeDocks from the ctor is simply undone (the
    /// same trap the editor shell documents in applyColumnWidthsOnce). Measured
    /// on the rig 2026-09-11 — both columns opened at their minimums.
    void showEvent(QShowEvent *event) override;

public:

    explicit EffectsPage( QWidget *parent = Q_NULLPTR, Database *database = Q_NULLPTR);
    void setNodeGraph(NodeGraph* graph);
    void newNodeGraph(QString *shaderName = Q_NULLPTR, int *templateType = Q_NULLPTR, QString *templateName = Q_NULLPTR);
	
	void setAssetView(AssetView* assetView) { this->assetView = assetView; }
	/// Injected by the shell: is a project scene open? Forwarded to the
	/// module's widgets (Phase 4: was UiManager::isSceneOpen).
	void setSceneOpenProbe(std::function<bool()> probe);
	/// Injected by the shell: the one live Project (Phase 4: was the
	/// Globals::project static). Forwarded to the module's asset widget.
	void setProject(Project *project);
	/// Injected by the shell: the app's settings, which is where the OPEN TAB
	/// SET lives (MATERIALS_TABS_SPEC §2.7) — per project, plus one for the
	/// library. Handing it in is also what asks for the first restore.
	void setSettings(SettingsManager *settings);
	/// THE PROJECT CHANGED (opened, closed, switched). Called by the shell,
	/// because nothing else can tell this page: `setProject` is called ONCE
	/// at module init with the one live Project instance, and ProjectService
	/// is not a QObject and has no signals. Saves the outgoing project's tab
	/// set, closes the documents that belonged to it (their scope is gone)
	/// and restores the incoming one's. LIBRARY tabs stay across the switch:
	/// the library is the same library.
	void onProjectChanged();
private:
	std::function<bool()> mSceneOpenProbe;
	Project *mProject = nullptr;
	/// A COPY-ON-WRITE IS IN FLIGHT (PRESET-EDIT-1, the rig's crash). The copy
	/// unpins the master and pins the copy, and the page LISTENS to project
	/// membership: without this, the membership signal raised in the middle of
	/// `saveShader` closed — and FREED — the very document being saved, and
	/// the next line of that save wrote into it (SIGSEGV in closeDocumentAt's
	/// `saveTimer->stop()`). Nothing about our own pin move is news to this
	/// page: it is re-pointing the document itself.
	bool mPresetCopyInFlight = false;
	/// THE PROJECT, BUT ONLY WHEN ONE IS REALLY OPEN (PRESET-EDIT-1). The app
	/// has ONE Project instance, mutated in place, and it keeps its guid after
	/// a close — so "is a project open?" is the scene probe, not the guid, and
	/// every preset decision on this page asks it through here.
	Project *openProject() const;
public:
	/// A GRAPH EDIT REACHES THE SCENE (OWNER_REVIEW 9, R19 D2). The page has
	/// no business knowing about scene nodes, so the shell hands it one
	/// callback and the page calls it with the material's guid every time it
	/// commits a definition; MaterialsModule wires it to the ONE apply
	/// (SceneEditService), which is what the drop and `material.apply` use.
	/// Until this lane an edit reached the scene only through a material
	/// SWITCH, and only while the Projects tab happened to be current.
	std::function<void(const QString &)> mMaterialChanged;

	/// THE COPY-ON-WRITE (PRESET-EDIT-1, the owner's rule: "only the MASTER
	/// materials should be locked"). A shipped preset a project holds is
	/// editable HERE, and the first edit is what makes the project its own
	/// copy. The page commits definitions; it has no undo service and no
	/// scene, so the shell hands it the one call — guid in, the guid to write
	/// to out (`copied` true the once) — and MaterialsModule wires it to
	/// `presetedit::forEdit`. Unset in a page with no shell: the save then
	/// stands down on a preset exactly as it did before.
	std::function<presetedit::Target(const QString &)> mMakeEditable;


	// Engine viewport mode: Studio hands in the engine-rendered Display preview
	// (core/materialpreviewwidget.h). Docks it, un-hides the Display dock and
	// starts pushing the graph's evaluated PbrMaterial to it on every edit.
	void setEnginePreview(IMaterialPreviewWidget *preview);

	void refreshShaderGraph();
	void setAssetWidgetDatabase(Database *db);
	void renameShader();

	/// Open a material bundle in the graph editor. `origin` is WHICH DRAWER
	/// it came from, and therefore whose version is read and written — see
	/// shaderInfo::Origin.
	void loadGraph(QString guid, shaderInfo::Origin origin = shaderInfo::Origin::Library);
	static QString genGUID();

	// §3a selection bridge for the graph.selectNode/selectedNode/deselect
	// verbs (wired up by MaterialsModule::registerApi)
	bool selectGraphNode(const QString& nodeId);
	QString selectedGraphNodeId();
	void deselectGraphNodes();

	// F2 edit bridge for graph.removeNode / graph.disconnect: the removal goes
	// through the page's UNDO STACK (GraphNodeScene::deleteNodeById /
	// deleteConnectionById push the same commands the canvas's Delete key
	// pushes), so graph.undo takes a scripted deletion back exactly as it takes
	// back a click. False = this page's graph has no such node/connection,
	// which is the signal the verb uses to fall back to its own script graph.
	bool removeGraphNode(const QString& nodeId);
	bool removeGraphConnection(const QString& connectionId);

	// ---- The graph's edit stack ----------------------------------------
	// THE page's undo stack (`stack` below): every GraphNodeScene this page
	// creates is given it (createNewScene -> setUndoRedoStack), so node adds,
	// deletes, moves, connections, pastes and the settings-dock property edits
	// all land on this one stack.
	//
	// These two exist so there is exactly ONE entry point to it, called by the
	// graph.undo/graph.redo verbs (MaterialsModule::registerApi wires them) AND
	// by the shell's Ctrl+Z when the Materials space is the active one — the
	// API-first rule, and the owner's decision that on this page the GRAPH undo
	// is the one that wins. Return false when there is nothing to undo/redo, so
	// a caller can tell "did nothing" from "not available".
	bool graphUndo();
	bool graphRedo();

	/// Opens the graph's node-SEARCH palette. The page-scoped entry point for
	/// the shell's Space key (owner decision 2026-09-05: on the Materials space
	/// Space searches nodes, everywhere else it cycles the gizmo) — the same
	/// single-claimant routing pattern graphUndo established for Ctrl+Z. Tab
	/// over the view still opens it too. False = no graph to search.
	bool openNodeSearch();

	// The graph's EDIT chords, page-scoped (EDITOR_MULTISELECT_SPEC §2.6).
	//
	// Delete, Ctrl+D, Ctrl+C and Ctrl+V are registry entries now — the editor
	// needs them for the selection SET, and a bare WindowShortcut in the graph
	// view would make each chord AMBIGUOUS, which is how Ctrl+Z came to do
	// nothing on this page (see graphUndo above). So the graph's four bare
	// QShortcuts are gone and these are what the shell calls when the Materials
	// space is the active one: same scene methods, one claimant per chord.
	// False = no graph (nothing to act on).
	bool graphDeleteSelected();
	bool graphDuplicateSelected();
	bool graphCopySelected();
	bool graphPaste();
	/// WHERE A NODE TILE IS, IN WINDOW PIXELS (hygiene lane, 2026-09-09).
	///
	/// The node palette is a QTabWidget of icon lists along the bottom of this
	/// page, and dragging a tile onto the canvas is the ONE graph edit no verb
	/// can make (the graph.* mutation verbs work on a script-local NodeGraph,
	/// never the page's), so app.input_keys has to perform the real gesture.
	/// It used to aim at a per-mille point measured from one window size; when
	/// the suite became hermetic the window opened taller and that point landed
	/// on the tab bar. This answers the question properly: select the tab that
	/// owns `name`, scroll the tile into view, and report its rect — and the
	/// canvas it must be dropped on — in MAIN-WINDOW coordinates, which is what
	/// a rig synthesising mouse events works in.
	///
	/// Empty map when no tile carries that name. Match is on the tile's
	/// display name, case-insensitively.
	QVariantMap paletteTileRect(const QString &name);

	/// Depth of the two halves of that stack — what the verbs report and what a
	/// test asserts against.
	int graphUndoCount() const;
	int graphRedoCount() const;

	// ---- THE OPEN TABS (MATERIALS_TABS_SPEC §3) -----------------------
	// The page half of the five `materials.*` tab verbs, which is also what
	// the tab bar itself calls: a click and a verb are the same gesture.
	/// Open (or activate) `guid` at `scope` — "library", "project", or empty
	/// for the default (the project's copy when it pins the guid). Empty
	/// when no drawer holds it.
	QVariantMap openMaterialTab(const QString &guid, const QString &scope);
	/// NEW MATERIAL — the toolbar's + and the drawer's New, without the
	/// dialog: `presetOrName` is the shipped preset the new material is based
	/// on (empty = a blank graph) and `name` what it is called (empty = the
	/// preset's name, bumped against the library's). It opens in its OWN tab
	/// (fix round F3) and becomes the active one.
	QVariantMap newMaterialTab(const QString &presetOrName, const QString &name);
	QVariantList materialTabs() const;
	QVariantMap activeMaterialTab() const;
	/// By tab INDEX (a number) or by guid (the first tab in bar order).
	bool activateMaterialTab(const QVariant &tabOrGuid);
	bool closeMaterialTab(const QVariant &tabOrGuid);
	/// WHAT THE PROJECT DRAWER IS SHOWING, in order (DRAWERS-1): the
	/// `materials.projectDrawer()` verb, through the page delegate — the one
	/// route the module's verbs take. Empty with no drawer.
	QVariantList projectDrawerTiles() const;

    ~EffectsPage();

	QList<NodeGraphPreset> list;
	/// The ACTIVE document's tile, in whichever drawer holds it. A view
	/// pointer, re-resolved from the guid after every drawer refill — the
	/// identity is the guid, and a QListWidgetItem* does not survive a
	/// `clear()`.
	QListWidgetItem *currentProjectShader = Q_NULLPTR;
    shaderInfo pressedShaderInfo;
	// (`stack` and `currentShaderInformation` are gone — MATERIALS_TABS_SPEC
	// §2.1/§7. There is one of each PER OPEN MATERIAL now, on
	// MaterialDocument, and nothing outside this class ever read either of
	// them: the undo stack is reached through graphUndo/graphRedo.)


private:
	
	/// Write the ACTIVE document (the toolbar's Save, a rename, a timeout).
	void saveShader();
	/// Write ONE document. Every line of a save is per document — its guid,
	/// its origin, its graph, its refusal — which is what lets a background
	/// tab's autosave fire against its OWN material instead of whichever
	/// material happens to be on screen 1.5 s later (the spec's C5).
	void saveShader(MaterialDocument *doc);
	/// Tell the user a save was REFUSED (F16): the graph is on screen and is
	/// not being written down, which no log line can say loudly enough.
	void reportSaveRefused(MaterialDocument *doc, const QString &why);
	/// Tell the user this material cannot be OPENED at all (LEGACY-MASTER-CRUD):
	/// it was written on the master node this build deleted, so there is
	/// nothing to put on the canvas. Same route as a refused save — a scene
	/// issue, which stays up until the condition is gone.
	void reportGraphRefused(const QString &guid, const QString &why);
	/// A DOCUMENT becomes this material: identity, read-only state and canvas
	/// in one step (LEGACY-CONVERT-CRUD's fix round — see the body). Called
	/// only once a graph has loaded; a refused open never reaches it, and with
	/// tabs it does not even open a tab.
	void adoptGraph(const QString &guid, shaderInfo::Origin origin,
	                const QString &shippedName, NodeGraph *graph);
	void adoptGraph(MaterialDocument *doc, const QString &guid, shaderInfo::Origin origin,
	                const QString &shippedName, NodeGraph *graph);
	void saveDefaultShader();

	/// Queues the saved graph's thumbnail on the shell's ThumbnailGenerator
	/// (the module never reaches into src/bridge itself) and delivers it in
	/// onShaderThumbnail — the tile, the asset row and the derived material
	/// all update from there (VISUAL_PARITY_SPEC item 5).
	void requestShaderThumbnail(const QString &shaderGuid);
	void onShaderThumbnail(const ThumbnailResult &result);
	bool mThumbnailConnected = false;

	/// THE OPEN MATERIAL IS A SHIPPED PRESET, ON SCREEN TO BE READ
	/// (PRESET-UNIFY-1). Selecting a preset shows its graph — the owner's
	/// first acceptance criterion — and a preset is read-only in fact: the
	/// definition writer refuses its reserved guid. So the page must not
	/// OFFER an edit it cannot honour: `saveShader` stands down, the autosave
	/// timer never fires a refusal into the scene-issue bar, and the banner
	/// above the canvas says so and offers the one gesture that works.
	QWidget *mReadOnlyBanner = nullptr;
	QLabel  *mReadOnlyLabel = nullptr;
	/// The page's own answer to a refused open (the scene-issue bar is the
	/// editor space's and is hidden here). Created on first use, lives with
	/// the page.
	QPointer<Toast> mRefusalToast;
	/// Mark the ACTIVE document read-only (or not) and show the banner.
	/// THE DOCUMENT BECOMES THIS PROJECT'S COPY OF THE PRESET IT WAS
	/// (PRESET-EDIT-1) — one named owner for that identity change, as
	/// `adoptGraph` is for the other; called by the save that made the copy.
	void adoptProjectCopy(MaterialDocument *doc, const QString &copyGuid);
	void setReadOnly(bool readOnly, const QString &presetName = QString());
	/// Re-assert the active document's read-only state on the SHARED
	/// widgets. The canvas, the properties panel, the settings dock and the
	/// banner are one set of widgets showing whichever document is active,
	/// so without this a preset's lock leaks onto the next tab.
	void applyReadOnlyUi();
	bool isReadOnly() const;
	/// The guids WE asked the shared thumbnail queue about (a material render
	/// is not ours by type alone any more — see requestShaderThumbnail).
	QSet<QString> mPendingThumbnails;
    void loadShadersFromDisk();


	/// Import a material share file (services/assetshare.h) into the library
	/// — the toolbar's Import and the drawer's "Import material…".
    void importGraph();

	/// Write ONE material and its closure as a share file.
	void exportEffect(QString guid);
	/// ONE ROW, COPIED: a second library bundle carrying the same definition
	/// (the members are SHARED, not copied — Make unique is how a picture
	/// becomes private to one material).
	void duplicateShader(QString guid);
    void restoreGraphPositions(MaterialDocument *doc, const QJsonObject& data);
    bool deleteShader(QString guid);

	void configureUI();
	void configureToolbar();
	void generateTileNode();
	void addTabs();
	void setNodeLibraryItem(QListWidgetItem *item, NodeLibraryItem *tile);
	bool createNewGraph(bool loadNewGraph = true);
	void updateAssetDock();

	bool eventFilter(QObject *watched, QEvent *event);

	void configureStyleSheet();
	void configureAssetsDock();
	void createShader(NodeGraphPreset preset, bool loadNewGraph = true,
	                  const QString &wanted = QString());
	void loadGraphFromTemplate(NodeGraphPreset preset, const QString &name = QString());
	/// Re-resolve the active document's tile from its guid (after a drawer
	/// refill, which deletes every item). Was `setCurrentShaderItem`, which
	/// asked the SCENE which tile it had last been handed.
	void refreshCurrentTile();
	/// The stored definition for `guid`, read at the scope the caller names.
	/// The ORIGIN IS AN ARGUMENT (fix round): it used to be read off
	/// `currentShaderInformation`, which forced `loadGraph` to write the page's
	/// identity before it knew whether the file could be opened at all — and
	/// which is no longer a thing (it is the document's).
	QByteArray fetchAsset(const QString &guid, shaderInfo::Origin origin);

	/// A GRAPH EDIT REACHES THE SCENE (OWNER_REVIEW 9, R19 D2). The page has
	/// no business knowing about scene nodes, so the shell hands it one
	/// callback and the page calls it with the material's guid every time it
	/// commits a definition; MaterialsModule wires it to the ONE apply
	/// (SceneEditService), which is what the drop and `material.apply` use.
	/// Until this lane an edit reached the scene only through a material
	/// SWITCH, and only while the Projects tab happened to be current.

    GraphNodeScene* createNewScene(MaterialDocument *doc);
	QListWidgetItem* selectCorrectItemFromDrop(QString guid);
	int selectCorrectTabForItem(QString guid);
	/// Which DRAWER a tile lives in, as the scope an edit to it belongs to.
	shaderInfo::Origin originForItem(QString guid);
	// (`loadedShadersGUID` is DELETED — written once, read never.)

private:
    void configureConnections();
    void editingFinishedOnListItem();
	/// THE ONE RENAME (MATERIALS_TABS_SPEC §7): the definition's own name,
	/// the graph payload's settings name, the catalog row through the ONE
	/// name writer (which is where the preset-name laws live), the tile, and
	/// every open document of that material. Both drawers call it.
	void renameMaterial(const QString &guid, const QString &wanted);
	void addMenuToSceneWidget();

	// Debounced graph -> PbrMaterial -> engine preview (engine viewport mode).
	void schedulePreviewUpdate();
	void updateEnginePreviewMaterial();

	IMaterialPreviewWidget *enginePreview = nullptr;
	QMainWindow *displayWindow = nullptr;   // the Display dock's inner window (menus + preview)
	QTimer *previewUpdateTimer = nullptr;   // 300ms debounce: slider drags bake once, not per pixel
	bool restoringGraph = false;            // suppress position-saves while a graph is being (re)built
	/// Set while a document is being put on screen. The material-settings
	/// widget and the properties panel EMIT when they are re-bound, and
	/// their edit signals push an undoable command — so without this, simply
	/// showing a tab wrote a settings change onto that tab's undo stack.
	bool mShowingDocument = false;

	// ---- THE OPEN MATERIALS (MATERIALS_TABS_SPEC §2) ------------------
	/// Every open material IN TAB ORDER, and which one is active. There is
	/// always at least one after construction: with nothing restored the
	/// page boots on the anonymous "Untitled" canvas, and closing the last
	/// tab leaves that same canvas rather than an empty window.
	QVector<MaterialDocument *> mDocs;
	int mActive = -1;
	/// The tab bar itself, at the top of the canvas column — inside the
	/// central pane, so it adds nothing to the window's minimum width.
	QTabBar *mTabBar = nullptr;
	/// Set while the bar is being rebuilt from `mDocs`, so its own
	/// currentChanged/tabMoved do not come back as user gestures.
	bool mSyncingTabs = false;
	/// Where the open tab set is stored, for the project (or the library)
	/// the page is currently showing.
	SettingsManager *mSettings = nullptr;
	QString mTabsKey;
	bool mTabsRestorePending = false;
	/// ONE node library for every graph this page opens — it is a stateless
	/// factory registry, and a copy per open was exactly that.
	NodeLibrary *mNodeLibrary = nullptr;

	MaterialDocument *activeDoc() const;
	NodeGraph *activeGraph() const;
	GraphNodeScene *activeScene() const;
	QUndoStack *activeStack() const;
	/// The active document's identity, or an empty one when there is none.
	shaderInfo currentInfo() const;
	/// A document with its own stack and its own 1.5 s autosave timer.
	MaterialDocument *newDocument();
	/// Give a document a graph: a fresh scene on ITS stack, the previous
	/// pair freed, the panels rebound when it is the active one.
	void bindGraph(MaterialDocument *doc, NodeGraph *graph);
	/// Put a document on screen — canvas, settings, properties, members,
	/// read-only state, preview.
	void showDocument(MaterialDocument *doc);
	/// OPEN A MATERIAL (MATERIALS_TABS_SPEC §2.2) — read it at the scope its
	/// ORIGIN names, deserialise it, and put it in a document. Null when no
	/// drawer holds the guid (NO TILE, NO OPEN).
	MaterialDocument *openDocument(const QString &guid, shaderInfo::Origin origin);
	/// This material's identity changed (it was just minted, or renamed):
	/// the panels and the tile that show it follow.
	void documentChanged(MaterialDocument *doc);
	/// WHICH COPIES OF A MATERIAL ARE GONE (fix round F2). Identity is
	/// (guid, origin), and the three delete gestures do three different
	/// things: taking a material out of a PROJECT leaves the library
	/// original open and editable; an UNLISTED library delete (a row a
	/// project still pins) leaves that project's copy open and editable;
	/// only a real library delete takes both. Matching on the guid alone
	/// closed tabs that were still perfectly valid.
	enum class Gone { ProjectCopy, LibraryCopy, Both };
	void forgetMaterial(const QString &guid, Gone gone);
	/// Every open document of this material takes the new name — the label,
	/// the graph's settings, the settings dock, and a save.
	void renameOpenDocuments(const QString &guid, const QString &newName);
	/// A master node on an empty canvas — the page's boot graph, and what a
	/// closed last tab leaves behind.
	NodeGraph *newMasterGraph();
	/// Make document `index` the one on screen.
	bool activateTab(int index);
	/// Close document `index`: a pending autosave is written FIRST, then the
	/// document's graph, canvas and undo history are freed.
	bool closeDocumentAt(int index);
	/// The boot canvas stands aside for the first material that opens — but
	/// only while nobody has drawn on it.
	void dropUntouchedAnonymous(MaterialDocument *keep);
	/// The bar's tabs, labels and visibility from `mDocs`.
	void syncTabBar();
	QVariantMap tabInfo(MaterialDocument *doc) const;
	/// `materials/tabs/<projectGuid>`, or `materials/tabs/library` with no
	/// project open.
	static QString tabsKeyFor(const QString &projectGuid);
	/// Write the open tab set under the key it belongs to. A no-op while a
	/// restore is pending — the page's state is not yet the saved set, and
	/// writing it would be the restore eating itself.
	void persistTabs();
	/// Open the tab set stored under `mTabsKey`. An entry whose material no
	/// drawer holds is skipped, silently: no tile, no open.
	void restoreTabs();
	/// Restore under `key` — now if the page is on screen, else the next
	/// time it is shown (opening half a dozen graphs is not boot work, and
	/// a project open usually lands the user in the editor).
	void requestTabRestore(const QString &key);
	int indexForRef(const QVariant &tabOrGuid) const;
	QSplitter *splitView = nullptr;
	AssetView* assetView;

	QDockWidget* nodeTray = nullptr;
	QWidget *centralWidget = nullptr;
	QDockWidget* displayWidget = nullptr;
	MaterialSettingsWidget *materialSettingsWidget = nullptr;

	/// Applies the PanelMetrics column widths; run once, from showEvent.
	void applyColumnWidths();
	bool mColumnsSized = false;

	QDockWidget *propertyWidget = nullptr;
	QDockWidget *materialSettingsDock = nullptr;
	QDockWidget *projectDock = nullptr;
	QDockWidget *assetsDock = nullptr;
	QTabWidget *tabbedWidget = nullptr;
	QTabWidget *tabWidget = nullptr;
	GraphicsView* graphicsView = nullptr;
	NodePropertiesPanel* nodePropertiesPanel = nullptr;
	QListWidget *nodeContainer = nullptr;
	QMenuBar *bar = nullptr;  
	QToolBar *toolBar = nullptr;
	QMenu *file = nullptr;
	QMenu *window = nullptr;
	QMenu *edit = nullptr;
	QFont font;

	ListWidget *presets = nullptr;
	ListWidget *effects = nullptr;
	/// WHAT THE OPEN MATERIAL IS MADE OF (MATERIAL_BUNDLE_SPEC 6). Lives in
	/// the left column under Material Settings, because it is about the
	/// material being edited, not about the graph's selected node.
	QDockWidget *membersDock = nullptr;
	MembersPanel *membersPanel = nullptr;

	QtAwesome *fontIcons;
	QSize defaultGridSize = QSize(70, 70);
	QSize defaultItemSize = QSize(90, 90);
	QString oldName;
	QString newName;

	QLineEdit *projectName = nullptr;
#if(EFFECT_BUILD_AS_LIB)
	ShaderAssetWidget *assetWidget;
	Database *dataBase = nullptr;
#endif
};

}
