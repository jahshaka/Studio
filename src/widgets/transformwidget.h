/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TRANSFORMSLIDERSUI_H
#define TRANSFORMSLIDERSUI_H

#include <QWidget>
#include <ui_transformsliders.h>

class SceneNode;

/**
 * @brief Contains sliders for transforming active scene node
 */
class TransformWidget:public QWidget
{
    Q_OBJECT

    Ui::TransformSliders* ui;
    SceneNode* activeSceneNode;
public:
    explicit TransformWidget(QWidget* parent=0);

    /**
     * @brief Sets active scene node
     * @param node SceneNode
     */
    void setActiveSceneNode(SceneNode* node);
    virtual ~TransformWidget() {}

private slots:
    void sliderXPos(int val);
    void sliderYPos(int val);
    void sliderZPos(int val);

    void sliderXScale(int val);
    void sliderYScale(int val);
    void sliderZScale(int val);

    void sliderXRot(int val);
    void sliderYRot(int val);
    void sliderZRot(int val);

    void resetScale();
    void resetPos();
    void resetRot();
};

#endif // TRANSFORMSLIDERSUI_H
