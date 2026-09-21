#pragma once
#include <functional>
#include <QWidget>
#include <QLayout>
#include <QLabel>
#include <QListWidget>
#include <QJsonObject>
#include <QPushButton>
#include <QLineEdit>
#include <QStackedWidget>
#include <QVariantList>
#include "listwidget.h"
#include "shaderlistwidget.h"
#include "data/project.h"


//#include "../widgets/assetwidget.h"

struct AssetItemShader {
	QString selectedPath; 
	QListWidgetItem *wItem = nullptr;
	QString selectedGuid;
	// add one for assetView maybe...
};



class Database;
class Project;
class ShaderAssetWidget : public QWidget
{
	Q_OBJECT
public:
	/// Injected by the module window: is a project scene open? (Phase 4:
	/// was UiManager::isSceneOpen). Null-safe: no probe = treated as closed.
	std::function<bool()> sceneOpenProbe;
	/// The one live Project, injected by the shadergraph window (Phase 4: was
	/// the Globals::project static).
	Project *project = nullptr;
	ShaderAssetWidget(Database *handle = Q_NULLPTR);
	~ShaderAssetWidget();


	QVBoxLayout *layout;
	QStackedWidget *stackWidget;
	AssetItemShader assetItemShader;

	void addItem(const AssetRecord &assetData);
	/// A library tile dropped on this drawer: pinned into the project.
	void addDroppedToProject(QListWidgetItem *item);
	// (createShader(QListWidgetItem*) is DELETED — MATERIAL_BUNDLE_SPEC phase
	// 1's Deletes column. It was the "add to project" CLONE: a second Shader
	// row in the project plus a QFile::copy of every texture into the project
	// folder under a guid-shaped name, outside the store, with no pin and no
	// sidecar. Adding a material to a project is ProjectAssets::addToProject
	// now, like every other asset — one row, pinned with its closure.)
	QByteArray fetchAsset(QString string);
	ShaderListWidget *assetViewWidget;
	/// THE PROJECT'S MATERIALS, WHEREVER THEY ARE FILED (DRAWERS-1). This
	/// drawer has no breadcrumb and no folder navigation — it never had, the
	/// folder code it carried was unreachable — so it is the FLAT view of the
	/// one model (services/assettray.h `listAll`, filtered to materials) and a
	/// material the user files in a folder in the editor's tray is still here.
	void updateAssetView();
    void setUpDatabase(Database *db);
	void refresh();
	/// What the drawer is showing, in order: [{guid, name}] — the
	/// `materials.projectDrawer()` verb, which is how a suite proves the two
	/// project drawers are one list.
	QVariantList shownTiles();
private:
	void setWidgetToBeShown();
	QWidget *noWidget;
	Database *db = nullptr;
	QSize currentSize = QSize(90, 90);
	QPushButton* closeBtn;

	void configureConnections();
	void deleteShader(QString guid);
	void editingFinishedOnListItem(QListWidgetItem *);
	/// A repopulate queued by the one refresh signal (coalesced).
	bool mRefreshPending = false;
	void flushPendingRefresh();
signals:
	void loadToGraph(QListWidgetItem *item);
	/// The user typed a new name on a tile in THIS drawer. The page does the
	/// rename (MATERIALS_TABS_SPEC §7) — there is one of them, and it is not
	/// `Database::renameAsset`.
	void assetRenamed(const QString &guid, const QString &newName);
	/// A tile left THIS PROJECT (the drawer's Delete is a pin removal). The
	/// page closes the project-scope tab that was editing it — with the pin
	/// gone there is nothing behind it (fix round F1).
	void assetRemoved(const QString &guid);
};

