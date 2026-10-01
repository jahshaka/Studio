/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/shellheader.h"

#include <QDesktopServices>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QUrl>

#include "irisgl/core/irisutils.h"
#include "thirdparty/qtawesome/QtAwesome.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"

ShellHeader::ShellHeader(QObject *parent) : QObject(parent)
{
}

QFont ShellHeader::glyphFont() const
{
	// 28px of the icon font — the size Help and Preferences always had, now
	// the size all three header glyphs share.
	return mIcons->font(28);
}

void ShellHeader::build(QGridLayout *layout, QtAwesome *icons,
                        std::function<void(WindowSpaces)> switchTo,
                        std::function<WindowSpaces()> current,
                        std::function<void()> showPreferences)
{
	mIcons = icons;

	worlds_menu = new QPushButton("Desktop");
	worlds_menu->setObjectName("worlds_menu");
	worlds_menu->setCursor(Qt::PointingHandCursor);
	player_menu = new QPushButton("Player");
	player_menu->setObjectName("player_menu");
	player_menu->setCursor(Qt::PointingHandCursor);
	editor_menu = new QPushButton("Editor");
	editor_menu->setObjectName("editor_menu");
	editor_menu->setCursor(Qt::PointingHandCursor);
	effect_menu = new QPushButton("Materials");
	effect_menu->setObjectName("effects_menu");
	effect_menu->setCursor(Qt::PointingHandCursor);
	assets_menu = new QPushButton("Assets");
	assets_menu->setObjectName("assets_menu");
	assets_menu->setCursor(Qt::PointingHandCursor);
	// Publish is an icon (circle + up arrow) in the right-hand cluster, owner
	// direction 2026-09-03 — the end of the pipeline lives beside Help/Prefs,
	// not among the space tabs. Same glyph mechanism as the help button.
	publish_menu = new QPushButton;
	publish_menu->setObjectName("publish_menu");
	publish_menu->setText(QChar(static_cast<ushort>(fa::arrowcircleup)));
	publish_menu->setToolTip("Publish");
	ThemeManager::applyHeaderGlyphButton(publish_menu, glyphFont());
	publish_menu->setCursor(Qt::PointingHandCursor);
	avatar_menu = new QPushButton("Avatar");
	avatar_menu->setObjectName("avatar_menu");
	avatar_menu->setCursor(Qt::PointingHandCursor);

	assets_panel = new QWidget;

	auto hl = new QHBoxLayout;
    hl->setContentsMargins(0,0,0,0);
	hl->setSpacing(12);
    hl->addWidget(worlds_menu);
    hl->addWidget(player_menu);
	hl->addWidget(editor_menu);
	hl->addWidget(effect_menu);
	hl->addWidget(assets_menu);
	// Avatar sits before Publish: Publish is the end of the pipeline and stays
	// last in the menu (BUTTON ORDER only — the pages are keyed by name).
	hl->addWidget(avatar_menu);

	assets_panel->setLayout(hl);

	jlogo = new QLabel;
    jlogo->setMinimumSize(QSize(244, 48));
    jlogo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    QString header_image_path;
#ifdef QT_DEBUG
    header_image_path = IrisUtils::getAbsoluteAssetPath("app/images/jahshakastudiodevheader.png");
#else
    header_image_path = IrisUtils::getAbsoluteAssetPath("app/images/jahshakastudioheader.svg");
#endif
    // Classic paints the logo via a stylesheet image; under Qlementine that
    // getter is neutralized, so set a real pixmap instead (sheet-free).
    if (ThemeManager::classicActive()) {
        jlogo->setStyleSheet(StyleSheet::MainWindowHeaderLogo(header_image_path));
    } else {
        jlogo->setPixmap(QPixmap(header_image_path)
                             .scaledToHeight(40, Qt::SmoothTransformation));
        jlogo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

	help = new QPushButton;
	help->setObjectName("helpButton");
    // for adapting Qt6.9.0
    help->setText(QChar(static_cast<ushort>(fa::questioncircle)));
	// Sheet + font together, through the one helper: the three header glyphs
	// (Publish, Help, Preferences) are the same size and sit on the header's
	// own colour instead of Qlementine's grey button plate.
	ThemeManager::applyHeaderGlyphButton(help, glyphFont());
	help->setCursor(Qt::PointingHandCursor);

    connect(help, &QPushButton::pressed, []() {
        QDesktopServices::openUrl(QUrl("https://www.jahshaka.com/learn/resources/"));
	});

	prefs = new QPushButton;
	prefs->setObjectName("prefsButton");

    // for adapting Qt6.9.0
    prefs->setText(QChar(static_cast<ushort>(fa::cog)));
	// (Classic still gets PrefsButton() — the helper picks by object name.)
	ThemeManager::applyHeaderGlyphButton(prefs, glyphFont());
	prefs->setCursor(Qt::PointingHandCursor);

	connect(prefs, &QPushButton::pressed, this, [showPreferences]() { showPreferences(); });

	QWidget *buttons = new QWidget;
	QHBoxLayout *bl = new QHBoxLayout;
	buttons->setLayout(bl);
	bl->setSpacing(20);
	ThemeManager::applyHeaderGlyphButton(publish_menu, glyphFont());
	bl->addWidget(publish_menu);
	bl->addWidget(help);
	bl->addWidget(prefs);

	// The header buttons are mouse-driven chrome: keep them out of the focus
	// chain, or the theme's focus indicator rings the focused space button
	// whenever the window is active (Qlementine only hijacks the policy of
	// Strong/ClickFocus buttons, so NoFocus sticks).
	for (auto *chrome : { worlds_menu, player_menu, editor_menu, effect_menu,
	                      assets_menu, publish_menu, avatar_menu, help, prefs })
		chrome->setFocusPolicy(Qt::NoFocus);

	layout->addWidget(jlogo, 0, 0, Qt::AlignLeft);
	layout->addWidget(assets_panel, 0, 1, Qt::AlignCenter);
	layout->addWidget(buttons, 0, 2, Qt::AlignRight);

    connect(worlds_menu, &QPushButton::pressed, this, [current, switchTo]() {
		// `!currentSpace == WindowSpaces::DESKTOP` stood here. It parses as
		// "(!currentSpace) == DESKTOP" — and because DESKTOP is 0 that
		// accidentally evaluated exactly like the `!=` below, so the BEHAVIOUR
		// was never wrong; it is written as what it means, and stops being one
		// renumbering of the enum away from being wrong (SMOKE-FIX-1's audit).
		if (current() != WindowSpaces::DESKTOP) switchTo(WindowSpaces::DESKTOP);
	});
    connect(player_menu, &QPushButton::pressed, this, [switchTo]() { switchTo(WindowSpaces::PLAYER); });
    connect(editor_menu, &QPushButton::pressed, this, [switchTo]() { switchTo(WindowSpaces::EDITOR); });
	connect(assets_menu, &QPushButton::pressed, this, [switchTo]() { switchTo(WindowSpaces::ASSETS); });
	connect(effect_menu, &QPushButton::pressed, this, [switchTo]() { switchTo(WindowSpaces::EFFECT); });
	connect(publish_menu, &QPushButton::pressed, this, [switchTo]() { switchTo(WindowSpaces::PUBLISH); });
	connect(avatar_menu, &QPushButton::pressed, this, [switchTo]() { switchTo(WindowSpaces::AVATAR); });
}

void ShellHeader::updateStates(WindowSpaces activeSpace, bool sceneOpen)
{
	// One state per space button: the active space, the rest, and — while no
	// scene is open — Editor and Player disabled. ThemeManager owns what each
	// state looks like in each theme (Classic's border-colour swap, or the
	// Qlementine header sheet with the accent-coloured active label).
	const QList<QPair<QPushButton *, WindowSpaces>> spaceButtons = {
		{ worlds_menu, WindowSpaces::DESKTOP }, { assets_menu, WindowSpaces::ASSETS },
		{ effect_menu, WindowSpaces::EFFECT }, { avatar_menu, WindowSpaces::AVATAR },
		{ editor_menu, WindowSpaces::EDITOR }, { player_menu, WindowSpaces::PLAYER }
	};
	for (const auto &pair : spaceButtons) {
		QPushButton *button = pair.first;
		const bool needsScene = pair.second == WindowSpaces::EDITOR
		                        || pair.second == WindowSpaces::PLAYER;
		const bool enabled = sceneOpen || !needsScene;
		if (needsScene) button->setEnabled(enabled);
		button->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
		ThemeManager::applyTopMenuButton(
			button, !enabled                        ? ThemeManager::TopMenuState::Disabled
			        : activeSpace == pair.second    ? ThemeManager::TopMenuState::Active
			                                        : ThemeManager::TopMenuState::Idle);
	}

	// publish_menu is an ICON in the right cluster with its own glyph sheet;
	// active-space feedback comes from the page itself. Re-applying the sheet
	// re-polishes the button, and Qlementine's polish re-sets its font, so the
	// icon font is pushed again HERE (the helper does both, in that order) —
	// otherwise the arrow drops to the inherited UI font while Help and
	// Preferences stay at 28 (owner report 2026-09-07).
	ThemeManager::applyHeaderGlyphButton(publish_menu, glyphFont());
	publish_menu->setCursor(Qt::PointingHandCursor);
}

void ShellHeader::disableSceneSpaces()
{
	ThemeManager::applyTopMenuButton(editor_menu, ThemeManager::TopMenuState::Disabled);
	editor_menu->setDisabled(true);
	editor_menu->setCursor(Qt::ArrowCursor);
	ThemeManager::applyTopMenuButton(player_menu, ThemeManager::TopMenuState::Disabled);
	player_menu->setDisabled(true);
	player_menu->setCursor(Qt::ArrowCursor);
}
