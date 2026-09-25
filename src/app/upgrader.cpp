/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "app/upgrader.h"
#include "services/apppaths.h"

#include <QDir>

#include "data/database/database.h"
#include "data/settingsmanager.h"
#include "irisgl/core/irisutils.h"
#include "data/constants.h"


void Upgrader::checkIfSchemaNeedsUpdating()
{
	const QString path = IrisUtils::join(
        AppPaths::dataRoot(), Constants::JAH_DATABASE
	);

	if (!QFile(path).exists()) return;

	// This runs from main() BEFORE MainWindow exists (main.cpp:139) and it takes
	// Qt's DEFAULT SQL connection — the same one MainWindow::setupProjectDB
	// re-opens moments later. It therefore MUST hand the connection back; the
	// closeDatabase() at the end of this function does that, and Database's
	// destructor now backstops every early-return path (the pair used to leak
	// the registration, which is half of the boot-time "duplicate connection
	// name 'qt_sql_default_connection'" warning — STABILITY_PROGRAM_SPEC §1.7a;
	// the other half was closeDatabase() reading the name after invalidating
	// the handle).
	Database db;
	if (db.initializeDatabase(path)) {
		auto projectDb = db.getDbMetadata();

		auto numbers = Constants::CONTENT_VERSION.split(".");
		int dbMajor = numbers[0].toInt();
		int dbMinor = numbers[1].toInt();

		if (numbers[2].length() > 1) numbers[2].chop(1);
		int dbPatch = numbers[2].toInt();

		bool updateSchema = false;
		bool majorGreater = false;
		bool minorGreater = false;
		bool patchGreater = false;

		if (dbMajor > projectDb.major) majorGreater = true;
		if (dbMinor > projectDb.minor) minorGreater = true;
		if (dbPatch > projectDb.patch) patchGreater = true;

		if (majorGreater || minorGreater || patchGreater) updateSchema = true;

		if (updateSchema) {
			db.updateSchema();
			db.updateMetadataVersion(Constants::CONTENT_VERSION); // Use the struct in the future
		}
	}

	db.closeDatabase();
}