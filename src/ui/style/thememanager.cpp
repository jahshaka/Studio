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

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QMenu>
#include <QMetaObject>
#include <QPointer>
#include <QWidgetAction>

#include <oclero/qlementine/widgets/Switch.hpp>

static bool s_classicActive = false;

namespace {

// Qt 6.10 + Qlementine v1.4.2 crash workaround (verified by backtrace,
// 2026-08-31): QComboBoxPrivateContainer's constructor calls ensurePolished()
// BEFORE QComboBoxPrivate::container is assigned. QlementineStyle::polish of
// the popup's item view reparents the popup (setWindowFlag) and installs a
// ComboboxItemViewFilter whose ChildAdded handler calls QComboBox::view() —
// which, container still null, constructs a SECOND container, recursing until
// the stack overflows on the first QComboBox::addItem of the app (upstream dev
// branch has the same code). Deferring that one polish by an event-loop tick
// lets the container pointer land first; the deferred polish then behaves
// exactly as upstream intended. Everything else passes straight through.
class JahQlementineStyle : public oclero::qlementine::QlementineStyle
{
public:
    using oclero::qlementine::QlementineStyle::QlementineStyle;

    void polish(QWidget *w) override
    {
        if (auto *itemView = qobject_cast<QAbstractItemView *>(w)) {
            auto *popup = itemView->parentWidget();
            if (popup && popup->inherits("QComboBoxPrivateContainer")) {
                QPointer<QWidget> guard(itemView);
                QMetaObject::invokeMethod(
                    this,
                    [this, guard]() {
                        if (guard) oclero::qlementine::QlementineStyle::polish(guard.data());
                    },
                    Qt::QueuedConnection);
                return;
            }
        }
        oclero::qlementine::QlementineStyle::polish(w);
    }

    // The base class overloads polish(QApplication*)/polish(QPalette&); keep
    // them reachable despite the QWidget override above.
    using oclero::qlementine::QlementineStyle::polish;
};

} // namespace

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
    auto *style = new JahQlementineStyle(&app);
    style->setThemeJsonPath(QStringLiteral(":/themes/jahshaka-dark.json"));
    QApplication::setStyle(style);
}

void ThemeManager::switchifyMenuToggles(QMenu *menu)
{
    if (s_classicActive || !menu) return;

    const auto actions = menu->actions();
    for (QAction *action : actions) {
        if (!action->isCheckable() || qobject_cast<QWidgetAction *>(action))
            continue;

        auto *container = new QWidget(menu);
        auto *lay = new QHBoxLayout(container);
        lay->setContentsMargins(12, 4, 12, 4);
        auto *sw = new oclero::qlementine::Switch(container);
        sw->setText(action->text());
        sw->setChecked(action->isChecked());
        lay->addWidget(sw);

        QObject::connect(sw, &QAbstractButton::toggled, action, &QAction::setChecked);
        QObject::connect(action, &QAction::toggled, sw, &QAbstractButton::setChecked);

        auto *wa = new QWidgetAction(menu);
        wa->setDefaultWidget(container);
        menu->insertAction(action, wa);
        menu->removeAction(action);
    }
}

void ThemeManager::clearClassicSheets(QWidget *root)
{
    if (s_classicActive || !root) return;

    if (!root->styleSheet().isEmpty()) root->setStyleSheet(QString());
    const auto children = root->findChildren<QWidget *>();
    for (auto *w : children)
        if (!w->styleSheet().isEmpty()) w->setStyleSheet(QString());
}
