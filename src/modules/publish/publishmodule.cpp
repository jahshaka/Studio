/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/publish/publishmodule.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "data/project.h"
#include "export/embeddedpreview.h"
#include "export/exportservice.h"
#include "export/previewlauncher.h"
#include "services/sceneeditservice.h"
#include "services/services.h"

namespace {

const char *kButtonStyle =
    "QPushButton { background: #3498db; color: #ffffff; border: none; border-radius: 4px;"
    "              padding: 10px 26px; font-size: 14px; font-weight: 500; }"
    "QPushButton:hover { background: #4aa3df; }"
    "QPushButton:pressed { background: #2c81ba; }"
    "QPushButton:disabled { background: #2c313a; color: rgba(255,255,255,0.35); }";

const char *kSecondaryButtonStyle =
    "QPushButton { background: #2c313a; color: rgba(255,255,255,0.85); border: none;"
    "              border-radius: 4px; padding: 10px 20px; font-size: 13px; }"
    "QPushButton:hover { background: #363c47; }"
    "QPushButton:pressed { background: #23272e; }"
    "QPushButton:disabled { background: #23262c; color: rgba(255,255,255,0.3); }";

} // namespace

PublishPage::PublishPage(ModuleHost host_, QWidget *parent)
    : QWidget(parent), host(host_)
{
    setObjectName("publishView");
    setStyleSheet("#publishView { background: #1e1e1e; }");

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 24, 0, 24);

    auto *title = new QLabel(QStringLiteral("Publish"));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 32px; font-weight: 500; color: rgba(255,255,255,0.92); background: transparent;");
    auto *subtitle = new QLabel(QStringLiteral("Package the open scene for the web — a WebGPU viewer anyone can double-click."));
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet("font-size: 15px; color: rgba(255,255,255,0.55); background: transparent;");

    // the Web target card
    auto *card = new QFrame();
    card->setObjectName("publishCard");
    card->setStyleSheet("#publishCard { background: #262a31; border: 1px solid #32363e; border-radius: 10px; }");
    card->setFixedWidth(560);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 24, 28, 24);
    cardLayout->setSpacing(10);

    auto *cardTitle = new QLabel(QStringLiteral("Web  ·  glTF 2.0 + three.js (WebGPU)"));
    cardTitle->setStyleSheet("font-size: 17px; font-weight: 500; color: rgba(255,255,255,0.9); background: transparent;");
    dirLabel = new QLabel();
    dirLabel->setStyleSheet("font-size: 12px; color: rgba(255,255,255,0.45); background: transparent;");
    dirLabel->setWordWrap(true);

    statusLabel = new QLabel(QStringLiteral("Open a project to publish its scene."));
    statusLabel->setStyleSheet("font-size: 13px; color: rgba(255,255,255,0.65); background: transparent;");
    statusLabel->setWordWrap(true);
    detailLabel = new QLabel();
    detailLabel->setStyleSheet("font-size: 12px; color: rgba(255,255,255,0.45); background: transparent;");
    detailLabel->setWordWrap(true);
    detailLabel->hide();

    processButton = new QPushButton(QStringLiteral("Process"));
    processButton->setStyleSheet(kButtonStyle);
    processButton->setCursor(Qt::PointingHandCursor);
    processButton->setMinimumHeight(40);
    browserButton = new QPushButton(QStringLiteral("Open in browser"));
    browserButton->setStyleSheet(kSecondaryButtonStyle);
    browserButton->setCursor(Qt::PointingHandCursor);
    browserButton->setMinimumHeight(40);
    folderButton = new QPushButton(QStringLiteral("Show folder"));
    folderButton->setStyleSheet(kSecondaryButtonStyle);
    folderButton->setCursor(Qt::PointingHandCursor);
    folderButton->setMinimumHeight(40);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(10);
    buttons->addWidget(processButton);
    buttons->addWidget(browserButton);
    buttons->addWidget(folderButton);
    buttons->addStretch();

    cardLayout->addWidget(cardTitle);
    cardLayout->addWidget(dirLabel);
    cardLayout->addSpacing(6);
    cardLayout->addLayout(buttons);
    cardLayout->addSpacing(4);
    cardLayout->addWidget(statusLabel);
    cardLayout->addWidget(detailLabel);

    // In-page preview area (Linux/X11 + Chrome only): hidden until an
    // EmbeddedWebPreview adoption succeeds; every failure keeps it hidden and
    // the preview opens as the companion window exactly as before.
    previewFrame = new QFrame();
    previewFrame->setObjectName("previewFrame");
    previewFrame->setStyleSheet(
        "#previewFrame { background: #14161a; border: 1px solid #32363e; border-radius: 8px; }");
    previewFrame->setMinimumHeight(320);
    auto *pv = new QVBoxLayout(previewFrame);
    pv->setContentsMargins(10, 6, 10, 10);
    pv->setSpacing(6);
    auto *pvHeader = new QHBoxLayout();
    auto *pvLabel = new QLabel(QStringLiteral("Preview"));
    pvLabel->setStyleSheet("font-size: 12px; color: rgba(255,255,255,0.55); background: transparent;");
    popOutButton = new QPushButton(QStringLiteral("Pop out"));
    popOutButton->setStyleSheet(kSecondaryButtonStyle);
    popOutButton->setCursor(Qt::PointingHandCursor);
    pvHeader->addWidget(pvLabel);
    pvHeader->addStretch();
    pvHeader->addWidget(popOutButton);
    pv->addLayout(pvHeader);
    previewSlot = new QWidget();
    previewSlot->setStyleSheet("background: #000;");
    auto *slotLayout = new QVBoxLayout(previewSlot);
    slotLayout->setContentsMargins(0, 0, 0, 0);
    pv->addWidget(previewSlot, 1);
    previewFrame->hide();

    vl->setContentsMargins(24, 24, 24, 24);
    vl->addStretch();
    vl->addWidget(title);
    vl->addSpacing(6);
    vl->addWidget(subtitle);
    vl->addSpacing(28);
    vl->addWidget(card, 0, Qt::AlignHCenter);
    vl->addSpacing(16);
    vl->addWidget(previewFrame, 10);
    vl->addStretch();

    connect(processButton, &QPushButton::clicked, this, &PublishPage::onProcess);
    connect(browserButton, &QPushButton::clicked, this, &PublishPage::onOpenBrowser);
    connect(folderButton, &QPushButton::clicked, this, &PublishPage::onOpenFolder);
    connect(popOutButton, &QPushButton::clicked, this, [this] { onPopOut(); });

    refreshState();
}

