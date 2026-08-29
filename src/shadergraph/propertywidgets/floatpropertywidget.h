#include <QWidget>
#include <QGridLayout>
#include <QSpinBox>
#include <QLineEdit>
#include <QPainter>
#include "../models/properties.h"
#include "basepropertywidget.h"
#include "propertywidgetbase.h"


class FloatPropertyWidget : public BasePropertyWidget
{
	Q_OBJECT
public:
	FloatPropertyWidget();
	~FloatPropertyWidget();
	void setProp(FloatProperty *prop);
	float getValue();

private:
	FloatProperty *prop;
	float x = 0;
	WidgetFloat *wid;
	void setConnections();
protected:

public slots:
	void setPropValue(double value);
signals:
	void valueChanged(double val);
	void nameChanged(QString name);

};