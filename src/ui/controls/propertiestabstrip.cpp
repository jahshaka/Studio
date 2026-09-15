/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/propertiestabstrip.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QTabBar>
#include <QVBoxLayout>

#include "ui/controls/rowfit.h"
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

    // THE FILTER BOX, UNDER THE BAR AND INSIDE THE TAB. A QLineEdit with the
    // style's own clear button — no sheet of our own anywhere in this widget
    // (theme.no_raw_sheets); RowFit keeps its minimum width off the column's
    // (the line edit's natural minimum is tens of px, the law is 300).
    box = new QLineEdit(this);
    box->setObjectName(QStringLiteral("propertiesFilterBox"));
    box->setPlaceholderText(tr("Filter rows…"));
    box->setClearButtonEnabled(true);
    box->setToolTip(tr("Show only the rows whose name, key or section matches — "
                       "\"ssr\" finds Screen-Space Reflections. Ctrl+F focuses this box, "
                       "Esc clears it. Each tab has its own filter."));
    RowFit::fitLineEdit(box);
    box->installEventFilter(this);
    layout->addWidget(box);

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
    // THE BOX IS A VIEW OF ITS TAB'S TEXT, both directions, one owner — the
    // same shape as the bar above it.
    connect(box, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (applying || !this->panel) return;
        this->panel->setPropertiesFilter(this->panel->propertiesTab(), text);
    });

    if (panel) {
        connect(panel, &SceneNodePropertiesWidget::propertiesTabChanged,
                this, &PropertiesTabStrip::showTab);
        connect(panel, &SceneNodePropertiesWidget::propertiesFilterChanged,
                this, &PropertiesTabStrip::showFilter);
        showTab(panel->propertiesTab());
        showFilter(panel->propertiesTab(), panel->propertiesFilter(panel->propertiesTab()));
    }
}

void PropertiesTabStrip::showTab(SceneNodePropertiesWidget::Tab tab)
{
    if (!bar) return;
    const int index = (tab == SceneNodePropertiesWidget::Tab::World) ? 0 : 1;
    applying = true;
    if (bar->currentIndex() != index) bar->setCurrentIndex(index);
    // THE OTHER TAB'S TEXT COMES BACK WITH IT: the two filters are independent
    // and both survive a trip to the other tab (per session, never persisted).
    if (box && panel) box->setText(panel->propertiesFilter(tab));
    applying = false;
}

void PropertiesTabStrip::showFilter(SceneNodePropertiesWidget::Tab tab, const QString &text)
{
    if (!box || !panel || tab != panel->propertiesTab()) return;
    if (box->text() == text) return;
    applying = true;
    box->setText(text);
    applying = false;
}

void PropertiesTabStrip::focusFilter()
{
    if (!box) return;
    box->setFocus(Qt::ShortcutFocusReason);
    box->selectAll();
}

bool PropertiesTabStrip::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == box && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            // Esc CLEARS, and hands the keyboard back — the viewport's own keys
            // (the fly keys, the tool keys) are dead while a line edit has
            // focus, so leaving focus here after a clear would be a trap.
            box->clear();
            box->clearFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
