/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROPERTIESTABSTRIP_H
#define PROPERTIESTABSTRIP_H

#include <QWidget>

#include "ui/panels/scenenodepropertieswidget.h"

class QTabBar;
class QLineEdit;

/// THE RIGHT COLUMN'S TAB BAR (PROPERTY_FILTER_SPEC §2): World | Selection.
///
/// It is a VIEW of SceneNodePropertiesWidget::propertiesTab() and nothing else
/// — the panel owns the state (a pick raises Selection, the root raises World),
/// the strip shows it and turns a click into setPropertiesTab(). Keeping the
/// decision in the panel is what lets the verbs, the shortcut and the offscreen
/// suite drive the same thing the user's click drives.
///
/// It lives ABOVE the dock's scroll area, not inside it (spec §6.5): the tab bar
/// must not scroll away with the rows, and the properties panel's minimum width
/// — the column-width law, ui.properties_width — has to keep measuring exactly
/// what it measured before. The bar elides and never expands, so the strip's own
/// minimum stays far below PanelMetrics::rightColumnMinWidth.
///
/// UNDER THE BAR SITS THE FILTER BOX (owner decision 2026-09-15: "put the
/// filter box under the tab; it belongs to its tab"). ONE box, showing the
/// CURRENT tab's text — World's text filters the world rows, Selection's the
/// selected object's, and each keeps its own while the other is on screen. The
/// panel owns both strings for exactly the reason it owns the tab: the verb,
/// the Ctrl+F shortcut, the offscreen suite and this box all drive one thing.
class PropertiesTabStrip : public QWidget
{
    Q_OBJECT
public:
    explicit PropertiesTabStrip(SceneNodePropertiesWidget *panel, QWidget *parent = nullptr);

    QTabBar *tabBar() const { return bar; }
    QLineEdit *filterBox() const { return box; }

    /// Ctrl+F: the box of the tab on screen takes the keyboard.
    void focusFilter();

protected:
    /// Esc in the box clears it and gives the keyboard back to the viewport.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void showTab(SceneNodePropertiesWidget::Tab tab);
    void showFilter(SceneNodePropertiesWidget::Tab tab, const QString &text);

    SceneNodePropertiesWidget *panel = nullptr;
    QTabBar *bar = nullptr;
    QLineEdit *box = nullptr;
    bool applying = false;
};

#endif // PROPERTIESTABSTRIP_H
