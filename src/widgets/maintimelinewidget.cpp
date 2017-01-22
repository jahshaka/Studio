/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "maintimelinewidget.h"
#include "ui_maintimelinewidget.h"
#include "../mainwindow.h"
#include "../globals.h"

#include <QTimer>
#include <QElapsedTimer>

MainTimelineWidget::MainTimelineWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MainTimelineWidget)
{
    timer = new QTimer(this);
    connect(timer,SIGNAL(timeout()),this,SLOT(updateTime()));
    ui->setupUi(this);
    ui->timeline->hide();
    ui->timeEnd->hide();
    ui->timeStart->hide();

    elapsedTimer = new QElapsedTimer();

    mainWindow=nullptr;

    minTimeRange = 3;
    minTime = 0;
    maxTime = 30;
    updateTimeRange();
    //trackLength = 30;
    time = 0;
    playMode = PlayMode::None;
    fastSpeed = 3.0f;

    //connect(ui->play,SIGNAL(clicked(bool)),this,SLOT(startTimer()));
    connect(ui->stop,SIGNAL(clicked(bool)),this,SLOT(stopTimer()));
//    connect(ui->timeStart,SIGNAL(valueChanged(int)),this,SLOT(timeStartChanged(int)));
//    connect(ui->timeEnd,SIGNAL(valueChanged(int)),this,SLOT(timeEndChanged(int)));

    //connect(ui->animLength,SIGNAL(valueChanged(int)),this,SLOT(animLengthChanged(int)));

    //connect(ui->timeline,SIGNAL(cursorMoved(float)),this,SLOT(timelineCursorChanged(float)));

    connect(ui->play,SIGNAL(clicked(bool)),this,SLOT(play()));
    connect(ui->rewind,SIGNAL(clicked(bool)),this,SLOT(rewind()));
    connect(ui->fastForward,SIGNAL(clicked(bool)),this,SLOT(fastForward()));
    connect(ui->fastRewind,SIGNAL(clicked(bool)),this,SLOT(fastRewind()));
    connect(ui->start,SIGNAL(clicked(bool)),this,SLOT(skipToStart()));
    connect(ui->end,SIGNAL(clicked(bool)),this,SLOT(skipToEnd()));


    this->setAnimationLength(30);
}

void MainTimelineWidget::timeStartChanged(int start)
{
    minTime=start;
    updateTimeRange();
}

void MainTimelineWidget::timeEndChanged(int end)
{
    maxTime = end;
    updateTimeRange();
}

//keeps the maxTime value at least one +1 minTime
void MainTimelineWidget::updateTimeRange()
{
    if(maxTime<=minTime+minTimeRange)
    {
        maxTime=minTime+minTimeRange;
    }

    //ui->timeStart->setValue(minTime);
    //ui->timeEnd->setValue(maxTime);
    ui->timeline->setTimeRange(minTime,maxTime);
}

void MainTimelineWidget::setMainWindow(MainWindow* window)
{
    this->mainWindow = window;
}

void MainTimelineWidget::setAnimationLength(float timeInSeconds)
{
    trackLength = timeInSeconds;
    //this->ui->timeline->setMaxTimeInSeconds(timeInSeconds);
    this->ui->timeline->setTimeRange(0,timeInSeconds);
    //this->ui->animLength->setValue((int)timeInSeconds);
}

void MainTimelineWidget::animLengthChanged(int timeInSeconds)
{
    trackLength = timeInSeconds;
    //this->ui->timeline->setMaxTimeInSeconds(timeInSeconds);
    this->ui->timeline->setTimeRange(0,timeInSeconds);
}

void MainTimelineWidget::startTimer()
{
    elapsedTimer->start();
    timer->start();
    if(mainWindow!=nullptr)
        mainWindow->stopAnimWidget();
}

void MainTimelineWidget::stopTimer()
{
    timer->stop();
}

void MainTimelineWidget::updateTime()
{
    float dt = elapsedTimer->nsecsElapsed()/(1000.0f*1000.0f*1000.0f);
    time += dt*animSpeed;
    elapsedTimer->restart();
    if(time<minTime)
    {
        time = minTime;
    }

    while(time>maxTime)
    {
        time-=maxTime;
    }

    Globals::animFrameTime = time;
    ui->timeline->setTime(time);

    if(mainWindow!=nullptr)
    {
        mainWindow->setSceneAnimTime(time);
    }
}

void MainTimelineWidget::timelineCursorChanged(float timeInSeconds)
{
    if(mainWindow!=nullptr)
    {
        mainWindow->setSceneAnimTime(timeInSeconds);
    }
}

TimelineWidget* MainTimelineWidget::getTimeline()
{
    return ui->timeline;
}

MainTimelineWidget::~MainTimelineWidget()
{
    delete ui;
}

void MainTimelineWidget::fastForward()
{
    playMode = PlayMode::FastForward;
    animSpeed = fastSpeed;
    startTimer();
}

void MainTimelineWidget::fastRewind()
{
    playMode = PlayMode::FastRewind;
    animSpeed = -fastSpeed;
    startTimer();
}

void MainTimelineWidget::play()
{
    playMode = PlayMode::Play;
    animSpeed = 1.0f;
    startTimer();
}

void MainTimelineWidget::rewind()
{
    playMode = PlayMode::Rewind;
    animSpeed = -1.0f;
    startTimer();
}

void MainTimelineWidget::skipToEnd()
{
    time = maxTime;
    ui->timeline->setTime(maxTime);
}

void MainTimelineWidget::skipToStart()
{
    time = minTime;
    ui->timeline->setTime(minTime);
}
