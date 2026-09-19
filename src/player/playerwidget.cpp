/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QPushButton>
#include <QIcon>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include "player/playerwidget.h"
#include "player/engineplayerview.h"
#include "ui/style/stylesheet.h"
#include "ui/style/themeroles.h"


PlayerWidget::PlayerWidget(QWidget* parent, EnginePlayerView* view) :
	QWidget(parent), playerView(view)
{
	createUI();
}

void PlayerWidget::createUI()
{
	auto playerControls = new QWidget;
	playerControls->setStyleSheet(StyleSheet::PlayerControlsBar());

	auto playerControlsLayout = new QHBoxLayout;

	playIcon = QIcon(":/icons/g_play.svg");
	stopIcon = QIcon(":/icons/g_stop.svg");

	playBtn = new QPushButton(playerControls);
	playBtn->setCursor(Qt::PointingHandCursor);
	playBtn->setToolTip("Play the scene");
	playBtn->setToolTipDuration(-1);
	playBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
	ThemeRoles::setFlat(playBtn);
	playBtn->setIcon(playIcon);
	playBtn->setIconSize(QSize(24, 24));

    playerControlsLayout->setSpacing(12);
    playerControlsLayout->setContentsMargins(6, 6, 6, 6);
    playerControlsLayout->addStretch();
	playerControlsLayout->addWidget(playBtn);
	playerControlsLayout->addStretch();


	connect(playBtn, &QPushButton::pressed, [this]() {
        onPlayScene();
	});

	// THE VR BUTTON, beside Play, because that is what it is: play this scene
	// in the headset. It calls what the editor toolbar's icon calls
	// (PlayerService::toggleVr, the same thing `vr.toggle()` calls); the shell
	// hands the call over with setVrToggle, so this widget learns nothing about
	// services, engines or runtimes. Disabled until the shell says VR is
	// available — which on a process that was not started with --vr is never.
	vrBtn = new QPushButton(playerControls);
	vrBtn->setObjectName(QStringLiteral("playerVrButton"));
	vrBtn->setCursor(Qt::PointingHandCursor);
	vrBtn->setCheckable(true);
	vrBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
	ThemeRoles::setFlat(vrBtn);
	vrBtn->setIconSize(QSize(24, 24));
	vrBtn->setEnabled(false);
	vrBtn->setToolTip("VR is unavailable in this session");
	connect(vrBtn, &QPushButton::clicked, [this]() { if (vrToggle) vrToggle(); });
	playerControlsLayout->addWidget(vrBtn);
	playerControls->setLayout(playerControlsLayout);

	auto mainLayout = new QVBoxLayout();
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
	// Headless runs (--headless scripts, --dump-api-docs) have no player
	// backend; the page is just the controls strip then.
	if (playerView) {
		playerView->asWidget()->setParent(this);
		mainLayout->addWidget(playerView->asWidget(), 1);
	}
	mainLayout->addWidget(playerControls, 0);

	this->setLayout(mainLayout);
}

void PlayerWidget::setScene(iris::ScenePtr scene)
{
	if (playerView) playerView->setScene(scene);
}

bool PlayerWidget::begin(QString *why)
{
	if (!playerView) {
		if (why) *why = tr("this session has no player view");
		return false;
	}
	return playerView->start(why);
}

void PlayerWidget::end()
{
	if (!playerView) return;
	playerView->end();
	if (playerView->isScenePlaying()) {
		playerView->stopScene();
    }
	showPlaying(playerView->isScenePlaying());
}

// THE ICON COMES FROM THE SHELL, which owns the QtAwesome font set (one
// instance per process). The alternative — a second QtAwesome here — would load
// the font twice for one glyph.
void PlayerWidget::setVrToggle(const std::function<void()> &toggle, const QIcon &icon)
{
	vrToggle = toggle;
	if (vrBtn && !icon.isNull()) vrBtn->setIcon(icon);
}

void PlayerWidget::showVr(bool available, bool active)
{
	if (!vrBtn) return;
	vrBtn->setEnabled(available);
	vrBtn->setChecked(active);
	vrBtn->setToolTip(!available ? "VR is unavailable in this session"
	                  : active   ? "Leave VR"
	                             : "Play this scene in the headset");
}

void PlayerWidget::showPlaying(bool playing)
{
	if (!playBtn) return;
	playBtn->setIcon(playing ? stopIcon : playIcon);
	playBtn->setToolTip(playing ? "Stop the scene" : "Play the scene");
}

void PlayerWidget::playScene()
{
    if (!playerView) return;
    if (!playerView->isScenePlaying()) {
        playerView->playScene();
        playerView->asWidget()->setFocus();
    }
    showPlaying(playerView->isScenePlaying());
}

void PlayerWidget::onPlayScene()
{
    if (!playerView) return;
    // The button goes through the same calls the verbs go through; the ICON is
    // driven by PlayerService::playingChanged, so it is correct whoever moved
    // the state. Calling the view directly here (rather than the service) keeps
    // the widget free of a service dependency it has no other use for — the
    // service observes the same object.
    const bool wasPlaying = playerView->isScenePlaying();
    if (wasPlaying) {
        playerView->stopScene();
    }
    else {
        playerView->playScene();
        playerView->asWidget()->setFocus();
    }
    showPlaying(playerView->isScenePlaying());
}

void PlayerWidget::endVrForSceneClose()
{
	if (playerView) playerView->endVrForSceneClose();
}