QString PublishPage::exportDir() const
{
    // A real project (guid + folder), not just any open scene — the engine
    // selftest boots a project-less scene that must not enable publishing.
    if (!host.project || host.project->getProjectGuid().isEmpty()) return QString();
    const QString folder = host.project->getProjectFolder();
    if (folder.isEmpty()) return QString();
    return QDir(folder).filePath(QStringLiteral("exports/web"));
}

QString PublishPage::lastIndexHtml() const
{
    const QString dir = exportDir();
    if (dir.isEmpty()) return QString();
    const QString index = QDir(dir).filePath(QStringLiteral("index.html"));
    return QFileInfo::exists(index) ? index : QString();
}

void PublishPage::refreshState()
{
    const bool projectOpen = host.services && host.services->sceneEdit &&
                             host.services->sceneEdit->scene() && !exportDir().isEmpty();
    const bool hasExport = !lastIndexHtml().isEmpty();

    processButton->setEnabled(projectOpen || previewRunning);
    processButton->setText(previewRunning ? QStringLiteral("Close preview") : QStringLiteral("Process"));
    browserButton->setEnabled(hasExport);
    folderButton->setEnabled(hasExport);
    if (projectOpen)
        dirLabel->setText(QStringLiteral("Output: %1").arg(QDir::toNativeSeparators(exportDir())));
    else
        dirLabel->setText(QStringLiteral("Output: <project folder>/exports/web"));
    if (!projectOpen && !previewRunning)
        setStatus(QStringLiteral("Open a project to publish its scene."));
}

void PublishPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshState();
}

void PublishPage::setStatus(const QString &text, bool isError)
{
    statusLabel->setStyleSheet(isError
        ? "font-size: 13px; color: #e74c3c; background: transparent;"
        : "font-size: 13px; color: rgba(255,255,255,0.65); background: transparent;");
    statusLabel->setText(text);
}

void PublishPage::closePreview()
{
    if (embed) {
        preview.clear();          // that QProcess is owned by the embed controller
        embed->disconnect(this);
        embed->stop();
        embed->deleteLater();
        embed = nullptr;
        previewFrame->hide();
    }
    // Take the pointer FIRST and disconnect: waitForFinished() delivers
    // finished() synchronously, and the launch-time lambda clears `preview` —
    // going back through the QPointer afterwards is a null deref (found as a
    // live segfault in libQt6Core during the embed rig test; the bug predates
    // the embedded preview).
    if (QProcess *proc = preview.data()) {
        preview.clear();
        proc->disconnect(this);
        proc->terminate();
        if (!proc->waitForFinished(1500)) proc->kill();
        proc->deleteLater();
    }
    previewRunning = false;
}

