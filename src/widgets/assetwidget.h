#ifndef ASSETWIDGET_H
#define ASSETWIDGET_H

namespace Ui {
    class AssetWidget;
}

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

private:
    Ui::AssetWidget *ui;
};

#endif // ASSETWIDGET_H
