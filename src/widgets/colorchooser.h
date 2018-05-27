#ifndef COLORCHOOSER_H
#define COLORCHOOSER_H

#include <QApplication>
#include <QDesktopWidget>
#include <QGroupBox>
#include <QLabel>
#include <QlineEdit>
#include <QPainter>
#include <QProxyStyle>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QWidget>

class ColorCircle;
class SliderMoveToMouseClickPositionStyle : public QProxyStyle
{
public:
	using QProxyStyle::QProxyStyle;

	int styleHint(QStyle::StyleHint hint,
		const QStyleOption* option = 0,
		const QWidget* widget = 0,
		QStyleHintReturn* returnData = 0) const
	{
		if (hint == QStyle::SH_Slider_AbsoluteSetButtons) 	return (Qt::LeftButton | Qt::MidButton | Qt::RightButton);
		return QProxyStyle::styleHint(hint, option, widget, returnData);
	}
};

class CustomBackground : public QWidget
{
	Q_OBJECT
public:
	bool isExpanded = false;

	CustomBackground(QWidget *parent = Q_NULLPTR, Qt::WindowFlags f = Qt::WindowFlags()) {
		QWidget::QWidget(parent);
		setWindowFlags(Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint | Qt::WindowStaysOnTopHint);
		setWindowFlag(Qt::SubWindow);
		setGeometry(QApplication::desktop()->screenGeometry().width() / 2, QApplication::desktop()->screenGeometry().height() / 2, 1, 1);
		setStyleSheet("background: rgba(0,0,0,0);");
	}

	void drawPixmap(QPixmap pm) {
		setGeometry(0, 0, QApplication::desktop()->screenGeometry().width(), QApplication::desktop()->screenGeometry().height());
		pixmap = &pm;
		image = pixmap->toImage();
		isExpanded = true;
		setMouseTracking(true);
		repaint();
	}

	void shrink() {
		setGeometry(QApplication::desktop()->screenGeometry().width() / 2, QApplication::desktop()->screenGeometry().height() / 2, 1, 1);
		isExpanded = false;
		emit finished(isExpanded);
	}

private:
	QPixmap *pixmap;
	QImage image;
	QColor color;

protected:
	void paintEvent(QPaintEvent *event) {
		if (isExpanded) {
			QPainter painter(this);
			painter.setRenderHint(QPainter::HighQualityAntialiasing);
			painter.drawPixmap(0, 0, QApplication::desktop()->screenGeometry().width(), QApplication::desktop()->screenGeometry().height(), *pixmap);
		}
	}

	void mousePressEvent(QMouseEvent *event) {
		shrink();
	}

	void mouseMoveEvent(QMouseEvent *event)
	{
		if (isExpanded) {
			color = color.fromRgb(image.pixel(this->mapFromGlobal(QCursor::pos())));
			emit positionChanged(color);
		}
	}

signals:
	void finished(bool b);
	void positionChanged(QColor color);
};

class CustomSlider;
class CustomBackground;
class ColorChooser : public QWidget
{
	Q_OBJECT

public:
	ColorChooser(QWidget *parent = Q_NULLPTR);
	void showWithColor(QColor color, QMouseEvent * event);
	~ColorChooser();

	private slots:
	void changeBackgroundColorOfDisplayWidgetHsv();
	void changeBackgroundColorOfDisplayWidgetRgb();
	void configureDisplay();
	void pickerMode(bool ye);

signals:
	void onColorChanged(QColor c);

private:
	void setConnections();
	void setColorBackground();
	void setStyleForApplication();
	void exitPickerMode();
	void enterPickerMode();

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void paintEvent(QPaintEvent *event);
	void leaveEvent(QEvent *event);

private slots:
	void setSliders(QColor color);
	void setRgbSliders(QColor color);
	void setHsvSliders(QColor color);
	void setColorFromHex();
	void setSliderLabels();
	void setValueInColor();

private:
	QDesktopWidget * desktop;
	QPixmap pixmap;
	CustomBackground *cbg;
	QRgb rgb;
	QColor color;
	ColorCircle* circlebg;
	int gWidth, gHeight;

	QGroupBox* groupBox;
	CustomSlider *alphaSlider, *redSlider, *greenSlider, *blueSlider, *hueSlider, *saturationSlider, *valueSlider, *adjustSlider;
	QStackedWidget *stackHolder;
	QPushButton *cancel, *select, *picker, *rgbBtn, *hsv, *hex;
	QWidget *colorDisplay, *rgbHolder, *hsvHolder, *hexHolder;
	QLineEdit *hexEdit;
	QLabel *hLabel, *sLabel, *vLabel, *rLabel, *gLabel, *bLabel;
	QLabel *hValueLabel, *sValueLabel, *vValueLabel, *rValueLabel, *gValueLabel, *bValueLabel;
	QSpinBox *rSpin, *gSpin, *bSpin, *hSpin, *sSpin, *vSpin;
	QDoubleSpinBox *alphaSpin;
};

class CustomSlider : public QSlider
{
	Q_OBJECT
public:

	QString minLabel;
	QString maxLabel;

	void setMinAndMaxLabels(QString min, QString max) {
		setMinLabel(min);
		setMaxLabel(max);
	}

	void setMaxLabel(QString string) {
		maxLabel = string;
		repaint();
	}

	void setMinLabel(QString string) {
		minLabel = string;
	}

	CustomSlider(QWidget *parent = Q_NULLPTR) {
		QSlider::QSlider(parent);
		QSlider::setStyle(new SliderMoveToMouseClickPositionStyle(this->style()));
	}

	CustomSlider(Qt::Orientation orientation, QWidget *parent = Q_NULLPTR) {
		CustomSlider::CustomSlider(parent);
		QSlider::setOrientation(orientation);
	}

private:
	QColor color;
	bool isColorSlider;
};
#endif // COLORCHOOSER_H