#pragma once
#include "listwidget.h"


class QListWidgetItem;

class ShaderListWidget : public ListWidget
{
	Q_OBJECT
public:
	ShaderListWidget();
	~ShaderListWidget();
protected:
	void dropEvent(QDropEvent* event);
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
signals:
	void itemDropped(QListWidgetItem *item);
};

