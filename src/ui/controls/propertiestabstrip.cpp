/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/propertiestabstrip.h"

#include <QTabBar>
#include <QVBoxLayout>

#include "ui/style/stylesheet.h"
#include "ui/style/themeroles.h"

PropertiesTabStrip::PropertiesTabStrip(SceneNodePropertiesWidget *panel, QWidget *parent)
    : QWidget(parent), panel(panel)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    bar = new QTabBar(this);
    bar->addTab(tr("World"));          // index 0 == Tab::World
    bar->addTab(tr("Selection"));      // index 1 == Tab::Selection
    // THE COLUMN-WIDTH LAW (PanelMetrics::rightColumnMinWidth = 300): the bar
    // must be able to shrink with the dock. Not expanding, eliding, and with
    // scroll buttons as the last resort, its minimum is a fraction of the
    // column's; ui.properties_width asserts it.
    bar->setExpanding(false);
    bar->setElideMode(Qt::ElideRight);
    bar->setUsesScrollButtons(true);
    bar->setDrawBase(false);
    bar->setStyleSheet(StyleSheet::PreferencesTabs());
    layout->addWidget(bar);

    ThemeRoles::setSurface(this, ThemeRoles::Surface::Panel);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMinimumWidth(0);

    // BOTH DIRECTIONS, ONE OWNER. A click sets the panel's tab; the panel's own
    // moves (a pick, a verb, the shortcut, a scene open) come back here. The
    // `applying` latch keeps the round trip from bouncing.
    connect(bar, &QTabBar::currentChanged, this, [this](int index) {
        if (applying || !this->panel) return;
        this->panel->setPropertiesTab(index == 0 ? SceneNodePropertiesWidget::Tab::World
                                                 : SceneNodePropertiesWidget::Tab::Selection);
    });
    if (panel) {
        connect(panel, &SceneNodePropertiesWidget::propertiesTabChanged,
                this, &PropertiesTabStrip::showTab);
        showTab(panel->propertiesTab());
    }
}

void PropertiesTabStrip::showTab(SceneNodePropertiesWidget::Tab tab)
{
    if (!bar) return;
    const int index = (tab == SceneNodePropertiesWidget::Tab::World) ? 0 : 1;
    if (bar->currentIndex() == index) return;
    applying = true;
    bar->setCurrentIndex(index);
    applying = false;
}
