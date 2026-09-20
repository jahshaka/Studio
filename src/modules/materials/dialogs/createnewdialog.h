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
/// A TILE IN THE "NEW MATERIAL" DIALOG — one of the shipped presets, which is
/// the only thing a new material can be based on now (PRESET-UNIFY-1). It used
/// to name a `.effect` TEMPLATE under app/shadergraph/ (`templatePath`, plus a
/// `list` of images to import into its texture properties); those templates
/// were the second preset family and they are deleted. The preset's own
/// reserved guid is the identity now, and the graph comes with it.
struct NodeGraphPreset {
	QString name = "Untitled Shader";
	QString title = "";
	QString iconPath = "";      ///< an ABSOLUTE path (the preset's shipped tile)
	QString guid = "";          ///< the preset's reserved guid; empty = a blank graph
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
	/// WHAT THE USER TYPED, trimmed, empty when they typed nothing.
	QString getName() { return name; }
	int getType() { return type; }
	NodeGraphPreset getPreset() { return currentInfoSelected; }

	void createViewWithOptions();
	void createViewWithoutOptions();

	/// THE ONE SHIPPED PRESET LIST, as dialog tiles (io/materialpresets.h).
	/// The three functions that used to be here — `getPresetList`,
	/// `getStarterList` and `getAdditionalPresetList`, which between them
	/// named seventeen `.effect` templates in two folders — are deleted with
	/// the templates themselves.
	static QList<NodeGraphPreset> presetTiles();
	/// The tile for the preset NAMED, for a caller that must not depend on
	/// list order (the blank-new dialog's base).
	static NodeGraphPreset presetTile(const QString &name);


private:
	QString name;
	int type; // presets = 1, assets =2 

	QPushButton * cancel;
	QPushButton * confirm;
	QLineEdit * nameEdit;
	QWidget* options;
	QWidget *holder;
	QTabWidget *tabbedWidget;
	QWidget *optionsScroll;
    QWidget *optionsWidget;
	NodeGraphPreset currentInfoSelected;
	QLabel *infoLabel;
    int num_of_widgets_per_row = 3;



signals:
	void confirmClicked(int option);

};



