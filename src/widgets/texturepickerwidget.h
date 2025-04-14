/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TEXTUREPICKERWIDGET_H
#define TEXTUREPICKERWIDGET_H

#include "basewidget.h"

#include <QLabel>
#include <QListWidgetItem>

namespace Ui {
	class TexturePickerWidget;
}

class TexturePickerWidget : public BaseWidget
{
	Q_OBJECT

private slots:
	void pickTextureMap();
	void clear();

	void changeMap(QListWidgetItem *);
	void changeMap(const QString &);

public:
	int index;
	QString filename, filePath, textureGuid;

	explicit TexturePickerWidget(QWidget *parent = 0);
	~TexturePickerWidget();

	Ui::TexturePickerWidget *ui;

	void setLabelImage(QLabel *label, QString file, bool emitSignal = true);
	bool eventFilter(QObject *object, QEvent *ev);
	void setTexture(QString path);
	QString getTexturePath();

	void dragEnterEvent(QDragEnterEvent *);
	void dropEvent(QDropEvent *);

signals:
	void valueChanged(QString value);
	void valuesChanged(QString value, QString guid);
};

#endif // TEXTUREPICKERWIDGET_H
