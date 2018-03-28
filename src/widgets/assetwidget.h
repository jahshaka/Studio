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

#include <QApplication>
#include <QStyledItemDelegate>
#include <QPainter>

class ListViewDelegate : public QStyledItemDelegate
{
protected:
	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
	{


		//        painter->save();
		QPalette::ColorRole textRole = QPalette::NoRole;

		if (option.state & QStyle::State_Selected) {
			textRole = QPalette::HighlightedText;
			painter->fillRect(option.rect, QColor(70, 70, 70, 255));
		}

		if (option.state & QStyle::State_MouseOver) {
			painter->fillRect(option.rect, QColor(64, 64, 64));
		}

		// handle selection
		//if (option.state & QStyle::State_Selected) {
		//	//          painter->save();
		//	textRole = QPalette::Mid;
		//	//          QBrush selectionBrush(QColor(128, 128, 128, 128));
		//	//          painter->setBrush(selectionBrush);
		//	//          painter->drawRect(r.adjusted(1, 1,-1,-1));
		//	//          painter->restore();
		//}

		painter->setRenderHint(QPainter::Antialiasing);


		auto opt = option;
		initStyleOption(&opt, index);
		QRect r = opt.rect;
		QString title = index.data(Qt::DisplayRole).toString();
		//        QString description = index.data(Qt::UserRole + 1).toString();

		QPalette::ColorGroup cg = opt.state & QStyle::State_Enabled ? QPalette::Normal : QPalette::Disabled;
		if (cg == QPalette::Normal && !(opt.state & QStyle::State_Active))
			cg = QPalette::Inactive;

		// set pen color
		if (opt.state & QStyle::State_Selected)
			painter->setPen(opt.palette.color(cg, QPalette::HighlightedText));
		else
			painter->setPen(opt.palette.color(cg, QPalette::Text));


		QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
		//        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

		QIcon ic = QIcon(qvariant_cast<QIcon>(index.data(Qt::DecorationRole)));
		r = option.rect.adjusted(0, 0, 0, 0);
		style->drawItemPixmap(painter, r, Qt::AlignCenter, ic.pixmap(QSize(128, 128)));

		//r = option.rect.adjusted(50, 0, 0, -50);
		//        painter->drawText(r.left(), r.top(), r.width(), r.height(),
		//                          Qt::AlignBottom|Qt::AlignCenter|Qt::TextWordWrap, title, &r);
		style->drawItemText(painter, opt.rect, Qt::AlignBottom | Qt::AlignCenter | Qt::TextWordWrap,
			opt.palette, true, title, textRole);


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

    void populateAssetTree(bool initialRun);
    void updateTree(QTreeWidgetItem* parentTreeItem, QString path);
    void generateAssetThumbnails();
    void syncTreeAndView(const QString&);
	void addItem(const FolderData &folderData);
	void addItem(const AssetTileData &assetData);
	void addCrumbs(const QVector<FolderData> &folderData);
    void updateAssetView(const QString &path, bool showDependencies = false);
    void trigger();
    void updateLabels();

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

	void exportMaterial();

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
