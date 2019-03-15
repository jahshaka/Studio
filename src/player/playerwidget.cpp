#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QPushButton>
#include <QIcon>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include "playerwidget.h"
#include "playerview.h"
#include "irisgl/IrisGL.h"


PlayerWidget::PlayerWidget(QWidget* parent) :
	QWidget(parent)
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

	auto playBtn = new QPushButton(playerControls);
	playBtn->setCursor(Qt::PointingHandCursor);
	playBtn->setToolTip("Play the scene");
	playBtn->setToolTipDuration(-1);
	playBtn->setStyleSheet("background: transparent");
	playBtn->setIcon(QIcon(":/icons/g_play.svg"));
	playBtn->setIconSize(QSize(24, 24));

	auto stopBtn = new QPushButton(playerControls);
	stopBtn->setCursor(Qt::PointingHandCursor);
	stopBtn->setToolTip("Stop playback");
	stopBtn->setToolTipDuration(-1);
	stopBtn->setStyleSheet("background: transparent");
	stopBtn->setIcon(QIcon(":/icons/g_stop.svg"));
	stopBtn->setIconSize(QSize(16, 16));

	playerControlsLayout->setSpacing(12);
	playerControlsLayout->setMargin(6);
	playerControlsLayout->addStretch();
	//playerControlsLayout->addWidget(restartBtn);
	playerControlsLayout->addWidget(playBtn);
	playerControlsLayout->addWidget(stopBtn);
	playerControlsLayout->addStretch();

	/*
	connect(restartBtn, &QPushButton::pressed, [playBtn]() {
		playBtn->setToolTip("Pause the scene");
		playBtn->setIcon(QIcon(":/icons/g_pause.svg"));
		//UiManager::restartScene();
	});
	*/

	connect(playBtn, &QPushButton::pressed, [playBtn, this]() {
		/*
		if (UiManager::isScenePlaying) {
			playBtn->setToolTip("Play the scene");
			playBtn->setIcon(QIcon(":/icons/g_play.svg"));
			UiManager::pauseScene();
		}
		else {
			playBtn->setToolTip("Pause the scene");
			playBtn->setIcon(QIcon(":/icons/g_pause.svg"));
			UiManager::playScene();
		}
		*/

		playerView->playScene();
	});

	connect(stopBtn, &QPushButton::pressed, [playBtn, this]() {
		//playBtn->setToolTip("Play the scene");
		//playBtn->setIcon(QIcon(":/icons/g_play.svg"));

		playerView->stopScene();
	});

	playerControls->setLayout(playerControlsLayout);

	playerView = new PlayerView(this);

	auto mainLayout = new QVBoxLayout();
	mainLayout->addWidget(playerView, 1);
	mainLayout->addWidget(playerControls, 0);

	this->setLayout(mainLayout);
}

void PlayerWidget::setScene(iris::ScenePtr scene)
{
	this->playerView->setScene(scene);
}

void PlayerWidget::begin()
{
	playerView->start();
}

void PlayerWidget::end()
{
	playerView->end();
}