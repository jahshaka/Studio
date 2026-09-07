#ifndef MATERIALSETTINGSWIDGET_H
#define MATERIALSETTINGSWIDGET_H

#include <QWidget>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLayout>
#include <QComboBox>
#include <QFormLayout>
#include "../graph/nodegraph.h"

namespace Ui {
class MaterialSettingsWidget;
}

// The graph material's settings form: NAME and BLEND MODE, and nothing else.
//
// It used to carry eight more rows — Z Write, Depth Test, Fog, Cast Shadows,
// Receive Shadows, Accept Lighting, Cull Mode, Render Layer — every one of
// them a live, styled, serialized, UNDOABLE control that reached no renderer,
// no material and no pixel (HLMS_ADOPTION P2). MaterialSettings no longer has
// the fields, so the rows cannot come back by accident.
//
// "Receive Shadows" is not gone from the product: it is a real PbrMaterial row
// now (HLMS_ADOPTION P1) and it reaches the datablock. The one here was a
// different, dead control with almost the same name — which is precisely why
// it had to go rather than be left beside the working one.
class MaterialSettingsWidget : public QWidget
{
	Q_OBJECT

public:
	MaterialSettingsWidget(QWidget *parent = 0);
	MaterialSettingsWidget(MaterialSettings settings, QWidget *parent = 0);
	~MaterialSettingsWidget();

	void setMaterialSettings(MaterialSettings settings);
	void updateMaterialSettingsWidget(MaterialSettings &);

	void setName(QString name);
	void setBlendMode(BlendMode index);

private:
    MaterialSettings settings;

	void setConnections();
	Ui::MaterialSettingsWidget* ui;
	QGridLayout *gridLayout;
	QFormLayout *formLayout;
	QLabel *label;
	QLineEdit *lineEdit;
	QLabel *label_4;
	QComboBox *comboBox;
	QFont font;
signals:
    void settingsChanged(MaterialSettings value);
};

#endif
