#include "progressbar.h"
#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QPropertyAnimation>
#include <QShortcut>
#include <QTime>
#include <QScreen>
#include <QDesktopWidget>
#include <QWindow>
#include <QDebug>


ProgressBar::ProgressBar(QWidget *parent)
	: QProgressBar(parent)
{
	configureUI();
	configureConnection();
	//show();
}


ProgressBar::~ProgressBar()
{
	proPainter = Q_NULLPTR;
	proPainter->deleteLater();
}


QPushButton * ProgressBar::confirmButton()
{
	if (confirm) return confirm;
	return nullptr;
}


QPushButton * ProgressBar::cancelButton()
{
	if (cancel) return cancel;
	return nullptr;
}


void ProgressBar::clearButtonConnection()
{
	disconnect(confirm);
	disconnect(cancel);
}


void ProgressBar::configureUI()
{
    setMinimumWidth(360);
	//setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
	//setAttribute(Qt::WA_TranslucentBackground);

	title = new QLabel(this);
	content = new QWidget;
	proPainter = new ProgressPainter;
	contentLayout = new QVBoxLayout;
	layout = new QVBoxLayout;
	closeBtn = new QPushButton(this);
	confirm = new QPushButton;
	cancel = new QPushButton;
	showingCancelDialog = false;
	confirmationText = tr("would you like to close the dialog?");
	opacity = new QGraphicsOpacityEffect;

	setLayout(layout);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(content);

    content->setMinimumWidth(360);
	content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
	content->setLayout(contentLayout);
	content->setStyleSheet("background:rgba(30,30,30,1);");

	title->setText(tr("hi"));
	title->setStyleSheet("color: rgba(255,255,255,1); padding: 5px;");
	title->setAlignment(Qt::AlignVCenter);
	title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	title->move(0, 0);
	QFont font = title->font();
	font.setStyleStrategy(QFont::PreferAntialias);
	font.setWeight(60);
	font.setPixelSize(15);
	title->setFont(font);

	contentLayout->setContentsMargins(10, 10, 10, 10);
	contentLayout->setSpacing(0);
	contentLayout->addWidget(title);
	contentLayout->addWidget(proPainter);

	int buttonSize = 25;
	font.setBold(true);
	font.setPixelSize(12);
	font.setStretch(120);
	closeBtn->setFont(font);
	closeBtn->setText("X");
	closeBtn->setFixedSize(buttonSize, buttonSize);
	closeBtn->move(320, 10);
	closeBtn->raise();
	closeBtn->setStyleSheet("QPushButton{color: rgb(0,0,0); background:rgba(230,230,230,.0); border: 1px solid transparent; border-radius:13px;}"
		"QPushButton:hover{color: rgba(200,30,60,1)}");
	

	font.setPointSize(10);
	font.setStretch(100);
	font.setBold(false);

	confirm->setFont(font);
	confirm->setText("Yes");
    confirm->setStyleSheet("QPushButton{color: rgba(255, 255, 255, 1); background:rgba(81,81,81,1); border: 1px solid rgba(0,0,0,0); padding: 6px;}"
                           "QPushButton:hover{color: rgba(255,255,255,1); background:rgba(52, 152, 219); }");
	cancel->setFont(font);
	cancel->setText("No");
	cancel->setStyleSheet(confirm->styleSheet());

	auto effect = new QGraphicsDropShadowEffect(content);
	effect->setBlurRadius(10);
	effect->setOffset(0);
	effect->setColor(QColor(0, 0, 0, 200));
	content->setGraphicsEffect(effect);    
    layout->setSizeConstraint(QLayout::SetFixedSize);
	show();

	//adjustSize();
}


void ProgressBar::configureConnection()
{
    connect(closeBtn, &QPushButton::clicked, [=]() {
        clearButtonConnection();
        if(showingCancelDialog) this->hideConfirmationDialog();
        setConfirmationButtons("yes", "no");
        setConfirmationText("would you like to close the dialog?");
        showConfirmationDialog();
        connect(cancel, &QPushButton::clicked, [=]() {
            widget->setVisible(false);
        });
        connect(confirm, &QPushButton::clicked, [=]() { close(); });
    });

    
	connect(this, &ProgressBar::valueChanged, [=](int value) {
		if (mode == Mode::Indefinite) setMode(Mode::Definite);
		proPainter->setThisValue(text().split('%')[0].toInt());
		proPainter->setText(text());
	});


	auto shorty = new QShortcut(QKeySequence("s"), this);
	connect(shorty, &QShortcut::activated, [=]() {
		qsrand(static_cast<quint64>(QTime::currentTime().msecsSinceStartOfDay()));
	//	setValue(qrand() % 100);
	});


	shorty = new QShortcut(QKeySequence("d"), this);
	connect(shorty, &QShortcut::activated, [=]() {
	//	setMode(Mode::Definite);
	});


	shorty = new QShortcut(QKeySequence("i"), this);
	connect(shorty, &QShortcut::activated, [=]() {
	//	setMode(Mode::Indefinite);
	});

	
}


