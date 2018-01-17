#ifndef ASSETVIEW_H
#define ASSETVIEW_H

#include <QWidget>

#include "irisgl/src/graphics/mesh.h"
#include "irisgl/src/core/irisutils.h"

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

class AssetView : public QWidget
{
	Q_OBJECT

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
	void focusInEvent(QFocusEvent *event);
	bool eventFilter(QObject *watched, QEvent *event);
    void copyTextures(const QString &folderGuid);
    void checkForEmptyState();
    void toggleFilterPane(bool);
    void closeViewer();
	QString getAssetType(int);

	void importModel(const QString &filename);

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

	QPushButton *addToLibrary;
	QPushButton *addToProject;
    QPushButton *deleteFromLibrary;
	QLabel *renameModel;
	QLineEdit *renameModelField;
	QLabel *tagModel;
	QLineEdit *tagModelField;
	QWidget *renameWidget;
	QWidget *tagWidget;

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
};

#endif // ASSETVIEW_H
