/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef NEWPROJECTDIALOG_H
#define NEWPROJECTDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QLayout>
#include <QLineEdit>

class QCheckBox;

/// What the New Scene dialog answers with. `empty` and `path` are both
/// options of the one create verb (`project.create(name, {empty, location})`)
/// — the dialog fills them in, it does not act on them.
struct ProjectInfo {
    QString projectName;
    QString projectPath;
    /// The "Empty scene" checkbox: a blank world instead of the template
    /// (owner review R1a, 2026-09-18). False is the template.
    bool    empty = false;
};

class SettingsManager;

class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    /// PARENTED, AND CENTRED ON THE PARENT (owner review R1, "not centered").
    /// It used to be constructed with no parent at all and shown wherever the
    /// window manager felt like putting it — with Qt::FramelessWindowHint, on
    /// a WM-less display, that is the top-left corner. A QDialog centres
    /// itself on its parent only if it HAS one, and every caller has a window
    /// to give it.
    explicit NewProjectDialog(QWidget *parent = nullptr);
    ~NewProjectDialog();

    ProjectInfo getProjectInfo();

    // --- the parts a test drives (ui.new_project_dialog) ----------------------
    QLineEdit  *nameEdit()     const { return projectNameEdit; }
    QLineEdit  *locationEdit() const { return projectPathEdit; }
    QPushButton *browseButton() const { return browse; }
    QCheckBox  *emptyCheck()   const { return emptyScene; }
    QPushButton *createButton() const { return create; }
    /// Sets the location the way Browse does, without a file dialog: the same
    /// one write, so a test drives the button's effect rather than a copy.
    void setProjectLocation(const QString &path);

protected slots:
    void setProjectPath();
    void createNewProject();
    void confirmProjectCreation();

protected:
    /// Centres on the parent window the first time the dialog is shown. Not in
    /// the constructor: the dialog has no laid-out size until then, so a
    /// constructor-time centring is off by half its own height.
    void showEvent(QShowEvent *event) override;

private:
    void centreOnParent();

    QString projectName,
            projectPath;
    SettingsManager *settingsManager;

	QLabel* scene;
	QLabel* path;
	QLineEdit* projectPathEdit;
	QLineEdit* projectNameEdit;
	QPushButton* browse;
	QCheckBox* emptyScene;
	QPushButton* cancel;
	QPushButton* create;
	bool centred = false;
};

#endif // NEWPROJECTDIALOG_H
