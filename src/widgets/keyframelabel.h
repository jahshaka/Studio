/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef KEYFRAMELABEL_H
#define KEYFRAMELABEL_H

#include <QWidget>
#include "../irisgl/src/irisglfwd.h"


namespace Ui {
class KeyFrameLabel;
}

class KeyFrameLabel : public QWidget
{
    Q_OBJECT

    bool collapsed;
    iris::FloatKeyFrame *keyFrame;

public:
    explicit KeyFrameLabel(QWidget *parent = 0);
    ~KeyFrameLabel();

    QList<KeyFrameLabel*> childLabels;

    void setTitle(QString title);

    void addChild(KeyFrameLabel* label);

    void collapse();
    void expand();

    void setKeyFrame(iris::FloatKeyFrame *value);

    iris::FloatKeyFrame *getKeyFrame() const;

protected slots:
    void toggleCollapse();

private:
    Ui::KeyFrameLabel *ui;
};

#endif // KEYFRAMELABEL_H
