/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PUBLISHMODULE_H
#define PUBLISHMODULE_H

// PublishModule — owns the Publish page. First real target: Web (WebGPU
// viewer), per WEB_EXPORT_AUDIT §7.6: "Process" runs the same
// ExportService::exportWeb seam as the `project.exportWeb` verb, then opens
// the export in a detected Chromium-family browser as an owned `--app`
// companion window (PreviewLauncher); "Open in browser" is the always-shown
// fallback, "Show folder" opens the export directory.

#include <QPointer>
#include <QWidget>

#include "modules/studiomodule.h"

class QLabel;
class QPushButton;
class QProcess;

class PublishPage : public QWidget
{
    Q_OBJECT
public:
    explicit PublishPage(ModuleHost host, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onProcess();
    void onOpenBrowser();
    void onOpenFolder();

private:
    QString exportDir() const;
    QString lastIndexHtml() const;
    void refreshState();
    void setStatus(const QString &text, bool isError = false);
    void closePreview();

    ModuleHost host;
    QLabel *statusLabel = nullptr;
    QLabel *detailLabel = nullptr;
    QLabel *dirLabel = nullptr;
    QPushButton *processButton = nullptr;
    QPushButton *browserButton = nullptr;
    QPushButton *folderButton = nullptr;
    QPointer<QProcess> preview;
    bool previewRunning = false;
};

class PublishModule : public StudioModule
{
public:
    QString id() const override { return QStringLiteral("publish"); }
    void initialize(ModuleHost &host) override { this->host = host; }
    QWidget *createPage() override;
    void shutdown() override {}

private:
    ModuleHost host;
};

#endif // PUBLISHMODULE_H