// Try the embedded preview first (Linux/X11 + detected Chrome); any failure —
// no window, no WebGPU in the X11 browser, adoption refused, browser death —
// degrades silently to launchCompanion, which is today's behavior verbatim.
void PublishPage::startPreview(const QString &indexHtml, const QString &summary)
{
    if (EmbeddedWebPreview::platformSupported()) {
        const QString browser = PreviewLauncher::findChromiumBrowser();
        if (!browser.isEmpty()) {
            embed = new EmbeddedWebPreview(this);
            connect(embed, &EmbeddedWebPreview::embedded, this, [this, summary] {
                previewFrame->show();
                setStatus(summary + QStringLiteral(" Preview embedded — \"Pop out\" for a separate window."));
                refreshState();
            });
            connect(embed, &EmbeddedWebPreview::failed, this,
                    [this, indexHtml, summary](const QString &) {
                if (embed) { embed->stop(); embed->deleteLater(); embed = nullptr; }
                previewFrame->hide();
                preview.clear();
                previewRunning = false;
                launchCompanion(indexHtml, summary);
                refreshState();
            });
            connect(embed, &EmbeddedWebPreview::closed, this, [this] {
                if (embed) { embed->deleteLater(); embed = nullptr; }
                previewFrame->hide();
                preview.clear();
                previewRunning = false;
                setStatus(QStringLiteral("Preview closed."));
                refreshState();
            });
            if (embed->start(browser, indexHtml, previewSlot)) {
                preview = embed->process();
                previewRunning = true;
                setStatus(summary + QStringLiteral(" Starting embedded preview…"));
                return;
            }
            embed->deleteLater();
            embed = nullptr;
        }
    }
    launchCompanion(indexHtml, summary);
}

void PublishPage::launchCompanion(const QString &indexHtml, const QString &summary)
{
    QProcess *proc = PreviewLauncher::launchKiosk(indexHtml, this);
    if (proc) {
        preview = proc;
        previewRunning = true;
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int, QProcess::ExitStatus) {
                    previewRunning = false;
                    if (preview) preview->deleteLater();
                    preview.clear();
                    refreshState();
                });
        setStatus(summary + QStringLiteral(" Preview window opened (%1).")
                                .arg(QFileInfo(PreviewLauncher::findChromiumBrowser()).fileName()));
    } else {
        PreviewLauncher::openInBrowser(indexHtml);
        setStatus(summary + QStringLiteral(" No Chrome/Chromium found — opened in the default browser."));
    }
}

void PublishPage::onPopOut()
{
    if (!embed) return;
    const QString index = lastIndexHtml();
    preview.clear();
    embed->disconnect(this);
    embed->stop();
    embed->deleteLater();
    embed = nullptr;
    previewFrame->hide();
    previewRunning = false;
    if (!index.isEmpty()) launchCompanion(index, QString());
    refreshState();
}

void PublishPage::onProcess()
{
    if (previewRunning) {
        closePreview();
        setStatus(QStringLiteral("Preview closed."));
        refreshState();
        return;
    }

    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    const QString dir = exportDir();
    if (!scene || dir.isEmpty()) {
        setStatus(QStringLiteral("Open a project first."), true);
        return;
    }

    processButton->setEnabled(false);
    setStatus(QStringLiteral("Processing… exporting scene."));
    detailLabel->hide();
    repaint();

    // The same seam the `project.exportWeb` verb drives (API-first rule).
    const auto r = ExportService::exportWeb(scene, host.project ? host.project->getProjectName()
                                                                : QString(), dir);
    if (!r.ok) {
        setStatus(QStringLiteral("Export failed: %1").arg(r.error), true);
        refreshState();
        return;
    }

    QString summary = QStringLiteral("Exported %1 nodes, %2 materials — index.html %3 MB.")
        .arg(r.nodeCount).arg(r.materialCount)
        .arg(QString::number(double(r.indexSize) / (1024.0 * 1024.0), 'f', 1));
    if (!r.warnings.isEmpty()) {
        detailLabel->setText(r.warnings.join(QStringLiteral("\n")));
        detailLabel->show();
    }

    // Embedded preview when possible (Linux/X11 + Chrome), companion kiosk
    // window otherwise; default browser when no Chromium family exists
    // (audit §7.6). All embedding failures degrade silently to the companion.
    startPreview(r.indexHtml, summary);
    refreshState();
}

void PublishPage::onOpenBrowser()
{
    const QString index = lastIndexHtml();
    if (index.isEmpty()) { setStatus(QStringLiteral("No export yet — Process first."), true); return; }
    PreviewLauncher::openInBrowser(index);
}

void PublishPage::onOpenFolder()
{
    const QString dir = exportDir();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        setStatus(QStringLiteral("No export yet — Process first."), true);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

QWidget *PublishModule::createPage()
{
    return new PublishPage(host, host.shellWidget);
}
