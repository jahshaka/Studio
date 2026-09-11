/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// THE APP'S DIALOGS, BY NAME (theme sweep, lane 16) — what app.dialogs() lists
// and app.dialog(name, open) opens and closes.
//
// Why a catalog at all: the theme walk (app.styleSheets) can only inspect
// widgets that EXIST, and most dialogs are built on demand and destroyed when
// they close — a walk of the running app never sees their sheets. Opening them
// by name is also what a rig needs to photograph them, and what an MCP client
// needs to show the user a dialog it is talking about.
//
// Every entry opens with show(), never exec(): a verb cannot sit inside exec()
// (the script host would never get control back), so a dialog that production
// code runs with exec() is shown here — same widget tree, same sheets, NO
// result to consume (newProject/renameProject/getName's accept buttons do
// nothing this way: INSPECTION ONLY). A dialog that sets its own modality
// (Preferences, Qt::ApplicationModal) is still modal when shown (theme review SF-2). Dialogs the window already owns (Preferences, About,
// Claude) are shown and hidden, never rebuilt; the rest are built fresh per
// open and deleted on close.

#include "shell/mainwindow.h"

#include "ui/dialogs/aboutdialog.h"
#include "ui/dialogs/donatedialog.h"
#include "ui/dialogs/getnamedialog.h"
#include "ui/dialogs/newprojectdialog.h"
#include "ui/dialogs/preferencesdialog.h"
#include "ui/dialogs/progressdialog.h"
#include "ui/dialogs/renameprojectdialog.h"
#include "ui/dialogs/screenshotwidget.h"
#include "ui/dialogs/softwareupdatedialog.h"
#include "ui/pages/projectmanager.h"
#include "ui/windows/claudechatwindow.h"

#include <QImage>
#include <QLayout>

QStringList MainWindow::dialogNames() const
{
    return { QStringLiteral("preferences"), QStringLiteral("about"),
             QStringLiteral("newProject"), QStringLiteral("renameProject"),
             QStringLiteral("sampleBrowser"), QStringLiteral("progress"),
             QStringLiteral("getName"), QStringLiteral("screenshot"),
             QStringLiteral("donate"), QStringLiteral("softwareUpdate"),
             QStringLiteral("claudeChat") };
}

QWidget *MainWindow::openDialog(const QString &name)
{
    if (!dialogNames().contains(name)) return nullptr;
    if (QWidget *open = scriptDialogs.value(name); open && open->isVisible()) {
        open->raise();
        return open;
    }

    QWidget *dialog = nullptr;
    bool owned = true;   // built here, deleted on close
    if (name == QLatin1String("preferences")) {
        dialog = prefsDialog;
        owned = false;
    } else if (name == QLatin1String("about")) {
        dialog = aboutDialog;
        owned = false;
    } else if (name == QLatin1String("claudeChat")) {
        if (!claudeChatWindow || !claudeChatWindow->isVisible()) toggleClaudeChat();
        dialog = claudeChatWindow;
        owned = false;
    } else if (name == QLatin1String("sampleBrowser")) {
        dialog = pmContainer ? pmContainer->prepareSampleBrowser() : nullptr;
        owned = false;
    } else if (name == QLatin1String("newProject")) {
        dialog = new NewProjectDialog;
    } else if (name == QLatin1String("renameProject")) {
        dialog = new RenameProjectDialog;
    } else if (name == QLatin1String("progress")) {
        auto *progress = new ProgressDialog(this);
        progress->setLabelText(tr("Importing assets"));
        progress->setStageText(tr("Reading files"));
        progress->setRange(0, 100);
        progress->setValue(40);
        dialog = progress;
    } else if (name == QLatin1String("getName")) {
        auto *getName = new GetNameDialog(this);
        getName->setName(QStringLiteral("Animation1"));
        getName->setWindowTitle(tr("New Animation Name"));
        dialog = getName;
    } else if (name == QLatin1String("screenshot")) {
        // A flat mid-grey frame stands in for the viewport grab: the dialog's
        // chrome is what matters here, and a render would need a scene.
        auto *shot = new ScreenshotWidget(this);
        shot->setMaximumWidth(1280);
        shot->setMaximumHeight(720);
        shot->layout()->setSizeConstraint(QLayout::SetNoConstraint);
        QImage frame(640, 360, QImage::Format_RGB32);
        frame.fill(QColor(0x40, 0x40, 0x40));
        shot->setImage(frame);
        dialog = shot;
    } else if (name == QLatin1String("donate")) {
        dialog = new DonateDialog;
    } else if (name == QLatin1String("softwareUpdate")) {
        auto *update = new SoftwareUpdateDialog(this);
        update->setVersionNotes(QStringLiteral("<p>Release notes.</p>"));
        dialog = update;
    }
    if (!dialog) return nullptr;

    if (owned) dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    scriptDialogs.insert(name, dialog);
    dialog->show();
    dialog->raise();
    return dialog;
}

bool MainWindow::closeDialog(const QString &name)
{
    QWidget *dialog = scriptDialogs.take(name);
    if (!dialog || !dialog->isVisible()) return false;
    if (name == QLatin1String("claudeChat")) {
        toggleClaudeChat();   // the window's own close path (it keeps state)
        return true;
    }
    dialog->close();          // owned entries delete themselves (WA_DeleteOnClose)
    return true;
}

bool MainWindow::isDialogOpen(const QString &name) const
{
    const QWidget *dialog = scriptDialogs.value(name);
    return dialog && dialog->isVisible();
}
