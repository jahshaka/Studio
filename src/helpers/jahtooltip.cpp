#include "jahtooltip.h"
#include <QApplication>
#include <QDesktopWidget>
#include <QDebug>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLayout>
#include <QPropertyAnimation>
#include <QTimer>
#include <QPushButton>

JahToolTip *JahToolTip::instance = 0;
bool JahToolTip::isFading = false;
bool JahToolTip::isShowing = false;
bool JahToolTip::isActive = false;
bool JahToolTip::morphing = false;
QWidget* JahToolTip::senderObj = Q_NULLPTR;


JahToolTip::JahToolTip(QWidget *parent) : QWidget(parent)
{

}

void JahToolTip::showToolTip(QWidget *sender)
{


	senderObj = sender;

	if (!instance) {
		instance = new JahToolTip;
		auto layout = new QVBoxLayout;
		instance->setLayout(layout);
		instance->setMouseTracking(true);

		instance->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
		instance->setAttribute(Qt::WA_TranslucentBackground, true);
		instance->setAttribute(Qt::WA_NoSystemBackground, true);
		instance->setAttribute(Qt::WA_DeleteOnClose);
		instance->setAttribute(Qt::WA_ShowWithoutActivating);
		instance->setWindowFlag(Qt::SubWindow);
		instance->setStyleSheet("#container{background:rgba(00,0,00,.1); border: 2px solid rgba(0,0,0,.5); border-radius: 1px; padding: 3px;}"
			"#header{background:rgba(50,50,50,1); border: 0px solid rgba(0,0,0,.3); border-radius: 0px; padding: 5px; color :rgba(255,255,255,.9);}"
			"#body{background:rgba(70,70,70,1); border: 0px solid rgba(0,0,0,.3); border-radius: 0px; padding: 5px; margin:0px; color :rgba(255,255,255,.9);}");
	}
	else {
		for (auto child : instance->findChildren<QWidget *>())
			child->deleteLater();
	}

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

	if (!tooltip.contains('|'))
		tooltip.prepend('|');

	auto parts = tooltip.split('|');

	auto header = new QLabel(parts.at(0));
	QFont font = header->font();
	font.setBold(true);
	font.setPointSize(12);
	header->setFont(font);
	header->setObjectName(QStringLiteral("header"));

	if (parts.at(0) != "")
		containerLayout->addWidget(header);

	//       auto button = new QPushButton("willy", instance);
	//       containerLayout->addWidget(button);
	//       button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	//       connect(button, &QPushButton::clicked,[=](){
	//           qDebug() << "clicked";
	//       });

	font.setBold(false);
	font.setPointSize(8);

	for (int i = 1; i < parts.size(); i++) {

		if (i >= 2)	font.setItalic(true);

		auto body = new QLabel(parts.at(i));
		body->setFont(font);
		body->setObjectName(QStringLiteral("body"));
		containerLayout->addWidget(body);
	}


	instance->setFocusPolicy(Qt::ClickFocus);


	instance->timer = new QTimer;
	instance->timer->setSingleShot(true);
	connect(instance->timer, &QTimer::timeout, [=]() {
		fadeOut();
	});
	instance->timer->start(4000);

	connect(instance, &JahToolTip::finishedFadingOut, [=]() {
		instance->hide();
		delete instance;
		instance = Q_NULLPTR;
	});

	fadeIn();
}

void JahToolTip::morphToolTip(QWidget *sender)
{
	morphing = true;
	instance->timer->stop();
	showToolTip(sender);
	morphing = false;
}

QWidget *JahToolTip::getSender()
{
	return senderObj;
}

void JahToolTip::setLocation()
{
	QPoint globalCursorPos = QCursor::pos();
	int mouseScreen = QApplication::desktop()->screenNumber(globalCursorPos);
	QRect mouseScreenGeometry = QApplication::desktop()->screen(mouseScreen)->geometry();
	auto location = globalCursorPos - mouseScreenGeometry.topLeft();
	//instance->layout()->setSizeConstraint(QLayout::SetFixedSize);

	int x;
	int y;
	int offset = -10;
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

void JahToolTip::hideImmediately()
{
	if (instance->timer->isActive())
		instance->timer->stop();
	isFading = false;
	instance->timer->start(100);
}

void JahToolTip::fadeIn()
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

void JahToolTip::fadeOut()
{
	if (isFading || morphing || !isShowing)
		return;

	isFading = true;
	qDebug() << "hide";

	QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(instance);
	instance->setGraphicsEffect(eff);
	QPropertyAnimation *a = new QPropertyAnimation(eff, "opacity");
	a->setDuration(350);
	a->setStartValue(1);
	a->setEndValue(0);
	a->setEasingCurve(QEasingCurve::OutBack);
	a->start();
	connect(a, &QPropertyAnimation::finished, [=]() {
		isShowing = false;
		if (instance->timer->isActive())
			instance->timer->stop();
		emit instance->finishedFadingOut();
		isFading = false;
	});
}

void JahToolTip::enterEvent(QEvent *event)
{
	qDebug() << "enntered line 220";

	isActive = true;
	timer->stop();
	updateGeometry();
	Q_UNUSED(event);

}

void JahToolTip::leaveEvent(QEvent *event)
{
	hideImmediately();
	Q_UNUSED(event);
	isActive = false;
}

void JahToolTip::mousePressEvent(QEvent *event)
{
	Q_UNUSED(event);
}

bool JahToolTip::eventFilter(QObject *watched, QEvent *event)
{
	switch (event->type())
	{

	case QEvent::HoverLeave:
	case QEvent::Leave:
		//  case QEvent::Close:
	case QEvent::MouseButtonPress:
		//   case QEvent::MouseButtonRelease:
	case QEvent::MouseButtonDblClick:
		//   case QEvent::MouseMove:
	case QEvent::Wheel:
		//  case QEvent::WindowActivate:
		//  case QEvent::FocusIn:
		//  case QEvent::FocusOut:
		if (!isFading && isShowing)
			hideImmediately();

		break;
	case QEvent::WindowDeactivate:

		break;

	default:
		break;
	}
	return QObject::eventFilter(watched, event);
}



bool JahToolTipHelper::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::ToolTip) {
		auto obj = qobject_cast<QWidget *>(watched);
		if (obj != nullptr) {
			auto tip = obj->toolTip();
			if (!tip.isEmpty()) {
				event->ignore();
				if (!JahToolTip::isShowing)
					JahToolTip::showToolTip(obj);
				if (JahToolTip::isShowing && JahToolTip::getSender() != obj)
					JahToolTip::morphToolTip(obj);

			}
		}
		return true;
	}

	return QObject::eventFilter(watched, event);

}
