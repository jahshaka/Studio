/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "texturepropertywidget.h"
#include "../core/texturemanager.h"
#include <QDebug>


TexturePropertyWidget::TexturePropertyWidget() : BasePropertyWidget()
{

	wid = new WidgetTexture;
	setWidget(wid);
	setConnections();

}


TexturePropertyWidget::~TexturePropertyWidget()
{
	// ITS RESOLVER ENTRY TOO (MATERIALS_TABS_SPEC §2.8). setProp mints a
	// GraphTexture per widget; with the page's process-wide clearTextures gone,
	// the entry has to leave with the widget that made it.
	if (graphTexture) {
		TextureManager::getSingleton()->removeTexture(graphTexture);
		delete graphTexture;
		graphTexture = nullptr;
	}
}

void TexturePropertyWidget::setProp(TextureProperty * prop)
{
	this->prop = prop;
	displayName->setText(prop->displayName);
	value = prop->value;
	modelProperty = prop;
	
	graphTexture = TextureManager::getSingleton()->loadTextureFromGuid(prop->value);
	QIcon icon(graphTexture->path);
	wid->texture->setIcon(icon);
	
	graphTexture->uniformName = prop->getUniformName();
}

QString TexturePropertyWidget::getValue()
{
	return prop->value;
}

void TexturePropertyWidget::setValue(QString guid)
{
	prop->value = guid;
}

void TexturePropertyWidget::setConnections()
{
	connect(wid->texture, &QPushButton::clicked, [=]() {
		auto filename = QFileDialog::getOpenFileName();

		TextureManager::getSingleton()->removeTexture(graphTexture);
		graphTexture = TextureManager::getSingleton()->importTexture(filename);
		prop->value = graphTexture->guid;
		QIcon icon(graphTexture->path);
		icon.addFile(graphTexture->path, { wid->texture->width(), wid->texture->height() });
		
		wid->texture->setIcon(icon);
		emit valueChanged(filename, this);
	});
}

void TexturePropertyWidget::setPropValue(QString value)
{
	prop->value = value;
}
