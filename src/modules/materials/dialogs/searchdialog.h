#pragma once
#include <QDialog>
#include <QListWidget>
#include "../graph/nodegraph.h"
#include "../models/library.h"
#include "../widgets/listwidget.h"
#include "../graph/graphnodescene.h"
#include "../widgets//treewidget.h"


class SearchDialog : public QDialog
{
	Q_OBJECT
public:
	SearchDialog(NodeGraph *graph, GraphNodeScene* scene, QPoint point);
	~SearchDialog();

	void generateTileNode(QList<NodeLibraryItem*> lis);
	void generateTileNode(NodeGraph *graph);
	void configureTreeWidget();
	void selectItemByName(const QString& name);

	TreeWidget *tree;

	QLineEdit *searchBar;
	QPoint point;

protected:
	void showEvent(QShowEvent *event) override;
	bool eventFilter(QObject *, QEvent *) override;

};

