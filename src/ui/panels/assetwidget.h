/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETWIDGET_H
#define ASSETWIDGET_H

namespace Ui {
    class AssetWidget;
}

class Database;
class Project;
struct StudioServices;
class ImportBatchRunner;
class Subscriber;

#include <QListWidget>
#include <QTreeWidgetItem>
#include <QCheckBox>
#include <QWidget>
#include <QFileDialog>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QComboBox>
#include <QVariantList>

#include "io/assetmanager.h"
#include "services/import/importtypes.h"
#include "ui/dialogs/progressdialog.h"
#include "services/thumbnailgenerator.h"
#include "data/project.h"
#include "viewport/ieditorviewport.h"
#include "irisgl/document/scenegraph/scene.h"
#include <QButtonGroup>
#include <QMenu>

// Look into this (iKlsR) - https://stackoverflow.com/questions/19465812/how-can-i-insert-qdockwidget-as-tab

struct AssetItem {
    QString selectedPath;
    QTreeWidgetItem *item;
    QListWidgetItem *wItem;
	QString selectedGuid;
    // add one for assetView maybe...
};

typedef struct directory_tupleA
{
    QString path;
    QString guid;
    QString parent_guid;
};

#include <QApplication>
#include <QStyledItemDelegate>
#include <QPainter>

class MainWindow;

class ListViewDelegate : public QStyledItemDelegate
{
protected:
	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
	{
		// painter->save();
		QPalette::ColorRole textRole = QPalette::NoRole;

		painter->setRenderHint(QPainter::Antialiasing);

		auto opt = option;
		initStyleOption(&opt, index);
		QRect r = opt.rect;

        QPen thickPen;
        thickPen.setColor(QColor(170, 169, 178, 142));
        thickPen.setWidth(3);
        painter->setPen(thickPen);

        if (option.state & QStyle::State_Selected) {
            painter->save();
            textRole = QPalette::HighlightedText;
            //painter->drawRect(r);
            painter->fillRect(r, QColor(76, 74, 72, 200));
            painter->restore();
        }
        
        if (option.state & QStyle::State_MouseOver) {
            painter->save();
            painter->drawRect(r);
            painter->fillRect(r, QColor(95, 93, 91, 128));
            painter->restore();
        }

        QFontMetrics metrix(painter->font());
        int width = r.width() - 8;
        QString clippedText = metrix.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, width);

		QString title = clippedText;
		//        QString description = index.data(Qt::UserRole + 1).toString();

		QPalette::ColorGroup cg = opt.state & QStyle::State_Enabled ? QPalette::Normal : QPalette::Disabled;
		if (cg == QPalette::Normal && !(opt.state & QStyle::State_Active)) cg = QPalette::Inactive;

		// set pen color
		if (opt.state & QStyle::State_Selected) painter->setPen(opt.palette.color(cg, QPalette::HighlightedText));
		else painter->setPen(opt.palette.color(cg, QPalette::Text));

		QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
		//        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

		QIcon ic = QIcon(qvariant_cast<QIcon>(index.data(Qt::DecorationRole)));
		r = option.rect.adjusted(0, 0, 0, 0);
		style->drawItemPixmap(painter, r, Qt::AlignCenter, ic.pixmap(QSize(128, 128)));

		//r = option.rect.adjusted(50, 0, 0, -50);
		//        painter->drawText(r.left(), r.top(), r.width(), r.height(),
		//                          Qt::AlignBottom|Qt::AlignCenter|Qt::TextWordWrap, title, &r);
		style->drawItemText(
            painter, opt.rect.adjusted(0, 0, 0, -2),
            Qt::AlignBottom | Qt::AlignCenter | Qt::TextSingleLine,
			opt.palette, true, title, textRole
        );

		//        painter->restore();
		//r = option.rect.adjusted(50, 50, 0, 0);
		//        painter->drawText(r.left(), r.top(), r.width(), r.height(), Qt::AlignLeft|Qt::TextWordWrap, description, &r);
		//        auto opt = option;
		//        initStyleOption(&opt, index);

