#ifndef GRAPHICSVIEW_H
#define GRAPHICSVIEW_H

#include <QWidget>
#include <QGraphicsView>

class GraphNodeScene;
class GraphicsView : public QGraphicsView
{
public:
    GraphicsView(QWidget *parent = Q_NULLPTR);
	static qreal currentScale;

	void setScene(GraphNodeScene *scene);

	// F: frame the selection (or the whole graph); H: reset zoom to 1:1
	void fitSelection();
	void resetZoom();

	/// Opens the node-search palette. PUBLIC since the Space key landed
	/// (EffectsPage::openNodeSearch routes it here): Tab still opens it under
	/// the cursor, and the shortcut opens it from anywhere on the page.
	/// Returns false when there is no graph to search.
	bool openNodeSearch();

private:
	void increaseScale();
	void decreaseScale();

	bool dragging = false;
	QPointF clickPos;
	QFont font;

	GraphNodeScene *scene;

	void addShortcuts();

protected:
	bool event(QEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void drawBackground(QPainter *painter, const QRectF &rect);
	void wheelEvent(QWheelEvent *event) ;

	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;


};

#endif // GRAPHICSVIEW_H