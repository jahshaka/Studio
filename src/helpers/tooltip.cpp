#include "tooltip.h"
#include <QApplication>
#include <QDesktopWidget>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QTimer>

ToolTip *ToolTip::instance = Q_NULLPTR;
bool ToolTip::isFading = false;
bool ToolTip::isShowing = false;
bool ToolTip::isActive = false;
bool ToolTip::morphing = false;
QWidget* ToolTip::senderObj = Q_NULLPTR;

ToolTip::ToolTip(QWidget *parent) : QWidget(parent)
{
}

void ToolTip::showToolTip(QWidget *sender)
{
	senderObj = sender;
	if (!instance) {
		instance = new ToolTip;
		auto layout = new QVBoxLayout;
		instance->setLayout(layout);
		instance->setMouseTracking(true);
		instance->setFocusPolicy(Qt::ClickFocus);
		instance->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
		instance->setAttribute(Qt::WA_TranslucentBackground, true);
		instance->setAttribute(Qt::WA_NoSystemBackground, true);
		instance->setAttribute(Qt::WA_DeleteOnClose);
		instance->setAttribute(Qt::WA_ShowWithoutActivating);
		QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
		qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "1");
		instance->setWindowFlag(Qt::SubWindow);
		instance->setStyleSheet("#container{background:rgba(0,0,00,.1); border: 2px solid rgba(0,0,0,.5); border-radius: .1px; padding: 3px;}"
			"#header{background:rgba(50,50,50,.9); border: 0px solid rgba(0,0,0,.3); border-radius: 0px; padding: 5px 3px; color :rgba(255,255,255,.9);}"
			"#body{background:rgba(70,70,70,.9); border: 0px solid rgba(0,0,0,.3); border-radius: 0px; padding: 3px; margin:0px; color :rgba(255,255,255,.9);}");
	}
	else for (auto child : instance->findChildren<QWidget *>())	child->deleteLater();
	

	isFading = false;
	sender->installEventFilter(instance);

	auto container = new QWidget(instance);
	auto containerLayout = new QVBoxLayout;
	container->setLayout(containerLayout);
	container->setObjectName(QStringLiteral("container"));
	containerLayout->setContentsMargins(1, 1, 1, 1);
	containerLayout->setSpacing(0);
	containerLayout->setSizeConstraint(QLayout::SetFixedSize);
	instance->layout()->addWidget(container);

	QString tooltip(sender->toolTip());

	if (!tooltip.contains('|'))	tooltip.prepend('|');
	auto parts = tooltip.split('|');

	auto header = new QLabel(parts.at(0));
	QFont font = header->font();
	font.setBold(true);
	font.setStyleStrategy(QFont::PreferAntialias);
	font.setPixelSize(14);
	header->setFont(font);
	header->setObjectName(QStringLiteral("header"));

	if (parts.at(0) != "")	containerLayout->addWidget(header);

	font.setBold(false);
	font.setPixelSize(13);
	for (int i = 1; i < parts.size(); i++) {
		if (i >= 2)	font.setItalic(true);
		auto body = new QLabel(parts.at(i));
		body->setFont(font);
		body->setObjectName(QStringLiteral("body"));
		containerLayout->addWidget(body);
	}

	instance->timer = new QTimer;
	instance->timer->setSingleShot(true);
	connect(instance->timer, &QTimer::timeout, [=]() {
		fadeOut();
	});
	instance->timer->start(4000);

	connect(instance, &ToolTip::finishedFadingOut, [=]() {
		instance->hide();
		delete instance;
		instance = Q_NULLPTR;
	});
	fadeIn();
}

void ToolTip::morphToolTip(QWidget *sender)
{
	morphing = true;
	instance->timer->stop();
	showToolTip(sender);
	morphing = false;
}

QWidget *ToolTip::getSender()
{
	return senderObj;
}

