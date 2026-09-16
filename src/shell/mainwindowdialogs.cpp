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
#include "ui/dialogs/importsettingsdialog.h"
#include "ui/dialogs/newprojectdialog.h"
#include "ui/dialogs/preferencesdialog.h"
#include "ui/dialogs/progressdialog.h"
#include "ui/dialogs/renameprojectdialog.h"
#include "ui/dialogs/screenshotwidget.h"
#include "ui/dialogs/softwareupdatedialog.h"
#include "scripting/modules/assetsapi.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "scripting/scriptengine.h"
#include "ui/pages/projectmanager.h"
#include "ui/windows/claudechatwindow.h"

#include <QFileInfo>
#include <QImage>
#include <QSqlDatabase>
#include <QLayout>
#include <QMessageBox>

QStringList MainWindow::dialogNames() const
{
    return { QStringLiteral("preferences"), QStringLiteral("about"),
             QStringLiteral("newProject"), QStringLiteral("renameProject"),
             QStringLiteral("sampleBrowser"), QStringLiteral("progress"),
             QStringLiteral("getName"), QStringLiteral("screenshot"),
             QStringLiteral("donate"), QStringLiteral("softwareUpdate"),
             QStringLiteral("claudeChat"), QStringLiteral("importSettings") };
}

QWidget *MainWindow::openDialog(const QString &name, const QVariantMap &options,
                                QVariantMap *extra)
{
    if (!dialogNames().contains(name)) return nullptr;
    // A NAMED SUBJECT REOPENS: `importSettings` with a guid is a request for
    // THAT asset, so an inspection dialog (or one on another asset) standing
    // open is closed rather than silently answered instead.
    if (options.contains(QStringLiteral("guid"))) closeDialog(name);
    if (QWidget *open = scriptDialogs.value(name); open && open->isVisible()) {
        open->raise();
        applyDialogOptions(name, open, options, extra);
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
    } else if (name == QLatin1String("importSettings")) {
        // THE IMPORT DECISION (SPECS/IMPORT_DIALOG_SPEC.md §8). With a `guid`
        // it opens on that asset, pre-filled and able to reimport; with none
        // it opens on nothing — which is what the theme walk and an MCP client
        // asking "show me the dialog" want, and its OK then has nothing to
        // commit, exactly like newProject's.
        const QString guid = options.value(QStringLiteral("guid")).toString();
        if (!guid.isEmpty()) {
            // A VERB opened this, so a refusal comes back as a string — never
            // as a modal box nothing can answer.
            QString why;
            ImportSettingsDialog *reimport = openImportSettings(guid, &why);
            if (!reimport) {
                if (extra) extra->insert(QStringLiteral("error"), why);
                return nullptr;
            }
            scriptDialogs.insert(name, reimport);
            applyDialogOptions(name, reimport, options, extra);
            return reimport;
        }
        auto *fresh = new ImportSettingsDialog(this);
        fresh->setMode(ImportSettingsDialog::Mode::Import);
        dialog = fresh;
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
    applyDialogOptions(name, dialog, options, extra);
    return dialog;
}

// PER-DIALOG OPTIONS AND ANSWERS. Only `importSettings` has any: it is the one
// dialog whose CONTENT is a decision a script needs to make and read back
// (SPECS/IMPORT_DIALOG_SPEC.md §8) — everything else in the catalog is opened
// for inspection. `accept` presses its OK button, which in reimport mode runs
// assets.reimport, so scripting.e2e.import_dialog exercises the very path the
// user's click takes instead of a parallel one.
void MainWindow::applyDialogOptions(const QString &name, QWidget *widget,
                                    const QVariantMap &options, QVariantMap *extra)
{
    auto *dialog = qobject_cast<ImportSettingsDialog *>(widget);
    if (name != QLatin1String("importSettings") || !dialog) return;

    if (options.contains(QStringLiteral("settings"))) {
        // MERGED OVER WHAT THE DIALOG ALREADY HOLDS, key by key — the same rule
        // assets.reimport follows, and for the same reason: {scale: 2} on its
        // own is not a record. Parsed standalone it would RESET the units, the
        // axes, the rotation, the origin and the clip choice the asset came in
        // with, silently, on the way to changing one number.
        QJsonObject merged = dialog->record();
        const QJsonObject given =
            QJsonObject::fromVariantMap(options.value(QStringLiteral("settings")).toMap());
        for (auto it = given.constBegin(); it != given.constEnd(); ++it)
            merged.insert(it.key(), it.value());
        QString error;
        const iris::ImportSettings parsed = iris::ImportSettings::fromJson(merged, &error);
        if (!error.isEmpty()) {
            if (extra) extra->insert(QStringLiteral("error"), error);
            return;
        }
        dialog->setSettings(parsed);
    }
    // READ THE RECORD FIRST: accept() closes the dialog, and a dialog built on
    // demand deletes itself when it closes.
    const QVariantMap record = dialog->record().toVariantMap();
    if (options.value(QStringLiteral("accept")).toBool()) {
        mDialogError.clear();
        dialog->accept();
        if (!mDialogError.isEmpty()) {
            if (extra) extra->insert(QStringLiteral("error"), mDialogError);
            return;
        }
        if (extra) extra->insert(QStringLiteral("accepted"), true);
    }
    if (extra) extra->insert(QStringLiteral("settings"), record);
}

// ---- THE IMPORT DECISION, REOPENED (§5/§8) --------------------------------
//
// ONE entry point for every "reimport this asset" gesture: the Assets page's
// "Import settings…" button, the page's and the tray's "Reimport…" rows, and
// app.dialog('importSettings', {guid}). It lives HERE, in the shell, because
// it is the one place that has both the widget layer and the ScriptHost — the
// pages stay free of the scripting layer, and the commit goes through the VERB
// (assets.reimport), so the open-scene mesh swap and the bake-store memo clear
// happen exactly as they do for a script.
ImportSettingsDialog *MainWindow::openImportSettings(const QString &guid, QString *errorOut)
{
    // A REFUSAL IS REPORTED THE WAY THE CALLER CAN SURVIVE (see the header): a
    // message box for a button press, a string for a verb.
    const auto refuse = [this, errorOut](const QString &why) -> ImportSettingsDialog * {
        if (errorOut) *errorOut = why;
        else QMessageBox::warning(this, tr("Import settings"), why);
        return nullptr;
    };
    if (guid.isEmpty() || !scriptEngine)
        return refuse(tr("There is no asset to reopen the import decision for."));
    ScriptHost &host = scriptEngine->scriptHost();

    // THE VERB, THROUGH THE SHELL-CALLER CONVENTION (ApiModule::quietly): a
    // refusal comes back as a string instead of waiting in the host's pending
    // slot for a bridge that is never going to run.
    AssetsApi assets(host);
    const QVariantMap record = assets.quietly([&] { return assets.importSettings(guid); });
    if (record.isEmpty())
        return refuse(assets.lastError().isEmpty() ? tr("This asset has no import record.")
                                                   : assets.lastError());

    auto *dialog = new ImportSettingsDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setMode(ImportSettingsDialog::Mode::Reimport);
    dialog->setSubject(record.value(QStringLiteral("name")).toString());
    dialog->setSettings(iris::ImportSettings::fromJson(
        QJsonObject::fromVariantMap(record.value(QStringLiteral("settings")).toMap())));

    // The FACTS come from the asset's own STORED SOURCE, read light on a
    // worker — the same pre-read an import does, so the units row, the clip
    // list and the extent preview mean the same thing in both modes. The store
    // names its objects by content hash, so the read carries the recorded
    // source file's extension as its format hint — both of them from the verb
    // (`sourceName`), not from a reach past it into the database and the CAS.
    const QString source = AssetCas::resolveSource(QSqlDatabase::database(),
                                                   AssetStorePaths::root(), guid);
    if (!source.isEmpty() && QFileInfo::exists(source))
        dialog->startPreRead(
            source, QFileInfo(record.value(QStringLiteral("sourceName")).toString()).suffix());

    dialog->setCommitHandler([this, guid](const QJsonObject &settings, QString *error) {
        if (!scriptEngine) return false;
        AssetsApi api(scriptEngine->scriptHost());
        const QVariantMap result =
            api.quietly([&] { return api.reimport(guid, settings.toVariantMap()); });
        if (result.isEmpty()) {
            if (error) *error = api.lastError();
            mDialogError = api.lastError();
            return false;
        }
        emit assetReimported(guid);
        return true;
    });

    dialog->setWindowModality(Qt::ApplicationModal);
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
