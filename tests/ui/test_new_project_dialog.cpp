/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.new_project_dialog — THE NEW SCENE DIALOG (owner review R1, 2026-09-18:
// "new scene dialog is not centered… add a button next to the location so the
// user can choose the location… make the dialog a little wider to fit the new
// location button"), and its Template drop-down (WORLD-MODEL-1, owner
// 2026-09-30: templates like Unreal's).
//
// The dialog has no capability of its own — its controls are options of
// `project.create(name, {template, location})`, which scripting.e2e.new_scene
// drives. What is asserted HERE is the part a verb cannot see: that the dialog
// is parented and lands centred on the window it was opened from, that its
// controls exist and are wired, and that what it ANSWERS carries them.
//
// Widgets only: no engine, no database, no display.

#include <QApplication>
#include <QComboBox>
#include <QStandardItemModel>
#include <QDir>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

#include "data/constants.h"
#include "data/settingsmanager.h"
#include "services/apppaths.h"
#include "ui/dialogs/newprojectdialog.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main(int argc, char **argv)
{
    // A data root of our own, so the dialog's default location is this run's
    // projects folder and never the developer's Documents.
    QTemporaryDir scratch;
    qputenv("JAHSHAKA_DATA_ROOT", scratch.path().toUtf8());

    QApplication app(argc, argv);
    // ...and RESOLVED, the way main() resolves it: AppPaths keeps the answer in
    // a function-local static that only initialize() writes, so the environment
    // variable alone leaves every path at its un-overridden value — which for
    // the dialog's default location is the developer's own Documents folder.
    AppPaths::initialize();

    // THE PARENT IS A REAL WINDOW, AND DELIBERATELY NOT AT THE ORIGIN: a
    // dialog that ignores its parent and lands at (0,0) — which is what a
    // FRAMELESS dialog with no parent does on a WM-less display — would look
    // correct against a window that started there.
    QMainWindow parent;
    parent.setGeometry(420, 260, 1100, 700);
    parent.show();
    app.processEvents();

    NewProjectDialog dialog(&parent);

    // ---- the controls exist ------------------------------------------------
    CHECK(dialog.nameEdit() && dialog.locationEdit(), "the dialog has a Name and a Location field");
    CHECK(dialog.browseButton() != nullptr, "…and a Browse button beside the location (R1d)");
    CHECK(dialog.templateCombo() != nullptr, "…and a Template drop-down (WORLD-MODEL-1)");
    CHECK(dialog.createButton() != nullptr, "…and a Create button");

    // ---- the default location is the projects root, not an empty box -------
    const QString preferenceBefore =
        SettingsManager::getDefaultManager()->getValue("default_directory", QString()).toString();
    const QString defaultLocation = dialog.locationEdit()->text();
    std::printf("info: default location = %s\n", qUtf8Printable(defaultLocation));
    CHECK(!defaultLocation.isEmpty(), "the location defaults to the Jahshaka projects folder");
    CHECK(QDir(defaultLocation).isAbsolute() || defaultLocation.startsWith('/'),
          "…as an absolute path");
    CHECK(dialog.getProjectInfo().projectPath == defaultLocation,
          "…and that is what the dialog would answer with");
    CHECK(dialog.locationEdit()->isReadOnly(),
          "the location is read-only (chosen with Browse), not disabled — a disabled "
          "QLineEdit cannot be selected or copied");

    // ---- the Template drop-down: Basic by default, and it rides the answer --
    QComboBox *combo = dialog.templateCombo();
    CHECK(combo->count() == 4, "four templates: Basic, Empty, World, Sample");
    CHECK(combo->itemText(0) == QLatin1String("Basic") && combo->itemText(1) == QLatin1String("Empty")
              && combo->itemText(2) == QLatin1String("World") && combo->itemText(3) == QLatin1String("Sample"),
          "…in that order");
    CHECK(combo->currentIndex() == 0 && combo->currentText() == QLatin1String("Basic"),
          "the drop-down DEFAULTS TO BASIC");
    CHECK(dialog.getProjectInfo().sceneTemplate == SceneTemplate::Basic, "…and the answer says so");
    const auto *model = qobject_cast<QStandardItemModel *>(combo->model());
    CHECK(model && model->item(3) && !model->item(3)->isEnabled(),
          "Sample is listed but DISABLED (it does not exist yet)");
    CHECK(combo->itemData(3, Qt::ToolTipRole).toString() == QLatin1String("Coming"),
          "…with the tooltip 'Coming'");
    combo->setCurrentIndex(1);
    CHECK(dialog.getProjectInfo().sceneTemplate == SceneTemplate::Empty,
          "picking Empty passes through to the verb's {template: \"empty\"}");
    combo->setCurrentIndex(2);
    CHECK(dialog.getProjectInfo().sceneTemplate == SceneTemplate::World,
          "picking World passes through to {template: \"world\"}");
    CHECK(scenetemplate::name(dialog.getProjectInfo().sceneTemplate) == QLatin1String("world"),
          "…whose verb spelling is 'world'");
    combo->setCurrentIndex(0);

    // ---- the default location IS the current projects root (fix round F2) --
    // "default is in the jahshaka documents folder" (owner review R1d). It is
    // AppPaths::projectsRoot — the `default_directory` preference, or this
    // run's data root when one is forced — and it is also where Browse OPENS
    // (setProjectPath hands the field's current value to the file dialog as its
    // starting directory).
    CHECK(defaultLocation == AppPaths::projectsRoot(
              SettingsManager::getDefaultManager()->getValue("default_directory",
                                                             QString()).toString(),
              Constants::PROJECT_FOLDER),
          "the default location is the CURRENT default projects root, not a literal");

    // ---- Browse sets the field --------------------------------------------
    // The button's EFFECT, without a modal file dialog: setProjectLocation is
    // the one write both take, so this drives the button's own path rather than
    // a copy of it.
    const QString chosen = QDir(scratch.path()).filePath("elsewhere");
    QDir().mkpath(chosen);
    dialog.setProjectLocation(chosen);
    CHECK(dialog.locationEdit()->text() == chosen, "Browse's answer lands in the location field");
    CHECK(dialog.getProjectInfo().projectPath == chosen,
          "…and is what the dialog answers with (the FIELD is not the value — the value is)");
    CHECK(dialog.locationEdit()->toolTip() == chosen,
          "…with the full path in the tooltip, because the field is allowed to shrink");
    // A cancelled Browse (an empty answer) must leave the location alone.
    dialog.setProjectLocation(QString());
    CHECK(dialog.getProjectInfo().projectPath == chosen,
          "a cancelled Browse leaves the location exactly as it was");

    // ...AND CHOOSING ONE IS PER PROJECT (fix round F2). Putting one project on
    // an external drive must not move every FUTURE project there too, so the
    // dialog must not write the `default_directory` preference — the one writer
    // of it in the tree is Preferences > World Settings. The chosen root is
    // recorded with the project row instead (Database::setProjectLocation).
    CHECK(SettingsManager::getDefaultManager()->getValue("default_directory",
                                                         QString()).toString() == preferenceBefore,
          "choosing a location does NOT change the default_directory preference");
    CHECK(AppPaths::projectsRoot(preferenceBefore, Constants::PROJECT_FOLDER) == defaultLocation,
          "…so the next new scene still defaults to the same projects root");

    // ---- the name rides the answer -----------------------------------------
    dialog.nameEdit()->setText(QStringLiteral("A New World"));
    // confirmProjectCreation() is the Create button's slot; it reads the name
    // box into the answer and closes. Driven through the button so the WIRING
    // is what is tested (the slot was connected to `pressed`, not `clicked`).
    dialog.show();
    app.processEvents();

    // ---- CENTRED ON THE PARENT (R1: "not centered to the desktop") ---------
    // The dialog is frameless, so nothing else places it: with no parent at all
    // it used to land wherever the platform put a (0,0) frameless window.
    const QRect anchor = parent.frameGeometry();
    const QPoint want = anchor.center();
    const QPoint got = dialog.frameGeometry().center();
    std::printf("info: parent centre (%d,%d), dialog centre (%d,%d), dialog %dx%d\n",
                want.x(), want.y(), got.x(), got.y(), dialog.width(), dialog.height());
    CHECK(qAbs(got.x() - want.x()) <= 4 && qAbs(got.y() - want.y()) <= 4,
          "the dialog opens CENTRED on its parent window (within 4 px)");

    // ---- wide enough for the Browse button ---------------------------------
    // 288 px was the width taken when the location row was a disabled read-out;
    // with a button beside the field that leaves the field about 150 px.
    CHECK(dialog.width() > 288,
          "the dialog is wider than it was, to fit the location button (R1e)");
    CHECK(dialog.browseButton()->width() > 0 && dialog.browseButton()->isVisible(),
          "…and the Browse button is laid out and visible in it");
    CHECK(dialog.locationEdit()->width() > 100,
          "…with the location field still readable beside it");

    // ---- A TAKEN NAME IS REFUSED IN THE DIALOG (PROJECT-NAMES-1) -------------
    // The check is the verb's rule (ProjectService::nextFreeName in the app);
    // here a stand-in that knows one taken name. Create must NOT close: it says
    // the name is taken and puts the free one in the box.
    dialog.setNameCheck([](const QString &name) {
        return name == QLatin1String("Taken World") ? QStringLiteral("Taken World 2") : name;
    });
    dialog.nameEdit()->setText(QStringLiteral("Taken World"));
    dialog.createButton()->click();
    app.processEvents();
    CHECK(dialog.isVisible(), "Create with a TAKEN name keeps the dialog open");
    CHECK(dialog.nameRefusalLabel() && dialog.nameRefusalLabel()->isVisible()
              && dialog.nameRefusalLabel()->text().contains(QLatin1String("Taken World 2")),
          "…says so, naming the free name");
    CHECK(dialog.nameEdit()->text() == QLatin1String("Taken World 2"),
          "…and offers it in the name box");
    CHECK(dialog.getProjectInfo().projectName.isEmpty(), "…and answers with nothing yet");
    dialog.nameEdit()->setText(QStringLiteral("A New World"));

    dialog.createButton()->click();
    app.processEvents();
    CHECK(!dialog.nameRefusalLabel()->isVisible(), "a free name clears the refusal");
    CHECK(dialog.getProjectInfo().projectName == QLatin1String("A New World"),
          "pressing Create answers with the typed name");
    CHECK(dialog.getProjectInfo().projectPath == chosen,
          "…and with the chosen location");

    std::printf(failures == 0 ? "ui.new_project_dialog: ALL PASS\n"
                              : "ui.new_project_dialog: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
