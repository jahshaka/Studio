/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "upgrader.h"

#include <QDesktopWidget>
#include <QDialog>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "core/database/database.h"
#include "core/settingsmanager.h"
#include "irisgl/src/core/irisutils.h"
#include "constants.h"

#include <QDebug>

void Upgrader::checkIfDeprecatedVersion()
{
	const QString path = IrisUtils::join(
		QStandardPaths::writableLocation(QStandardPaths::DataLocation), Constants::JAH_DATABASE
	);

    if (!QFile(path).exists()) return;

	Database db;
	if (db.initializeDatabase(path)) {
		if (!db.checkIfTableExists("metadata") && db.getTableCount() != 0) {
			QDialog dialog;
			dialog.setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
			dialog.setWindowTitle("Outdated App Version!");

			QLabel *textLabel = new QLabel("The version of the application you are using has been deprecated!");
			QLabel *descDoc = new QLabel(
				"Jahshaka is currently in an alpha stage and as such, some versions might constitute breaking changes.\n"
				"To proceed, confirm wiping your database and asset library. This will delete all projects, "\
				"assets and worlds that you have on your desktop or in the asset manager.\n\n"
				"If you choose not to at the moment, you can continue using the current version but will be "\
				"unable to import future dated assets and worlds."
			);

			auto layout = new QVBoxLayout;

			layout->addWidget(textLabel);
			layout->addSpacing(8);
			layout->addWidget(descDoc);

			auto blayout = new QHBoxLayout;
			auto bwidget = new QWidget;
			bwidget->setLayout(blayout);
			QPushButton *yes = new QPushButton("Confirm wipe");
			QPushButton *no = new QPushButton("Not now");
			QPushButton *cont = new QPushButton("Continue");
			cont->setVisible(false);
			blayout->addStretch(1);
			blayout->addWidget(yes);
			blayout->addWidget(no);
			blayout->addWidget(cont);

			layout->addSpacing(8);
			layout->addWidget(bwidget);

			dialog.setStyleSheet(
				"* { color: #EEE; }"
				"QDialog { background: #222222; padding: 4px; }"
				"QPushButton { background: #444; color: #EEE; border: 0; padding: 6px 10px; }"
				"QPushButton:hover { background: #555; color: #EEE; }"
				"QPushButton:pressed { background: #333; color: #EEE; }"
			);

			bool proceed = false;

			connect(yes, &QPushButton::pressed, this, [&]() {
				proceed = true;
				dialog.setWindowTitle("Database Wiped!");
				textLabel->setVisible(false);
				no->setVisible(false);
				yes->setVisible(false);
				cont->setVisible(true);
				descDoc->setText("Your database has been recreated successfully! Enjoy the updated version of Jahshaka! \n");
				dialog.adjustSize();

				QRect position = dialog.frameGeometry();
				position.moveCenter(QDesktopWidget().availableGeometry().center());
				dialog.move(position.topLeft());
			});

			connect(no, &QPushButton::pressed, this, [&proceed, &dialog, &db]() { proceed = false; dialog.close(); });
			connect(cont, &QPushButton::pressed, this, [&proceed, &dialog]() { dialog.close(); });

			dialog.setLayout(layout);
			dialog.exec();

			if (proceed) {
				db.wipeDatabase();

				QDir storeDir(IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::DataLocation), "AssetStore"));
				if (!storeDir.removeRecursively()) {
#ifdef Q_OS_WIN
					RemoveDirectory(storeDir.absolutePath().toStdString().c_str());
#endif // Q_OS_WIN
				}

				// get the current project working directory
				auto pFldr = IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
					Constants::PROJECT_FOLDER);
				auto defaultProjectDirectory = SettingsManager::getDefaultManager()->getValue("default_directory", pFldr).toString();

				QDir projectDir(defaultProjectDirectory);
				if (!projectDir.removeRecursively()) {
#ifdef Q_OS_WIN
					RemoveDirectory(projectDir.absolutePath().toStdString().c_str());
#endif // Q_OS_WIN
				}

				dialog.close();
			}
		}

		db.closeDatabase();
	}
}

void Upgrader::checkIfSchemaNeedsUpdating()
{
    const QString path = IrisUtils::join(
        QStandardPaths::writableLocation(QStandardPaths::DataLocation), Constants::JAH_DATABASE
    );

    if (!QFile(path).exists()) return;

    Database db;
    if (db.initializeDatabase(path)) {
        auto projectDb = db.getDbMetadata();

        auto numbers = Constants::CONTENT_VERSION.split(".");
        int dbMajor = numbers[0].toInt();
        int dbMinor = numbers[1].toInt();

        if (numbers[2].length() > 1) numbers[2].chop(1);
        int dbPatch = numbers[2].toInt();

        qDebug() << "INCOMING  " << dbMajor << dbMinor << dbPatch;
        qDebug() << "CURRENT " << projectDb.major << projectDb.minor << projectDb.patch;

        bool updateSchema = false;
        bool majorGreater = false;
        bool minorGreater = false;
        bool patchGreater = false;

        if (dbMajor > projectDb.major) majorGreater = true;
        if (dbMinor > projectDb.minor) minorGreater = true;
        if (dbPatch > projectDb.patch) patchGreater = true;

        if (majorGreater || minorGreater || patchGreater) updateSchema = true;

        // if (updateSchema) db.updateSchema();
    }

    db.closeDatabase();
}
