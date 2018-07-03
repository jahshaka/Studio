#include "progressbar.h"

#include <QPainter>
#include <QPropertyAnimation>
#include <QVBoxlayout>
#include <QDebug>

ProgressBar::ProgressBar(QWidget *parent) : QProgressBar(parent)
{
    setTextVisible(false);
    configureProgressBar(parent);
    configureConnections();

}

ProgressBar::~ProgressBar()
{

}

QPoint ProgressBar::startPoint()
{
    return _startPoint;
}

void ProgressBar::setStartPoint(QPoint point)
{
    _startPoint = point;
}

void ProgressBar::setMode(Mode mode)
{
    this->mode = mode;
    emit modeChanged(mode);
    updateMode();
}

void ProgressBar::setTitle(QString string)
{
    auto faded = new QPropertyAnimation(opacity, "opacity");
    faded->setDuration(600);
    faded->setKeyValueAt(0.0,1.0);
    faded->setKeyValueAt(0.5,0.0);
    faded->setKeyValueAt(1.0,1.0);
    faded->setEasingCurve(QEasingCurve::Linear);
    faded->start(QAbstractAnimation::DeleteWhenStopped);
    connect(faded,&QPropertyAnimation::valueChanged,[=](QVariant value){
        if(value.toDouble() <=0.02) title->setText(string);
    });
}

void ProgressBar::configureProgressBar(QWidget *parent)
{


    _height = 90;
    titleSize = 45;
    confirmationSize = 160;
    title = new QLabel(this);
    text = new QLabel(this);
    close = new QPushButton(this);
    animator = new QPropertyAnimation(this, "startPoint");
    mode = Mode::Definite;
    animating = false;
    showingCancelDialog = false;

    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedHeight(_height);
    setMinimumWidth(350);
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Minimum);

    auto layout = new QVBoxLayout;
    auto hLayout = new QHBoxLayout;
    this->setLayout(layout);

   // hLayout->addWidget(title);
    hLayout->addStretch(300);
    hLayout->setSpacing(0);
    this->setContentsMargins(0,0,0,0);
    layout->setContentsMargins(0,0,0,0);
    hLayout->setContentsMargins(0,0,0,0);



    layout->addLayout(hLayout);
    layout->addSpacing(30);
    layout->addWidget(text);

    QFont font = title->font();
    font.setPixelSize(14);
    font.setStyleStrategy(QFont::PreferAntialias);


    title->setText("hello");
    opacity = new QGraphicsOpacityEffect(title);
    title->setGraphicsEffect(opacity);
    //title->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    title->setAlignment(Qt::AlignLeft);
    title->setStyleSheet("color:rgba(250,250,250,1);");
    title->setFont(font);
    title->setMinimumSize(minimumSize());
    title->move(5,5);


    font.setPixelSize(12);
    text->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    text->setAlignment(Qt::AlignHCenter);
    text->setText("hello");
    text->setStyleSheet("color:rgba(250,250,250,1);");
    text->setFont(font);

    int buttonSize = 25;
    font.setBold(true);
    font.setStretch(120);
    close->setFont(font);
    close->setText("X");
    close->setFixedSize(buttonSize,buttonSize);
    close->setStyleSheet("QPushButton{color: rgb(0,0,0); background:rgba(230,230,230,.0); border: 1px solid transparent; border-radius:13px;}"
                         "QPushButton:hover{color: rgba(200,30,60,1)}");
    hLayout->addWidget(close);

    qDebug() << width();


    _startPoint = QPoint(0,_height - titleSize);



}

void ProgressBar::configureConnections()
{
    connect(this,&ProgressBar::valueChanged,[=](){
        if(animating) text->setText("");
        else text->setText(QString::number(value())+"%");
    });

    connect(close, &QPushButton::clicked,[=](){
        showConfirmationDialog();
    });
}

void ProgressBar::updateMode()
{
    if(mode ==Mode::Indefinite){
            animate();
            animator->start();
            animating = true;
        }

    if(mode == Mode::Definite){
        animator->stop();
        setStartPoint(QPoint( 0, minimumHeight()/2));
        update();
        animating = false;
    }
     emit modeChanged(mode);
}

void ProgressBar::animate()
{
    qreal point = 40*width()/maximum();
    setValue(40);
    animator->setDuration(2000);
    animator->setStartValue(QPoint( -point, _height - titleSize));
    animator->setEndValue(QPoint( width(), _height - titleSize));
    animator->setEasingCurve(QEasingCurve::Linear);
    animator->setLoopCount(-1);

    connect(animator,&QPropertyAnimation::valueChanged,[=](){
        update();
    });
}

void ProgressBar::showConfirmationDialog()
{
    if(showingCancelDialog) return;
    showingCancelDialog = true;
        auto layout = new QVBoxLayout;
        auto widgetlayout = new QVBoxLayout;
        auto buttonLayout = new QHBoxLayout;
        auto widget = new QWidget;
//        widget->setFixedSize( 300, minimumHeight());
//        setLayout(layout);
 //       layout->setContentsMargins(0,0,0,0);
//        setFixedSize(300,140);
//        layout->addSpacing(height());
//        layout->addWidget(widget);
        setFixedHeight(confirmationSize);

        this->layout()->addWidget(widget);

        auto questionLabel = new QLabel("Are you sure you want to cancel operations?");
        auto confirm = new QPushButton("Yes");
        auto cancel = new QPushButton("No");

        widget->setLayout(widgetlayout);
        widgetlayout->addWidget(questionLabel);
        widgetlayout->addLayout(buttonLayout);
        buttonLayout->addWidget(confirm);
        buttonLayout->addWidget(cancel);

        widget->setStyleSheet("QWidget{background:rgba(25,25,25,1); color:rgb(250,250,250)}"
                              "QPushButton{background:rgba(50,50,50,0); border: 1px solid rgba(52, 152, 219); padding: 3px;  }"
                              "QPushButton:hover{background:rgba(52, 152, 219);}"
                              "");

        widget->show();
//        auto effect = new QGraphicsDropShadowEffect(widget);
//        effect->setOffset(0,0);
//        effect->setBlurRadius( 30);
//        effect->setColor(QColor(25,25,25));
//        widget->setGraphicsEffect(effect);

        connect(confirm, &QPushButton::clicked,[=](){

            emit cancelOperations();
            emit finished();
            this->hide();
            deleteLater();
        });

        connect(cancel, &QPushButton::clicked,[=](){
            widget->hide();
          //  setFixedSize(300,70);
            setFixedHeight(_height);
            widget->deleteLater();
            showingCancelDialog = false;
        });
}

void ProgressBar::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    //paint background
    painter.fillRect(0,0,width(),height(),QColor(25,25,25));
    //paint value
    painter.fillRect( startPoint().x(), _height - titleSize, value()*width()/maximum(), _height - titleSize, QColor(52, 152, 219));
    Q_UNUSED(event);
}

void ProgressBar::setValue(int val)
{
    if(animating) setMode(Mode::Definite);
    auto anim = new QPropertyAnimation( this, "value");
        anim->setStartValue(value());
        anim->setEndValue(val);
        anim->setDuration(600);
        anim->setEasingCurve(QEasingCurve::Linear);
        anim->start();
        connect(anim,&QPropertyAnimation::finished,[=](){

        });
}

