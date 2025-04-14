/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QWidget>

class StyleSheet : public QObject
{
public:
	static const QString QPushButtonBlue();
	static const QString QPushButtonBlueBig();
	static const QString QPushButtonInvisible();
    static const QString QPushButtonGreyscale();
	static const QString QPushButtonGrouped();
	static const QString QPushButtonGroupedBig();
	static const QString QPushButtonDanger();
    static const QString QPushButtonGreyscaleBig();
    static const QString QPushButtonRounded(int size);
    static const QString QSpinBox();
    static const QString QSlider();
    static const QString QLineEdit();
    static const QString QWidgetDark();
    static const QString QWidgetTransparent();
    static const QString QLabelWhite();
    static const QString QLabelBlack();
    static const QString QComboBox();
	static const QString QCheckBox();
	static const QString QSplitter();
	static const QString QAbstractScrollArea();
	static const QString QMenu();
	static void setStyle(QWidget *);
	static void setStyle(QObject *);
	static void setStyle(QList<QWidget *>);
};


