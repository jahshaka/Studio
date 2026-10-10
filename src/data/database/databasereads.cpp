/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "data/database/databasereads.h"

#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlQuery>
#include <QVariant>

#include <cstring>

#if defined(JAH_HAVE_SQLITE3)
#include <sqlite3.h>
#endif
#if defined(__linux__)
#include <elf.h>
#include <link.h>
#endif

namespace databasereads {
namespace {

#if defined(JAH_HAVE_SQLITE3) && defined(__linux__)
/// Does the loaded qsqlite plugin NEED the system libsqlite3 (its DT_NEEDED, read from the mapped
/// dynamic section)? Then the handle it hands out belongs to the library this TU links.
struct PluginScan { bool pluginSeen = false; bool needsSystemSqlite = false; };
int scanObject(struct dl_phdr_info *info, size_t, void *data)
{
    auto *scan = static_cast<PluginScan *>(data);
    if (!info->dlpi_name || !std::strstr(info->dlpi_name, "libqsqlite")) return 0;
    scan->pluginSeen = true;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type != PT_DYNAMIC) continue;
        const auto *dyn = reinterpret_cast<const ElfW(Dyn) *>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
        const char *strtab = nullptr;
        for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; ++d)
            if (d->d_tag == DT_STRTAB) strtab = reinterpret_cast<const char *>(d->d_un.d_ptr);
        if (!strtab) break;
        for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; ++d)
            if (d->d_tag == DT_NEEDED && std::strstr(strtab + d->d_un.d_val, "libsqlite3"))
                scan->needsSystemSqlite = true;
    }
    return 1;
}
#endif

}   // namespace

Stats read()
{
    Stats out;
#if !defined(JAH_HAVE_SQLITE3)
    out.why = QStringLiteral("this build links no system SQLite (CMake found no SQLite3)");
    return out;
#elif !defined(__linux__)
    out.why = QStringLiteral("the Qt SQL driver here carries its own SQLite (not the system library this build links)");
    return out;
#else
    const QSqlDatabase main = QSqlDatabase::database(QLatin1String(QSqlDatabase::defaultConnection), false);
    if (!main.isOpen() || !main.driver()) { out.why = QStringLiteral("the library database is not open"); return out; }
    PluginScan scan;
    dl_iterate_phdr(scanObject, &scan);
    if (!scan.pluginSeen || !scan.needsSystemSqlite) {
        out.why = scan.pluginSeen ? QStringLiteral("the qsqlite plugin carries its own SQLite")
                                  : QStringLiteral("the qsqlite plugin is not loaded");
        return out;
    }
    const QVariant h = main.driver()->handle();
    if (!h.isValid() || std::strcmp(h.typeName(), "sqlite3*") != 0) {
        out.why = QStringLiteral("the driver's handle is not an sqlite3*");
        return out;
    }
    sqlite3 *db = *static_cast<sqlite3 *const *>(h.constData());
    if (!db) { out.why = QStringLiteral("the driver's handle is null"); return out; }
    int cur = 0, high = 0;
    if (sqlite3_db_status(db, SQLITE_DBSTATUS_CACHE_MISS, &cur, &high, 0) != SQLITE_OK) {
        out.why = QStringLiteral("sqlite3_db_status refused");
        return out;
    }
    out.available = true;
    out.pages = cur;
    QSqlQuery q(main);
    if (q.exec(QStringLiteral("PRAGMA page_size")) && q.next()) out.pageSize = q.value(0).toInt();
    return out;
#endif
}

}   // namespace databasereads
