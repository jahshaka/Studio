#ifndef ASSETPICKERWIDGET_H
#define ASSETPICKERWIDGET_H

#include <QListWidgetItem>
#include <QDialog>
#include "../io/assetmanager.h"

namespace Ui {
    class AssetPickerWidget;
}

class AssetPickerWidget : public QDialog
{
    Q_OBJECT

public:
    AssetPickerWidget(AssetType type, QDialog *parent = nullptr);
    ~AssetPickerWidget();

    void populateWidget(AssetType);

public slots:
    void assetViewDblClicked(QListWidgetItem*);

signals:
    void itemDoubleClicked(QListWidgetItem*);

protected:
    bool eventFilter(QObject *watched, QEvent *event);

private:
    Ui::AssetPickerWidget *ui;
};

#endif // ASSETPICKERWIDGET_H
