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
// fallback, "Show folder" opens the export directory, "Preview" (re)opens the
// preview of the existing publish without re-exporting.
//
// A publish is LINKED to its project (owner decision): one publish per
// project at the stable `<project>/exports/web` path. Process always
// (re)generates it there, PublishRecord remembers it per project, and the
// page shows the last publish (path + when, live buttons) whenever it opens —
// the empty state exists only for a never-published project, and a publish on
// record whose directory was deleted degrades to "previous publish missing —
// Process to regenerate".
//
// On Linux/X11 with a Chromium-family browser detected, the preview is first
// attempted EMBEDDED in the page (EmbeddedWebPreview adopts Chrome's X window
// into previewSlot). Every embedding failure — no window, no WebGPU, browser
// death — falls back silently to the companion window; "Pop out" switches an
// embedded preview back to the companion window on demand.
//
// Re-entry lifecycle (the double-preview fix): Process and Preview ALWAYS run
// closePreview() first — terminate the owned Chrome, destroy the container,
// hide the embed area, reset state — before starting a fresh flow. The embed
// area is visible ONLY while an adoption is live (its slot paints black by
// design); every degraded path hides it.

#include <QPointer>
#include <QWidget>

#include "modules/studiomodule.h"

class QFrame;
class QLabel;
class QPushButton;
class QProcess;
class EmbeddedWebPreview;

class PublishPage : public QWidget
{
    Q_OBJECT
public:
    explicit PublishPage(ModuleHost host, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onProcess();
    void onPreviewToggle();
    void onOpenBrowser();
    void onOpenFolder();

private:
    QString projectFolder() const;
    QString exportDir() const;
    QString lastIndexHtml() const;
    void refreshState();
    void setStatus(const QString &text, bool isError = false);
    void closePreview();
    void startPreview(const QString &indexHtml, const QString &summary);
    void launchCompanion(const QString &indexHtml, const QString &summary);
    void onPopOut();

    ModuleHost host;
    QLabel *statusLabel = nullptr;
    QLabel *detailLabel = nullptr;
    QLabel *dirLabel = nullptr;
    QPushButton *processButton = nullptr;
    QPushButton *previewButton = nullptr;  // "Preview" / "Close preview"
    QPushButton *browserButton = nullptr;
    QPushButton *folderButton = nullptr;
    QFrame *previewFrame = nullptr;    // in-page area the embed lands in
    QWidget *previewSlot = nullptr;
    QPushButton *popOutButton = nullptr;
    EmbeddedWebPreview *embed = nullptr;
    QPointer<QProcess> preview;
    bool previewRunning = false;
    QString previewSourceDir;          // export dir the live preview shows —
                                       // a project switch tears it down
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
