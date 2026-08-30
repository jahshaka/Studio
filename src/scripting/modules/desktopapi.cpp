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
        { "tiles", "desktop.tiles() -> [{guid, name, row, index}]",
          "Lists the current desktop's tiles with their slider assignment (row 1..N, index 0-based; -1/-1 when never assigned).",
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

QVariantList DesktopApi::tiles()
{
    if (!host.projectManager) { fail("desktop: not available in this session"); return {}; }
    return host.projectManager->sliderTilesForApi();
}
