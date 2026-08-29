#pragma once
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QMimeData>

class TreeWidget : public QTreeWidget
{
public:
	TreeWidget();
	virtual ~TreeWidget();

protected:
    QMimeData * mimeData(const QList<QTreeWidgetItem *> items) const;
};

