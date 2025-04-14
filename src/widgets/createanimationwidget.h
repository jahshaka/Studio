/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef CREATEANIMATIONWIDGET_H
#define CREATEANIMATIONWIDGET_H

#include <QWidget>

namespace Ui {
class CreateAnimationWidget;
}

class QPushButton;

class CreateAnimationWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CreateAnimationWidget(QWidget *parent = 0);
    ~CreateAnimationWidget();

    QPushButton* getCreateButton();

    void showButton();
    void hideButton();
    void setButtonText(QString text);

private:
    Ui::CreateAnimationWidget *ui;
};

#endif // CREATEANIMATIONWIDGET_H
