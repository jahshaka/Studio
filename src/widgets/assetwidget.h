#ifndef ASSETWIDGET_H
#define ASSETWIDGET_H

namespace Ui {
    class AssetWidget;
}

#include <QListWidget>
#include <QTreeWidgetItem>
#include <QWidget>

// TODO - https://stackoverflow.com/questions/19465812/how-can-i-insert-qdockwidget-as-tab

struct AssetItem {
    QString selectedPath;
    QTreeWidgetItem *item;
    // add one for assetView...
};

class AssetWidget : public QWidget
{
    Q_OBJECT

public:
    AssetWidget(QWidget *parent = nullptr);
    ~AssetWidget();

    void populateAssetTree();
    void updateTree(QTreeWidgetItem* parentTreeItem, QString path);
    void addItem(const QString &asset);
    void updateAssetView(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event);

protected slots:
    void treeItemSelected(QTreeWidgetItem* item);
    void treeItemChanged(QTreeWidgetItem* item,int index);
    void sceneTreeCustomContextMenu(const QPoint &);
    void sceneViewCustomContextMenu(const QPoint &);
    void assetViewClicked(QListWidgetItem*);
    void assetViewDblClicked(QListWidgetItem*);

    void deleteFolder();
    void openAtFolder();
    void createFolder();
    void importAsset();

private:
    Ui::AssetWidget *ui;
    AssetItem assetItem;
};

#endif // ASSETWIDGET_H
