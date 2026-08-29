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

private:
	void increaseScale();
	void decreaseScale();

	bool dragging = false;
	QPointF clickPos;
	QFont font;

	GraphNodeScene *scene;

	void addShortcuts();
	void openNodeSearch();

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