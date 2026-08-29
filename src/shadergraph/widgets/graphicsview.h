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

private:
	void increaseScale();
	void decreaseScale();

	bool dragging = false;
	QPointF clickPos;
	QFont font;

	GraphNodeScene *scene;

	void addShortcuts();

protected:
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