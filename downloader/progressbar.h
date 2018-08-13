#ifndef PROGRESSBAR_H
#define PROGRESSBAR_H


#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>


class ProgressPainter;
class ProgressBar : public QProgressBar
{
	Q_OBJECT
public:
	enum class Mode {
		Definite,
		Indefinite
	};
	ProgressBar(QWidget *parent = Q_NULLPTR);
	~ProgressBar();
	QPushButton *confirmButton();
	QPushButton *cancelButton();
	QPushButton *confirm;
	QPushButton *cancel;


	void clearButtonConnection();
private:
	int _width = 360 * logicalDpiX() / 100;
	Mode mode;
	QLabel *title;
	QWidget *content;
	QWidget *widget;
	ProgressPainter *proPainter;
	QVBoxLayout *contentLayout;
	QVBoxLayout *layout;
	QPoint oldPos;
	QPushButton *closeBtn;


	QString confirmationText;
	QGraphicsOpacityEffect *opacity;


	bool animating;
	bool showingCancelDialog;

	void configureUI();
	void configureConnection();
	void updateMode();
signals:
	void confirmButtonClicked();
	void cancelButtonClicked();
public slots:
	void showConfirmationDialog();
	void hideConfirmationDialog();
	void setConfirmationText(QString string);
	void setConfirmationButtons(QString confirm, QString cancel, bool showConfirm = true, bool showCancel = true);
	void showConfirmButton(bool showConfirm);
	void showCancelButton(bool showCancel);
	void setMode(ProgressBar::Mode mode);
	void setTitle(QString string);
	void close();
protected:
	void paintEvent(QPaintEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void mouseMoveEvent(QMouseEvent *event);
	void mouseDoubleClickEvent(QMouseEvent *event);
	void closeEvent(QCloseEvent *event);
};

class ProgressPainter : public QWidget
{
	Q_OBJECT
		Q_PROPERTY(qreal startPoint READ startPoint WRITE setStartPoint)
		Q_PROPERTY(qreal value READ value WRITE setValue)
		Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
		Q_PROPERTY(qreal size READ size WRITE setSize)
public:
	ProgressPainter(QWidget *parent = 0);
	~ProgressPainter();

public slots:
	void setStartPoint(qreal point) { _startPoint = point; }
	qreal startPoint() { return _startPoint; }
	void setValue(qreal val) { _value = val; setSize(val); }
	qreal value() { return _value; }
	void setThisValue(qreal val);
	QColor backgroundColor() { return _backgroundColor; }
	void setBackgroundColor(QColor color) { _backgroundColor = color; }
	qreal size() { return _size; }
	void setSize(qreal val) { _size = val * (qreal)width() / 100.0; }

	void animate(bool val);
	void setText(QString text);
private:
	qreal _startPoint;
	qreal _value;
	qreal _size;
	QLabel *textValue;
	QColor _backgroundColor;
	QPropertyAnimation *animator;

protected:
	void paintEvent(QPaintEvent *event);
};
#endif // PROGRESSBAR_H