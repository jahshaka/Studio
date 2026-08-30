/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef JAH_THEMEMANAGER_H
#define JAH_THEMEMANAGER_H

#include <QString>

class QApplication;
class QWidget;

// App-wide theme selection (THEME_AUDIT.md §4). Two themes exist:
//
//   "qlementine-dark"  — the default: the Qlementine QStyle (thirdparty/qlementine)
//                        with the Jahshaka Dark JSON theme. All classic StyleSheet::
//                        getters are neutralized (return "") so the style owns rendering.
//   "classic"          — "Jahshaka Classic (archived)": today's per-widget stylesheets,
//                        bit-for-bit. Kept selectable for comparison while the classic
//                        crud is cleaned out; will be deleted eventually.
//
// The choice persists in jahsettings.ini under appearance/theme and takes effect
// at the NEXT start (restart-on-change — classic sheets are pushed at construction
// from hundreds of sites and cannot be cleanly un-applied live).
class ThemeManager
{
public:
    static QString settingsKey();     // "appearance/theme"
    static QString qlementineDarkId() { return QStringLiteral("qlementine-dark"); }
    static QString classicId()        { return QStringLiteral("classic"); }
    static QString defaultThemeId()   { return qlementineDarkId(); }

    // The persisted choice (normalized: anything unknown maps to the default).
    static QString currentThemeId();
    static void setThemeId(const QString &id);

    // True when the archived classic theme was applied at startup.
    static bool classicActive();

    // Reads the persisted choice and applies it. MUST run after the QApplication
    // is constructed and before ANY widget (the Upgrader dialog is the first).
    static void applyAtStartup(QApplication &app);

    // Crud sweeper for Qlementine mode: recursively clears every stylesheet on
    // root and its child widgets so the QStyle owns the whole subtree. Reaches
    // what the StyleSheet:: kill switch cannot — .ui-embedded sheets and raw
    // setStyleSheet crud applied earlier in a constructor. No-op under Classic.
    // Call at the END of a constructor, after setupUi and sheet-pushing code.
    static void clearClassicSheets(QWidget *root);
};

#endif // JAH_THEMEMANAGER_H
