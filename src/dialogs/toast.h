/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TOAST_H
#define TOAST_H

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

class Toast : QFrame
{
    Q_OBJECT

public:
    Toast(QWidget *parent = 0);
	void showToast(const QString &title, const QString &text, const float &delay);
	void showToast(const QString &title, const QString &text, const float &delay, const QPoint &pos, const QRect &rect);

private:
	QVBoxLayout *toastLayout;
	QLabel *caption;
	QLabel *info;
};

#endif // TOAST_H