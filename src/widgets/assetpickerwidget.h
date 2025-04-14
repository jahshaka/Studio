/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

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
