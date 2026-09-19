/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/softwareupdatedialog.h"
#include "ui_softwareupdatedialog.h"

#include <QDesktopServices>
#include "data/settingsmanager.h"
#include "app/updatechecker.h"
#include <QProcess>
#include "ui/style/stylesheet.h"


SoftwareUpdateDialog::SoftwareUpdateDialog(QWidget *parent) : QDialog(parent), ui(new Ui::SoftwareUpdateDialog)
{
	ui->setupUi(this); 
	// softwareupdatedialog.ui used to embed these (classic-only now; theme sweep)
	setStyleSheet(StyleSheet::SoftwareUpdateDialogRoot());
	ui->widget->setStyleSheet(StyleSheet::SoftwareUpdateDialogWidget());
	ui->textEdit->setStyleSheet(StyleSheet::SoftwareUpdateDialogTextEdit());
	ui->close->setStyleSheet(StyleSheet::SoftwareUpdateDialogClose());
	ui->download->setStyleSheet(StyleSheet::SoftwareUpdateDialogDownload());
	setWindowTitle("Software Update");
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	connect(ui->download, &QPushButton::clicked, [this]() {
		QProcess *process = new QProcess(this);
        QStringList args;
        args << downloadUrl;
#ifdef WIN32
		QString file = QCoreApplication::applicationDirPath() + QDir::separator() + "downloader.exe";
        process->start(file, args);
#else
        QString file = QDir::currentPath() + "/downloader.app";
        process->setProgram(file);
        process->setArguments(args);
        process->start(file, args);
#endif
		QStringList cmdline_args = QCoreApplication::arguments();
		this->close();
	});

	connect(ui->close, &QPushButton::clicked, [this]() {
		this->close();
	});

	// ONE KEY AND ONE DEFAULT, shared with the launch check and the Preferences
	// row (UpdateChecker::kAutomaticChecks*): this box read `true` for an absent
	// preference while the Preferences row read it as set and the launch ignored
	// both — three answers to one question (SMOKE-FIX-1's fix round).
	auto updates = SettingsManager::getDefaultManager()
	                   ->getValue(UpdateChecker::kAutomaticChecksKey,
	                              UpdateChecker::kAutomaticChecksDefault).toBool();
	ui->checkBox->setChecked(updates);

	connect(ui->checkBox, &QCheckBox::clicked, [this](bool checked) {
		SettingsManager::getDefaultManager()->setValue(UpdateChecker::kAutomaticChecksKey, checked);
	});
}

void SoftwareUpdateDialog::setVersionNotes(QString notes)
{
	ui->textEdit->setHtml(notes);
}

void SoftwareUpdateDialog::setDownloadUrl(QString url)
{
	this->downloadUrl = url;
}

void SoftwareUpdateDialog::setType(QString string)
{
	type = string;
}

SoftwareUpdateDialog::~SoftwareUpdateDialog()
{
	delete ui;
}
