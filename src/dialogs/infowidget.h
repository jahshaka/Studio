#pragma once
#include "customdialog.h"

enum class MainWindowMenus {
	Editormenu,
	Datamenu,
};

class InfoWidget : public CustomDialog
{
	Q_OBJECT
public:
	InfoWidget(MainWindowMenus menu);
	~InfoWidget();

private:
	void editorMenu(QKeyEvent*);
	void configureEditorMenu();

	void dataMenu(QKeyEvent*);
	void configureDataMenu();
	MainWindowMenus menus;
protected:
	void keyPressEvent(QKeyEvent* event) override;

signals:
	void translateTool();
	void rotateTool();
	void scaleTool();
	void useGlobalSpace();
	void useLocalSpace();
	void useArcballCamera();
	void useFreeCamera();
	void orthagonal();
	void perspective();

	void toggleFps();
	void togglePerspective();
};