void ToolTip::setLocation()
{
	QPoint globalCursorPos = QCursor::pos();
	int mouseScreen = QApplication::desktop()->screenNumber(globalCursorPos);
	QRect mouseScreenGeometry = QApplication::desktop()->screen(mouseScreen)->geometry();
	auto location = globalCursorPos - mouseScreenGeometry.topLeft();

	int x;
	int y;
	int offset = 5;
	if (location.x() + instance->width() + offset >= QApplication::desktop()->screenGeometry().width())	x = location.x() - instance->width() - offset;
	else	x = location.x() + offset;
	if (location.y() + instance->height() + offset > QApplication::desktop()->screenGeometry().height())	y = location.y() - instance->height() - offset;
	else	y = location.y() + offset;

	if (morphing) {
		auto anim = new QPropertyAnimation(instance, "pos");
		anim->setDuration(100);
		anim->setStartValue(instance->pos());
		anim->setEndValue(QPoint(x, y));
		anim->setEasingCurve(QEasingCurve::BezierSpline);
		anim->start(QPropertyAnimation::DeleteWhenStopped);
		connect(anim, &QPropertyAnimation::finished, [=]() {
		});
	}
	else
		instance->move(x, y);
	emit instance->locationChanged(instance->pos());
}

void ToolTip::hideImmediately()
{
	if (instance->timer->isActive())
		instance->timer->stop();
	isFading = false;
	instance->timer->start(100);
}

void ToolTip::fadeIn()
{
	QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(instance);
	instance->setGraphicsEffect(eff);
	instance->show();
	isShowing = true;
	setLocation();
	QPropertyAnimation *a = new QPropertyAnimation(eff, "opacity");
	a->setDuration(350);
	a->setStartValue(0);
	if (morphing)
		a->setStartValue(.99);
	a->setEndValue(1);
	a->setEasingCurve(QEasingCurve::InBack);
	a->start();
	connect(a, &QPropertyAnimation::finished, [=]() {
		isShowing = true;
		instance->setGraphicsEffect(NULL);
		emit instance->finishedFadingIn();
	});
}

void ToolTip::fadeOut()
{
	if (isFading || morphing || !isShowing)	return;

	isFading = true;

	QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(instance);
	instance->setGraphicsEffect(eff);
	QPropertyAnimation *anim = new QPropertyAnimation(eff, "opacity");
	anim->setDuration(350);
	anim->setStartValue(1);
	anim->setEndValue(0);
	anim->setEasingCurve(QEasingCurve::OutBack);
	anim->start();
	connect(anim, &QPropertyAnimation::finished, [=]() {
		isShowing = false;
		if (instance->timer->isActive())
			instance->timer->stop();
		emit instance->finishedFadingOut();
		isFading = false;
	});
}

void ToolTip::enterEvent(QEvent *event)
{
	isActive = true;
	timer->stop();
	updateGeometry();
	Q_UNUSED(event);
}

void ToolTip::leaveEvent(QEvent *event)
{
	hideImmediately();
	Q_UNUSED(event);
	isActive = false;
}

void ToolTip::mousePressEvent(QEvent *event)
{
	//mousePressEvent(event);
    Q_UNUSED(event);
}

bool ToolTip::eventFilter(QObject *watched, QEvent *event)
{
	switch (event->type())
	{
	case QEvent::HoverLeave:
	case QEvent::Leave:
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick:
	case QEvent::Wheel:
		if (!isFading && isShowing)	hideImmediately();
		break;
	default:
		break;
	}
	return QObject::eventFilter(watched, event);
}

bool ToolTipHelper::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::ToolTip) {
		auto obj = qobject_cast<QWidget *>(watched);
		if (obj != nullptr) {
			auto tip = obj->toolTip();
			if (!tip.isEmpty()) {
				event->ignore();
				if (!ToolTip::isShowing)	ToolTip::showToolTip(obj);
				if (ToolTip::isShowing && ToolTip::getSender() != obj) ToolTip::morphToolTip(obj);
			}
		}
		return true;
	}
	return QObject::eventFilter(watched, event);
}
