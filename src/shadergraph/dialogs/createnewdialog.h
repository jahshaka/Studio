#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QGroupBox>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include "../widgets/listwidget.h"
#include <QWidget>
#include <QScrollArea>
struct NodeGraphPreset {
	QString name = "Untitled Shader";
	QString title = "";
	QString iconPath = "";
	QString templatePath = "";
	QVector<QString> list;
};

struct dialogType {

};

class OptionSelection : public QPushButton
{
	Q_OBJECT
public:
	OptionSelection(NodeGraphPreset node);

	NodeGraphPreset info;
	QPixmap checkedIconIcon;
	int type = 0;
protected:
    void paintEvent(QPaintEvent *event) override;

signals:
	void buttonSelected(OptionSelection* button);
	void OptionSelected(NodeGraphPreset info);
};

class CreateNewDialog : public QDialog
{
	Q_OBJECT
public:
	CreateNewDialog(bool maximized = true);
	~CreateNewDialog();

	void configureStylesheet();
	QString getName() { return name; }
	QString getTemplateName() { return templateName; }
	int getType() { return type; }
	QVector<QString> getList() { return currentInfoSelected.list; }
	NodeGraphPreset getPreset() { return currentInfoSelected; }

	void createViewWithOptions();
	void createViewWithoutOptions();

	static QList<NodeGraphPreset> getPresetList();
	static QList<NodeGraphPreset> getAdditionalPresetList();
	static QList<NodeGraphPreset> getStarterList();


private:
	QString name;
	int type; // presets = 1, assets =2 
	QString templateName;

	QPushButton * cancel;
	QPushButton * confirm;
	QLineEdit * nameEdit;
	QWidget* options;
	QWidget* presets;
	QWidget *holder;
	QTabWidget *tabbedWidget;
	QWidget *optionsScroll;
	QWidget *presetsScroll;
    QWidget *optionsWidget;
    QWidget *presetsWidget;
	NodeGraphPreset currentInfoSelected;
	QLabel *infoLabel;
    int num_of_widgets_per_row = 3;



signals:
	void confirmClicked(int option);

};



