/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

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
#include <QStackedLayout>

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

class AssetView : public QWidget
{
	Q_OBJECT

public slots:
	void fetchMetadata(AssetGridItem*);

	void addAssetItemToProject(AssetGridItem*);
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
	void addToJahLibrary(const QString fileName, const QString guid, bool jfx = false);
	void addToLibrary(bool jfx = false);
	void spaceSplits();
    void closeViewer();
	void clearViewer();
	QString getAssetType(int);

	void importJahModel(const QString &filename);
	void importJahBundle(const QString &filename);
	void importModel(const QString &filename, bool jfx = false);

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

    QWidget *assetImageViewer;
    QLabel *assetImageCanvas;

    QWidget *viewersWidget;
    QStackedLayout *viewers;

	QWidget *parent;
};

#endif // ASSETVIEW_H
