/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "shaderlistwidget.h"
#include <QDropEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QDebug>
#include <QUuid>

#include "src/core/project.h"


ShaderListWidget::ShaderListWidget() : ListWidget()
{
	setAcceptDrops(true);
	viewport()->setAcceptDrops(true);
	addToProjectMenuAllowed = false;
}


ShaderListWidget::~ShaderListWidget()
{

}

void ShaderListWidget::dropEvent(QDropEvent * event)
{

	if(event->mimeData()->data("MODEL_TYPE_ROLE").toInt() == (int)ModelTypes::Shader)
	{
		event->accept();
		event->setDropAction(Qt::CopyAction);

		
		QListWidgetItem *item = new QListWidgetItem;
		item->setFlags(item->flags() | Qt::ItemIsEditable);
		item->setSizeHint({90,90});
		item->setTextAlignment(Qt::AlignCenter);
		item->setIcon(QIcon(":/icons/icons8-file-72.png"));
		item->setText(event->mimeData()->text());

		item->setData(MODEL_GUID_ROLE, event->mimeData()->data("MODEL_GUID_ROLE"));
		item->setData(Qt::DisplayRole, event->mimeData()->text());
		item->setData(MODEL_PARENT_ROLE, event->mimeData()->data("MODEL_PARENT_ROLE"));
		item->setData(MODEL_ITEM_TYPE, event->mimeData()->data("MODEL_ITEM_TYPE"));
		item->setData(MODEL_TYPE_ROLE, event->mimeData()->data("MODEL_TYPE_ROLE"));

		emit itemDropped(item);

	}
	else	event->ignore();
}

void ShaderListWidget::dragMoveEvent(QDragMoveEvent * event)
{
	event->acceptProposedAction();
}

void ShaderListWidget::dragEnterEvent(QDragEnterEvent * event)
{
	if(event->source() != this)
	event->acceptProposedAction();
}

