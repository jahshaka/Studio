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
        rangeEnd = 100;

        minRange = 10;
        maxRange = 1000;

        minValue = -1.0f;
        maxValue = 1.0f;

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
        return rangeStart + range * widgetWidth;
    }

};

#endif // ANIMATIONWIDGETDATA_H
