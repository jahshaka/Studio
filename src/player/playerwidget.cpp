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


PlayerWidget::PlayerWidget(QWidget* parent, EnginePlayerView* view) :
	QWidget(parent), playerView(view)
{
	createUI();
}

void PlayerWidget::createUI()
{
	auto playerControls = new QWidget;
	playerControls->setStyleSheet("background: #1A1A1A");

	auto playerControlsLayout = new QHBoxLayout;
	/*
	auto restartBtn = new QPushButton(playerControls);
	restartBtn->setCursor(Qt::PointingHandCursor);
	restartBtn->setToolTip("Restart playback");
	restartBtn->setToolTipDuration(-1);
	restartBtn->setStyleSheet("background: transparent");
	restartBtn->setIcon(QIcon(":/icons/rotate-to-right.svg"));
	restartBtn->setIconSize(QSize(16, 16));
	*/

	playIcon = QIcon(":/icons/g_play.svg");
	stopIcon = QIcon(":/icons/g_stop.svg");

	playBtn = new QPushButton(playerControls);
	playBtn->setCursor(Qt::PointingHandCursor);
	playBtn->setToolTip("Play the scene");
	playBtn->setToolTipDuration(-1);
	playBtn->setStyleSheet("background: transparent");
	playBtn->setIcon(playIcon);
	playBtn->setIconSize(QSize(24, 24));
	/*
	auto stopBtn = new QPushButton(playerControls);
	stopBtn->setCursor(Qt::PointingHandCursor);
	stopBtn->setToolTip("Stop playback");
	stopBtn->setToolTipDuration(-1);
	stopBtn->setStyleSheet("background: transparent");
	stopBtn->setIcon(QIcon(":/icons/g_stop.svg"));
	stopBtn->setIconSize(QSize(16, 16));
	*/

    playerControlsLayout->setSpacing(12);
    playerControlsLayout->setContentsMargins(6, 6, 6, 6);
    playerControlsLayout->addStretch();
	//playerControlsLayout->addWidget(restartBtn);
	playerControlsLayout->addWidget(playBtn);
	//playerControlsLayout->addWidget(stopBtn);
	playerControlsLayout->addStretch();

	/*
	connect(restartBtn, &QPushButton::pressed, [playBtn]() {
		playBtn->setToolTip("Pause the scene");
		playBtn->setIcon(QIcon(":/icons/g_pause.svg"));

	});
    */

	connect(playBtn, &QPushButton::pressed, [this]() {
        onPlayScene();
	});
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

void PlayerWidget::begin()
{
	if (playerView) playerView->start();
}

void PlayerWidget::end()
{
	if (!playerView) return;
	playerView->end();
	if (playerView->isScenePlaying()) {
		playerView->stopScene();
		playBtn->setIcon(playIcon);
    }
}

void PlayerWidget::onPlayScene()
{
    if (!playerView) return;
    if (playerView->isScenePlaying()) {
        playerView->stopScene();
        playBtn->setIcon(playIcon);
    }
    else {
        playerView->playScene();
        playBtn->setIcon(stopIcon);
        playerView->asWidget()->setFocus();
    }
}
