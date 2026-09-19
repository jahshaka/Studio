/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETPICKERWIDGET_H
#define ASSETPICKERWIDGET_H

#include <QListWidgetItem>
#include <QDialog>
#include <QPushButton>
#include <functional>
#include "io/assetmanager.h"

namespace Ui {
    class AssetPickerWidget;
}

/// THE ONE ASSET PICKER (MATERIAL_BUNDLE_SPEC P-2). The owner's "two ways" to
/// put an image on a material — pick one the project already has, or bring one
/// in from anywhere on disk — are two buttons of ONE dialog, so the editor's
/// material panel and the Materials module's texture node ask the same
/// question in the same words. The module used to open a bare QFileDialog
/// instead and write around the store; that is gone.
class AssetPickerWidget : public QDialog
{
    Q_OBJECT

public:
    AssetPickerWidget(ModelTypes type, QDialog *parent = nullptr);
    ~AssetPickerWidget();

    void populateWidget(QString filter = nullptr);

    /// IMPORT FROM DISK…, enabled by giving the picker a handler. The handler
    /// runs the content import for a chosen file and answers the asset guid —
    /// the caller owns it because only the caller knows the library and the
    /// project, and (for a material's texture node) what the image becomes a
    /// member OF. An empty answer is a refusal and the dialog stays open.
    using ImportHandler = std::function<QString(const QString &path)>;
    void setImportFromDisk(const ImportHandler &handler);

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
    ImportHandler importHandler;
    QPushButton *importButton = nullptr;
};

#endif // ASSETPICKERWIDGET_H
