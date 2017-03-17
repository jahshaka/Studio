/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef LABELWIDGET_H
#define LABELWIDGET_H

#include <QWidget>

namespace Ui {
    class LabelWidget;
}

class LabelWidget : public QWidget
{
    Q_OBJECT

public:
    LabelWidget(QWidget* parent = 0);
    ~LabelWidget();

    void setText(const QString&);
    void setLabel(const QString&);
    void clearText();

private slots:
    void onTextInputChanged(const QString&);

private:
    Ui::LabelWidget* ui;
};


#endif // LABELWIDGET_H
