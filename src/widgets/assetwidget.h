#ifndef ASSETWIDGET_H
#define ASSETWIDGET_H

namespace Ui {
    class AssetWidget;
}

class Database;

#include <QListWidget>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QFileDialog>
#include <QHBoxLayout>

#include "../io/assetmanager.h"
#include "../dialogs/progressdialog.h"
#include "../editor/thumbnailgenerator.h"
#include "../core/project.h"
#include "../widgets/sceneviewwidget.h"

// Look into this (iKlsR) - https://stackoverflow.com/questions/19465812/how-can-i-insert-qdockwidget-as-tab

struct AssetItem {
    QString selectedPath;
    QTreeWidgetItem *item;
    QListWidgetItem *wItem;
	QString selectedGuid;
    // add one for assetView maybe...
};

// This is a simple custom search predicate to be used with a stl search
struct find_asset_thumbnail
{
	QString guid;
	find_asset_thumbnail(const QString guid) : guid(guid) {}
	bool operator () (const Asset* data) const {
		return data->assetGuid == guid;
	}
};

typedef struct directory_tuple
{
	QString path;
	QString guid;
	QString parent_guid;
};

class AssetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AssetWidget(Database *handle, QWidget *parent = Q_NULLPTR);
    ~AssetWidget();

    void populateAssetTree(bool initialRun);
    void updateTree(QTreeWidgetItem* parentTreeItem, QString path);
    void generateAssetThumbnails();
    void syncTreeAndView(const QString&);
	void addItem(const FolderData &folderData);
	void addItem(const AssetTileData &assetData);
	void addCrumbs(const QVector<FolderData> &folderData);
    void updateAssetView(const QString &path);
    void trigger();
    void updateLabels();

	void extractTexturesAndMaterialFromMaterial(
		const QString &filePath,
		QStringList &textureList,
		QJsonObject &material
	);

	iris::SceneNodePtr extractTexturesAndMaterialFromMesh(
		const QString &filePath,
		QStringList &textureList,
		QJsonObject &material
	) const;

	SceneViewWidget *sceneView;

protected:
    bool eventFilter(QObject *watched, QEvent *event);
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

protected slots:
    void treeItemSelected(QTreeWidgetItem* item);
    void treeItemChanged(QTreeWidgetItem* item,int index);


    void sceneTreeCustomContextMenu(const QPoint &);
    void sceneViewCustomContextMenu(const QPoint &);
    void assetViewClicked(QListWidgetItem*);
    void assetViewDblClicked(QListWidgetItem*);

    void updateAssetItem();

    void renameTreeItem();
    void renameViewItem();

    void searchAssets(QString);
    void OnLstItemsCommitData(QWidget*);

    void deleteTreeFolder();
    void deleteItem();
    void openAtFolder();
    void createFolder();
    void importAssetB();
    void createDirectoryStructure(const QList<directory_tuple>&);
    void importAsset(const QStringList &path);

    void onThumbnailResult(ThumbnailResult* result);

private:
    Ui::AssetWidget *ui;
    AssetItem assetItem;
    QPoint startPos;

    Database *db;
	ProgressDialog *progressDialog;

    QString currentPath;

	QHBoxLayout *breadCrumbLayout;

	QButtonGroup *assetViewToggleButtonGroup;
	QPushButton *toggleIconView;
	QPushButton *toggleListView;

	QSize iconSize;
	QSize listSize;
	QSize currentSize;
};

#endif // ASSETWIDGET_H
