#ifndef ASSETWIDGET_H
#define ASSETWIDGET_H

namespace Ui {
    class AssetWidget;
}

#include <QListWidget>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QFileDialog>

#include "../io/assetmanager.h"

// TODO - https://stackoverflow.com/questions/19465812/how-can-i-insert-qdockwidget-as-tab

struct AssetItem {
    QString selectedPath;
    QTreeWidgetItem *item;
    QListWidgetItem *wItem;
    // add one for assetView...
};

#define     ID_ROLE     0x0111

class AssetWidget : public QWidget
{
    Q_OBJECT

public:
    AssetWidget(QWidget *parent = nullptr);
    ~AssetWidget();

    void populateAssetTree();
    void updateTree(QTreeWidgetItem* parentTreeItem, QString path);
    void walkFileSystem(QString folder, QString path);
    void addItem(const QString &asset);
    void updateAssetView(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event);
//    void mousePressEvent(QMouseEvent*);
//    void mouseMoveEvent(QMouseEvent*);
//    void dragMoveEvent(QDragMoveEvent*);
    void dragEnterEvent(QDragEnterEvent*);
    void dropEvent(QDropEvent*);
    void handleMouseMoveEvent(QMouseEvent *event);

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
    void deleteViewFolder();
    void openAtFolder();
    void createFolder();
    void importAssetB();
    void importAsset(const QStringList &path);

private:
    Ui::AssetWidget *ui;
    AssetItem assetItem;
};

#endif // ASSETWIDGET_H
