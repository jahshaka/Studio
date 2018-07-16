/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

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
#include <QLineEdit>
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

	AssetItem assetItem;

	bool hideDependencies;

    void updateNodeMaterialValues(iris::SceneNodePtr &node, QJsonObject definition);

    void populateAssetTree(bool initialRun);
    void updateTree(QTreeWidgetItem* parentTreeItem, QString path);
    void generateAssetThumbnails();
    void syncTreeAndView(const QString&);
	void addItem(const FolderRecord &folderData);
	void addItem(const AssetRecord &assetData);
	void addCrumbs(const QVector<FolderRecord> &folderData);
    void updateAssetView(const QString &path, int filter = -1, bool showDependencies = false);
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

	SceneViewWidget *sceneView;

signals:
	void assetItemSelected(QListWidgetItem*);

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
    void favoriteItem();

	void editFileExternally();
	void exportTexture();
	void exportMaterial();
	void exportMaterialPreview();
	void exportShader();
	void exportAssetPack();

    void searchAssets(QString);
    void OnLstItemsCommitData(QWidget*);

    void deleteTreeFolder();
    void deleteItem();
    void openAtFolder();
	void createShader();
    void createFolder();
    void importAssetB();
    void importAsset(const QStringList &path);
    void importRegularAssets(const QList<directory_tuple>&);
    void importJafAssets(const QList<directory_tuple>&);

    void onThumbnailResult(ThumbnailResult* result);

private:
    Ui::AssetWidget *ui;
    QPoint startPos;

    Database *db;
	ProgressDialog *progressDialog;

    QString currentPath;

	QHBoxLayout *breadCrumbLayout;

    QHBoxLayout *filterGroupLayout;
    QComboBox *assetFilterCombo;

	QButtonGroup *assetViewToggleButtonGroup;
	QPushButton *toggleIconView;
	QPushButton *toggleListView;

	QPushButton *goBackOneControl;  // goes to previous dir
	QPushButton *goUpOneControl;    // goes to parent dir

	QSize iconSize;
	QSize listSize;
	QSize currentSize;

    bool draggingItem;
};

#endif // ASSETWIDGET_H