void ProgressBar::updateMode()
{
	if (mode == Mode::Definite) {
		proPainter->animate(false);
		proPainter->setThisValue(value());
	}
	if (mode == Mode::Indefinite) {
		proPainter->animate(true);
	}
}


void ProgressBar::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event);
}


void ProgressBar::mousePressEvent(QMouseEvent *event)
{
	oldPos = event->globalPos();
	Q_UNUSED(event);
}


void ProgressBar::mouseMoveEvent(QMouseEvent *event)
{
	//QPoint delta = event->globalPos() - oldPos;
	//move(x() + delta.x(), y() + delta.y());
	//oldPos = event->globalPos();
	Q_UNUSED(event);
}


void ProgressBar::mouseDoubleClickEvent(QMouseEvent *event)
{
	Q_UNUSED(event);
}

void ProgressBar::closeEvent(QCloseEvent * event)
{
	qDebug() << "close";
}


void ProgressBar::showConfirmationDialog()
{
	if (showingCancelDialog) return;
	showingCancelDialog = true;


	auto widgetlayout = new QVBoxLayout;
	auto buttonLayout = new QHBoxLayout;
	widget = new QWidget;

	contentLayout->addWidget(widget);

	auto questionLabel = new QLabel(confirmationText);
	QFont font = questionLabel->font();
	font.setPixelSize(14);
	questionLabel->setFont(font);

	widget->setLayout(widgetlayout);
	widgetlayout->addWidget(questionLabel);
	widgetlayout->addLayout(buttonLayout);
	buttonLayout->addWidget(confirm);
	buttonLayout->addWidget(cancel);

	widget->setStyleSheet("QWidget{background:rgba(25,25,25,1); color:rgb(255,255,255)}"
		"QPushButton{background:rgba(50,50,50,0); border: 1px solid rgba(52, 152, 219); padding: 3px;  }"
		"QPushButton:hover{background:rgba(52, 152, 219);}"
		"");

	widget->show();


	connect(confirm, &QPushButton::clicked, [=]() {
		this->close();
	});


	connect(cancel, &QPushButton::clicked, [=]() {
		widget->hide();
		showingCancelDialog = false;
	});
}


void ProgressBar::hideConfirmationDialog()
{
	widget->hide();
	showingCancelDialog = false;
}


void ProgressBar::setConfirmationText(QString string)
{
	confirmationText = string;
}


void ProgressBar::setConfirmationButtons(QString confirm, QString cancel, bool showConfirm, bool showCancel)
{
	this->confirm->setText(confirm);
	this->cancel->setText(cancel);
	showConfirmButton(showConfirm);
	showCancelButton(showCancel);
}


void ProgressBar::showConfirmButton(bool showConfirm)
{
	confirm->setVisible(showConfirm);
}


void ProgressBar::showCancelButton(bool showCancel)
{
	cancel->setVisible(showCancel);
}


void ProgressBar::setMode(ProgressBar::Mode mode)
{
	this->mode = mode;
	updateMode();
}


void ProgressBar::setTitle(QString string)
{
	title->setText(string);
}

void ProgressBar::close()
{
	qApp->quit();
}


///////////////////////////////////////////////////////////////////////
ProgressPainter::ProgressPainter(QWidget *parent)
{
	textValue = new QLabel;
	textValue->setStyleSheet("background: rgba(0,0,0,0); color: rgba(255,255,255,1);");
	textValue->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	textValue->setAlignment(Qt::AlignCenter);

	auto layout = new QVBoxLayout;
	setLayout(layout);
	layout->addWidget(textValue);

	setBackgroundColor(QColor(52, 152, 219));
	animator = new QPropertyAnimation(this, "startPoint");
	animator->setDuration(2000);
	animator->setStartValue(0);
	animator->setEndValue(width());
	animator->setEasingCurve(QEasingCurve::Linear);
	animator->setLoopCount(-1);

	connect(animator, &QPropertyAnimation::valueChanged, [=]() {
		if (animator) update();
	});
}


ProgressPainter::~ProgressPainter()
{
	animator->stop();
	animator = Q_NULLPTR;
}


void ProgressPainter::setText(QString text)
{
	textValue->setText(text);
}


void ProgressPainter::setThisValue(qreal val)
{
	setSize(val);
	update();
}


void ProgressPainter::animate(bool val)
{
	if (val) {
		setSize(40);
		textValue->setText("");
		animator->setStartValue(-size());
		animator->setEndValue(width());
		animator->start();
	}
	else {
		textValue->setText(QString::number(value()) + "%");
		animator->stop();
		setStartPoint(0);
		update();
	}
}


void ProgressPainter::paintEvent(QPaintEvent *event)
{
	QPainter painter(this);
	painter.fillRect(startPoint(), 0, size(), height(), backgroundColor());
	Q_UNUSED(event);
}
