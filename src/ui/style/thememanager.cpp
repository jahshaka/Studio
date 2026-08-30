/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/style/thememanager.h"
#include "ui/style/stylesheet.h"
#include "data/settingsmanager.h"
#include "data/constants.h"

#include <QApplication>
#include <QFontDatabase>

#include <oclero/qlementine/style/QlementineStyle.hpp>

static bool s_classicActive = false;

QString ThemeManager::settingsKey()
{
    return QStringLiteral("appearance/theme");
}

QString ThemeManager::currentThemeId()
{
    const auto raw = SettingsManager::getDefaultManager()
                         ->getValue(settingsKey(), defaultThemeId()).toString();
    if (raw == classicId()) return classicId();
    return qlementineDarkId();
}

void ThemeManager::setThemeId(const QString &id)
{
    SettingsManager::getDefaultManager()->setValue(settingsKey(), id);
}

bool ThemeManager::classicActive()
{
    return s_classicActive;
}

void ThemeManager::applyAtStartup(QApplication &app)
{
    s_classicActive = (currentThemeId() == classicId());
    StyleSheet::setClassicThemeActive(s_classicActive);

    if (s_classicActive) {
        // Jahshaka Classic (archived): no QStyle, per-widget stylesheets, DroidSans.
        // This is bit-for-bit the pre-Qlementine appearance path.
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        int id = QFontDatabase::addApplicationFont(":/fonts/DroidSans.ttf");
        if (id != -1) {
            QString family = QFontDatabase::applicationFontFamilies(id).at(0);
            QFont droid(family, Constants::UI_FONT_SIZE);
            droid.setStyleStrategy(QFont::PreferAntialias);
            QApplication::setFont(droid);
        }
#endif
        return;
    }

    // Qlementine Dark (the default): a real QStyle owns every stock widget.
    // Must run before any widget is constructed. Typography comes from the
    // theme (Inter/Roboto Mono, bundled by Qlementine) — no DroidSans override.
    auto *style = new oclero::qlementine::QlementineStyle(&app);
    style->setThemeJsonPath(QStringLiteral(":/themes/jahshaka-dark.json"));
    QApplication::setStyle(style);
}

void ThemeManager::clearClassicSheets(QWidget *root)
{
    if (s_classicActive || !root) return;

    if (!root->styleSheet().isEmpty()) root->setStyleSheet(QString());
    const auto children = root->findChildren<QWidget *>();
    for (auto *w : children)
        if (!w->styleSheet().isEmpty()) w->setStyleSheet(QString());
}
