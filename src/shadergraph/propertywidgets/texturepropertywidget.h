#pragma once
#include <QWidget>
#include <QFileDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include "../models/properties.h"
#include "basepropertywidget.h"
#include "propertywidgetbase.h"

class GraphTexture;
class TexturePropertyWidget : public BasePropertyWidget
{
	Q_OBJECT
public:
	TexturePropertyWidget();
	~TexturePropertyWidget();
	void setProp(TextureProperty *prop);
	QString getValue();
	void setValue(QString guid);

private:
	TextureProperty *prop;
	GraphTexture* graphTexture;
	QPushButton *texture;
	QString value = "";
	WidgetTexture *wid;
	void setConnections();

public slots:
	void setPropValue(QString value);
signals:
	void valueChanged(QString val, TexturePropertyWidget* widget);
	void nameChanged(QString name);
};

