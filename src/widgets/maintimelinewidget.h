/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MAINTIMELINEWIDGET_H
#define MAINTIMELINEWIDGET_H

#include <QWidget>
class QTimer;
class QElapsedTimer;
class MainWindow;
class TimelineWidget;

namespace Ui {
class MainTimelineWidget;
}

enum class PlayMode
{
    None,
    Play,
    Rewind,
    FastForward,
    FastRewind
};

class MainTimelineWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MainTimelineWidget(QWidget *parent = 0);
    ~MainTimelineWidget();

    void setAnimationLength(float timeInSeconds);
    void setMainWindow(MainWindow* window);
    TimelineWidget* getTimeline();

private slots:
    void updateTime();
    void startTimer();
    void stopTimer();
    void fastForward();
    void fastRewind();
    void play();
    void rewind();
    void skipToEnd();
    void skipToStart();

    void animLengthChanged(int timeInSeconds);

    void timeStartChanged(int);
    void timeEndChanged(int);

    void timelineCursorChanged(float timeInSeconds);

private:
    void updateTimeRange();

    Ui::MainTimelineWidget *ui;
    QTimer* timer;
    QElapsedTimer* elapsedTimer;
    MainWindow* mainWindow;

    //the time is in seconds
    float minTime;
    float maxTime;
    float trackLength;
    float time;
    int minTimeRange;

    float animSpeed;//speed cursor should be moving at
    float fastSpeed;//fastforward and fastrewind speeds
    PlayMode playMode;
};

#endif // MAINTIMELINEWIDGET_H
