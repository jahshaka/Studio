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
/// (The filter box of lane A lands in this strip, under the bar.)
class PropertiesTabStrip : public QWidget
{
    Q_OBJECT
public:
    explicit PropertiesTabStrip(SceneNodePropertiesWidget *panel, QWidget *parent = nullptr);

    QTabBar *tabBar() const { return bar; }

private:
    void showTab(SceneNodePropertiesWidget::Tab tab);

    SceneNodePropertiesWidget *panel = nullptr;
    QTabBar *bar = nullptr;
    bool applying = false;
};

#endif // PROPERTIESTABSTRIP_H
