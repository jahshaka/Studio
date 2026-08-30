#include <QWidget>
#include <QGridLayout>
#include <QSpinBox>
#include <QLineEdit>
#include <QPainter>
#include "../models/properties.h"
#include "basepropertywidget.h"
#include "propertywidgetbase.h"


class IntPropertyWidget : public BasePropertyWidget
{
	Q_OBJECT
public:
	IntPropertyWidget();
	~IntPropertyWidget();
	void setProp(IntProperty *prop);
	int getValue();

private:
	
	IntProperty *prop;
	int x = 0;
	WidgetInt *wid;
	void setConnections();

protected:

public slots:
	void setPropValue(int value);
signals:
	void valueChanged(int val);
	void nameChanged(QString name);

};