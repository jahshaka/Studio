#ifndef ASSETVIEW_H
#define ASSETVIEW_H

#include <QWidget>

#include "irisgl/src/graphics/mesh.h"
#include "irisgl/src/core/irisutils.h"
#include "../helpinglabels.h"

class QSplitter;
class QListWidget;

class QLineEdit;
class QComboBox;
class QTreeWidgetItem;
class QFocusEvent;
#include <QTreeWidget>
#include <QPushButton>
#include <QJsonObject>
#include <QJsonArray>
#include <QApplication>
#include <QResizeEvent>
#include <QGridLayout>
#include <QScrollArea>
#include <qdebug.h>
#include <QLabel>
#include <QOpenGLWidget>
#include <QButtonGroup>

#ifdef Q_OS_WIN
	#include <Windows.h>
	#define WIN32_MEAN_AND_LEAN
#endif // Q_OS_WIN


enum AssetSource
{
	LOCAL,
	ONLINE
};

enum AssetMetaType
{
	Shader,
	Material,
	Texture,
	Video,
	Cubemap,
	Object,
	SoundEffect,
	Music
};

class AssetViewGrid;
class AssetGridItem;
class AssetViewer;
class Database;
class SettingsManager;
class PreferencesDialog;
class HelpingLabels;

class AssetView : public QWidget
{
	Q_OBJECT

protected:
	void changeEvent(QEvent *event) override;
public slots:
	void fetchMetadata(AssetGridItem*);

	void addAssetToProject(AssetGridItem*);
	void changeAssetCollection(AssetGridItem*);
	void removeAssetFromProject(AssetGridItem*);

public:
	int gridCount;
	QButtonGroup *sourceGroup;
	QAbstractButton *localAssetsButton;
	QAbstractButton *onlineAssetsButton;
	AssetSource assetSource;
	AssetView(Database *handle, QWidget *parent = Q_NULLPTR);
	~AssetView();
	void changeAllLabels();
	void focusInEvent(QFocusEvent *event);
	bool eventFilter(QObject *watched, QEvent *event);
    void copyTextures(const QString &folderGuid);
    void checkForEmptyState();
    void toggleFilterPane(bool);
	void addToJahLibrary(const QString fileName, const QString guid, bool jfx = false);
	void addToLibrary(bool jfx = false);
	void spaceSplits();
    void closeViewer();
	void clearViewer();
	QString getAssetType(int);

	void importJahModel(const QString &filename);
	void importModel(const QString &filename, bool jfx = false);

    void updateNodeMaterialValues(iris::SceneNodePtr &node, QJsonObject definition);

signals:
    void refreshCollections();

private:
	Database *db;
	QSplitter *_splitter;
	QWidget *_filterBar;
	QWidget *_navPane;
	QOpenGLWidget *_assetPreview;
	QWidget *_previewPane;
	QWidget *_viewPane;
	QWidget *_metadataPane;

	QWidget *assetDropPad;

    QSplitter *split;

	QListWidget *_assetView;

	QByteArray downloadedImage;
	QVector<QByteArray> iconList;
	QString filename;

	PreferencesDialog* prefsDialog;

	QPushButton *updateAsset;
	QPushButton *addToProject;
    QPushButton *deleteFromLibrary;
	QLabel *renameModel;
	QLineEdit *renameModelField;
	QLabel *tagModel;
	QLineEdit *tagModelField;
	QWidget *renameWidget;
	QWidget *tagWidget;

	QLabel *backdropLabel;
	QComboBox *backdropColor;

    QTreeWidget *treeWidget;
	QTreeWidgetItem *rootItem;
	QVector<int> collections;

	AssetViewGrid *fastGrid;
	QWidget *emptyGrid;
	QWidget *filterPane;

    QPushButton *normalize;
	QLabel *metadataMissing;
	QLabel *metadataName;
	QLabel *metadataType;
	QLabel *metadataVisibility;
	QLabel *metadataLicense;
	QLabel *metadataAuthor;
	QLabel *metadataTags;
    QWidget *metadataWidget;
    QHBoxLayout *metadataLayout;
    QPushButton *changeMetaCollection;
	QLabel *metadataCollection;

	QJsonArray fetchedOnlineAssets;

    QString collectionName;
    SettingsManager* settings;
	AssetViewer *viewer;
    AssetGridItem *selectedGridItem;
	QTimer *searchTimer;
	QString searchTerm;
	QLineEdit *le;

	HelpingLabels *helpingLabels;
	QLabel *header;
	QPushButton *collectionButton;
	QPushButton *accept;
	QLabel *emptyLabel;
	QLabel *assetDropPadLabel;
	QPushButton *browseButton ;
	QPushButton *downloadWorld ;
};

#endif // ASSETVIEW_H
