#pragma once

#include <functional>

#include <QSet>

#include "ui/style/columnedpage.h"
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
#include "misc/QtAwesome.h"
#include "misc/QtAwesomeAnim.h"

#if(EFFECT_BUILD_AS_LIB)
#include "widgets/shaderassetwidget.h"
#endif

class QLabel;
class QMenuBar;
class GraphNodeScene;
class NodeGraph;
class NodeLibraryItem;
class Database;
class Project;
class TexturePropertyWidget;
class UndoRedo;
class AssetView;
struct ThumbnailResult;

class QTimer;

namespace materials
{
Q_NAMESPACE

class IMaterialPreviewWidget;
class MembersPanel;


struct nodeListModel {
	QString name;
	//NodeType type;
	int inSockets;
	int outSockets;

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
private:
	std::function<bool()> mSceneOpenProbe;
	Project *mProject = nullptr;
public:
	/// A GRAPH EDIT REACHES THE SCENE (OWNER_REVIEW 9, R19 D2). The page has
	/// no business knowing about scene nodes, so the shell hands it one
	/// callback and the page calls it with the material's guid every time it
	/// commits a definition; MaterialsModule wires it to the ONE apply
	/// (SceneEditService), which is what the drop and `material.apply` use.
	/// Until this lane an edit reached the scene only through a material
	/// SWITCH, and only while the Projects tab happened to be current.
	std::function<void(const QString &)> mMaterialChanged;


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

    ~EffectsPage();

	QList<NodeGraphPreset> list;
	QListWidgetItem *currentProjectShader = Q_NULLPTR;
	shaderInfo currentShaderInformation;
    shaderInfo pressedShaderInfo;
	QUndoStack *stack;
	

private:
	
	void saveShader();
	/// Tell the user a save was REFUSED (F16): the graph is on screen and is
	/// not being written down, which no log line can say loudly enough.
	void reportSaveRefused(const QString &why);
	void saveDefaultShader();

	/// Queues the saved graph's thumbnail on the shell's ThumbnailGenerator
	/// (the module never reaches into src/bridge itself) and delivers it in
	/// onShaderThumbnail — the tile, the asset row and the derived material
	/// all update from there (VISUAL_PARITY_SPEC item 5).
	void requestShaderThumbnail(const QString &shaderGuid);
	void onShaderThumbnail(const ThumbnailResult &result);
	bool mThumbnailConnected = false;
	bool mSaveRefused = false;

	/// THE OPEN MATERIAL IS A SHIPPED PRESET, ON SCREEN TO BE READ
	/// (PRESET-UNIFY-1). Selecting a preset shows its graph — the owner's
	/// first acceptance criterion — and a preset is read-only in fact: the
	/// definition writer refuses its reserved guid. So the page must not
	/// OFFER an edit it cannot honour: `saveShader` stands down, the autosave
	/// timer never fires a refusal into the scene-issue bar, and the banner
	/// above the canvas says so and offers the one gesture that works.
	bool mReadOnly = false;
	QString mReadOnlyName;
	QWidget *mReadOnlyBanner = nullptr;
	QLabel  *mReadOnlyLabel = nullptr;
	/// Show or hide the banner and set `mReadOnly`.
	void setReadOnly(bool readOnly, const QString &presetName = QString());
	/// The guids WE asked the shared thumbnail queue about (a material render
	/// is not ours by type alone any more — see requestShaderThumbnail).
	QSet<QString> mPendingThumbnails;
    void loadShadersFromDisk();

	void deleteMaterialFile(QString filename);

	/// Import a material share file (services/assetshare.h) into the library
	/// — the toolbar's Import and the drawer's "Import material…".
    void importGraph();

	NodeGraph* importGraphFromFilePath(QString filePath, bool assign = true);
	/// Write ONE material and its closure as a share file.
	void exportEffect(QString guid);
	/// ONE ROW, COPIED: a second library bundle carrying the same definition
	/// (the members are SHARED, not copied — Make unique is how a picture
	/// becomes private to one material).
	void duplicateShader(QString guid);
    void restoreGraphPositions(const QJsonObject& data);
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
	void loadGraphFromTemplate(NodeGraphPreset preset);
	void setCurrentShaderItem();
	QByteArray fetchAsset(QString string);

	/// A GRAPH EDIT REACHES THE SCENE (OWNER_REVIEW 9, R19 D2). The page has
	/// no business knowing about scene nodes, so the shell hands it one
	/// callback and the page calls it with the material's guid every time it
	/// commits a definition; MaterialsModule wires it to the ONE apply
	/// (SceneEditService), which is what the drop and `material.apply` use.
	/// Until this lane an edit reached the scene only through a material
	/// SWITCH, and only while the Projects tab happened to be current.

    GraphNodeScene* createNewScene();
	QListWidgetItem* selectCorrectItemFromDrop(QString guid);
	int selectCorrectTabForItem(QString guid);
	/// Which DRAWER a tile lives in, as the scope an edit to it belongs to.
	shaderInfo::Origin originForItem(QString guid);
	QList<QString> loadedShadersGUID;

private:
    void configureConnections();
    void editingFinishedOnListItem();
	void addMenuToSceneWidget();

	// Debounced graph -> PbrMaterial -> engine preview (engine viewport mode).
	void schedulePreviewUpdate();
	void updateEnginePreviewMaterial();

    GraphNodeScene* scene;
	IMaterialPreviewWidget *enginePreview = nullptr;
	QMainWindow *displayWindow = nullptr;   // the Display dock's inner window (menus + preview)
	QTimer *previewUpdateTimer = nullptr;   // 300ms debounce: slider drags bake once, not per pixel
	QTimer *positionSaveTimer = nullptr;    // 1.5s debounce: moved nodes persist without an explicit save
	bool restoringGraph = false;            // suppress position-saves while a graph is being (re)built
	quint64 previewGeneration = 0;          // latest-wins stamp for async preview bakes
	NodeGraph *graph;
	QSplitter *splitView;
	AssetView* assetView;

	QDockWidget* nodeTray;
	QWidget *centralWidget;
	QDockWidget* displayWidget;
	MaterialSettingsWidget *materialSettingsWidget;

	/// Applies the PanelMetrics column widths; run once, from showEvent.
	void applyColumnWidths();
	bool mColumnsSized = false;

	QDockWidget *propertyWidget;
	QDockWidget *materialSettingsDock;
	QDockWidget *projectDock;
	QDockWidget *assetsDock;
	QTabWidget *tabbedWidget;
	QTabWidget *tabWidget;
	GraphicsView* graphicsView;
	NodePropertiesPanel* nodePropertiesPanel;
	QListWidget *nodeContainer;
	QMenuBar *bar;  
	QToolBar *toolBar;
	QMenu *file;
	QMenu *window;
	QMenu *edit;
	QFont font;

	ListWidget *presets;
	ListWidget *effects;
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

	QLineEdit *projectName;
#if(EFFECT_BUILD_AS_LIB)
	ShaderAssetWidget *assetWidget;
	Database *dataBase = nullptr;
#endif
};

}