		//        QString line0 = index.model()->data(index.model()->index(index.row(), 0)).toString();
		//        QString line1 = index.model()->data(index.model()->index(index.row(), 2)).toString();

		//        // draw correct background
		//        opt.text = "";


		//        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);


		//        painter->drawText(QRect(rect.left(), rect.height(), rect.width(), rect.height()), opt.displayAlignment, line0);
	}

	QSize sizeHint(const QStyleOptionViewItem & option, const QModelIndex & index) const
	{
		QSize result = QStyledItemDelegate::sizeHint(option, index);
		result.setHeight(100);
		result.setWidth(100);
		return result;
	}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
        const QModelIndex &index) const override
    {
        if (index.data().canConvert<QString>()) {
            QLineEdit *editor = new QLineEdit(parent);
            connect(editor, &QLineEdit::editingFinished, this, &ListViewDelegate::commitAndCloseEditor);
            return editor;
        }
        else {
            return QStyledItemDelegate::createEditor(parent, option, index);
        }
    }

    void setEditorData(QWidget *editor,
        const QModelIndex &index) const
    {
        if (index.data().canConvert<QString>()) {
            const QString text = index.data().toString();
            QLineEdit *lineEdit = qobject_cast<QLineEdit*>(editor);
            //lineEdit->setMaxLength(15);
            lineEdit->setText(text);
        }
        else {
            QStyledItemDelegate::setEditorData(editor, index);
        }
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
        const QModelIndex &index) const
    {
        if (index.data().canConvert<QString>()) {
            QLineEdit *textEditor = qobject_cast<QLineEdit *>(editor);
            model->setData(index, QVariant::fromValue(qobject_cast<QLineEdit*>(textEditor)->text()));
        }
        else {
            QStyledItemDelegate::setModelData(editor, model, index);
        }
    }

    void commitAndCloseEditor()
    {
        QLineEdit *editor = qobject_cast<QLineEdit*>(sender());
        emit commitData(editor);
        emit closeEditor(editor);
    }
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

// typedef struct directory_tuple
// {
// 	QString path;
// 	QString guid;
// 	QString parent_guid;
// };

class AssetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AssetWidget(Database *handle, QWidget *parent = Q_NULLPTR);
    /// Wired by the shell (Phase 4: the Globals::eventSubscriber bus is injected).
    void setEventBus(Subscriber *bus);
    /// The one live Project, wired by the shell in MainWindow::setupServices
    /// (Phase 4: was the Globals::project static). Nothing on the construction
    /// path reads it — the first reads happen on user interaction.
    void setProject(Project *p) { project = p; }
    /// The service bundle, for the pin-change announcement the drawer's
    /// "Update from Library" fires (AVATAR_ASSET_SPEC §4 D4). Optional: the
    /// panel works without it, the scene just re-resolves on the next open.
    void setServices(StudioServices *s) { services = s; }
    ~AssetWidget();

	AssetItem assetItem;

	int activeFilter = -1;
	/// "Show member textures" (MATERIAL_BUNDLE_SPEC V-2): off = the pictures
	/// that arrived inside a material bundle are part of it, not tiles of
	/// their own. Persisted as `tray_show_members`.
	bool showMembers = false;
	QCheckBox *showMembersBox = nullptr;
	/// A pin change for the open project queued a repopulate (coalesced).
	bool membershipRefreshPending = false;

    void updateNodeMaterialValues(iris::SceneNodePtr &node, QJsonObject definition);

    void populateAssetTree(bool initialRun);

    /// Public entry to the interactive threaded import (editor.importAssets
    /// verb; same path as the Import button and panel drops). Returns false
    /// while a batch is already running.
    /// THE VERB'S ENTRY (editor.importAssets, through
    /// MainWindow::startInteractiveImport): the same threaded batch and the
    /// same progress dialog the panel's own gestures run, but WITHOUT the
    /// import-settings dialog — a script cannot answer a modal question, and a
    /// verb that stopped on one would hang the run and everything queued
    /// behind it (found by import.shutdown, IMPORT-2). A script that wants
    /// import settings passes them: assets.import(path, {...}).
    bool importFiles(const QStringList &files);

    /// Shutdown teardown: close the progress dialog, abort a running import
    /// batch and join its worker (bounded). Safe to call repeatedly. False
    /// when the worker did NOT stop in time — the caller must then force the
    /// process exit rather than tear down objects the worker still uses.
    bool shutdownImports(int msTimeout);
    void updateTree(QTreeWidgetItem* parentTreeItem, QString path);
    void generateAssetThumbnails();
    void syncTreeAndView(const QString&);
	void addItem(const FolderRecord &folderData);
	void addItem(const AssetRecord &assetData);
	void addCrumbs(const QVector<FolderRecord> &folderData);
    /// Lists `path` (a folder guid; the project guid = the root) through the
    /// tray rule (services/assettray.h) — the same listing assets.list({tray:
    /// true}) answers with.
    void updateAssetView(const QString &path, int filter = 0);
    /// WHERE A TILE IS, for a synthesised gesture (DRAWERS-1,
    /// `editor.dragAssetToTray`): the widget a drop is posted to — THIS panel,
    /// because the list's viewport does not accept drops — and the point at the
    /// centre of `guid`'s tile in that widget's coordinates. A null point when
    /// the tray is not showing it, or is not laid out.
    QWidget *tileViewport() const;
    QPoint tileCentre(const QString &guid);
    /// What the tray is SHOWING, in order: [{guid, name, folder}] (`name` is
    /// the catalog name for an asset, the label for a folder) — the
    /// editor.trayAssets verb, which is how a suite proves the panel and the
    /// verb agree. A repopulate queued by a pin change is applied first.
    QVariantList shownTiles();
    /// Applies a repopulate a pin change queued (no-op when none is pending).
    void flushPendingRefresh();
    void updateAssetContentsView(const QString &guid);
    void trigger();
    void refresh();

	void extractTexturesAndMaterialFromMaterial(
		const QString &filePath,
		QStringList &textureList,
		QJsonObject &material
	);

	void extractTexturesAndMaterialFromMaterial(
		const QByteArray &blob,
		QStringList &textureList,
		QJsonObject &material
	);

    void setMainWindow(MainWindow* mainWindow) {
        this->mainWindow = mainWindow;
    }

    MainWindow *mainWindow;

	IEditorViewport *sceneView;

signals:
	void assetItemSelected(QListWidgetItem*);
	/// THE DRAWER -> MODULE SEAM (AVATAR_ASSET_SPEC §5.5): "Edit in Avatar
	/// Module" on a drawer row. The shell switches space and calls the
	/// module's verb; the panel never includes mainwindow.h.
	void editAssetInModule(const QString &guid, const QString &moduleId, const QString &scope);
	/// The scene wants an instance of an avatar asset ("Add to Scene", and the
	/// viewport's drop of an avatar row). Routed through the shell for the
	/// same reason.
	void spawnAvatarInScene(const QString &guid);
	/// THE IMPORT DECISION (SPECS/IMPORT_DIALOG_SPEC.md §8), both halves,
	/// routed through the shell for the same reason as the rows above — the
	/// dialog's OK commits through the assets.reimport verb, and this panel
	/// never includes the scripting layer.
	void reimportAssetRequested(const QString &guid);

protected:
    bool eventFilter(QObject *watched, QEvent *event);
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;

    /// The texture/material .jaf exports' payload: each member guid's stored
    /// bytes copied into `<writePath>/assets/` under its display name.
    void copyMemberFilesForExport(const QStringList &members, const QString &writePath);

    /// The FOLDER tile at a viewport point, or null — the internal drop's
    /// target test (DRAWERS-1).
    QListWidgetItem *folderItemAt(const QPoint &pos) const;
    /// Files `guids` in `folderGuid`, undoably and as ONE step, and repopulates
    /// (the body of `assets.moveToFolder`, on the editor's stack).
    void moveToFolder(const QStringList &guids, const QString &folderGuid);

