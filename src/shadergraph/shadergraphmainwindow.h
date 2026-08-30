#pragma once

#include <functional>
#include <QListWidget>
#include <QMainWindow>
#include <QWidget>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QTextEdit>
#include <QDockWidget>
#include <QSplitter>
#include <QToolBar>
#include <QUndoStack>

#include "widgets/propertylistwidget.h"
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

class QMenuBar;
class GraphNodeScene;
class NodeGraph;
class NodeLibraryItem;
class Database;
class TexturePropertyWidget;
class UndoRedo;
class AssetView;

class QTimer;

namespace shadergraph
{
Q_NAMESPACE

class IMaterialPreviewWidget;

namespace Ui {
class MainWindow;
}

struct nodeListModel {
	QString name;
	//NodeType type;
	int inSockets;
	int outSockets;

};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow( QWidget *parent = Q_NULLPTR, Database *database = Q_NULLPTR);
    void setNodeGraph(NodeGraph* graph);
    void newNodeGraph(QString *shaderName = Q_NULLPTR, int *templateType = Q_NULLPTR, QString *templateName = Q_NULLPTR);
	
	void setAssetView(AssetView* assetView) { this->assetView = assetView; }
	/// Injected by the shell: is a project scene open? Forwarded to the
	/// module's widgets (Phase 4: was UiManager::isSceneOpen).
	void setSceneOpenProbe(std::function<bool()> probe);
private:
	std::function<bool()> mSceneOpenProbe;
public:

	// Engine viewport mode: Studio hands in the engine-rendered Display preview
	// (core/materialpreviewwidget.h). Docks it, un-hides the Display dock and
	// starts pushing the graph's evaluated PbrMaterial to it on every edit.
	void setEnginePreview(IMaterialPreviewWidget *preview);

	void refreshShaderGraph();
	void setAssetWidgetDatabase(Database *db);
	void renameShader();

	void loadGraph(QString guid);
	static QString genGUID();

    ~MainWindow();

	QList<NodeGraphPreset> list;
	QListWidgetItem *currentProjectShader = Q_NULLPTR;
	shaderInfo currentShaderInformation;
    shaderInfo pressedShaderInfo;
	QUndoStack *stack;
	

private:
	
	void saveShader();
	void saveDefaultShader();
    void loadShadersFromDisk();

	void saveMaterialFile(QString filename, TexturePropertyWidget* widget);
	void deleteMaterialFile(QString filename);

    void importGraph();
	void importEffect(QString fileName);

	NodeGraph* importGraphFromFilePath(QString filePath, bool assign = true);
	void exportEffect(QString guid);
	// keeping this around for standalone (nick)
	void exportGraph();
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
	void configureProjectDock();
	void configureAssetsDock();
	void createShader(NodeGraphPreset preset, bool loadNewGraph = true);
	void loadGraphFromTemplate(NodeGraphPreset preset);
	void setCurrentShaderItem();
	QByteArray fetchAsset(QString string);

    GraphNodeScene* createNewScene();
	void regenerateShader();
	QListWidgetItem* selectCorrectItemFromDrop(QString guid);
	int selectCorrectTabForItem(QString guid);
	QList<QString> loadedShadersGUID;

	void updateMaterialThumbnail(QString shaderGuid, QString materialGuid);
	void generateMaterialInProjectFromShader(QString guid);
	void updateMaterialFromShader(QString guid);
	void writeMaterial(QJsonObject& matObj, QString guid);
	QJsonObject writeMaterialValuesFromShader(QString guid);
private:
    void configureConnections();
    void editingFinishedOnListItem();
	void addMenuToSceneWidget();

	// Debounced graph -> PbrMaterial -> engine preview (engine viewport mode).
	void schedulePreviewUpdate();
	void updateEnginePreviewMaterial();

    Ui::MainWindow *ui;
    GraphNodeScene* scene;
	IMaterialPreviewWidget *enginePreview = nullptr;
	QMainWindow *displayWindow = nullptr;   // the Display dock's inner window (menus + preview)
	QTimer *previewUpdateTimer = nullptr;   // ~150ms debounce: slider drags evaluate once, not per pixel
	NodeGraph *graph;
	QSplitter *splitView;
	AssetView* assetView;

	QDockWidget* nodeTray;
	QWidget *centralWidget;
	QDockWidget* textWidget;
	QDockWidget* displayWidget;
	MaterialSettingsWidget *materialSettingsWidget;

	QDockWidget *propertyWidget;
	QDockWidget *materialSettingsDock;
	QDockWidget *projectDock;
	QDockWidget *assetsDock;
	QTabWidget *tabbedWidget;
	QTabWidget *tabWidget;
	GraphicsView* graphicsView;
	QTextEdit* textEdit;
	PropertyListWidget* propertyListWidget;
	QListWidget *nodeContainer;
	QMenuBar *bar;  
	QToolBar *toolBar;
	QMenu *file;
	QMenu *window;
	QMenu *edit;
	QFont font;

	ListWidget *presets;
	ListWidget *effects;

	QtAwesome *fontIcons;
	QSize defaultGridSize = QSize(70, 70);
	QSize defaultItemSize = QSize(90, 90);
	QString oldName;
	QString newName;

	QLineEdit *projectName;
#if(EFFECT_BUILD_AS_LIB)
	ShaderAssetWidget *assetWidget;
	Database *dataBase;
#endif
};

}
