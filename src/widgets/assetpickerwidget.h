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
    AssetPickerWidget(ModelTypes type, QDialog *parent = nullptr);
    ~AssetPickerWidget();

    void populateWidget(QString filter = nullptr);

public slots:
    void assetViewDblClicked(QListWidgetItem*);
    void refreshList();
    void changeView(bool);
    void searchAssets(QString);

signals:
    void itemDoubleClicked(QListWidgetItem*);

protected:
    bool eventFilter(QObject *watched, QEvent *event);

private:
    Ui::AssetPickerWidget *ui;
	ModelTypes type;
};

#endif // ASSETPICKERWIDGET_H
