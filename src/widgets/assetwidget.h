#ifndef ASSETWIDGET_H
#define ASSETWIDGET_H

namespace Ui {
    class AssetWidget;
}

#include <QListWidget>
#include <QTreeWidgetItem>
#include <QWidget>

// TODO - https://stackoverflow.com/questions/19465812/how-can-i-insert-qdockwidget-as-tab

class AssetWidget : public QWidget
{
    Q_OBJECT

public:
    AssetWidget(QWidget *parent = nullptr);
    ~AssetWidget();

    void populateAssetTree();
    void addTreeRoot(const QString& name, const QString &desc = nullptr);
    void addTreeChild(QTreeWidgetItem *parent, const QString& name, const QString &desc = nullptr);
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

    void deleteFolder();

private:
    Ui::AssetWidget *ui;
};

#endif // ASSETWIDGET_H
