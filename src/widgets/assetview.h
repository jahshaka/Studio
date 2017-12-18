#ifndef ASSETVIEW_H
#define ASSETVIEW_H

#include <QWidget>

#include "irisgl/src/core/irisutils.h"

class QSplitter;
class QListWidget;

class QLineEdit;
class QComboBox;
class QTreeWidgetItem;
class QFocusEvent;
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

class AssetViewGrid;
class AssetGridItem;
class AssetViewer;
class Database;
class SettingsManager;

class AssetView : public QWidget
{
	Q_OBJECT

public slots:
	void fetchMetadata(AssetGridItem*);

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

private:
	Database *db;
	QSplitter *_splitter;
	QWidget *_filterBar;
	QWidget *_navPane;
	QOpenGLWidget *_assetPreview;
	QWidget *_previewPane;
	QWidget *_viewPane;
	QWidget *_metadataPane;

	QListWidget *_assetView;

	QByteArray downloadedImage;
	QVector<QByteArray> iconList;
	QString filename;

	QPushButton *addToLibrary;
	QPushButton *addToProject;
	QLabel *renameModel;
	QLineEdit *renameModelField;
	QWidget *renameWidget;

	QTreeWidgetItem *rootItem;
	QVector<int> collections;

	AssetViewGrid *fastGrid;
	QWidget *emptyGrid;
	QWidget *filterPane;

	QLabel *metadataMissing;
	QLabel *metadataName;
	QLabel *metadataType;
	QLabel *metadataVisibility;
	QLabel *metadataCollection;

	QJsonArray fetchedOnlineAssets;

    QString collectionName;
    SettingsManager* settings;
	AssetViewer *viewer;
    AssetGridItem *selectedGridItem;
};

#endif // ASSETVIEW_H
