#ifndef COLORCHOOSER_H
#define COLORCHOOSER_H

#include <QApplication>
#include <QDesktopWidget>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
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

    CustomBackground( QWidget *parent = Q_NULLPTR, Qt::WindowFlags f = Qt::WindowFlags()) :QWidget(parent){

		setWindowFlags(Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint | Qt::WindowStaysOnTopHint);
		setWindowFlag(Qt::SubWindow);
		setGeometry(QApplication::desktop()->screenGeometry().width() / 2, QApplication::desktop()->screenGeometry().height() / 2, 1, 1);
		setStyleSheet("background: rgba(0,0,0,0);");
	}

	void drawPixmap(QPixmap pm) {
		//this->parentWidget()->setWindowModality(Qt::NonModal);
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
		//this->parentWidget()->setWindowModality(Qt::ApplicationModal);
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
		emit shouldHide(true);
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
	void shouldHide(bool b);
};

class CustomSlider;
class CustomBackground;
class ColorChooser : public QWidget
{
	Q_OBJECT

public:
	void showWithColor(QColor color, QMouseEvent * event, QString name);
	ColorChooser( QWidget * parent = Q_NULLPTR);
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
	bool fromHexEdit = true;

	QGroupBox* groupBox;
    QSlider *alphaSlider, *redSlider, *greenSlider, *blueSlider, *hueSlider, *saturationSlider, *valueSlider, *adjustSlider;
	QStackedWidget *stackHolder;
	QPushButton *cancel, *select, *picker, *rgbBtn, *hsv, *hex;
	QWidget *colorDisplay, *rgbHolder, *hsvHolder, *hexHolder;
	QLineEdit *hexEdit;
	QLabel *hLabel, *sLabel, *vLabel, *rLabel, *gLabel, *bLabel;
	QLabel *hValueLabel, *sValueLabel, *vValueLabel, *rValueLabel, *gValueLabel, *bValueLabel;
	QSpinBox *rSpin, *gSpin, *bSpin, *hSpin, *sSpin, *vSpin;
	QDoubleSpinBox *alphaSpin;
};

#endif // COLORCHOOSER_H