protected slots:
    void treeItemSelected(QTreeWidgetItem* item);
    void treeItemChanged(QTreeWidgetItem* item,int index);
	void updateAssetSkyItemFromSkyPropertyWidget(const QString &guid, iris::SkyType skyType);

    void sceneTreeCustomContextMenu(const QPoint &);
    void sceneViewCustomContextMenu(const QPoint &);
    void assetViewClicked(QListWidgetItem*);
    void assetViewDblClicked(QListWidgetItem*);


    void renameViewItem();
    void favoriteItem();
    void refreshThumbnail();

	void editFileExternally();
	void exportTexture();
	/// Image items (IMAGE_PLANE_SPEC option B1): mints the companion PBR
	/// material asset for the selected image and pins it into the project.
	void createMaterialFromImage();
	/// AVATAR_ASSET_SPEC §5.5, the drawer's four avatar rows.
	void editAvatarInModule();
	void addAvatarToScene();
	void createAvatarFromModel();
	void updateAvatarFromLibrary();
	void saveAvatarToLibrary();
    void exportSky();
    void exportMaterial();
	void exportMaterialPreview();
	void exportShader();
	void exportAssetPack();

    void searchAssets(QString);
    void OnLstItemsCommitData(QWidget*);

    void deleteTreeFolder();
    void deleteItem();
    void createSky();
    void createFolder();
    /// Right-click on a FOLDER tile (DRAWERS-1): the project-side delete — the
    /// folder goes and everything in it moves up to its parent.
    void deleteFolderItem();
    /// Right-click on the BACKGROUND > Create > Material (owner review item 4):
    /// `materials.create(name, {folder})` at the folder the user is looking at.
    void createMaterial();
    void importAssetB();
    /// `askImportSettings` opens the import dialog once per MODEL file first
    /// (SPECS/IMPORT_DIALOG_SPEC.md §8). True for the panel's own gestures — a
    /// drop, the Import Asset row — and false for the verb (above).
    void importAsset(const QStringList &path, bool askImportSettings = true);

    void onThumbnailResult(const ThumbnailResult &result);

private:
    /// True while the import-settings dialog is up. It is part of the
    /// one-import-at-a-time guard: ImportBatchRunner::isRunning() is false
    /// while a modal question waits, and a second batch started behind it
    /// aborts the Debug build (SPECS/IMPORT_DIALOG_SPEC.md §8).
    bool mAsking = false;

    Ui::AssetWidget *ui;
    QPoint startPos;

    Database *db = nullptr;
    StudioServices *services = nullptr;
    Project *project = nullptr;   // the live Project (Phase 4: was Globals::project)
	ProgressDialog *progressDialog;
	// Threaded import batch (UI-freeze fix): heavy pipeline half on a worker,
	// pin + refresh back on the UI thread. One batch at a time.
	ImportBatchRunner *importRunner = nullptr;
	QStringList importErrors;
	/// MODELS IMPORTED HERE STILL OWE A THUMBNAIL (THUMBS-1 item 3). The
	/// editor tray's import ran the pipeline and pinned the row and then
	/// stopped — it never asked for the render the Assets page's tail asks
	/// for, so a model dragged into the TRAY got a generic type icon for
	/// ever. Drained one per event-loop turn, like the page's tail queue, so
	/// a batch of models never holds the UI thread.
	QStringList thumbnailBacklog;
	/// Renders and stores one backlog entry, then re-arms itself for the next.
	void drainThumbnailBacklog();

    QString currentPath;

	QHBoxLayout *breadCrumbLayout;

    QHBoxLayout *filterGroupLayout;
    QComboBox *assetFilterCombo;

	// Display ▾ popup (desktop popup-button pattern): the checked menu
	// entry is the current view mode (Grid / List).
	QPushButton *displayButton;
	QMenu *displayMenu;
	QAction *displayGridAction;
	QAction *displayListAction;

	QPushButton *goUpOneControl;    // goes to parent dir

	QSize iconSize;
	QSize listSize;
	QSize currentSize;

    bool draggingItem;
};

#endif // ASSETWIDGET_H
