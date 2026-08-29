/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

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
}

void TexturePropertyWidget::setProp(TextureProperty * prop)
{
	this->prop = prop;
	displayName->setText(prop->displayName);
	value = prop->value;
	modelProperty = prop;
	
#if(EFFECT_BUILD_AS_LIB)
	graphTexture = TextureManager::getSingleton()->loadTextureFromGuid(prop->value);
	QIcon icon(graphTexture->path);
	wid->texture->setIcon(icon);
#else
	graphTexture = TextureManager::getSingleton()->createTexture();
	if (QFile::exists(prop->value))
		graphTexture->setImage(prop->value);
	QIcon icon(prop->value);
	texture->setIcon(icon);
#endif
	
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

#if(EFFECT_BUILD_AS_LIB)
		TextureManager::getSingleton()->removeTexture(graphTexture);
		graphTexture = TextureManager::getSingleton()->importTexture(filename);
		prop->value = graphTexture->guid;
		//graphTexture->uniformName = prop->getUniformName();
		QIcon icon(graphTexture->path);
		icon.addFile(graphTexture->path, { wid->texture->width(), wid->texture->height() });
#else
		graphTexture->setImage(filename);
		prop->value = filename;
		QIcon icon(filename);
#endif
		
		wid->texture->setIcon(icon);
	//	wid->texture->setIconSize({ wid->texture->width(), wid->texture->height() });
		emit valueChanged(filename, this);
	});
}

void TexturePropertyWidget::setPropValue(QString value)
{
	prop->value = value;
}
