/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DATA_DATABASE_DATABASEREADS_H
#define DATA_DATABASE_DATABASEREADS_H

// WHAT THE LIBRARY DATABASE READ ON THE REQUEST PATH (TESTING-CLEANUP-2; app.databaseReads).
//
// SQLite's own pager counts the pages it reads from the file per connection
// (SQLITE_DBSTATUS_CACHE_MISS) whether anyone asks or not; this reads that count off the UI
// thread's DEFAULT connection (the library's request path: worker connections are not in it, so
// a reading taken around one verb is what that verb read, whenever it is taken). Nothing here
// runs unless the verb is called: the product never asks.
//
// THE SAME SQLITE, OR NONE. The connection's handle belongs to the SQLite the Qt driver uses. This
// TU is linked to the SYSTEM SQLite (CMake's SQLite3) and calls it directly — so it answers only
// when the loaded qsqlite plugin is itself linked to that system library (Linux distribution Qt:
// read from the plugin's own dynamic section). Where qsqlite carries its own SQLite (the Qt
// installer's macOS and Windows builds) a call into ours with its handle would be undefined
// behaviour: `available` is false there, and the counted suites skip saying so.

#include <QString>
#include <QtGlobal>

namespace databasereads {

struct Stats {
    bool available = false;   ///< the driver's SQLite is the one this TU calls
    QString why;              ///< when not available: the reason, printed by the caller
    qint64 pages = 0;         ///< pages the default connection's pager read from the file
    int pageSize = 0;         ///< the file's page size
};

Stats read();

}   // namespace databasereads

#endif   // DATA_DATABASE_DATABASEREADS_H
