/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/desktopapi.h"

#include "ui/pages/projectmanager.h"
#include "shell/mainwindow.h"

QVector<VerbInfo> DesktopApi::verbs() const
{
    return {
        { "viewMode", "desktop.viewMode() -> mode",
          "Returns the current desktop's view mode: 'rows', 'freeform' or 'sliders' (persisted per desktop).",
          Needs::Window },
        { "setViewMode", "desktop.setViewMode(mode) -> bool",
          "Sets the current desktop's view mode: 'rows', 'freeform' or 'sliders'. Persists per desktop; switching is lossless (each mode keeps its own layout).",
          Needs::Window },
        { "moveTile", "desktop.moveTile(guid, row, index=-1) -> bool",
          "Sliders mode: moves the project tile into filmstrip row 1..N at the insert index (0-based within the row; -1 appends). Tiles after the index shift right. The assignment persists.",
          Needs::Window },
        { "tiles", "desktop.tiles() -> [{guid, name, row, index, open}]",
          "Lists the current desktop's tiles with their slider assignment (row 1..N, index 0-based; -1/-1 when never assigned) and whether each tile is the project currently open in the editor (open: the tile the desktop paints with a dark blue caption bar).",
          Needs::Window },
        { "gridStats", "desktop.gridStats() -> {builds, lastBuildMs, lastBuildTiles, lastBuildDecodes, decodes, outOfStep, tiles}",
          "How the Desktop grid has been kept this session. The grid is a MODEL: a create, import, move, rename or delete changes ONE tile, and the whole grid is BUILT only when the desktop itself changes — its first showing, another desktop selected. `builds` counts those builds; `lastBuildMs`, `lastBuildTiles` and `lastBuildDecodes` describe the latest (decodes = PNG thumbnails it had to inflate; 0 when the session has seen them all); `decodes` is the session's thumbnail decodes in total. `outOfStep` counts Desktop entries that found the tiles differing from the library and fixed them — a path that changed the library without its tile verb (0 is the only healthy value). `tiles` is the grid's tile count now.",
          Needs::Window },
        { "exportTile", "desktop.exportTile(guid, zipPath) -> bool",
          "Exports the project `guid` to `zipPath` exactly as its desktop tile's Export does, minus the save dialog: threaded on the window's archiver (poll project.archiveState(), read project.archiveResult()). It reads the project's own row and never re-points the current project — the open world, its autosave and project.current() are untouched, except that exporting the project that IS open saves it first. False with an error when an archive operation is already running or no project has that guid.",
          Needs::Window },
        { "sliderRows", "desktop.sliderRows() -> rows",
          "How many filmstrip rows the Sliders view mode stacks (2..10). A per-user setting, not per desktop.",
          Needs::Window },
        { "setSliderRows", "desktop.setSliderRows(rows) -> rows",
          "Sets the number of filmstrip rows the Sliders view mode stacks and re-lays the desktop out immediately. Clamped to 2..10; returns the value that took effect. Same setting as Preferences -> Desktop -> Slider Rows.",
          Needs::Window },
    };
}

QString DesktopApi::viewMode()
{
    if (!host.projectManager) { fail("desktop: not available in this session"); return QString(); }
    return host.projectManager->desktopViewMode();
}

bool DesktopApi::setViewMode(const QString &mode)
{
    if (!host.projectManager) return fail("desktop: not available in this session");
    if (!host.projectManager->setDesktopViewMode(mode))
        return fail(QStringLiteral("desktop.setViewMode: unknown mode '%1' (rows, freeform, sliders)").arg(mode));
    return true;
}

bool DesktopApi::moveTile(const QString &guid, int row, int index)
{
    if (!host.projectManager) return fail("desktop: not available in this session");
    if (guid.isEmpty()) return fail("desktop.moveTile: guid is required");
    if (host.projectManager->desktopViewMode() != QLatin1String("sliders"))
        return fail("desktop.moveTile: the current desktop is not in 'sliders' view mode");
    if (!host.projectManager->moveTileToSliderPos(guid, row - 1, index))
        return fail(QStringLiteral("desktop.moveTile: no tile '%1' on the current desktop").arg(guid));
    return true;
}

bool DesktopApi::exportTile(const QString &guid, const QString &zipPath)
{
    if (!host.mainWindow) return fail("desktop.exportTile: this verb needs the editor window");
    if (zipPath.trimmed().isEmpty()) return fail("desktop.exportTile: a destination path is required");
    QString why;
    if (!host.mainWindow->startProjectExport(guid, zipPath, &why))
        return fail(QStringLiteral("desktop.exportTile: %1").arg(why));
    return true;
}

QVariantMap DesktopApi::gridStats()
{
    if (!host.projectManager) { fail("desktop: not available in this session"); return {}; }
    return host.projectManager->gridStats();
}

int DesktopApi::sliderRows()
{
    if (!host.projectManager) { fail("desktop: not available in this session"); return 0; }
    return host.projectManager->sliderRows();
}

int DesktopApi::setSliderRows(int rows)
{
    if (!host.projectManager) { fail("desktop: not available in this session"); return 0; }
    if (rows < 2 || rows > 10)
        { fail(QStringLiteral("desktop.setSliderRows: rows must be 2..10 (got %1)").arg(rows)); return 0; }
    return host.projectManager->setSliderRows(rows);
}

QVariantList DesktopApi::tiles()
{
    if (!host.projectManager) { fail("desktop: not available in this session"); return {}; }
    return host.projectManager->sliderTilesForApi();
}
