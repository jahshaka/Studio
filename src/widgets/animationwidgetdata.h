/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ANIMATIONWIDGETDATA_H
#define ANIMATIONWIDGETDATA_H

#include "../irisgl/src/irisglfwd.h"

class AnimationWidgetData
{
public:
    float rangeStart;
    float rangeEnd;

    float minRange;
    float maxRange;

    float minValue;
    float maxValue;

    float cursorPosInSeconds;

    iris::SceneNodePtr sceneNode;

    QList<QWidget*> displayWidget;

    AnimationWidgetData()
    {
        rangeStart = -5;
        rangeEnd = 70;

        minRange = 10;
        maxRange = 1000;

        minValue = -11.0f;
        maxValue = 11.0f;

        cursorPosInSeconds = 0.0f;
    }

    void refreshWidgets()
    {
        for (auto widget : displayWidget) {
            widget->repaint();
        }
    }

    void addDisplayWidget(QWidget* widget)
    {
        displayWidget.append(widget);
    }

    int timeToPos(float timeInSeconds, int widgetWidth)
    {
        float timeSpacePos = (timeInSeconds - rangeStart) / (rangeEnd - rangeStart);
        return (int)(timeSpacePos * widgetWidth);
    }

    float posToTime(int xpos, int widgetWidth)
    {
        float range = rangeEnd - rangeStart;
        return rangeStart + range * ((float)xpos / widgetWidth);
    }

    int valueToPos(float timeInSeconds, int widgetHeight)
    {
        float timeSpacePos = 1.0f - (timeInSeconds - minValue) / (maxValue - minValue);
        return (int)(timeSpacePos * widgetHeight);
    }

    float posToValue(int ypos, int widgetHeight)
    {
        float range = maxValue - minValue;
        return range - (minValue + range * ((float)ypos / widgetHeight));
    }

};

#endif // ANIMATIONWIDGETDATA_H
