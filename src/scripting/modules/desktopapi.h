/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_DESKTOPAPI_H
#define SCRIPTING_DESKTOPAPI_H

// desktop.* — the desktop page's view modes and slider filmstrip
// (DESKTOP_SLIDER_SPEC.md, API-first per SCRIPTING_SPEC §2.3). Window verbs:
// they drive the live ProjectManager/DynamicGrid, so headless sessions fail
// them gracefully. Rows in this API are 1-based, like desktops 1-4.

#include <QVariant>

#include "scripting/apimodule.h"

class DesktopApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("desktop"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QString viewMode();
    Q_INVOKABLE bool setViewMode(const QString &mode);
    Q_INVOKABLE bool moveTile(const QString &guid, int row, int index = -1);
    Q_INVOKABLE QVariantList tiles();
};

#endif // SCRIPTING_DESKTOPAPI_H
